#include <linux/export.h>
#include <linux/fs.h>
#include <linux/kobject.h>
#include <linux/module.h>
#include <linux/rcupdate.h>
#include <linux/sched.h>
#include <linux/workqueue.h>
#include <linux/moduleparam.h>

#include "policy/allowlist.h"
#include "policy/app_profile.h"
#include "policy/feature.h"
#include "klog.h" // IWYU pragma: keep
#include "manager/manager_observer.h"
#include "manager/throne_tracker.h"
#include "hook/setuid_hook.h"
#include "feature/sucompat.h"
#include "runtime/ksud.h"
#include "runtime/ksud_boot.h"
#include "feature/sulog.h"
#include "supercall/supercall.h"
#include "ksu.h"
#include "infra/file_wrapper.h"
#include "selinux/selinux.h"
#include "feature/adb_root.h"
#include "feature/selinux_hide.h"
#include "feature/module_blacklist.h"
#include "feature/shell_su_adb_only.h"
#include "infra/symbol_resolver.h"

struct cred *ksu_cred;
bool ksu_late_loaded = false;

bool allow_shell = true;
module_param(allow_shell, bool, 0);

bool ksu_no_custom_rc = false;
module_param_named(norc, ksu_no_custom_rc, bool, 0);

int __init kernelsu_init(void)
{
    pr_info("welcome to KernelSU version " __stringify(KERNEL_SU_VERSION) ", package name " KSU_PACKAGE_NAME "\n");

#ifdef CONFIG_KSU_DEBUG
    pr_alert("*************************************************************");
    pr_alert("**     NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE    **");
    pr_alert("**                                                         **");
    pr_alert("**         You are running KernelSU in DEBUG mode          **");
    pr_alert("**                                                         **");
    pr_alert("**     NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE NOTICE    **");
    pr_alert("*************************************************************");
#endif
    if (allow_shell)
        pr_alert("shell is allowed at init!");

    ksu_cred = prepare_creds();
    if (!ksu_cred) {
        pr_err("prepare cred failed!\n");
        return -ENOSYS;
    }

    ksu_init_symbol_resolver();

    // Block modules early via native module_blacklist (works with INTEGRATE_MODULES).
    ksu_module_blacklist_init();

    ksu_feature_init();
    ksu_supercalls_init();
    ksu_sucompat_init();
    ksu_setuid_hook_init();
    ksu_sulog_init();
    ksu_adb_root_init();
    ksu_selinux_hide_init();
    ksu_shell_su_adb_only_init();
    ksu_app_profile_init();

    ksu_allowlist_init();
    ksu_throne_tracker_init();
    ksu_ksud_init();
    ksu_file_wrapper_init();

    return 0;
}

void __exit kernelsu_exit(void)
{
    ksu_supercalls_exit();
    ksu_ksud_exit();

    synchronize_rcu();

    ksu_observer_exit();
    ksu_throne_tracker_exit();
    ksu_allowlist_exit();

    ksu_shell_su_adb_only_exit();
    ksu_selinux_hide_exit();
    ksu_adb_root_exit();
    ksu_sulog_exit();
    ksu_setuid_hook_exit();
    ksu_sucompat_exit();
    ksu_feature_exit();
    ksu_module_blacklist_exit();

    put_cred(ksu_cred);
}

module_init(kernelsu_init);
module_exit(kernelsu_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("weishu");
MODULE_DESCRIPTION("Android KernelSU");
MODULE_IMPORT_NS("VFS_internal_I_am_really_a_filesystem_and_am_NOT_a_driver");
