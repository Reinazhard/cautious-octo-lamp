#include <linux/file.h>
#include <linux/fcntl.h>
#include <linux/namei.h>
#include <linux/compiler_types.h>
#include <linux/preempt.h>
#include <linux/printk.h>
#include <linux/mm.h>
#include <linux/pgtable.h>
#include <linux/uaccess.h>
#include <asm/current.h>
#include <linux/cred.h>
#include <linux/fs.h>
#include <linux/fs_struct.h>
#include <linux/types.h>
#include <linux/version.h>
#include <linux/ptrace.h>

#include "policy/allowlist.h"
#include "policy/feature.h"
#include "klog.h" // IWYU pragma: keep
#include "runtime/ksud.h"
#include "feature/sucompat.h"
#include "feature/adb_root.h"
#include "policy/app_profile.h"
#include "supercall/supercall.h"
#include "sulog/event.h"
#include "selinux/selinux.h"
#include "ksu.h"
#include "util.h"

#define SU_PATH "/system/bin/su"
#define SU_PATH_XBIN "/system/xbin/su"
#define SH_PATH "/system/bin/sh"

static const char sh_path[] = SH_PATH;
static const char ksud_path[] = KSUD_PATH;

DEFINE_STATIC_KEY_TRUE(ksu_su_compat_enabled);

static inline bool is_su_path(const char *name)
{
    if (!name)
        return false;
    return !strcmp(name, SU_PATH) || !strcmp(name, SU_PATH_XBIN);
}

static bool is_ksud_exists(void)
{
    struct path path;

    if (kern_path(KSUD_PATH, 0, &path) < 0)
        return false;

    path_put(&path);
    return true;
}

static int su_compat_feature_get(u64 *value)
{
    *value = static_key_enabled(&ksu_su_compat_enabled) ? 1 : 0;
    return 0;
}

static int su_compat_feature_set(u64 value)
{
    bool enable = value != 0;

    if (enable)
        static_branch_enable(&ksu_su_compat_enabled);
    else
        static_branch_disable(&ksu_su_compat_enabled);
    pr_info("su_compat: set to %d\n", enable);
    return 0;
}

static const struct ksu_feature_handler su_compat_handler = {
    .feature_id = KSU_FEATURE_SU_COMPAT,
    .name = "su_compat",
    .get_handler = su_compat_feature_get,
    .set_handler = su_compat_feature_set,
};

static int ksu_handle_execveat_init(struct filename *filename, struct user_arg_ptr *argv_user,
                                    struct user_arg_ptr *envp_user)
{
    int ret;

    if (current->pid == 1)
        return -EINVAL;

    if (!is_init(current_cred()))
        return -EINVAL;

    if (unlikely(!strcmp(filename->name, KSUD_PATH))) {
        struct ksu_sulog_pending_event *pending_sucompat;
        const char __user *const __user *uargv = argv_user ? argv_user->ptr.native : NULL;

        pr_info("hook_manager: escape to root for init executing ksud: %d\n", current->pid);
        pending_sucompat = ksu_sulog_capture_sucompat((const char __user *)filename->name, uargv, GFP_KERNEL);
        escape_to_root_for_init();
        ksu_sulog_emit_pending(pending_sucompat, 0, GFP_KERNEL);
        return 0;
    }

    if (static_branch_unlikely(&ksu_adb_root)) {
#ifdef CONFIG_COMPAT
        if (unlikely(envp_user && envp_user->is_compat))
            ret = ksu_adb_root_handle_execveat(filename->name, (void ***)&envp_user->ptr.compat);
        else
            ret = ksu_adb_root_handle_execveat(filename->name, envp_user ? (void ***)&envp_user->ptr.native : NULL);
#else
        ret = ksu_adb_root_handle_execveat(filename->name, envp_user ? (void ***)&envp_user->ptr.native : NULL);
#endif
        if (ret)
            pr_err("adb root failed: %d\n", ret);
        return ret;
    }

    return -EINVAL;
}

