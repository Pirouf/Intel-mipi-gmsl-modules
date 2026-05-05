/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2022 Intel Corporation */

#ifndef D457_H
#define D457_H

#define D457_NAME "d4xx"
#define MAX9296_NAME "MAX9296"
#define MAX96724_NAME "MAX96724"

#define D457_I2C_ADDRESS 0x10

#define d4xx_subdev_csi_link_id(link) \
 link == GMSL_SERDES_CSI_LINK_A ? "GMSL A" : \
 GMSL_SERDES_CSI_LINK_B ? "GMSL B" : \
 GMSL_SERDES_CSI_LINK_C ? "GMSL C" : "GMSL D"

#endif
