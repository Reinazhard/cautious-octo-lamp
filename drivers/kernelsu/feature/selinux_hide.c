#include "selinux_hide.h"
#include <linux/init.h>

/*
 * Runtime .rodata / LSM static-call patching is intentionally omitted in
 * built-in manual-hook mode, so KSU_FEATURE_SELINUX_HIDE is not registered
 * and cleanly reports as unsupported to userspace.
 */
void __init ksu_selinux_hide_init(void)
{
}

void __exit ksu_selinux_hide_exit(void)
{
}

void ksu_selinux_hide_drop_backup_if_unused(void)
{
}

void ksu_selinux_hide_handle_second_stage(void)
{
}

void ksu_selinux_hide_handle_post_fs_data(void)
{
}
