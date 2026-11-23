/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_SCHED_GS101_FRAME_H
#define _LINUX_SCHED_GS101_FRAME_H

#include <linux/types.h>

struct task_struct;
struct cpufreq_policy;

enum gs101_touch_state {
	GS101_TOUCH_IDLE = 0,
	GS101_TOUCH_ACTIVE,		/* finger down / dragging */
	GS101_TOUCH_FLING,		/* lift after motion; coast window */
};

enum gs101_sugov_action {
	GS101_SUGOV_NORMAL = 0,		/* run standard schedutil logic */
	GS101_SUGOV_HOLD_FLOOR,		/* hold OPP across active VSync/touch gap */
	GS101_SUGOV_SKIP_INFLIGHT,	/* in-flight ACPM IPC already >= req_khz */
	GS101_SUGOV_URGENT_UP,		/* commit immediately: frame overrun risk */
};

/* Per-task implicit frame-budget tracker embedded in struct task_struct. */
struct gs101_task_frame {
	u32	frame_seq;		/* VSync epoch of current frame_exec_us */
	u32	frame_exec_us;		/* CPU time burned inside this VSync epoch */
	u16	ewma_work_us;		/* Smoothed per-frame CPU work */
	u16	adpf_prev_min;		/* Previous sched_setattr uclamp_min */
	s16	adpf_trend;		/* EWMA slope of PowerHAL uclamp_min changes */
	u8	flags;			/* GTF_* */
	u8	_pad;
};

/* struct gs101_sched_hint::flags */
#define GFH_FRAME_CRITICAL	(1U << 0)
#define GFH_OVERRUN_RESCUE	(1U << 1)
#define GFH_MEM_BOUND		(1U << 2)

struct gs101_sched_hint {
	u64		slice_ns;	/* EEVDF target slice (0 = keep default) */
	unsigned long	util_floor;	/* Effective perf floor [0..1024] */
	unsigned long	util_cap;	/* Energy/thermal ceiling [0..1024] */
	u32		rem_frame_us;	/* Remaining us until next TE/VSync */
	u16		dvfs_stall_us;	/* Estimated ACPM latch stall on cluster */
	u8		flags;		/* GFH_* */
};

#ifdef CONFIG_SCHED_GS101_FRAME

/* Layer 1: Lockless hardware signal publishers (safe from IRQ/driver context) */
void gs101_frame_publish_vsync(u32 period_us);
void gs101_frame_publish_touch(enum gs101_touch_state state);
void gs101_frame_dvfs_submit(int cpu, u32 target_khz, u16 target_cap);
void gs101_frame_dvfs_latched(int cpu, u32 latched_khz, u16 latched_cap);
void gs101_frame_publish_cpu_stall(int cpu, u16 mem_stall_permille);
void gs101_frame_publish_mif(u16 mif_pressure_scale, bool throttled);

/* Layer 2: Implicit userspace signal capture (kernel/sched/ internal) */
void gs101_frame_task_init(struct task_struct *p);
void gs101_frame_note_uclamp(struct task_struct *p, u16 new_min, u16 new_max);
void gs101_frame_note_exec(struct task_struct *p, u64 delta_exec_ns);

/* Layer 3: Wait-free scheduler & schedutil consumer queries */
bool gs101_frame_task_hint(const struct task_struct *p, int target_cpu,
			   struct gs101_sched_hint *out);
bool gs101_frame_cluster_ready(int cpu, unsigned long req_util,
			       u32 budget_slack_us);
enum gs101_sugov_action
gs101_frame_sugov_advise(const struct cpufreq_policy *policy,
			 u32 cur_next_khz, u32 req_khz, s64 elapsed_ns);

#else /* !CONFIG_SCHED_GS101_FRAME */

static inline void gs101_frame_publish_vsync(u32 period_us) { }
static inline void gs101_frame_publish_touch(enum gs101_touch_state state) { }
static inline void gs101_frame_dvfs_submit(int cpu, u32 target_khz,
					   u16 target_cap) { }
static inline void gs101_frame_dvfs_latched(int cpu, u32 latched_khz,
					    u16 latched_cap) { }
static inline void gs101_frame_publish_cpu_stall(int cpu,
						 u16 mem_stall_permille) { }
static inline void gs101_frame_publish_mif(u16 mif_pressure_scale,
					   bool throttled) { }
static inline void gs101_frame_task_init(struct task_struct *p) { }
static inline void gs101_frame_note_uclamp(struct task_struct *p,
					   u16 new_min, u16 new_max) { }
static inline void gs101_frame_note_exec(struct task_struct *p,
					 u64 delta_exec_ns) { }
static inline bool gs101_frame_task_hint(const struct task_struct *p,
					 int target_cpu,
					 struct gs101_sched_hint *out)
{
	return false;
}
static inline bool gs101_frame_cluster_ready(int cpu, unsigned long req_util,
					     u32 budget_slack_us)
{
	return true;
}
static inline enum gs101_sugov_action
gs101_frame_sugov_advise(const struct cpufreq_policy *policy,
			 u32 cur_next_khz, u32 req_khz, s64 elapsed_ns)
{
	return GS101_SUGOV_NORMAL;
}

#endif /* CONFIG_SCHED_GS101_FRAME */

#endif /* _LINUX_SCHED_GS101_FRAME_H */
