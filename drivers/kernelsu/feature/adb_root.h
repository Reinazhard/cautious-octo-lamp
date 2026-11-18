#ifndef __KSU_H_ADB_ROOT
#define __KSU_H_ADB_ROOT

#include <linux/static_key.h>

extern struct static_key_false ksu_adb_root;

long ksu_adb_root_handle_execveat(const char *filename, void ***envp_user_ptr);

void __init ksu_adb_root_init(void);

void __exit ksu_adb_root_exit(void);

#endif
