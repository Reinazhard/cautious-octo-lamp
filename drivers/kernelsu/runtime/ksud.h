#ifndef __KSU_H_KSUD
#define __KSU_H_KSUD

#include <linux/fs.h>
#include <linux/jump_label.h>
#include <linux/types.h>

#include "ksu.h"

#define KSUD_PATH "/data/adb/ksud"

extern struct static_key_true ksu_is_init_rc_hook_enabled;
extern struct static_key_true ksu_is_input_hook_enabled;
extern struct static_key_true is_first_zygote;

void __init ksu_ksud_init(void);
void __exit ksu_ksud_exit(void);

int ksu_handle_execveat_ksud(int *fd, struct filename **filename_ptr, struct user_arg_ptr *argv,
                             struct user_arg_ptr *envp, int *flags);
void ksu_handle_sys_read(unsigned int fd);
void ksu_handle_vfs_fstat(int fd, loff_t *kstat_size_ptr);
int ksu_handle_input_handle_event(unsigned int *type, unsigned int *code, int *value);
void ksu_stop_input_hook_runtime(void);

#endif
