/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arm64 KMSAN support.
 */

#ifndef __ASM_KMSAN_H
#define __ASM_KMSAN_H

#ifndef MODULE

#include <asm/memory.h>
#include <asm/sections.h>
#include <linux/kmsan.h>
#include <linux/mmzone.h>
#include <linux/preempt.h>

/*
 * Functions below are declared in the header to make sure they are inlined.
 * They all are called from kmsan_get_metadata() for every memory access in
 * the kernel, so speed is important here.
 */

/*
 * The kernel image is mapped inside the vmalloc area, but, unlike vmalloc()
 * memory, it has no metadata mapped at the corresponding vmalloc shadow and
 * origin addresses. Its metadata belongs to the struct pages backing the
 * image, so look it up via the linear map alias instead. Avoid lm_alias(),
 * which may call into the instrumented __phys_addr_symbol().
 */
static inline void *arch_kmsan_get_meta_or_null(void *addr, bool is_origin)
{
	unsigned long addr64 = (unsigned long)addr;

	if (addr64 - (unsigned long)_text >=
	    (unsigned long)_end - (unsigned long)_text)
		return NULL;

	addr64 = __phys_to_virt(__kimg_to_phys(addr64));
	return kmsan_get_metadata((void *)addr64, is_origin);
}

/*
 * Non-instrumented PFN validity check mirroring generic SPARSEMEM pfn_valid().
 * Bracket the section lookup with raw __preempt_count_inc() and
 * __preempt_count_dec() instead of preempt_disable()/preempt_enable_no_resched()
 * so CONFIG_DEBUG_PREEMPT=y never recurses into instrumented preempt_count_add().
 */
static inline bool kmsan_pfn_valid(unsigned long pfn)
{
#ifdef CONFIG_SPARSEMEM
	struct mem_section *ms;
	bool ret;

	if (PHYS_PFN(PFN_PHYS(pfn)) != pfn)
		return false;

	if (pfn_to_section_nr(pfn) >= NR_MEM_SECTIONS)
		return false;

	ms = __pfn_to_section(pfn);
#ifdef CONFIG_PREEMPT_COUNT
	__preempt_count_inc();
	barrier();
#endif
	ret = valid_section(ms) &&
	      (early_section(ms) || pfn_section_valid(ms, pfn));
#ifdef CONFIG_PREEMPT_COUNT
	barrier();
	__preempt_count_dec();
#endif
	return ret;
#else
	return pfn_valid(pfn);
#endif
}

/*
 * Only addresses in the linear map are backed by struct pages that can be
 * found with virt_to_page(). Use a non-instrumented check so
 * kmsan_get_metadata() never triggers recursive preempt_count_add() calls.
 */
static inline bool kmsan_virt_addr_valid(void *addr)
{
	unsigned long addr64 = (unsigned long)__tag_reset(addr);

	if (!__is_lm_address(addr64))
		return false;

	return kmsan_pfn_valid(__phys_to_pfn(__lm_to_phys(addr64)));
}

#endif /* !MODULE */

#endif /* __ASM_KMSAN_H */
