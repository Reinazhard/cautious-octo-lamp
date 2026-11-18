#include <linux/compiler.h>
#include <linux/version.h>
#include <linux/slab.h>
#include <linux/task_work.h>
#include <linux/thread_info.h>
#include <linux/seccomp.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <linux/sched/signal.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/uaccess.h>
#include <linux/uidgid.h>

#include "policy/allowlist.h"
#include "hook/setuid_hook.h"
#include "klog.h" // IWYU pragma: keep
#include "manager/manager_identity.h"
#include "infra/seccomp_cache.h"
#include "supercall/supercall.h"
#include "feature/kernel_umount.h"

int ksu_handle_setresuid(uid_t ruid, uid_t euid, uid_t suid)
{
    uid_t old_uid = current_uid().val;
    uid_t new_uid = ruid;

    // Android Zygote always calls setresuid(uid, uid, uid) with identical UIDs.
    if (ruid != euid || euid != suid)
        return 0;

    if (old_uid != 0)
        return 0;

    if (unlikely(is_uid_manager(new_uid))) {
        if (current->sighand) {
            spin_lock_irq(&current->sighand->siglock);
            if (current->seccomp.mode == SECCOMP_MODE_FILTER && current->seccomp.filter)
                ksu_seccomp_allow_cache(current->seccomp.filter, __NR_reboot);
            spin_unlock_irq(&current->sighand->siglock);
        }

        pr_info("install fd for manager: %d\n", new_uid);
        ksu_install_fd();
        return 0;
    }

    if (ksu_is_allow_uid_for_current(new_uid)) {
        if (current->seccomp.mode == SECCOMP_MODE_FILTER && current->seccomp.filter) {
            if (current->sighand) {
                spin_lock_irq(&current->sighand->siglock);
                ksu_seccomp_allow_cache(current->seccomp.filter, __NR_reboot);
                spin_unlock_irq(&current->sighand->siglock);
            }
        }
    }

    // Handle kernel umount
    ksu_handle_umount(old_uid, new_uid);

    return 0;
}

void __init ksu_setuid_hook_init(void)
{
    ksu_kernel_umount_init();
}

void __exit ksu_setuid_hook_exit(void)
{
    pr_info("ksu_core_exit\n");
    ksu_kernel_umount_exit();
}
