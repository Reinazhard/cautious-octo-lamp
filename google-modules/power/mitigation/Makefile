# SPDX-License-Identifier: GPL-2.0-only

subdir-ccflags-y += -Wno-missing-prototypes -I$(srctree)/drivers/thermal

subdir-ccflags-y += \
		-I$(srctree)/google-modules/power/mitigation \
		-I$(srctree)/google-modules/power/mitigation/core_pmic \
		-I$(srctree)/google-modules/power/mitigation/core_pmic/s2mpg1x \
		-I$(srctree)/google-modules/power/mitigation/ifpmic \
		-I$(srctree)/google-modules/bms \
		-I$(ROOT_DIR)/private/google-modules/bms

obj-$(CONFIG_GOOGLE_BCL) += google_bcl.o
google_bcl-y			+= google_bcl_core.o
google_bcl-y			+= google_bcl_core_helper.o
google_bcl-y			+= google_bcl_data_logging.o
google_bcl-y			+= google_bcl_debugfs.o
google_bcl-y			+= google_bcl_irq_mon.o
google_bcl-y			+= google_bcl_votable.o
google_bcl-y			+= ifpmic/ifpmic_class.o
google_bcl-y			+= ifpmic/max777x9.o
google_bcl-y			+= soc/userspace/userspace_bcl_qos.o

ifneq ($(filter y,$(CONFIG_SOC_GS101) $(CONFIG_SOC_GS201) $(CONFIG_SOC_ZUMA)),)
google_bcl-y			+= google_bcl_sysfs.o
google_bcl-y			+= soc/gs/gs_bcl_qos.o
google_bcl-y			+= soc/gs/gs_bcl_soc.o
endif

ifneq ($(filter y m,$(CONFIG_REGULATOR_S2MPG10)),)
google_bcl-y			+= core_pmic/s2mpg1x/s2mpg1x.o
google_bcl-y			+= core_pmic/s2mpg1x/s2mpg1011/s2mpg10_bcl.o
google_bcl-y			+= core_pmic/s2mpg1x/s2mpg1011/s2mpg11_bcl.o
google_bcl-y			+= core_pmic/s2mpg1x/s2mpg1011/s2mpg1011_bcl.o
endif

ifneq ($(filter y m,$(CONFIG_GOOGLE_BCL_MAX77759) $(CONFIG_SOC_GS101)),)
subdir-ccflags-y		+= -DCONFIG_GOOGLE_BCL_MAX77759=1
google_bcl-y			+= ifpmic/max77759/max77759_vdroop.o
endif

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
M ?= $(shell pwd)

EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/bms/Module.symvers
EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/bms/misc/Module.symvers

ifneq ($(wildcard $(COMMON_OUT_DIR)/private/google-modules/radio/samsung/*/Module.symvers),)
EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/radio/samsung/*/Module.symvers
endif

KBUILD_OPTIONS += CONFIG_GOOGLE_BCL=m
include $(ROOT_DIR)/private/google-modules/soc/gs/Makefile.include

modules modules_install clean compile_commands.json:
	$(MAKE) -C $(KERNEL_SRC) M=$(M) \
	$(KBUILD_OPTIONS) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)