int ksu_handle_execveat_sucompat(int *fd, struct filename **filename_ptr, void *argv_user, void *envp_user, int *flags)
{
    struct filename *filename;
    struct ksu_sulog_pending_event *pending_sucompat = NULL;
    const struct cred *old_cred;
    bool ksud_ok;
    int ret;

    if (unlikely(!filename_ptr))
        return -EINVAL;

    filename = *filename_ptr;
    if (IS_ERR(filename) || !filename->name)
        return -EINVAL;

    if (!ksu_handle_execveat_init(filename, (struct user_arg_ptr *)argv_user, (struct user_arg_ptr *)envp_user))
        return -EINVAL;

    if (!static_branch_likely(&ksu_su_compat_enabled))
        return -EINVAL;

    if (likely(!__ksu_is_allow_uid_for_current(current_uid().val)))
        return -EINVAL;

    if (likely(!is_su_path(filename->name)))
        return -EINVAL;

    if (current_chrooted()) {
        pr_err("ksu_handle_execveat_sucompat: su found but NOT allowed! chrooted\n");
        return -EINVAL;
    }

    old_cred = override_creds(ksu_cred);
    ksud_ok = is_ksud_exists();
    revert_creds(old_cred);
    if (!ksud_ok)
        return -EINVAL;

    {
        const char __user *const __user *uargv = argv_user ? ((struct user_arg_ptr *)argv_user)->ptr.native : NULL;
        pending_sucompat = ksu_sulog_capture_sucompat((const char __user *)filename->name, uargv, GFP_KERNEL);
    }

    ret = escape_with_root_profile();
    ksu_sulog_emit_pending(pending_sucompat, ret, GFP_KERNEL);
    if (ret) {
        pr_err("escape_with_root_profile failed: %d\n", ret);
        return ret;
    }

    pr_info("ksu_handle_execveat_sucompat: su->ksud!\n");
    memcpy((void *)filename->name, ksud_path, sizeof(ksud_path));
    return 0;
}

int ksu_handle_post_execveat_sucompat(int *fd, struct filename **filename_ptr, void *argv_user, void *envp_user,
                                      int *flags, int *retval)
{
    int su_fd;

    if (unlikely(!retval))
        return 0;

    if (*retval >= 0) {
        su_fd = ksu_install_su_fd();
        if (su_fd < 0)
            pr_warn("install su session fd failed: %d\n", su_fd);
    }
    return 0;
}

int ksu_handle_execveat(int *fd, struct filename **filename_ptr, void *argv, void *envp, int *flags)
{
    if (static_branch_unlikely(&is_first_zygote))
        ksu_handle_execveat_ksud(fd, filename_ptr, (struct user_arg_ptr *)argv, (struct user_arg_ptr *)envp, flags);

    return ksu_handle_execveat_sucompat(fd, filename_ptr, argv, envp, flags);
}

int ksu_handle_faccessat(int *dfd, struct filename **filename, int *mode, int *__unused_flags)
{
    const struct cred *old_cred;
    bool ksud_ok;

    if (unlikely(!filename || IS_ERR(*filename) || !(*filename)->name))
        return 0;

    if (!ksu_is_allow_uid_for_current(current_uid().val))
        return 0;

    if (likely(!is_su_path((*filename)->name)))
        return 0;

    if (current_chrooted()) {
        pr_err("ksu_handle_faccessat: su found but NOT allowed! chrooted\n");
        return 0;
    }

    old_cred = override_creds(ksu_cred);
    ksud_ok = is_ksud_exists();
    revert_creds(old_cred);
    if (!ksud_ok)
        return 0;

    pr_info("ksu_handle_faccessat: su->sh!\n");
    memcpy((void *)((*filename)->name), sh_path, sizeof(sh_path));
    return 0;
}

int ksu_handle_stat(int *dfd, struct filename **filename, int *flags)
{
    const struct cred *old_cred;
    bool ksud_ok;

    if (unlikely(!filename || IS_ERR(*filename) || !(*filename)->name))
        return 0;

    if (!ksu_is_allow_uid_for_current(current_uid().val))
        return 0;

    if (likely(!is_su_path((*filename)->name)))
        return 0;

    if (current_chrooted()) {
        pr_err("ksu_handle_stat: su found but NOT allowed! chrooted\n");
        return 0;
    }

    old_cred = override_creds(ksu_cred);
    ksud_ok = is_ksud_exists();
    revert_creds(old_cred);
    if (!ksud_ok)
        return 0;

    pr_info("ksu_handle_stat: su->sh!\n");
    memcpy((void *)((*filename)->name), sh_path, sizeof(sh_path));
    return 0;
}

void __init ksu_sucompat_init(void)
{
    if (ksu_register_feature_handler(&su_compat_handler)) {
        pr_err("Failed to register su_compat feature handler\n");
    }
}

void __exit ksu_sucompat_exit(void)
{
    ksu_unregister_feature_handler(KSU_FEATURE_SU_COMPAT);
}
