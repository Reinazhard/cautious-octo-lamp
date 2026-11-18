# SPDX-License-Identifier: GPL-2.0
#
# Makefile for bigwave
#

obj-$(CONFIG_BIGWAVE) += bigwave.o
bigwave-$(CONFIG_BIGWAVE) += bigo.o bigo_pm.o bigo_io.o bigo_of.o bigo_iommu.o bigo_prioq.o
bigwave-$(ENABLE_SLC) += bigo_slc.o
bigwave-$(CONFIG_DEBUG_FS) += bigo_debug.o

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

KBUILD_OPTIONS += CONFIG_BIGWAVE=m ENABLE_SLC=y

include $(ROOT_DIR)/private/google-modules/soc/gs/Makefile.include

ccflags-y += -I$(srctree)/google-modules/video/gchips/include -DENABLE_SLC=1
CFLAGS_MODULE += -I$(ROOT_DIR)/private/google-modules/video/gchips/include
CFLAGS_MODULE += -DENABLE_SLC=1

modules modules_install headers_install clean compile_commands.json:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) \
	$(KBUILD_OPTIONS) \
	CFLAGS_MODULE="$(CFLAGS_MODULE)" \
	KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" \
	$(@)

modules_install: headers_install
