# SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
#
# (C) COPYRIGHT 2010-2024 ARM Limited. All rights reserved.
#
# This program is free software and is provided to you under the terms of the
# GNU General Public License version 2 as published by the Free Software
# Foundation, and any use by you of this program is subject to the terms
# of such GNU license.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, you can access it online at
# http://www.gnu.org/licenses/gpl-2.0.html.
#

KERNEL_SRC ?= /lib/modules/$(shell uname -r)/build
KDIR ?= $(KERNEL_SRC)
M ?= $(shell pwd)

ifeq ($(KDIR),)
    $(error Must specify KDIR to point to the kernel to target)
endif

#
# Authoritative Pixel (GS101 / Mali-G78 JM) configuration defaults.
#
# In-tree builds invoke this Makefile via scripts/Makefile.build's
# KBUILD_OPTIONS two-pass mechanism: Pass 1 exports MAKE_ARGS via
# KBUILD_OPTIONS, and Pass 2 re-includes this Makefile before Kbuild.
# Do not duplicate CONFIG_MALI_* flags in Kbuild; Kbuild derives its
# preprocessor -D flags directly from MALI_CONFIGS below.
#

ifeq ($(MALI_KCONFIG_EXT_PREFIX),)
    # Core Mali driver configuration
    CONFIG_MALI_MIDGARD ?= m
    CONFIG_MALI_PLATFORM_NAME ?= "pixel"
    CONFIG_MALI_EXPERT ?= y
    CONFIG_MALI_REAL_HW ?= y
    CONFIG_MALI_MIDGARD_DVFS ?= y
    CONFIG_MALI_DEVFREQ ?= y
    CONFIG_LARGE_PAGE_SUPPORT ?= n

    # Pixel integration features
    CONFIG_MALI_PIXEL_GPU_PM ?= y
    CONFIG_MALI_PIXEL_GPU_QOS ?= y
    CONFIG_MALI_PIXEL_GPU_BTS ?= y
    CONFIG_MALI_PIXEL_GPU_THERMAL ?= y
    CONFIG_MALI_PIXEL_GPU_SLC ?= y
    CONFIG_MALI_PIXEL_GPU_SECURE_RENDERING ?= y

    # Optional debug / tracing / KUTF test options (off by default)
    CONFIG_MALI_DEBUG ?= n
    CONFIG_MALI_SYSTEM_TRACE ?= n
    CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD ?= n
    CONFIG_MALI_MIDGARD_ENABLE_TRACE ?= n
    CONFIG_MALI_FENCE_DEBUG ?= n
    CONFIG_MALI_KUTF ?= n
    CONFIG_MALI_KUTF_IRQ_TEST ?= n
    CONFIG_MALI_KUTF_CLK_RATE_TRACE ?= n
    CONFIG_MALI_KUTF_MGM_INTEGRATION_TEST ?= n

    ifeq ($(CONFIG_MALI_NO_MALI),y)
        CONFIG_MALI_REAL_HW = n
        CONFIG_MALI_NO_MALI_DEFAULT_GPU ?= "tMIx"
    endif

    ifeq ($(CONFIG_MALI_DEBUG),y)
        CONFIG_MALI_MIDGARD_ENABLE_TRACE ?= y
        CONFIG_MALI_SYSTEM_TRACE ?= y
        ifeq ($(CONFIG_SYNC_FILE),y)
            CONFIG_MALI_FENCE_DEBUG ?= y
        else
            CONFIG_MALI_FENCE_DEBUG = n
        endif
    else
        CONFIG_MALI_MIDGARD_ENABLE_TRACE = n
        CONFIG_MALI_FENCE_DEBUG = n
    endif

    ifneq ($(CONFIG_MALI_MIDGARD),m)
        CONFIG_MALI_ARBITRATION = n
        CONFIG_MALI_KUTF = n
        CONFIG_MALI_KUTF_IRQ_TEST = n
        CONFIG_MALI_KUTF_CLK_RATE_TRACE = n
        CONFIG_MALI_KUTF_MGM_INTEGRATION_TEST = n
    endif
endif

