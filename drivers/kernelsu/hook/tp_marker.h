#ifndef __KSU_H_TP_MARKER
#define __KSU_H_TP_MARKER

#include <linux/sched.h>
#include <linux/errno.h>

// Syscall tracepoint marking is unused in built-in manual-hook mode.
static inline void ksu_mark_all_process(void)
{
}

static inline void ksu_unmark_all_process(void)
{
}

static inline void ksu_mark_running_process(void)
{
}

static inline int ksu_get_task_mark(pid_t pid)
{
    return -EOPNOTSUPP;
}

static inline int ksu_set_task_mark(pid_t pid, bool mark)
{
    return -EOPNOTSUPP;
}

static inline void ksu_set_task_tracepoint_flag(struct task_struct *t)
{
}

static inline void ksu_clear_task_tracepoint_flag(struct task_struct *t)
{
}

static inline void ksu_clear_task_tracepoint_flag_if_needed(struct task_struct *t)
{
}

static inline void ksu_mark_running_process_locked(void)
{
}

#endif
