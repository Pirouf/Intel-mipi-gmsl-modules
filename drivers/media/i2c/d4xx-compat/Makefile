# SPDX-License-Identifier: GPL-2.0
# Copyright (c) 2010 - 2025 Intel Corporation.

KERNEL_VERSION := $(shell echo $(KERNELRELEASE) | cut -d- -f1 | sed -r 's/([0-9]+\.[0-9]+).*/\1.0/g')

ccflags-y += -I$(src)/../../../$(KERNEL_VERSION)/include-overrides
ccflags-y += -DCONFIG_VIDEO_D4XX_SERDES -DCONFIG_SYSFS  -DPLATFORM_AXIOMTEK

obj-$(CONFIG_VIDEO_D4XX_MAX9295) += d4xx-max9295.o
obj-$(CONFIG_VIDEO_D4XX_MAX9296) += d4xx-max9296.o
obj-$(CONFIG_VIDEO_D4XX_COMPAT) += d4xx.o
