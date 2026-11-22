/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Google, Inc.
 */
#ifndef _TRUSTY_SMC_H
#define _TRUSTY_SMC_H

#include <linux/kmsan-checks.h>
#include <linux/types.h>

struct smc_ret8 {
	unsigned long r0;
	unsigned long r1;
	unsigned long r2;
	unsigned long r3;
	unsigned long r4;
	unsigned long r5;
	unsigned long r6;
	unsigned long r7;
};

struct smc_ret8 __trusty_smc8(unsigned long r0, unsigned long r1,
			      unsigned long r2, unsigned long r3,
			      unsigned long r4, unsigned long r5,
			      unsigned long r6, unsigned long r7);

static inline struct smc_ret8 trusty_smc8(unsigned long r0, unsigned long r1,
					  unsigned long r2, unsigned long r3,
					  unsigned long r4, unsigned long r5,
					  unsigned long r6, unsigned long r7)
{
	struct smc_ret8 ret;

	ret = __trusty_smc8(r0, r1, r2, r3, r4, r5, r6, r7);
	kmsan_unpoison_memory(&ret, sizeof(ret));
	return ret;
}

#endif /* _TRUSTY_SMC_H */
