// SPDX-License-Identifier: GPL-2.0

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/highmem.h>
#include <linux/cpuhotplug.h>
#include <linux/local_lock.h>
#include <linux/lz4.h>
#include <linux/lzo.h>

#include "zcomp.h"

static const char * const backends[] = {
	"lzo",
	"lzo-rle",
	"lz4",
};

struct zcomp_strm {
	/* The members ->buffer and ->workmem are protected by ->lock. */
	local_lock_t lock;
	void *buffer;
	void *workmem;
};

struct zcomp_strm *zcomp_stream_get(struct zcomp *comp)
{
	struct zcomp_strm *zstrm = comp->private;

	local_lock(&zstrm->lock);
	return this_cpu_ptr(zstrm);
}

void zcomp_stream_put(struct zcomp *comp)
{
	struct zcomp_strm *zstrm = comp->private;

	local_unlock(&zstrm->lock);
}

int zcomp_cpu_compress(struct zcomp *comp, struct page *page,
				struct zcomp_cookie *cookie)
{
	int err = 0;
	unsigned int comp_len = 0;
	void *src;
	struct zcomp_strm *stream;

	/*
	 * Our dst memory (stream->buffer) is always `2 * PAGE_SIZE' sized
	 * because sometimes we can end up having a bigger compressed data
	 * due to padding or incompressible data.
	 */
	stream = zcomp_stream_get(comp);
	src = kmap_atomic(page);

	if (!strcmp(comp->algo_name, "lz4")) {
		int ret = LZ4_compress_fast(src, stream->buffer, PAGE_SIZE,
					    PAGE_SIZE * 2, LZ4_ACCELERATION_DEFAULT,
					    stream->workmem);
		if (ret <= 0)
			err = -EINVAL;
		else
			comp_len = ret;
	} else if (!strcmp(comp->algo_name, "lzo") || !strcmp(comp->algo_name, "lzo-rle")) {
		size_t dlen = PAGE_SIZE * 2;
		int ret = lzo1x_1_compress(src, PAGE_SIZE, stream->buffer,
					   &dlen, stream->workmem);
		if (ret != LZO_E_OK)
			err = -EINVAL;
		else
			comp_len = dlen;
	} else {
		err = -EOPNOTSUPP;
	}

	kunmap_atomic(src);
	if (unlikely(err)) {
		zcomp_stream_put(comp);
		pr_err("Compression failed! err=%d\n", err);
	}

	err = zcomp_copy_buffer(err, stream->buffer, comp_len, cookie);
	zcomp_stream_put(comp);

	return err;
}

int zcomp_cpu_decompress(struct zcomp *comp, void *src,
			unsigned int src_len, struct page *page)
{
	void *dst;
	unsigned int dst_len = PAGE_SIZE;
	int ret;

	dst = kmap_atomic(page);

	if (!strcmp(comp->algo_name, "lz4")) {
		ret = LZ4_decompress_safe(src, dst, src_len, dst_len);
		if (ret < 0)
			ret = -EINVAL;
		else
			ret = 0;
	} else if (!strcmp(comp->algo_name, "lzo") || !strcmp(comp->algo_name, "lzo-rle")) {
		size_t dlen = dst_len;
		ret = lzo1x_decompress_safe(src, src_len, dst, &dlen);
		if (ret != LZO_E_OK)
			ret = -EINVAL;
		else
			ret = 0;
	} else {
		ret = -EOPNOTSUPP;
	}

	kunmap_atomic(dst);

	return ret;
}

static void zcomp_strm_free(struct zcomp_strm *zstrm)
{
	kfree(zstrm->workmem);
	zstrm->workmem = NULL;
	if (zstrm->buffer) {
		free_pages((unsigned long)zstrm->buffer, 1);
		zstrm->buffer = NULL;
	}
}

/*
 * Initialize zcomp_strm structure with work memory and 2-page buffer.
 * Return a negative value on error.
 */
static int zcomp_strm_init(struct zcomp_strm *zstrm, struct zcomp *comp)
{
	size_t workmem_size = max_t(size_t, LZ4_MEM_COMPRESS, LZO1X_MEM_COMPRESS);

	zstrm->workmem = kzalloc(workmem_size, GFP_KERNEL);
	zstrm->buffer = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO, 1);
	if (!zstrm->workmem || !zstrm->buffer) {
		zcomp_strm_free(zstrm);
		return -ENOMEM;
	}
	return 0;
}

int zcomp_cpu_create(struct zcomp *comp, const char *name)
{
	int ret = -ENOMEM;

	comp->private = alloc_percpu(struct zcomp_strm);
	if (!comp->private)
		return -ENOMEM;

	ret = cpuhp_state_add_instance(CPUHP_ZCOMP_PREPARE, &comp->node);
	if (ret < 0)
		goto cleanup;

	__module_get(THIS_MODULE);
	return ret;
cleanup:
	free_percpu(comp->private);
	return ret;
}

void zcomp_cpu_destroy(struct zcomp *comp)
{
	cpuhp_state_remove_instance(CPUHP_ZCOMP_PREPARE, &comp->node);
	free_percpu(comp->private);
	module_put(THIS_MODULE);
}

const struct zcomp_operation zcomp_cpu_op = {
	.create = zcomp_cpu_create,
	.destroy = zcomp_cpu_destroy,
	.compress = zcomp_cpu_compress,
	.decompress = zcomp_cpu_decompress,
};

int zcomp_cpu_up_prepare(unsigned int cpu, struct hlist_node *node)
{
	struct zcomp *comp = hlist_entry(node, struct zcomp, node);
	struct zcomp_strm *zstrm;
	int ret;

	zstrm = per_cpu_ptr((struct zcomp_strm __percpu *)comp->private, cpu);
	local_lock_init(&zstrm->lock);

	ret = zcomp_strm_init(zstrm, comp);
	if (ret)
		pr_err("Can't allocate a compression stream\n");
	return ret;
}

int zcomp_cpu_dead(unsigned int cpu, struct hlist_node *node)
{
	struct zcomp *comp = hlist_entry(node, struct zcomp, node);
	struct zcomp_strm *zstrm;

	zstrm = per_cpu_ptr((struct zcomp_strm __percpu *)comp->private, cpu);
	zcomp_strm_free(zstrm);
	return 0;
}

static int __init zcomp_cpu_init(void)
{
	int ret;
	int i;

	for (i = 0; i < ARRAY_SIZE(backends); i++) {
		ret = zcomp_register(backends[i], &zcomp_cpu_op);
		if (ret)
			goto out;
	}

	ret = cpuhp_setup_state_multi(CPUHP_ZCOMP_PREPARE,
			"block/zram/zcomp/cpu:prepare",
			zcomp_cpu_up_prepare, zcomp_cpu_dead);
	if (ret)
		goto out;

	return ret;

out:
	for (i = i - 1; i >= 0; i--)
		zcomp_unregister(backends[i]);

	return ret;
}

static void __exit zcomp_cpu_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(backends); i++)
		zcomp_unregister(backends[i]);

	cpuhp_remove_multi_state(CPUHP_ZCOMP_PREPARE);
}

module_init(zcomp_cpu_init);
module_exit(zcomp_cpu_exit);
MODULE_LICENSE("GPL");