MALI_CONFIGS := \
    CONFIG_MALI_MIDGARD \
    CONFIG_MALI_EXPERT \
    CONFIG_MALI_REAL_HW \
    CONFIG_MALI_DEVFREQ \
    CONFIG_MALI_MIDGARD_DVFS \
    CONFIG_LARGE_PAGE_SUPPORT \
    CONFIG_MALI_DEBUG \
    CONFIG_MALI_MIDGARD_ENABLE_TRACE \
    CONFIG_MALI_SYSTEM_TRACE \
    CONFIG_MALI_TRACE_POWER_GPU_WORK_PERIOD \
    CONFIG_MALI_FENCE_DEBUG \
    CONFIG_MALI_NO_MALI \
    CONFIG_MALI_IS_FPGA \
    CONFIG_MALI_ARBITER_SUPPORT \
    CONFIG_MALI_ARBITRATION \
    CONFIG_MALI_PARTITION_MANAGER \
    CONFIG_MALI_CORESTACK \
    CONFIG_MALI_PWRSOFT_765 \
    CONFIG_MALI_JOB_DUMP \
    CONFIG_MALI_GATOR_SUPPORT \
    CONFIG_MALI_XEN \
    CONFIG_MALI_CORESIGHT \
    CONFIG_MALI_CINSTR_GWT \
    CONFIG_MALI_DMA_BUF_MAP_ON_DEMAND \
    CONFIG_MALI_DMA_BUF_LEGACY_COMPAT \
    CONFIG_MALI_HW_ERRATA_1485982_NOT_AFFECTED \
    CONFIG_MALI_HW_ERRATA_1485982_USE_CLOCK_ALTERNATIVE \
    CONFIG_MALI_PRFCNT_SET_PRIMARY \
    CONFIG_MALI_PRFCNT_SET_SECONDARY \
    CONFIG_MALI_PRFCNT_SET_TERTIARY \
    CONFIG_MALI_PRFCNT_SET_SELECT_VIA_DEBUG_FS \
    CONFIG_MALI_KUTF \
    CONFIG_MALI_KUTF_IRQ_TEST \
    CONFIG_MALI_KUTF_CLK_RATE_TRACE \
    CONFIG_MALI_KUTF_MGM_INTEGRATION_TEST \
    CONFIG_MALI_PIXEL_GPU_PM \
    CONFIG_MALI_PIXEL_GPU_QOS \
    CONFIG_MALI_PIXEL_GPU_BTS \
    CONFIG_MALI_PIXEL_GPU_THERMAL \
    CONFIG_MALI_PIXEL_GPU_SLC \
    CONFIG_MALI_PIXEL_GPU_SECURE_RENDERING

MAKE_ARGS := $(foreach cfg,$(MALI_CONFIGS), \
    $(if $(filter y m,$($(cfg))),$(cfg)=$($(cfg)),$(cfg)=n))

ifeq ($(MALI_KCONFIG_EXT_PREFIX),)
    MAKE_ARGS += CONFIG_MALI_PLATFORM_NAME=$(CONFIG_MALI_PLATFORM_NAME)
endif

KBUILD_OPTIONS += $(MAKE_ARGS)

include $(ROOT_DIR)/private/google-modules/soc/gs/Makefile.include

BASE_SYMBOLS = $(COMMON_OUT_DIR)/private/google-modules/gpu/mali_pixel/Module.symvers
EXTRA_SYMBOLS += $(BASE_SYMBOLS)
EXTRA_SYMBOLS += $(COMMON_OUT_DIR)/private/google-modules/power/mitigation/Module.symvers

ifdef MALI_ENABLE_EXTRAWARN
    CFLAGS_MODULE += -Wall -Werror -Wextra -Wunused -Wno-unused-parameter
    CFLAGS_MODULE += -Wmissing-declarations -Wmissing-format-attribute
    CFLAGS_MODULE += -Wmissing-prototypes -Wold-style-definition
    CFLAGS_MODULE += $(call cc-option,-Wunused-but-set-variable)
    CFLAGS_MODULE += $(call cc-option,-Wunused-const-variable)
    CFLAGS_MODULE += $(call cc-option,-Wpacked-not-aligned)
    CFLAGS_MODULE += $(call cc-option,-Wstringop-truncation)
    CFLAGS_MODULE += -Wno-sign-compare -Wno-shift-negative-value
    CFLAGS_MODULE += $(call cc-option,-Wno-cast-function-type)
    CFLAGS_MODULE += $(call cc-option,-Werror=designated-init)
    CFLAGS_MODULE += -Wdisabled-optimization $(call cc-option,-Wlogical-op)
    CFLAGS_MODULE += -Wmissing-field-initializers -Wno-type-limits
    CFLAGS_MODULE += $(call cc-option,-Wmaybe-uninitialized)
    CFLAGS_MODULE += $(call cc-option,-Wunused-macros)
    CFLAGS_MODULE += -Wframe-larger-than=4096
endif

KBUILD_CPPFLAGS += -DKBUILD_EXTRA_WARN1 -DKBUILD_EXTRA_WARN2
CFLAGS_MODULE += -Wno-ignored-qualifiers

ifeq ($(CONFIG_GCOV_KERNEL),y)
    CFLAGS_MODULE += $(call cc-option,-ftest-coverage)
    CFLAGS_MODULE += $(call cc-option,-fprofile-arcs)
    EXTRA_CFLAGS += -DGCOV_PROFILE=1
endif

ifeq ($(CONFIG_MALI_KCOV),y)
    CFLAGS_MODULE += $(call cc-option,-fsanitize-coverage=trace-cmp)
    EXTRA_CFLAGS += -DKCOV=1 -DKCOV_ENABLE_COMPARISONS=1
endif

all: modules

modules modules_install clean compile_commands.json:
	$(MAKE) -C $(KDIR) M=$(M) W=1 $(MAKE_ARGS) KBUILD_EXTRA_SYMBOLS="$(EXTRA_SYMBOLS)" $(@)
