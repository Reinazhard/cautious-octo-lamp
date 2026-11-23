// SPDX-License-Identifier: GPL-2.0
/*
 * GS101 Zero-UAPI Implicit Frame-Budget Bus
 *
 * Reconstructs interactive frame-budget pressure from unmodified Android
 * userspace signals (PowerHAL ADPF sched_setattr uclamp_min) combined with a
 * 3-tier lockless hardware signal bus fed by exynos_drm (VSync/TE),
 * goog_touch_interface (touch/fling), exynos-acme (ACPM DVFS submit/latch),
 * and tensor_aio (per-CPU memory stall + MIF pressure).
 */

#include <linux/arch_topology.h>
#include <linux/cache.h>
#include <linux/cpufreq.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/math64.h>
#include <linux/percpu.h>
#include <linux/sched/gs101_frame.h>
#include <linux/seqlock.h>
#include <linux/timekeeping.h>

#ifdef CONFIG_ARM64
#include <clocksource/arm_arch_timer.h>
#endif

#include "sched.h"

#define GS101_NR_CLUSTERS		3	/* 0: LIT, 1: MID, 2: BIG */

#define GTF_ADPF_ACTIVE			(1U << 0)
#define GTF_OVERRUN_THIS_FRAME		(1U << 1)

/* Touch boost windows in microseconds */
#define GS101_TOUCH_ACTIVE_HOLD_US	120000U	/* 120 ms while dragging */
#define GS101_TOUCH_FLING_HOLD_US	250000U	/* 250 ms post-lift fling */

/* Staleness thresholds in microseconds */
#define GS101_STALL_STALE_US		40000U
#define GS101_MIF_STALE_US		40000U
#define GS101_ADPF_CLUSTER_HOLD_US	35000U

/* Default ACPM IPC latch latency estimate before first measurement */
#define GS101_DEFAULT_LATCH_US		600U
#define GS101_MAX_LATCH_US		8000U

/* Tight EEVDF slice on mid-frame overrun rescue (1.0 ms) */
#define GS101_RESCUE_SLICE_NS		1000000ULL

enum gs101_dvfs_phase {
	GS101_DVFS_SETTLED = 0,
	GS101_DVFS_RAMP_UP,
	GS101_DVFS_RAMP_DOWN,
};

/*
 * Tier 1: Per-CPU cacheline (single writer: memperfd in tensor_aio).
 * Layout: [63:16] ts_us | [15:0] mem_stall_permille (0..1000).
 */
struct gs101_cpu_line {
	u64	stall_word;
	u64	last_adpf_us;		/* updated under local rq->lock */
	u32	overrun_seq;		/* VSync seq of last overrun on CPU */
} ____cacheline_aligned_in_smp;

/*
 * Tier 2: Per-Cluster DVFS latch state (single writer per cluster under
 * exynos-acme domain->rate_info_lock; wait-free readers via seqcount_latch_t).
 */
struct gs101_dvfs_rec {
	u64	submit_us;
	u32	latched_khz;
	u32	target_khz;
	u16	latched_cap;
	u16	target_cap;
	u16	ewma_latch_us;
	u8	phase;
};

struct gs101_cluster_line {
	seqcount_latch_t	seq;
	struct gs101_dvfs_rec	buf[2];
} ____cacheline_aligned_in_smp;

/*
 * Tier 3: SoC read-mostly signals (independent single-writers <= 240 Hz).
 */
struct gs101_vsync_rec {
	u64	ts_us;
	u32	seq;
	u32	period_us;
};

struct gs101_vsync_line {
	seqcount_latch_t	seq;
	struct gs101_vsync_rec	buf[2];
} ____cacheline_aligned_in_smp;

struct gs101_soc_line {
	/* Sole writer: goog_touch_interface — [63:4] ts_us | [3:0] state */
	u64	touch_word;
	/* Sole writer: tensor_aio memperfd — [63:16] ts_us | [15:4] mif_press | [3:0] flags */
	u64	mem_word;
} ____cacheline_aligned_in_smp;

static DEFINE_PER_CPU_ALIGNED(struct gs101_cpu_line, gs101_cpu_bus);
static struct gs101_cluster_line gs101_cluster_bus[GS101_NR_CLUSTERS] = {
	[0] = { .seq = SEQCNT_LATCH_ZERO(gs101_cluster_bus[0].seq),
		.buf[0].ewma_latch_us = GS101_DEFAULT_LATCH_US,
		.buf[1].ewma_latch_us = GS101_DEFAULT_LATCH_US },
	[1] = { .seq = SEQCNT_LATCH_ZERO(gs101_cluster_bus[1].seq),
		.buf[0].ewma_latch_us = GS101_DEFAULT_LATCH_US,
		.buf[1].ewma_latch_us = GS101_DEFAULT_LATCH_US },
	[2] = { .seq = SEQCNT_LATCH_ZERO(gs101_cluster_bus[2].seq),
		.buf[0].ewma_latch_us = GS101_DEFAULT_LATCH_US,
		.buf[1].ewma_latch_us = GS101_DEFAULT_LATCH_US },
};
static struct gs101_vsync_line gs101_vsync_bus = {
	.seq = SEQCNT_LATCH_ZERO(gs101_vsync_bus.seq),
};
static struct gs101_soc_line gs101_soc_bus;

#ifdef CONFIG_ARM64
static u32 cntpct_us_mult __read_mostly;
static u32 cntpct_us_shift __read_mostly;

static __always_inline u64 gs101_now_us(void)
{
	if (likely(cntpct_us_mult))
		return mul_u64_u32_shr(__arch_counter_get_cntpct(),
				       cntpct_us_mult, cntpct_us_shift);
	return ktime_get_mono_fast_ns() >> 10;
}

static int __init gs101_frame_clock_init(void)
{
	u32 rate = arch_timer_get_rate();

	if (rate) {
		clocks_calc_mult_shift(&cntpct_us_mult, &cntpct_us_shift,
				       rate, USEC_PER_SEC, 10);
	}
	return 0;
}
early_initcall(gs101_frame_clock_init);
#else
static __always_inline u64 gs101_now_us(void)
{
	return ktime_get_mono_fast_ns() >> 10;
}
#endif

static __always_inline int gs101_cpu_to_cluster(int cpu)
{
	unsigned long cap = arch_scale_cpu_capacity(cpu);

	if (cap < SCHED_CAPACITY_SCALE / 2)
		return 0;
	if (cap < SCHED_CAPACITY_SCALE)
		return 1;
	return 2;
}

/* =========================================================================
 * LAYER 1: HARDWARE SIGNAL PUBLISHERS
 * ========================================================================= */

void gs101_frame_publish_vsync(u32 period_us)
{
	struct gs101_vsync_rec rec;

	/*
	 * Single writer per display TE IRQ. Double-buffered latch keeps
	 * scheduler readers wait-free even under rq->lock / NMI.
	 */
	rec = gs101_vsync_bus.buf[0];
	rec.ts_us = gs101_now_us();
	rec.period_us = period_us;
	if (period_us)
		rec.seq++;

	raw_write_seqcount_latch(&gs101_vsync_bus.seq);
	gs101_vsync_bus.buf[0] = rec;
	raw_write_seqcount_latch(&gs101_vsync_bus.seq);
	gs101_vsync_bus.buf[1] = rec;
}
EXPORT_SYMBOL_GPL(gs101_frame_publish_vsync);

void gs101_frame_publish_touch(enum gs101_touch_state state)
{
	u64 word = (gs101_now_us() << 4) | ((u64)state & 0xfULL);

	WRITE_ONCE(gs101_soc_bus.touch_word, word);
}
EXPORT_SYMBOL_GPL(gs101_frame_publish_touch);

void gs101_frame_dvfs_submit(int cpu, u32 target_khz, u16 target_cap)
{
	struct gs101_cluster_line *cl = &gs101_cluster_bus[gs101_cpu_to_cluster(cpu)];
	struct gs101_dvfs_rec rec = cl->buf[0];
	u64 now_us = gs101_now_us();

	if (rec.phase == GS101_DVFS_SETTLED || !rec.submit_us)
		rec.submit_us = now_us;

	rec.target_khz = target_khz;
	rec.target_cap = target_cap;
	if (target_khz > rec.latched_khz)
		rec.phase = GS101_DVFS_RAMP_UP;
	else if (target_khz < rec.latched_khz)
		rec.phase = GS101_DVFS_RAMP_DOWN;
	else
		rec.phase = GS101_DVFS_SETTLED;

	raw_write_seqcount_latch(&cl->seq);
	cl->buf[0] = rec;
	raw_write_seqcount_latch(&cl->seq);
	cl->buf[1] = rec;
}
EXPORT_SYMBOL_GPL(gs101_frame_dvfs_submit);

void gs101_frame_dvfs_latched(int cpu, u32 latched_khz, u16 latched_cap)
{
	struct gs101_cluster_line *cl = &gs101_cluster_bus[gs101_cpu_to_cluster(cpu)];
	struct gs101_dvfs_rec rec = cl->buf[0];
	u64 now_us = gs101_now_us();

	if (rec.submit_us && now_us > rec.submit_us) {
		u32 lat = (u32)min_t(u64, now_us - rec.submit_us, GS101_MAX_LATCH_US);

		rec.ewma_latch_us = (u16)((3U * rec.ewma_latch_us + lat) >> 2);
	}

	rec.submit_us = 0;
	rec.latched_khz = latched_khz;
	rec.latched_cap = latched_cap;
	rec.target_khz = latched_khz;
	rec.target_cap = latched_cap;
	rec.phase = GS101_DVFS_SETTLED;

	raw_write_seqcount_latch(&cl->seq);
	cl->buf[0] = rec;
	raw_write_seqcount_latch(&cl->seq);
	cl->buf[1] = rec;
}
EXPORT_SYMBOL_GPL(gs101_frame_dvfs_latched);

void gs101_frame_publish_cpu_stall(int cpu, u16 mem_stall_permille)
{
	u64 word = (gs101_now_us() << 16) |
		   min_t(u16, mem_stall_permille, 1000U);

	WRITE_ONCE(per_cpu(gs101_cpu_bus, cpu).stall_word, word);
}
EXPORT_SYMBOL_GPL(gs101_frame_publish_cpu_stall);

void gs101_frame_publish_mif(u16 mif_pressure_scale, bool throttled)
{
	u64 press = min_t(u64, mif_pressure_scale, SCHED_CAPACITY_SCALE);
	u64 word = (gs101_now_us() << 16) | (press << 4) | (throttled ? 1ULL : 0ULL);

	WRITE_ONCE(gs101_soc_bus.mem_word, word);
}
EXPORT_SYMBOL_GPL(gs101_frame_publish_mif);

/* =========================================================================
 * INTERNAL WAIT-FREE BUS READERS
 * ========================================================================= */

static __always_inline void gs101_read_vsync(struct gs101_vsync_rec *out)
{
	unsigned int seq;

	do {
		seq = raw_read_seqcount_latch(&gs101_vsync_bus.seq);
		*out = gs101_vsync_bus.buf[seq & 1];
	} while (raw_read_seqcount_latch_retry(&gs101_vsync_bus.seq, seq));
}

static __always_inline void gs101_read_dvfs(int cl_id, struct gs101_dvfs_rec *out)
{
	struct gs101_cluster_line *cl = &gs101_cluster_bus[cl_id];
	unsigned int seq;

	do {
		seq = raw_read_seqcount_latch(&cl->seq);
		*out = cl->buf[seq & 1];
	} while (raw_read_seqcount_latch_retry(&cl->seq, seq));
}

static __always_inline bool gs101_touch_is_active(u64 now_us)
{
	u64 word = READ_ONCE(gs101_soc_bus.touch_word);
	enum gs101_touch_state st = (enum gs101_touch_state)(word & 0xfULL);
	u64 ts_us = word >> 4;
	u64 hold_us;

	if (st == GS101_TOUCH_IDLE || !ts_us || now_us < ts_us)
		return false;

	hold_us = (st == GS101_TOUCH_ACTIVE) ?
		  GS101_TOUCH_ACTIVE_HOLD_US : GS101_TOUCH_FLING_HOLD_US;
	return (now_us - ts_us) <= hold_us;
}

static __always_inline bool gs101_cpu_is_mem_bound(int cpu, u64 now_us)
{
	u64 mem_w = READ_ONCE(gs101_soc_bus.mem_word);
	u64 mif_ts = mem_w >> 16;
	u16 mif_press = (u16)((mem_w >> 4) & 0xfffU);
	u64 cpu_w = READ_ONCE(per_cpu(gs101_cpu_bus, cpu).stall_word);
	u64 cpu_ts = cpu_w >> 16;
	u16 stall = (u16)(cpu_w & 0xffffU);

	if (!mif_ts || now_us < mif_ts || (now_us - mif_ts) > GS101_MIF_STALE_US)
		return false;
	if (!cpu_ts || now_us < cpu_ts || (now_us - cpu_ts) > GS101_STALL_STALE_US)
		return false;

	return mif_press >= 700 && stall >= 600;
}

static __always_inline bool gs101_vsync_frame_rem(u64 now_us,
						  struct gs101_vsync_rec *vs,
						  u32 *rem_us)
{
	u64 elapsed;

	gs101_read_vsync(vs);
	if (!vs->period_us || !vs->ts_us || now_us < vs->ts_us)
		return false;

	elapsed = now_us - vs->ts_us;
	/* Expire after 4 missed VSync periods (idle/static command-mode panel) */
	if (elapsed > (u64)vs->period_us * 4ULL)
		return false;

	while (elapsed >= vs->period_us)
		elapsed -= vs->period_us;

	*rem_us = vs->period_us - (u32)elapsed;
	return true;
}

static __always_inline u16 gs101_dvfs_rem_latch_us(const struct gs101_dvfs_rec *d,
						   u64 now_us)
{
	u64 elapsed;

	if (d->phase == GS101_DVFS_SETTLED || !d->submit_us || now_us < d->submit_us)
		return 0;

	elapsed = now_us - d->submit_us;
	if (elapsed >= d->ewma_latch_us)
		return 0;

	return d->ewma_latch_us - (u16)elapsed;
}

/* =========================================================================
 * LAYER 2: IMPLICIT USERSPACE SIGNAL CAPTURE
 * ========================================================================= */

void gs101_frame_task_init(struct task_struct *p)
{
	memset(&p->gs101_frame, 0, sizeof(p->gs101_frame));
}

void gs101_frame_note_uclamp(struct task_struct *p, u16 new_min, u16 new_max)
{
	struct gs101_task_frame *f = &p->gs101_frame;
	s32 delta;

	if (new_min > 0) {
		delta = (s32)new_min - (s32)f->adpf_prev_min;
		f->adpf_trend = (s16)clamp_t(s32,
				((s32)f->adpf_trend * 3 + delta) >> 2,
				-256, 256);
		f->adpf_prev_min = new_min;
		WRITE_ONCE(f->flags, f->flags | GTF_ADPF_ACTIVE);
	} else {
		f->adpf_prev_min = 0;
		f->adpf_trend = 0;
		WRITE_ONCE(f->flags, 0);
	}
}

void gs101_frame_note_exec(struct task_struct *p, u64 delta_exec_ns)
{
	struct gs101_task_frame *f = &p->gs101_frame;
	struct gs101_vsync_rec vs;
	struct gs101_cpu_line *cline;
	u64 now_us;
	u32 rem_us, exec_us;
	u8 flags = READ_ONCE(f->flags);

	if (!(flags & GTF_ADPF_ACTIVE))
		return;

	now_us = gs101_now_us();
	cline = &per_cpu(gs101_cpu_bus, task_cpu(p));
	WRITE_ONCE(cline->last_adpf_us, now_us);

	if (!gs101_vsync_frame_rem(now_us, &vs, &rem_us))
		return;

	if (f->frame_seq != vs.seq) {
		u16 prev = (u16)min_t(u32, f->frame_exec_us, U16_MAX);

		if (prev) {
			f->ewma_work_us = f->ewma_work_us ?
				(u16)((3U * f->ewma_work_us + prev) >> 2) : prev;
		}
		f->frame_seq = vs.seq;
		f->frame_exec_us = 0;
		flags &= ~GTF_OVERRUN_THIS_FRAME;
	}

	exec_us = f->frame_exec_us + (u32)(delta_exec_ns >> 10);
	f->frame_exec_us = exec_us;

	/*
	 * Mid-frame overrun detection: task already burned over half the VSync
	 * period and either has <25% frame slack left or exceeded its EWMA
	 * work budget by >500 us.
	 */
	if (exec_us > (vs.period_us >> 1) &&
	    (rem_us < (vs.period_us >> 2) ||
	     (f->ewma_work_us && exec_us > f->ewma_work_us + 500U))) {
		flags |= GTF_OVERRUN_THIS_FRAME;
		WRITE_ONCE(cline->overrun_seq, vs.seq);
	}

	WRITE_ONCE(f->flags, flags);
}

/* =========================================================================
 * LAYER 3: WAIT-FREE SCHEDULER & SCHEDUTIL CONSUMER QUERIES
 * ========================================================================= */

bool gs101_frame_task_hint(const struct task_struct *p, int target_cpu,
			   struct gs101_sched_hint *out)
{
	const struct gs101_task_frame *f = &p->gs101_frame;
	struct gs101_vsync_rec vs;
	struct gs101_dvfs_rec dvfs;
	u64 now_us;
	u32 rem_us = 0;
	u8 tflags = READ_ONCE(f->flags);
	u8 hflags = 0;
	bool touch_on, vsync_ok;
	unsigned long uc_min, uc_max, floor;

	if (!(tflags & GTF_ADPF_ACTIVE))
		return false;

	now_us = gs101_now_us();
	vsync_ok = gs101_vsync_frame_rem(now_us, &vs, &rem_us);
	touch_on = gs101_touch_is_active(now_us);
	if (!vsync_ok && !touch_on)
		return false;

	uc_min = uclamp_eff_value((struct task_struct *)p, UCLAMP_MIN);
	uc_max = uclamp_eff_value((struct task_struct *)p, UCLAMP_MAX);
	floor = uc_min;

	if (gs101_cpu_is_mem_bound(target_cpu, now_us))
		hflags |= GFH_MEM_BOUND;

	if (touch_on || (vsync_ok && rem_us < (vs.period_us >> 1)))
		hflags |= GFH_FRAME_CRITICAL;

	out->slice_ns = 0;

	/*
	 * Closed-loop intra-frame rescue: escalate floor + tighten EEVDF slice
	 * when CPU work is outrunning the VSync budget and not memory-bound.
	 */
	if (((tflags & GTF_OVERRUN_THIS_FRAME) || READ_ONCE(f->adpf_trend) > 24) &&
	    !(hflags & GFH_MEM_BOUND)) {
		unsigned long util_est = READ_ONCE(p->se.avg.util_est) & ~UTIL_AVG_UNCHANGED;
		unsigned long base = max(uc_min, util_est);

		floor = min(uc_max, base + (base >> 2));
		out->slice_ns = GS101_RESCUE_SLICE_NS;
		hflags |= GFH_OVERRUN_RESCUE | GFH_FRAME_CRITICAL;
	}

	gs101_read_dvfs(gs101_cpu_to_cluster(target_cpu), &dvfs);
	out->util_floor = min(floor, uc_max);
	out->util_cap = uc_max;
	out->rem_frame_us = rem_us;
	out->dvfs_stall_us = gs101_dvfs_rem_latch_us(&dvfs, now_us);
	out->flags = hflags;

	return true;
}

bool gs101_frame_cluster_ready(int cpu, unsigned long req_util,
			       u32 budget_slack_us)
{
	struct gs101_dvfs_rec dvfs;
	u64 now_us = gs101_now_us();
	u16 rem_latch;

	gs101_read_dvfs(gs101_cpu_to_cluster(cpu), &dvfs);
	if (dvfs.phase != GS101_DVFS_RAMP_UP)
		return true;

	if (dvfs.latched_cap >= req_util)
		return true;

	rem_latch = gs101_dvfs_rem_latch_us(&dvfs, now_us);
	if (!rem_latch || !budget_slack_us)
		return true;

	return rem_latch <= (budget_slack_us >> 2);
}

enum gs101_sugov_action
gs101_frame_sugov_advise(const struct cpufreq_policy *policy,
			 u32 cur_next_khz, u32 req_khz, s64 elapsed_ns)
{
	int cl_id = gs101_cpu_to_cluster(policy->cpu);
	struct gs101_vsync_rec vs;
	u64 now_us = gs101_now_us();
	u32 rem_us = 0;
	bool vsync_ok, touch_on, adpf_recent = false, overrun = false;
	int cpu;

	vsync_ok = gs101_vsync_frame_rem(now_us, &vs, &rem_us);
	touch_on = gs101_touch_is_active(now_us);

	for_each_cpu(cpu, policy->cpus) {
		struct gs101_cpu_line *cline = &per_cpu(gs101_cpu_bus, cpu);
		u64 last = READ_ONCE(cline->last_adpf_us);

		if (last && now_us >= last &&
		    (now_us - last) <= GS101_ADPF_CLUSTER_HOLD_US)
			adpf_recent = true;
		if (vsync_ok && READ_ONCE(cline->overrun_seq) == vs.seq)
			overrun = true;
	}

	/* 1. Up-scale path: never drop higher target; bypass rate limit on rescue/touch */
	if (req_khz > cur_next_khz) {
		if (overrun || touch_on)
			return GS101_SUGOV_URGENT_UP;

		return GS101_SUGOV_NORMAL;
	}

	/* 2. Down-scale path: phase-lock MID/BIG hold to active VSync cadence */
	if (req_khz < cur_next_khz && cl_id >= 1 && (adpf_recent || touch_on)) {
		u64 hold_us = vsync_ok ?
			min_t(u64, (u64)vs.period_us + 2000ULL, 18000ULL) :
			12000ULL;

		if (elapsed_ns < (s64)(hold_us * NSEC_PER_USEC))
			return GS101_SUGOV_HOLD_FLOOR;
	}

	return GS101_SUGOV_NORMAL;
}
