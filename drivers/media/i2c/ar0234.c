// SPDX-License-Identifier: GPL-2.0
// Copyright (c) 2019 - 2026 Intel Corporation.

#include <linux/acpi.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/version.h>

#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-event.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include "media/i2c/ar0234.h"

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
#include <media/mipi-csi2.h>
#endif

/* Chip ID */
#define AR0234_CHIP_ID                  0x0a56            /* AR0234_CHIP_VERSION */

/*
 * Full AR0234 register map, as defined by the sensor vendor. Kept here for
 * reference only -- this driver does not use these names directly; each
 * #define below mirrors the matching entry from this list for the specific
 * registers this driver actually accesses.
 */
#define AR0234_REG_CHIP_ID              CCI_REG16(0x3000) /* AR0234_REG_CHIP_VERSION */
#define AR0234_REG_VTS                  CCI_REG16(0x300a) /* AR0234_REG_FRAME_LENGTH_LINES */
#define AR0234_REG_HTS                  CCI_REG16(0x300c) /* AR0234_REG_FRAME_LENGTH_LINES */
#define AR0234_REG_MODE_SELECT          CCI_REG16(0x301a) /* AR0234_REG_RESET_REGISTER */
#define AR0234_REG_REVISION_NUMBER      CCI_REG16(0x300e) /* AR0234_REG_REVISION_NUMBER */
#define AR0234_REG_EXPOSURE             CCI_REG16(0x3012) /* AR0234_REG_COARSE_INT_TIME */
#define AR0234_REG_FINE_INT_TIME        CCI_REG16(0x3014) /* AR0234_REG_FINE_INT_TIME */
#define AR0234_REG_GROUPED_PARAM_HOLD   CCI_REG16(0x3022) /* AR0234_REG_GROUPED_PARAMETER_HOLD */
#define AR0234_REG_VT_PIX_CLK_DIV       CCI_REG16(0x302a) /* AR0234_REG_VT_PIX_CLK_DIV */
#define AR0234_REG_VT_SYS_CLK_DIV       CCI_REG16(0x302c) /* AR0234_REG_VT_SYS_CLK_DIV */
#define AR0234_REG_PRE_PLL_CLK_DIV      CCI_REG16(0x302e) /* AR0234_REG_PRE_PLL_CLK_DIV */
#define AR0234_REG_PLL_MULTIPLIER       CCI_REG16(0x3030) /* AR0234_REG_PLL_MULTIPLIER */
#define AR0234_REG_ORIENTATION          CCI_REG16(0x3040) /* AR0234_REG_READ_MODE */
#define AR0234_REG_GLOBAL_GAIN          CCI_REG16(0x305e) /* AR0234_REG_GLOBAL_GAIN */
#define AR0234_REG_SERIAL_FORMAT        CCI_REG16(0x31ae) /* AR0234_REG_SERIAL_FORMAT */
#define AR0234_REG_CUSTOMER_REV         CCI_REG16(0x31fe) /* AR0234_REG_CUSTOMER_REV */
#define AR0234_REG_ANALOG_GAIN          CCI_REG16(0x3060) /* AR0234_REG_ANALOG_GAIN */
#define AR0234_REG_TEMPSENS_DATA        CCI_REG16(0x30b2) /* AR0234_REG_TEMPSENS_DATA */
#define AR0234_REG_TEMPSENS_CTRL        CCI_REG16(0x30b4) /* AR0234_REG_TEMPSENS_CTRL */
#define AR0234_REG_RESERVED_MFR_30BA    CCI_REG16(0x30ba) /* AR0234_REG_RESERVED_MFR_30BA */
#define AR0234_REG_TEMPSENS_CALIB1      CCI_REG16(0x30c6) /* AR0234_REG_TEMPSENS_CALIB1 */
#define AR0234_REG_GRR_CONTROL1         CCI_REG16(0x30ce) /* AR0234_REG_GRR_CONTROL1 */
#define AR0234_REG_TEST_PATTERN         CCI_REG16(0x0600) /* AR0234_REG_TEST_PATTERN */
#define AR0234_GRR_SLAVE_SH_SYNC_MODE	BIT(8)
#define AR0234_GRR_FRAME_START_MODE	BIT(5)

/* Exposure control */
#define AR0234_EXPOSURE_MIN             0
#define AR0234_EXPOSURE_MAX_MARGIN      80
#define AR0234_EXPOSURE_STEP            1

/* Analog gain control */
#define AR0234_ANALOG_GAIN_MIN          0
#define AR0234_ANALOG_GAIN_MAX          0x7f  /* 127 */
#define AR0234_ANALOG_GAIN_STEP         1
#define AR0234_ANALOG_GAIN_DEFAULT      0xe   /* 14 */

/* Digital gain control */
#define AR0234_GLOBAL_GAIN_MIN          0     /* 0 */
#define AR0234_GLOBAL_GAIN_MAX          0x7ff /* 2047 */
#define AR0234_GLOBAL_GAIN_STEP         1
#define AR0234_GLOBAL_GAIN_DEFAULT      0x80  /* 128 */

/* Pixel array geometry & readout orientation */
#define AR0234_NATIVE_WIDTH		1920
#define AR0234_NATIVE_HEIGHT		1200
#define AR0234_COMMON_WIDTH		1280
#define AR0234_COMMON_HEIGHT		960
/*
 * Where the 1280x960 mode's window sits in the 1920x1200 active array: X_ADDR_START 0x148 and
 * Y_ADDR_START 0x80 (328, 128) less the array's 8 border columns and rows, which centres it.
 */
#define AR0234_PIXEL_ARRAY_LEFT		320
#define AR0234_PIXEL_ARRAY_TOP		120
#define AR0234_ORIENTATION_HFLIP	BIT(14)
#define AR0234_ORIENTATION_VFLIP	BIT(15)


/* Horizontal/vertical line timing */
#define AR0234_HTS_DEFAULT              0x04c4 /* 1220 */
#define AR0234_HTS_MIN                  0x0264 /* 612 */
#define AR0234_VTS_DEFAULT              0x04c4 /* 1220 */
#define AR0234_VTS_MAX                  0xffff /* 65535 */
#define AR0234_PPL_DEFAULT              3498

#define AR0234_MODE_RESET		0x00d9
#define AR0234_MODE_STANDBY		0x2058
#define AR0234_MODE_STREAMING		0x205c
/*
 * Streaming in multi-sensor sync mode: each frame starts on the TRIGGER
 * input instead of the sensor's own timing, so sensors sharing a trigger
 * (the two of a ZED X, on MAX9295D MFP9 and MFP10) expose and read out
 * together. On top of streaming, this enables the input pins (GPI_EN),
 * keeps the PLL running between frames (forced_pll_on) and restarts the
 * current frame.
 */
#define AR0234_MODE_STREAMING_SYNC	0x295e

/* FPS */
#define AR0234_FPS_60                   60
#define AR0234_FPS_30                   30

/* AR0234_REG_MODE_SELECT values */
#define AR0234_MODE_RESET               0x00d9 /* 217 */
#define AR0234_MODE_STANDBY             0x2058 /* 8280 */
#define AR0234_MODE_STREAMING           0x205c /* 8284 */

/* Clock rates */
#define AR0234_PIXEL_RATE_256M          256000000ULL /* 256MHz */
#define AR0234_PIXEL_RATE_128M          128000000ULL /* 128MHz */

/* Test Pattern Control */
#define AR0234_TEST_PATTERN_DISABLE     0   /* Disabled */
#define AR0234_TEST_PATTERN_SOLID_COLOR 1   /* Solid color */
#define AR0234_TEST_PATTERN_COLOR_BARS  2   /* Color bars */
#define AR0234_TEST_PATTERN_GREY_COLOR  3   /* Grey color */
#define AR0234_TEST_PATTERN_WALKING     256 /* Walking pattern */

/* PM resume GPIO-ready polling */
#define AR0234_PM_MAX_RETRY             10
#define AR0234_REG_SLEEP_200MS          200

#define to_ar0234(_sd)	container_of(_sd, struct ar0234, sd)

/* To serialize asynchronous callbacks */
static DEFINE_MUTEX(ar0234_mutex);

static int frame_sync = -1;
module_param(frame_sync, int, 0644);
MODULE_PARM_DESC(frame_sync,
		 "Start each frame on the TRIGGER input (multi-sensor sync mode), read at stream start: -1 as the firmware node's gmsl-frame-sync-enable says (default), 0 never, 1 always");

struct ar0234_reg_list {
	u32 num_of_regs;
	const struct cci_reg_sequence *regs;
};

struct ar0234_mode {
	u32 width;
	u32 height;
	u32 hts;
	u32 vts_def;
	u32 code;
	u32 fps;
	u64 pixel_rate;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
	u8 datatype;
#endif
	/* PLL/MIPI/window-geometry regs; the only segment that differs between modes */
	const struct cci_reg_sequence *timing_regs;
	u32 num_timing_regs;
};

/*
 * AR0234 Register Sequence: DDC & Operating Mode.
 */
static const struct cci_reg_sequence ar0234_common_ddc_mode_regs[] = {
	{ CCI_REG16(0x3f4c), 0x121f }, /* DDC Threshold 1: single-pixel defect sensitivity */
	{ CCI_REG16(0x3f4e), 0x121f }, /* DDC Threshold 2: Single-pixel defect correction limit */
	{ CCI_REG16(0x3f50), 0x0b81 }, /* DDC Edge Detector: Edge detection threshold for 1D-DDC */
	{ CCI_REG16(0x31e0), 0x0003 }, /* PIX_DEF_ID: enable 1D defect & edge correction */
	{ CCI_REG16(0x30b0), 0x0028 }, /* DIGITAL_TEST: Context A, Color Bayer mode, PLL enabled */
};

/*
 * AR0234 Register Sequence: Sequencer RAM Microcode Initialization.
 * Target RAM Address: 0x000 (Halts sequencer during program write).
 */
static const struct cci_reg_sequence ar0234_common_seq_microcode_regs[] = {
	{ CCI_REG16(0x3088), 0x8000 }, /* SEQ_CTRL_PORT: halt sequencer, set write ptr 0x000 */
	{ CCI_REG16(0x3086), 0xc1ae }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x000 */
	{ CCI_REG16(0x3086), 0x327f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x001 */
	{ CCI_REG16(0x3086), 0x5780 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x002 */
	{ CCI_REG16(0x3086), 0x272f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x003 */
	{ CCI_REG16(0x3086), 0x7416 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x004 */
	{ CCI_REG16(0x3086), 0x7e13 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x005 */
	{ CCI_REG16(0x3086), 0x8000 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x006 */
	{ CCI_REG16(0x3086), 0x307e }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x007 */
	{ CCI_REG16(0x3086), 0xff80 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x008 */
	{ CCI_REG16(0x3086), 0x20c3 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x009 */
	{ CCI_REG16(0x3086), 0xb00e }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x00A */
	{ CCI_REG16(0x3086), 0x8190 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x00B */
	{ CCI_REG16(0x3086), 0x1643 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x00C */
	{ CCI_REG16(0x3086), 0x1651 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x00D */
	{ CCI_REG16(0x3086), 0x9d3e }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x00E */
	{ CCI_REG16(0x3086), 0x9545 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x00F */
	{ CCI_REG16(0x3086), 0x2209 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x010 */
	{ CCI_REG16(0x3086), 0x3781 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x011 */
	{ CCI_REG16(0x3086), 0x9016 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x012 */
	{ CCI_REG16(0x3086), 0x4316 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x013 */
	{ CCI_REG16(0x3086), 0x7f90 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x014 */
	{ CCI_REG16(0x3086), 0x8000 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x015 */
	{ CCI_REG16(0x3086), 0x387f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x016 */
	{ CCI_REG16(0x3086), 0x1380 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x017 */
	{ CCI_REG16(0x3086), 0x233b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x018 */
	{ CCI_REG16(0x3086), 0x7f93 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x019 */
	{ CCI_REG16(0x3086), 0x4502 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x01A */
	{ CCI_REG16(0x3086), 0x8000 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x01B */
	{ CCI_REG16(0x3086), 0x7fb0 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x01C */
	{ CCI_REG16(0x3086), 0x8d66 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x01D */
	{ CCI_REG16(0x3086), 0x7f90 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x01E */
	{ CCI_REG16(0x3086), 0x8192 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x01F */
	{ CCI_REG16(0x3086), 0x3c16 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x020 */
	{ CCI_REG16(0x3086), 0x357f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x021 */
	{ CCI_REG16(0x3086), 0x9345 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x022 */
	{ CCI_REG16(0x3086), 0x0280 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x023 */
	{ CCI_REG16(0x3086), 0x007f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x024 */
	{ CCI_REG16(0x3086), 0xb08d }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x025 */
	{ CCI_REG16(0x3086), 0x667f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x026 */
	{ CCI_REG16(0x3086), 0x9081 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x027 */
	{ CCI_REG16(0x3086), 0x8237 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x028 */
	{ CCI_REG16(0x3086), 0x4502 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x029 */
	{ CCI_REG16(0x3086), 0x3681 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x02A */
	{ CCI_REG16(0x3086), 0x8044 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x02B */
	{ CCI_REG16(0x3086), 0x1631 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x02C */
	{ CCI_REG16(0x3086), 0x4374 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x02D */
	{ CCI_REG16(0x3086), 0x1678 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x02E */
	{ CCI_REG16(0x3086), 0x7b7d }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x02F */
	{ CCI_REG16(0x3086), 0x4502 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x030 */
	{ CCI_REG16(0x3086), 0x450a }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x031 */
	{ CCI_REG16(0x3086), 0x7e12 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x032 */
	{ CCI_REG16(0x3086), 0x8180 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x033 */
	{ CCI_REG16(0x3086), 0x377f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x034 */
	{ CCI_REG16(0x3086), 0x1045 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x035 */
	{ CCI_REG16(0x3086), 0x0a0e }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x036 */
	{ CCI_REG16(0x3086), 0x7fd4 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x037 */
	{ CCI_REG16(0x3086), 0x8024 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x038 */
	{ CCI_REG16(0x3086), 0x0e82 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x039 */
	{ CCI_REG16(0x3086), 0x9cc2 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x03A */
	{ CCI_REG16(0x3086), 0xafa8 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x03B */
	{ CCI_REG16(0x3086), 0xaa03 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x03C */
	{ CCI_REG16(0x3086), 0x430d }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x03D */
	{ CCI_REG16(0x3086), 0x2d46 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x03E */
	{ CCI_REG16(0x3086), 0x4316 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x03F */
	{ CCI_REG16(0x3086), 0x5f16 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x040 */
	{ CCI_REG16(0x3086), 0x530d }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x041 */
	{ CCI_REG16(0x3086), 0x1660 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x042 */
	{ CCI_REG16(0x3086), 0x401e }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x043 */
	{ CCI_REG16(0x3086), 0x2904 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x044 */
	{ CCI_REG16(0x3086), 0x2984 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x045 */
	{ CCI_REG16(0x3086), 0x81e7 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x046 */
	{ CCI_REG16(0x3086), 0x816f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x047 */
	{ CCI_REG16(0x3086), 0x1706 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x048 */
	{ CCI_REG16(0x3086), 0x81e7 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x049 */
	{ CCI_REG16(0x3086), 0x7f81 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x04A */
	{ CCI_REG16(0x3086), 0x5c0d }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x04B */
	{ CCI_REG16(0x3086), 0x5754 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x04C */
	{ CCI_REG16(0x3086), 0x495f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x04D */
	{ CCI_REG16(0x3086), 0x5305 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x04E */
	{ CCI_REG16(0x3086), 0x5307 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x04F */
	{ CCI_REG16(0x3086), 0x4d2b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x050 */
	{ CCI_REG16(0x3086), 0xf810 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x051 */
	{ CCI_REG16(0x3086), 0x164c }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x052 */
	{ CCI_REG16(0x3086), 0x0755 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x053 */
	{ CCI_REG16(0x3086), 0x562b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x054 */
	{ CCI_REG16(0x3086), 0xb82b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x055 */
	{ CCI_REG16(0x3086), 0x984e }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x056 */
	{ CCI_REG16(0x3086), 0x1129 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x057 */
	{ CCI_REG16(0x3086), 0x9460 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x058 */
	{ CCI_REG16(0x3086), 0x5c09 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x059 */
	{ CCI_REG16(0x3086), 0x5c1b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x05A */
	{ CCI_REG16(0x3086), 0x4002 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x05B */
	{ CCI_REG16(0x3086), 0x4500 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x05C */
	{ CCI_REG16(0x3086), 0x4580 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x05D */
	{ CCI_REG16(0x3086), 0x29b6 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x05E */
	{ CCI_REG16(0x3086), 0x7f80 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x05F */
	{ CCI_REG16(0x3086), 0x4004 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x060 */
	{ CCI_REG16(0x3086), 0x7f88 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x061 */
	{ CCI_REG16(0x3086), 0x4109 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x062 */
	{ CCI_REG16(0x3086), 0x5c0b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x063 */
	{ CCI_REG16(0x3086), 0x29b2 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x064 */
	{ CCI_REG16(0x3086), 0x4115 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x065 */
	{ CCI_REG16(0x3086), 0x5c03 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x066 */
	{ CCI_REG16(0x3086), 0x4105 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x067 */
	{ CCI_REG16(0x3086), 0x5f2b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x068 */
	{ CCI_REG16(0x3086), 0x902b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x069 */
	{ CCI_REG16(0x3086), 0x8081 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x06A */
	{ CCI_REG16(0x3086), 0x6f40 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x06B */
	{ CCI_REG16(0x3086), 0x1041 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x06C */
	{ CCI_REG16(0x3086), 0x0160 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x06D */
	{ CCI_REG16(0x3086), 0x29a2 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x06E */
	{ CCI_REG16(0x3086), 0x29a3 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x06F */
	{ CCI_REG16(0x3086), 0x5f4d }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x070 */
	{ CCI_REG16(0x3086), 0x1c17 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x071 */
	{ CCI_REG16(0x3086), 0x0281 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x072 */
	{ CCI_REG16(0x3086), 0xe729 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x073 */
	{ CCI_REG16(0x3086), 0x8345 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x074 */
	{ CCI_REG16(0x3086), 0x8840 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x075 */
	{ CCI_REG16(0x3086), 0x0f7f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x076 */
	{ CCI_REG16(0x3086), 0x8a40 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x077 */
	{ CCI_REG16(0x3086), 0x2345 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x078 */
	{ CCI_REG16(0x3086), 0x8024 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x079 */
	{ CCI_REG16(0x3086), 0x4008 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x07A */
	{ CCI_REG16(0x3086), 0x7f88 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x07B */
	{ CCI_REG16(0x3086), 0x5d29 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x07C */
	{ CCI_REG16(0x3086), 0x9288 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x07D */
	{ CCI_REG16(0x3086), 0x102b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x07E */
	{ CCI_REG16(0x3086), 0x0489 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x07F */
	{ CCI_REG16(0x3086), 0x165c }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x080 */
	{ CCI_REG16(0x3086), 0x4386 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x081 */
	{ CCI_REG16(0x3086), 0x170b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x082 */
	{ CCI_REG16(0x3086), 0x5c03 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x083 */
	{ CCI_REG16(0x3086), 0x8a48 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x084 */
	{ CCI_REG16(0x3086), 0x4d4e }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x085 */
	{ CCI_REG16(0x3086), 0x2b80 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x086 */
	{ CCI_REG16(0x3086), 0x4c09 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x087 */
	{ CCI_REG16(0x3086), 0x4119 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x088 */
	{ CCI_REG16(0x3086), 0x816f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x089 */
	{ CCI_REG16(0x3086), 0x4110 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x08A */
	{ CCI_REG16(0x3086), 0x4001 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x08B */
	{ CCI_REG16(0x3086), 0x6029 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x08C */
	{ CCI_REG16(0x3086), 0x8229 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x08D */
	{ CCI_REG16(0x3086), 0x8329 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x08E */
	{ CCI_REG16(0x3086), 0x435c }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x08F */
	{ CCI_REG16(0x3086), 0x055f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x090 */
	{ CCI_REG16(0x3086), 0x4d1c }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x091 */
	{ CCI_REG16(0x3086), 0x81e7 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x092 */
	{ CCI_REG16(0x3086), 0x4502 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x093 */
	{ CCI_REG16(0x3086), 0x8180 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x094 */
	{ CCI_REG16(0x3086), 0x7f80 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x095 */
	{ CCI_REG16(0x3086), 0x410a }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x096 */
	{ CCI_REG16(0x3086), 0x9144 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x097 */
	{ CCI_REG16(0x3086), 0x1609 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x098 */
	{ CCI_REG16(0x3086), 0x2fc3 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x099 */
	{ CCI_REG16(0x3086), 0xb130 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x09A */
	{ CCI_REG16(0x3086), 0xc3b1 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x09B */
	{ CCI_REG16(0x3086), 0x0343 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x09C */
	{ CCI_REG16(0x3086), 0x164a }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x09D */
	{ CCI_REG16(0x3086), 0x0a43 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x09E */
	{ CCI_REG16(0x3086), 0x160b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x09F */
	{ CCI_REG16(0x3086), 0x4316 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0A0 */
	{ CCI_REG16(0x3086), 0x8f43 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0A1 */
	{ CCI_REG16(0x3086), 0x1690 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0A2 */
	{ CCI_REG16(0x3086), 0x4316 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0A3 */
	{ CCI_REG16(0x3086), 0x7f81 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0A4 */
	{ CCI_REG16(0x3086), 0x450a }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0A5 */
	{ CCI_REG16(0x3086), 0x410f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0A6 */
	{ CCI_REG16(0x3086), 0x7f83 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0A7 */
	{ CCI_REG16(0x3086), 0x5d29 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0A8 */
	{ CCI_REG16(0x3086), 0x4488 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0A9 */
	{ CCI_REG16(0x3086), 0x102b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0AA */
	{ CCI_REG16(0x3086), 0x0453 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0AB */
	{ CCI_REG16(0x3086), 0x0d40 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0AC */
	{ CCI_REG16(0x3086), 0x2345 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0AD */
	{ CCI_REG16(0x3086), 0x0240 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0AE */
	{ CCI_REG16(0x3086), 0x087f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0AF */
	{ CCI_REG16(0x3086), 0x8053 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0B0 */
	{ CCI_REG16(0x3086), 0x0d89 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0B1 */
	{ CCI_REG16(0x3086), 0x165c }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0B2 */
	{ CCI_REG16(0x3086), 0x4586 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0B3 */
	{ CCI_REG16(0x3086), 0x170b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0B4 */
	{ CCI_REG16(0x3086), 0x5c05 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0B5 */
	{ CCI_REG16(0x3086), 0x8a60 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0B6 */
	{ CCI_REG16(0x3086), 0x4b91 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0B7 */
	{ CCI_REG16(0x3086), 0x4416 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0B8 */
	{ CCI_REG16(0x3086), 0x09c1 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0B9 */
	{ CCI_REG16(0x3086), 0x2ca9 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0BA */
	{ CCI_REG16(0x3086), 0xab30 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0BB */
	{ CCI_REG16(0x3086), 0x51b3 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0BC */
	{ CCI_REG16(0x3086), 0x3d5a }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0BD */
	{ CCI_REG16(0x3086), 0x7e3d }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0BE */
	{ CCI_REG16(0x3086), 0x7e19 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0BF */
	{ CCI_REG16(0x3086), 0x8000 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0C0 */
	{ CCI_REG16(0x3086), 0x8b1f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0C1 */
	{ CCI_REG16(0x3086), 0x2a1f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0C2 */
	{ CCI_REG16(0x3086), 0x83a2 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0C3 */
	{ CCI_REG16(0x3086), 0x7516 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0C4 */
	{ CCI_REG16(0x3086), 0xad33 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0C5 */
	{ CCI_REG16(0x3086), 0x450a }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0C6 */
	{ CCI_REG16(0x3086), 0x7f53 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0C7 */
	{ CCI_REG16(0x3086), 0x8023 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0C8 */
	{ CCI_REG16(0x3086), 0x8c66 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0C9 */
	{ CCI_REG16(0x3086), 0x7f13 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0CA */
	{ CCI_REG16(0x3086), 0x8184 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0CB */
	{ CCI_REG16(0x3086), 0x1481 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0CC */
	{ CCI_REG16(0x3086), 0x8031 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0CD */
	{ CCI_REG16(0x3086), 0x3d64 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0CE */
	{ CCI_REG16(0x3086), 0x452a }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0CF */
	{ CCI_REG16(0x3086), 0x9451 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0D0 */
	{ CCI_REG16(0x3086), 0x9e96 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0D1 */
	{ CCI_REG16(0x3086), 0x3d2b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0D2 */
	{ CCI_REG16(0x3086), 0x3d1b }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0D3 */
	{ CCI_REG16(0x3086), 0x529f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0D4 */
	{ CCI_REG16(0x3086), 0x0e3d }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0D5 */
	{ CCI_REG16(0x3086), 0x083d }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0D6 */
	{ CCI_REG16(0x3086), 0x167e }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0D7 */
	{ CCI_REG16(0x3086), 0x307e }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0D8 */
	{ CCI_REG16(0x3086), 0x1175 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0D9 */
	{ CCI_REG16(0x3086), 0x163e }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0DA */
	{ CCI_REG16(0x3086), 0x970e }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0DB */
	{ CCI_REG16(0x3086), 0x82b2 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0DC */
	{ CCI_REG16(0x3086), 0x3d7f }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0DD */
	{ CCI_REG16(0x3086), 0xac3e }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0DE */
	{ CCI_REG16(0x3086), 0x4502 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0DF */
	{ CCI_REG16(0x3086), 0x7e11 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0E0 */
	{ CCI_REG16(0x3086), 0x7fd0 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0E1 */
	{ CCI_REG16(0x3086), 0x8000 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0E2 */
	{ CCI_REG16(0x3086), 0x8c66 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0E3 */
	{ CCI_REG16(0x3086), 0x7f90 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0E4 */
	{ CCI_REG16(0x3086), 0x8194 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0E5 */
	{ CCI_REG16(0x3086), 0x3f44 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0E6 */
	{ CCI_REG16(0x3086), 0x1681 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0E7 */
	{ CCI_REG16(0x3086), 0x8416 }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0E8 */
	{ CCI_REG16(0x3086), 0x2c2c }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0E9 */
	{ CCI_REG16(0x3086), 0x2c2c }, /* SEQ_DATA_PORT: Write Sequencer Instruction Word 0x0EA */
};

/*
 * AR0234 Register Sequence: 1280x960 @ 30fps PLL/MIPI/window-geometry.
 * Mode-specific segment; applied on every start_streaming() in addition to
 * the one-time common segments above.
 */
static const struct cci_reg_sequence mode_1280x960_30fps_10bit_2lane_timing[] = {
	/* Phase-Locked Loop (PLL) & System Clock Dividers */
	{ CCI_REG16(0x302a), 0x0005 }, /* VT_PIX_CLK_DIV = 5: Pixel clock divisor */
	{ CCI_REG16(0x302c), 0x0001 }, /* VT_SYS_CLK_DIV = 1: System clock divisor */
	{ CCI_REG16(0x302e), 0x0003 }, /* PRE_PLL_CLK_DIV = 3: pre-PLL clock divider */
	{ CCI_REG16(0x3030), 0x0032 }, /* PLL_MULTIPLIER = 50: PLL frequency multiplier (2M=50) */
	{ CCI_REG16(0x3036), 0x000a }, /* OP_PIX_CLK_DIV = 10: Output pixel clock divisor */
	{ CCI_REG16(0x3038), 0x0001 }, /* OP_SYS_CLK_DIV = 1: Output system clock divisor */
	{ CCI_REG16(0x30b0), 0x0028 }, /* DIGITAL_TEST: Re-verify PLL complete bypass disabled */

	/* MIPI CSI-2 Protocol & D-PHY Physical Layer Timing */
	{ CCI_REG16(0x31b0), 0x0082 }, /* FRAME_PREAMBLE = 130: MIPI frame-start delay */
	{ CCI_REG16(0x31b2), 0x005c }, /* LINE_PREAMBLE = 92: MIPI line header delay */
	{ CCI_REG16(0x31b4), 0x5248 }, /* MIPI_TIMING_0: T_HS_PREPARE, T_HS_TRAIL, T_CLK_TRAIL */
	{ CCI_REG16(0x31b6), 0x3257 }, /* MIPI_TIMING_1: T_CLK_PREPARE, T_HS_EXIT */
	{ CCI_REG16(0x31b8), 0x904b }, /* MIPI_TIMING_2: T_BGAP, T_CLK_PRE, T_CLK_POST */
	{ CCI_REG16(0x31ba), 0x030b }, /* MIPI_TIMING_3: T_LPX, T_WAKE_UP */
	{ CCI_REG16(0x31bc), 0x8e09 }, /* MIPI_TIMING_4: Continuous clock enable & T_INIT */
	{ CCI_REG16(0x3354), 0x002b }, /* MIPI_CNTRL = 0x002B: Set MIPI Data Type to RAW10 */
	{ CCI_REG16(0x31d0), 0x0000 }, /* COMPANDING = 0: Uncompressed RAW10 transmission */
	{ CCI_REG16(0x31ae), 0x0204 }, /* SERIAL_FORMAT = 0x0204: Initial lane configuration */

	/* Window Geometry, Active Resolution & Framing Timing: 1280 (H) x 960 (V) */
	{ CCI_REG16(0x3002), 0x0080 }, /* Y_ADDR_START = 128: First active row index */
	{ CCI_REG16(0x3004), 0x0148 }, /* X_ADDR_START = 328: First active column index */
	{ CCI_REG16(0x3006), 0x043f }, /* Y_ADDR_END = 1087 (960-line active height) */
	{ CCI_REG16(0x3008), 0x0647 }, /* X_ADDR_END = 1607 (1280-pixel active width) */
	{ CCI_REG16(0x3064), 0x1802 }, /* SMIA_TEST: Production datapath verification mode */
	{ CCI_REG16(0x300a), 0x04c4 }, /* FRAME_LENGTH_LINES = 1220: total frame height */
	{ CCI_REG16(0x300c), 0x04c4 }, /* LINE_LENGTH_PCK = 1220: line width in pixel clocks */
	{ CCI_REG16(0x30a2), 0x0001 }, /* X_ODD_INC = 1: No horizontal skipping (1x subsampling) */
	{ CCI_REG16(0x30a6), 0x0001 }, /* Y_ODD_INC = 1: No vertical skipping (1x subsampling) */
	{ CCI_REG16(0x3012), 0x010c }, /* COARSE_INTEGRATION_TIME = 268 lines exposure */
	{ CCI_REG16(0x3786), 0x0006 }, /* DIGITAL_CTRL_1: LSB alignment & gain buffering control */
	{ CCI_REG16(0x31ae), 0x0202 }, /* SERIAL_FORMAT = 0x0202: 2-lane MIPI CSI-2 mode */
};

/*
 * AR0234 Register Sequence: 1920x1200 @ 60fps PLL/MIPI/window-geometry.
 * Mode-specific segment; applied on every start_streaming() in addition to
 * the one-time common segments above.
 */
static const struct cci_reg_sequence mode_1920x1200_60fps_10bit_2lane_timing[] = {
	/* Phase-Locked Loop (PLL) & System Clock Dividers */
	{ CCI_REG16(0x302a), 0x0005 }, /* VT_PIX_CLK_DIV = 5 */
	{ CCI_REG16(0x302c), 0x0001 }, /* VT_SYS_CLK_DIV = 1 */
	{ CCI_REG16(0x302e), 0x0003 }, /* PRE_PLL_CLK_DIV = 3 */
	{ CCI_REG16(0x3030), 0x0032 }, /* PLL_MULTIPLIER = 50 */
	{ CCI_REG16(0x3036), 0x000a }, /* OP_PIX_CLK_DIV = 10 */
	{ CCI_REG16(0x3038), 0x0001 }, /* OP_SYS_CLK_DIV = 1 */
	{ CCI_REG16(0x30b0), 0x0028 }, /* DIGITAL_TEST (matches gain-commit value below) */

	/* MIPI CSI-2 Protocol & D-PHY Physical Layer Timing */
	{ CCI_REG16(0x31b0), 0x004c }, /* FRAME_PREAMBLE = 76 */
	{ CCI_REG16(0x31b2), 0x003c }, /* LINE_PREAMBLE = 60 */
	{ CCI_REG16(0x31b4), 0x31c5 }, /* MIPI_TIMING_0 = 12741 */
	{ CCI_REG16(0x31b6), 0x214c }, /* MIPI_TIMING_1 = 8524 */
	{ CCI_REG16(0x31b8), 0x5048 }, /* MIPI_TIMING_2 = 20552 */
	{ CCI_REG16(0x31ba), 0x0186 }, /* MIPI_TIMING_3 = 390 */
	{ CCI_REG16(0x31bc), 0x8805 }, /* MIPI_TIMING_4 = 34821 */
	{ CCI_REG16(0x3354), 0x002b }, /* MIPI_CNTRL = 43 */
	{ CCI_REG16(0x31d0), 0x0000 }, /* COMPANDING = 0 */
	{ CCI_REG16(0x31ae), 0x0204 }, /* SERIAL_FORMAT = 0x0204: Initial lane configuration */

	/* Window Geometry, Active Resolution & Framing Timing: 1920 (H) x 1200 (V) */
	{ CCI_REG16(0x3002), 0x0008 }, /* Y_ADDR_START = 8 */
	{ CCI_REG16(0x3004), 0x0008 }, /* X_ADDR_START = 8 */
	{ CCI_REG16(0x3006), 0x04b7 }, /* Y_ADDR_END = 1207 */
	{ CCI_REG16(0x3008), 0x0787 }, /* X_ADDR_END = 1927 */
	{ CCI_REG16(0x300a), 0x04c4 }, /* FRAME_LENGTH_LINES = 1220 */
	{ CCI_REG16(0x300c), 0x0264 }, /* LINE_LENGTH_PCK = 612 */
	{ CCI_REG16(0x30a2), 0x0001 }, /* X_ODD_INC = 1 */
	{ CCI_REG16(0x30a6), 0x0001 }, /* Y_ODD_INC = 1 */
	{ CCI_REG16(0x3012), 0x02df }, /* COARSE_INTEGRATION_TIME = 735 */
	{ CCI_REG16(0x3786), 0x0006 }, /* DIGITAL_CTRL_1 (reused tuning) */
	{ CCI_REG16(0x31ae), 0x0202 }, /* SERIAL_FORMAT = 514 (final, 2-lane) */

	/* Output Data Format, Datapath Routing & Read Mode Selection */
	{ CCI_REG16(0x31ac), 0x0a0a }, /* DATA_FORMAT_BITS = 2570 */
	{ CCI_REG16(0x306e), 0x9010 }, /* DATAPATH_SELECT = 36880 */
	{ CCI_REG16(0x3082), 0x0003 }, /* OPERATION_MODE_CTRL = 3 */
	{ CCI_REG16(0x3040), 0x0000 }, /* READ_MODE = 0 */
};

/*
 * AR0234 Register Sequence: Analog Front-End (AFE), Colamp Trimming & Gain
 * Calibration. 42-Register Factory-Tuned Analog Initialization Block.
 */
static const struct cci_reg_sequence ar0234_common_afe_colamp_regs[] = {
	{ CCI_REG16(0x3088), 0x8050 }, /* SEQ_CTRL_PORT: Halt sequencer, set RAM addr 0x050 */
	{ CCI_REG16(0x3086), 0x9237 }, /* SEQ_DATA_PORT: Write microcode word 0x9237 */
	{ CCI_REG16(0x3044), 0x0410 }, /* Datapath Control: Pixel clipping and timing thresholds */
	{ CCI_REG16(0x3094), 0x03d4 }, /* Sequencer Execution Delay: Column readout timing delay */
	{ CCI_REG16(0x3096), 0x0280 }, /* Column Readout Timing: Readout phase relative to PIXCLK */
	{ CCI_REG16(0x30ba), 0x7606 }, /* Column Gain Bias: fine bias for 90/45 MHz op */
	{ CCI_REG16(0x30b0), 0x0028 }, /* DIGITAL_TEST: Context A, Color Bayer mode, PLL enabled */
	{ CCI_REG16(0x30ba), 0x7600 }, /* Column Gain Bias: Update bias setting for low noise */
	{ CCI_REG16(0x30fe), 0x002a }, /* NOISE_PEDESTAL: Dark level pedestal offset = 42 LSB */
	{ CCI_REG16(0x31de), 0x0410 }, /* Datapath Control: Pipeline processing flags */
	{ CCI_REG16(0x3ed6), 0x1435 }, /* AFE / Colamp Bias 1: Column amplifier bias currents */
	{ CCI_REG16(0x3ed8), 0x9865 }, /* ADC Conversion Timing: Ramp timing & conversion slope */
	{ CCI_REG16(0x3eda), 0x7698 }, /* Column Readout Phase: Parallel readout phase timing */
	{ CCI_REG16(0x3edc), 0x99ff }, /* Analog Trimming: Circuit trimming & reference buffers */
	{ CCI_REG16(0x3ee2), 0xbb88 }, /* AFE Signal Path: Signal buffering & gain switching */
	{ CCI_REG16(0x3ee4), 0x8836 }, /* Colamp Gain Tuning: Calibrate gain feedback capacitors */
	{ CCI_REG16(0x3ef0), 0x1cf0 }, /* Pixel Array Bias: Reset voltage & column readout bias */
	{ CCI_REG16(0x3ef2), 0x0000 }, /* Analog Diagnostic: Normal mode, clear test overrides */
	{ CCI_REG16(0x3ef8), 0x6166 }, /* Column FPN Trim: Suppress vertical striping / FPN */
	{ CCI_REG16(0x3efa), 0x3333 }, /* ADC Ramp Reference: Calibrate reference ramp slope */
	{ CCI_REG16(0x3efc), 0x6634 }, /* AFE Column Trimming: Voltage buffering & ADC bias */
	{ CCI_REG16(0x3088), 0x81ba }, /* SEQ_CTRL_PORT: Halt sequencer, set RAM addr 0x1BA */
	{ CCI_REG16(0x3086), 0x3d02 }, /* SEQ_DATA_PORT: Write microcode word 0x3D02 */
	{ CCI_REG16(0x3276), 0x05dc }, /* LED_FLASH_DURATION: Active flash pulse duration */
	{ CCI_REG16(0x3f00), 0x9d05 }, /* Black Level Pipeline: Digital pipeline black offsets */
	{ CCI_REG16(0x3ed2), 0xfa86 }, /* ADC Ramp (High Gain): High-gain conversion slope */
	{ CCI_REG16(0x3eee), 0xa4fe }, /* Colamp Trim (High Gain): High-gain bias & capacitors */
	{ CCI_REG16(0x3ecc), 0x6e42 }, /* Column Power Sequence: Assert column reset bit */
	{ CCI_REG16(0x3ecc), 0x0e42 }, /* Column Power Sequence: Deassert column reset bit */
	{ CCI_REG16(0x3eec), 0x0c0c }, /* Fine Bias Trim: Balanced odd/even column bias currents */
	{ CCI_REG16(0x3ee8), 0xaae4 }, /* Colamp Bias Settling: Pre-stream bias write 1 */
	{ CCI_REG16(0x3ee6), 0x3363 }, /* Colamp Bias Toggle: Stabilize readout pipeline 1 */
	{ CCI_REG16(0x3ee6), 0x3363 }, /* Colamp Bias Toggle: Stabilize readout pipeline 2 */
	{ CCI_REG16(0x3ee8), 0xaae4 }, /* Colamp Bias Settling: Pre-stream bias write 2 */
	{ CCI_REG16(0x3ee8), 0xaae4 }, /* Colamp Bias Settling: Pre-stream bias write 3 */
	{ CCI_REG16(0x3180), 0xc24f }, /* DELTA_DK_CONTROL: Per-frame dark current correction */
	{ CCI_REG16(0x3102), 0x5000 }, /* AE_LUMA_TARGET_REG: AEC target luma brightness */
	{ CCI_REG16(0x3060), 0x000d }, /* ANALOG_GAIN: Context A 1x coarse, ~1.68x fine gain */
	{ CCI_REG16(0x3ed0), 0xff44 }, /* Colamp Gain Stage: High-gain stage bias setting */
	{ CCI_REG16(0x3ed2), 0xaa86 }, /* ADC Ramp (Std Gain): Standard conversion slope */
	{ CCI_REG16(0x3ed4), 0x031f }, /* Colamp Bypass Control: Amplifier stage bypass */
	{ CCI_REG16(0x3eee), 0xa4aa }, /* Colamp Trim (Std Gain): Standard stage bias & trim */
};

static const char * const ar0234_test_pattern_menu[] = {
	"Disabled",
	"Color Bars",
	"Solid Color",
	"Grey Color Bars",
	"Walking 1s",
};

static const int ar0234_test_pattern_val[] = {
	AR0234_TEST_PATTERN_DISABLE,
	AR0234_TEST_PATTERN_COLOR_BARS,
	AR0234_TEST_PATTERN_SOLID_COLOR,
	AR0234_TEST_PATTERN_GREY_COLOR,
	AR0234_TEST_PATTERN_WALKING,
};

static const s64 link_freq_menu_items[] = {
	360000000ULL,
};

static const struct ar0234_mode supported_modes[] = {
	{
		.width            = AR0234_NATIVE_WIDTH,
		.height           = AR0234_NATIVE_HEIGHT,
		.hts              = AR0234_HTS_MIN,
		.vts_def          = AR0234_VTS_DEFAULT,
		.code             = MEDIA_BUS_FMT_SGRBG10_1X10,
		.fps              = AR0234_FPS_60,
		.pixel_rate       = AR0234_PIXEL_RATE_256M,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
		.datatype         = MIPI_CSI2_DT_RAW10,
#endif
		.timing_regs      = mode_1920x1200_60fps_10bit_2lane_timing,
		.num_timing_regs  = ARRAY_SIZE(mode_1920x1200_60fps_10bit_2lane_timing),
	},
	{
		.width            = AR0234_COMMON_WIDTH,
		.height           = AR0234_COMMON_HEIGHT,
		.hts              = AR0234_HTS_DEFAULT,
		.vts_def          = AR0234_VTS_DEFAULT,
		.code             = MEDIA_BUS_FMT_SGRBG10_1X10,
		.fps              = AR0234_FPS_30,
		.pixel_rate       = AR0234_PIXEL_RATE_128M,
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
		.datatype         = MIPI_CSI2_DT_RAW10,
#endif
		.timing_regs      = mode_1280x960_30fps_10bit_2lane_timing,
		.num_timing_regs  = ARRAY_SIZE(mode_1280x960_30fps_10bit_2lane_timing),
	},
};

struct ar0234 {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct v4l2_ctrl_handler ctrl_handler;

	/* V4L2 Controls */
	struct v4l2_ctrl *link_freq;
	struct v4l2_ctrl *exposure;
	struct v4l2_ctrl *hblank;
	struct v4l2_ctrl *vblank;
	struct v4l2_ctrl *vflip;
	struct v4l2_ctrl *hflip;
	struct v4l2_ctrl *pixel_rate;

	struct regmap *regmap;
	const struct ar0234_mode *cur_mode;
	struct gpio_desc *reset_gpio;
	ar0234_platform_data *platform_data;
	u8 lanes;

	bool streaming;
	/* Firmware node's "gmsl-frame-sync-enable": frames start on TRIGGER */
	bool fw_frame_sync;
	bool common_regs_loaded;
#if IS_ENABLED(CONFIG_VIDEO_ZEDX)
	bool routing_initialized;
#endif
};

#if IS_ENABLED(CONFIG_VIDEO_ZEDX)
static unsigned int ar0234_g_stream(struct v4l2_subdev *sd)
{
	unsigned int len = strlen(sd->name);
	if (len >= 4 &&
	    (strncmp(sd->name + len - 4, "0010", 4) ||
	     !strncmp(sd->name, "ar0234 e", 8) ||
	     !strncmp(sd->name, "ar0234 f", 8) ||
	     !strncmp(sd->name, "ar0234 g", 8) ||
	     !strncmp(sd->name, "ar0234 h", 8)))
		return 1;
	else
		return 0;
}
#endif

static int ar0234_set_ctrl(struct v4l2_ctrl *ctrl)
{
	struct ar0234 *ar0234 =
		container_of(ctrl->handler, struct ar0234, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	s64 exposure_max, exposure_def;
	struct v4l2_subdev_state *state;
	const struct v4l2_mbus_framefmt *format;
	int ret;

	state = v4l2_subdev_get_locked_active_state(&ar0234->sd);
#if IS_ENABLED(CONFIG_VIDEO_ZEDX)
	struct v4l2_subdev *sd = &ar0234->sd;
	unsigned int streamid = ar0234_g_stream(sd);
	format = v4l2_subdev_state_get_format(state, 0, streamid);
	if (!format) {
		dev_warn(&client->dev, "Failed to get %s format for pad %u/%u \n",
			 sd->name, 0, streamid);
		return -EINVAL;

	}
#else
	format = v4l2_subdev_state_get_format(state, 0);
#endif

	/* Propagate change of current control to all related controls */
	if (ctrl->id == V4L2_CID_VBLANK) {
		/* Update max exposure while meeting expected vblanking */
		exposure_max = format->height + ctrl->val -
			       AR0234_EXPOSURE_MAX_MARGIN;
		exposure_def = format->height - AR0234_EXPOSURE_MAX_MARGIN;
		ret = __v4l2_ctrl_modify_range(ar0234->exposure,
					 ar0234->exposure->minimum,
					 exposure_max, ar0234->exposure->step,
					 exposure_def);
		if (ret) {
			dev_err(&client->dev, "Exposure ctrl range update failed");
			return ret;
		}
	}

	/* V4L2 controls values will be applied only when power is already up */
	if (!pm_runtime_get_if_in_use(&client->dev))
		return 0;

	switch (ctrl->id) {
	case V4L2_CID_ANALOGUE_GAIN:
		ret = cci_write(ar0234->regmap, AR0234_REG_ANALOG_GAIN,
				ctrl->val, NULL);
		break;

	case V4L2_CID_DIGITAL_GAIN:
		ret = cci_write(ar0234->regmap, AR0234_REG_GLOBAL_GAIN,
				ctrl->val, NULL);
		break;

	case V4L2_CID_EXPOSURE:
		ret = cci_write(ar0234->regmap, AR0234_REG_EXPOSURE,
				ctrl->val, NULL);
		break;

	case V4L2_CID_VBLANK:
		ret = cci_write(ar0234->regmap, AR0234_REG_VTS,
				ar0234->cur_mode->height + ctrl->val, NULL);
		break;

	case V4L2_CID_HFLIP:
	case V4L2_CID_VFLIP:
		u64 reg;

		ret = cci_read(ar0234->regmap, AR0234_REG_ORIENTATION,
			       &reg, NULL);
		if (ret)
			break;

		reg &= ~(AR0234_ORIENTATION_HFLIP |
			 AR0234_ORIENTATION_VFLIP);
		if (ar0234->hflip->val)
			reg |= AR0234_ORIENTATION_HFLIP;
		if (ar0234->vflip->val)
			reg |= AR0234_ORIENTATION_VFLIP;

		ret = cci_write(ar0234->regmap, AR0234_REG_ORIENTATION,
				reg, NULL);
		break;

	case V4L2_CID_TEST_PATTERN:
		ret = cci_write(ar0234->regmap, AR0234_REG_TEST_PATTERN,
				ar0234_test_pattern_val[ctrl->val], NULL);
		break;

	/* Read-only controls, range updated on mode change; nothing to write */
	case V4L2_CID_PIXEL_RATE:
	case V4L2_CID_HBLANK:
	case V4L2_CID_LINK_FREQ:
		ret = 0;
		break;

	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops ar0234_ctrl_ops = {
	.s_ctrl = ar0234_set_ctrl,
};

static int ar0234_init_controls(struct ar0234 *ar0234)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	struct v4l2_fwnode_device_properties props;
	struct v4l2_ctrl_handler *ctrl_hdlr;
	s64 exposure_max, vblank_max, vblank_def, hblank;
	int ret;

	ctrl_hdlr = &ar0234->ctrl_handler;
	ret = v4l2_ctrl_handler_init(ctrl_hdlr, 10);
	if (ret)
		return ret;

	ar0234->link_freq = v4l2_ctrl_new_int_menu(ctrl_hdlr,
							&ar0234_ctrl_ops,
							V4L2_CID_LINK_FREQ,
							ARRAY_SIZE(link_freq_menu_items) - 1, 0,
							link_freq_menu_items);
	if (ar0234->link_freq)
		ar0234->link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
			  AR0234_ANALOG_GAIN_MIN, AR0234_ANALOG_GAIN_MAX,
			  AR0234_ANALOG_GAIN_STEP, AR0234_ANALOG_GAIN_DEFAULT);
	v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops, V4L2_CID_DIGITAL_GAIN,
			  AR0234_GLOBAL_GAIN_MIN, AR0234_GLOBAL_GAIN_MAX,
			  AR0234_GLOBAL_GAIN_STEP, AR0234_GLOBAL_GAIN_DEFAULT);

	exposure_max = ar0234->cur_mode->vts_def - AR0234_EXPOSURE_MAX_MARGIN;
	ar0234->exposure = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					     V4L2_CID_EXPOSURE,
					     AR0234_EXPOSURE_MIN, exposure_max,
					     AR0234_EXPOSURE_STEP,
					     exposure_max);

	vblank_max = AR0234_VTS_MAX - ar0234->cur_mode->height;
	vblank_def = ar0234->cur_mode->vts_def - ar0234->cur_mode->height;
	/* min = vblank_def: vts_def is the shortest frame (max fps) of the mode */
	ar0234->vblank = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					   V4L2_CID_VBLANK, vblank_def,
					   vblank_max, 1, vblank_def);
	hblank = AR0234_PPL_DEFAULT - ar0234->cur_mode->width;
	ar0234->hblank = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					   V4L2_CID_HBLANK, hblank, hblank, 1,
					   hblank);
	if (ar0234->hblank)
		ar0234->hblank->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ar0234->pixel_rate = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					       V4L2_CID_PIXEL_RATE,
					       ar0234->cur_mode->pixel_rate,
					       ar0234->cur_mode->pixel_rate, 1,
					       ar0234->cur_mode->pixel_rate);
	if (ar0234->pixel_rate)
		ar0234->pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	ar0234->hflip = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					  V4L2_CID_HFLIP, 0, 1, 1, 0);
	ar0234->vflip = v4l2_ctrl_new_std(ctrl_hdlr, &ar0234_ctrl_ops,
					  V4L2_CID_VFLIP, 0, 1, 1, 0);

	v4l2_ctrl_new_std_menu_items(ctrl_hdlr, &ar0234_ctrl_ops,
				     V4L2_CID_TEST_PATTERN,
				     ARRAY_SIZE(ar0234_test_pattern_menu) - 1,
				     0, 0, ar0234_test_pattern_menu);

	if (ctrl_hdlr->error)
		return ctrl_hdlr->error;

	ret = v4l2_fwnode_device_parse(&client->dev, &props);
	if (ret)
		return ret;

	ret = v4l2_ctrl_new_fwnode_properties(ctrl_hdlr, &ar0234_ctrl_ops,
					      &props);
	if (ret)
		return ret;

	ar0234->sd.ctrl_handler = ctrl_hdlr;

	return 0;
}

static void ar0234_update_pad_format(const struct ar0234_mode *mode,
				     struct v4l2_mbus_framefmt *fmt)
{
	fmt->width = mode->width;
	fmt->height = mode->height;
	fmt->code = mode->code;
	fmt->field = V4L2_FIELD_NONE;
}

static int ar0234_get_num_lane(struct ar0234 *ar0234, struct device *dev)
{
	struct fwnode_handle *endpoint;
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};

	int ret;

	endpoint =
		fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0,
						FWNODE_GRAPH_ENDPOINT_NEXT);
	if (!endpoint) {
		dev_err(dev, "endpoint node not found");
		return -EPROBE_DEFER;
	}

	ret = v4l2_fwnode_endpoint_alloc_parse(endpoint, &bus_cfg);
	if (ret) {
		dev_err(dev, "parsing endpoint node fail");
		goto out_err;
	}

	/* Check the number of MIPI CSI2 data lanes */
	if (bus_cfg.bus.mipi_csi2.num_data_lanes != 2) {
		dev_err(dev, "only 2 data lanes are currently supported");
		goto out_err;
	}

	ar0234->lanes = bus_cfg.bus.mipi_csi2.num_data_lanes;

out_err:
	v4l2_fwnode_endpoint_free(&bus_cfg);
	fwnode_handle_put(endpoint);

	return ret;
}

static int ar0234_start_streaming(struct ar0234 *ar0234)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	const struct ar0234_reg_list *reg_list;
	bool sync = frame_sync < 0 ? ar0234->fw_frame_sync : frame_sync > 0;
	const struct ar0234_mode *mode = ar0234->cur_mode;
	int ret;

	/*
	 * Setting 0x301A.bit[0] will initiate a reset sequence:
	 * the frame being generated will be truncated.
	 */
	ret = cci_write(ar0234->regmap, AR0234_REG_MODE_SELECT,
			AR0234_MODE_RESET, NULL);
	if (ret) {
		dev_err(&client->dev, "failed to reset");
		goto err_rpm_put;
	}

	msleep(10);

	/* DDC/operating-mode & sequencer microcode: one-time load only. */
	if (!ar0234->common_regs_loaded) {
		ret = cci_multi_reg_write(ar0234->regmap, ar0234_common_ddc_mode_regs,
					  ARRAY_SIZE(ar0234_common_ddc_mode_regs), NULL);
		if (ret) {
			dev_err(&client->dev, "failed to set DDC/mode regs");
			goto err_rpm_put;
		}

		ret = cci_multi_reg_write(ar0234->regmap, ar0234_common_seq_microcode_regs,
					  ARRAY_SIZE(ar0234_common_seq_microcode_regs), NULL);
		if (ret) {
			dev_err(&client->dev, "failed to load sequencer microcode");
			goto err_rpm_put;
		}
	}

	ret = cci_multi_reg_write(ar0234->regmap, mode->timing_regs,
				  mode->num_timing_regs, NULL);
	if (ret) {
		dev_err(&client->dev, "failed to set mode");
		goto err_rpm_put;
	}

	/* AFE/colamp calibration: one-time load only. */
	if (!ar0234->common_regs_loaded) {
		ret = cci_multi_reg_write(ar0234->regmap, ar0234_common_afe_colamp_regs,
					  ARRAY_SIZE(ar0234_common_afe_colamp_regs), NULL);
		if (ret) {
			dev_err(&client->dev, "failed to set AFE/colamp calibration");
			goto err_rpm_put;
		}

		ar0234->common_regs_loaded = true;
	}

	ret = __v4l2_ctrl_handler_setup(ar0234->sd.ctrl_handler);
	if (ret)
		goto err_rpm_put;

	/*
	 * In slave sync mode, a trigger starts an interval of frame length
	 * lines at the end of which readout begins, and a trigger arriving
	 * during that interval is ignored: the trigger period must not be
	 * shorter than the frame (33.2 ms at the default VTS, which a 30 Hz
	 * trigger allows). Written either way, so that a stream started
	 * without sync never waits for a trigger.
	 */
	ret = cci_write(ar0234->regmap, AR0234_REG_GRR_CONTROL1,
			sync ? AR0234_GRR_SLAVE_SH_SYNC_MODE |
			       AR0234_GRR_FRAME_START_MODE : 0, NULL);
	if (ret) {
		dev_err(&client->dev, "failed to set the frame sync mode");
		goto err_rpm_put;
	}

	ret = cci_write(ar0234->regmap, AR0234_REG_MODE_SELECT,
			sync ? AR0234_MODE_STREAMING_SYNC : AR0234_MODE_STREAMING,
			NULL);
	if (ret) {
		dev_err(&client->dev, "failed to start stream");
		goto err_rpm_put;
	}
	if (sync)
		dev_dbg(&client->dev, "streaming on the frame sync trigger");

	ar0234->streaming = true;
	return 0;

err_rpm_put:
	return ret;
}

static int ar0234_stop_streaming(struct ar0234 *ar0234)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	int ret;

	ret = cci_write(ar0234->regmap, AR0234_REG_MODE_SELECT,
			AR0234_MODE_STANDBY, NULL);
	if (ret) {
		dev_err(&client->dev, "failed to stop stream: %d", ret);
		goto err_rpm_put;
	}

	ar0234->streaming = false;
	return 0;

err_rpm_put:
	return ret;
}

static int ar0234_set_stream(struct v4l2_subdev *sd, int enable)
{
	struct ar0234 *ar0234 = to_ar0234(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret = 0;

	mutex_lock(&ar0234_mutex);

	if (ar0234->streaming == enable)
		goto unlock;

	if (enable) {
		ret = pm_runtime_resume_and_get(&client->dev);
		if (ret < 0)
			goto unlock;

		ret = ar0234_start_streaming(ar0234);
		if (ret) {
			ret = ar0234_stop_streaming(ar0234);
			pm_runtime_put(&client->dev);
			goto unlock;
		}
	}
	else {
		ret = ar0234_stop_streaming(ar0234);
		if (ret) {
			goto unlock;
		}
		pm_runtime_put(&client->dev);
	}

	/* vflip and hflip cannot change during streaming */
	__v4l2_ctrl_grab(ar0234->vflip, enable);
	__v4l2_ctrl_grab(ar0234->hflip, enable);

unlock:
	mutex_unlock(&ar0234_mutex);
	return ret;
}

static int ar0234_set_format(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state,
			     struct v4l2_subdev_format *fmt)
{
	struct ar0234 *ar0234 = to_ar0234(sd);
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	struct v4l2_rect *crop;
	const struct ar0234_mode *mode;
	s64 hblank;
	int ret=0;

	mode = v4l2_find_nearest_size(supported_modes,
				      ARRAY_SIZE(supported_modes),
				      width, height,
				      fmt->format.width,
				      fmt->format.height);

	dev_dbg(&client->dev,
		"%s(): fmt->pad/stream: %u/%u@0x%x/%ux%u\n",__func__,
		fmt->pad, fmt->stream,
		fmt->format.code,
		fmt->format.width,
		fmt->format.height);

#if IS_ENABLED(CONFIG_VIDEO_ZEDX)
	if (!sd_state) {
		ret = -EINVAL;
		goto ar0234_s_fmt_state_end;
	}

	if (fmt->pad) {
		ret = -EINVAL;
		goto ar0234_s_fmt_state_end;
	}

	crop = v4l2_subdev_state_get_crop(sd_state, fmt->pad, fmt->stream);
#else
	crop = v4l2_subdev_state_get_crop(sd_state, fmt->pad);
#endif
	crop->left = AR0234_PIXEL_ARRAY_LEFT;
	crop->top = AR0234_PIXEL_ARRAY_TOP;
	crop->width = mode->width;
	crop->height = mode->height;

	ar0234_update_pad_format(mode, &fmt->format);

#if IS_ENABLED(CONFIG_VIDEO_ZEDX)
	struct v4l2_mbus_framefmt *mf;
	mf = v4l2_subdev_state_get_format(sd_state, fmt->pad, fmt->stream);
	if (!mf) {
		dev_warn(&client->dev, "Failed to get %s format for pad %u/%u \n",
			 sd->name, fmt->pad, fmt->stream);
		ret = -EINVAL;
		goto ar0234_s_fmt_state_end;
	}
	*mf = fmt->format;
#else
	*v4l2_subdev_state_get_format(sd_state, fmt->pad) = fmt->format;
#endif

	if (fmt->which == V4L2_SUBDEV_FORMAT_TRY)
		return 0;

	ar0234->cur_mode = mode;

	hblank = AR0234_PPL_DEFAULT - mode->width;
	ret = __v4l2_ctrl_modify_range(ar0234->hblank, hblank, hblank,
				       1, hblank);
	if (ret) {
		dev_err(&client->dev, "HB ctrl range update failed");
		return ret;
	}

	ret = __v4l2_ctrl_modify_range(ar0234->pixel_rate, mode->pixel_rate,
				       mode->pixel_rate, 1, mode->pixel_rate);
	if (ret) {
		dev_err(&client->dev, "pixel rate ctrl update failed");
		return ret;
	}

	/* Update limits and set FPS to default */
	ret = __v4l2_ctrl_modify_range(ar0234->vblank,
				       mode->vts_def - mode->height,
				       AR0234_VTS_MAX - mode->height, 1,
				       mode->vts_def - mode->height);
	if (ret) {
		dev_err(&client->dev, "VB ctrl range update failed");
		return ret;
	}

	ret = __v4l2_ctrl_s_ctrl(ar0234->vblank, mode->vts_def - mode->height);
	if (ret) {
		dev_err(&client->dev, "VB ctrl set failed");
		return ret;
	}

#if IS_ENABLED(CONFIG_VIDEO_ZEDX)
ar0234_s_fmt_state_end:
	return ret;
#else
	return 0;
#endif
}

static int ar0234_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index > 0)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SGRBG10_1X10;

	return 0;
}

static int ar0234_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index >= ARRAY_SIZE(supported_modes))
		return -EINVAL;

	if (fse->code != MEDIA_BUS_FMT_SGRBG10_1X10)
		return -EINVAL;

	fse->min_width = supported_modes[fse->index].width;
	fse->max_width = fse->min_width;
	fse->min_height = supported_modes[fse->index].height;
	fse->max_height = fse->min_height;

	return 0;
}

/*
 * mode->fps is only the nominal rate baked into supported_modes[]; the live
 * rate can differ because V4L2_CID_VBLANK lets userspace stretch VTS. Scale
 * that nominal (fps, hts, vts_def) baseline by the live HTS/VTS registers
 * instead, since the absolute pixel clock isn't otherwise derivable here.
 */
static unsigned int ar0234_get_fps(struct ar0234 *ar0234)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	const struct ar0234_mode *mode = ar0234->cur_mode;
	u64 hts, vts;
	int ret;

	if (!pm_runtime_get_if_in_use(&client->dev))
		return mode->fps;

	ret = cci_read(ar0234->regmap, AR0234_REG_HTS, &hts, NULL);
	if (!ret)
		ret = cci_read(ar0234->regmap, AR0234_REG_VTS, &vts, NULL);

	pm_runtime_put(&client->dev);

	if (ret || !hts || !vts)
		return mode->fps;

	/* live_fps = nominal_fps * (nominal_hts * nominal_vts) / (live_hts * live_vts) */
	return div64_u64((u64)mode->fps * mode->hts * mode->vts_def,
			  hts * vts);
}

/*
 * Each resolution has exactly one fixed frame rate baked into its register
 * sequence (see supported_modes[]), so enumeration just reports the single
 * fps that matches the requested code/width/height -- the live rate (see
 * ar0234_get_fps()) if that's the currently active mode, otherwise the
 * nominal value since the sensor isn't configured for it.
 */
static int ar0234_enum_frame_interval(struct v4l2_subdev *sd,
				      struct v4l2_subdev_state *sd_state,
				      struct v4l2_subdev_frame_interval_enum *fie)
{
	struct ar0234 *ar0234 = to_ar0234(sd);
	unsigned int i;

	if (fie->index > 0)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(supported_modes); i++) {
		if (supported_modes[i].code != fie->code ||
		    supported_modes[i].width != fie->width ||
		    supported_modes[i].height != fie->height)
			continue;

		fie->interval.numerator = 1;
		fie->interval.denominator = &supported_modes[i] == ar0234->cur_mode ?
			ar0234_get_fps(ar0234) : supported_modes[i].fps;
		return 0;
	}

	return -EINVAL;
}

static int ar0234_get_frame_interval(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_frame_interval *fi)
{
	struct ar0234 *ar0234 = to_ar0234(sd);

	mutex_lock(&ar0234_mutex);
	fi->interval.numerator = 1;
	fi->interval.denominator = ar0234_get_fps(ar0234);
	mutex_unlock(&ar0234_mutex);

	return 0;
}

/*
 * Resolution fixes the register sequence (see supported_modes[]), so the
 * frame rate can't be switched independently of it; this does not change
 * mode. It only reports the live rate (see ar0234_get_fps()), which keeps
 * callers that unconditionally try to (re)apply the detected FPS
 * (e.g. mc-setup.sh) working instead of failing with -ENOIOCTLCMD.
 */
static int ar0234_set_frame_interval(struct v4l2_subdev *sd,
				     struct v4l2_subdev_state *sd_state,
				     struct v4l2_subdev_frame_interval *fi)
{
	struct ar0234 *ar0234 = to_ar0234(sd);

	if (fi->interval.numerator == 0 || fi->interval.denominator == 0)
		return -EINVAL;

	mutex_lock(&ar0234_mutex);
	fi->interval.numerator = 1;
	fi->interval.denominator = ar0234_get_fps(ar0234);
	mutex_unlock(&ar0234_mutex);

	return 0;
}

static int ar0234_get_selection(struct v4l2_subdev *sd,
				struct v4l2_subdev_state *state,
				struct v4l2_subdev_selection *sel)
{

	switch (sel->target) {
	case V4L2_SEL_TGT_CROP_DEFAULT:
	case V4L2_SEL_TGT_CROP_BOUNDS:
		/* The whole active array: the mode's window is reported by V4L2_SEL_TGT_CROP. */
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = AR0234_NATIVE_WIDTH;
		sel->r.height = AR0234_NATIVE_HEIGHT;
		break;

	case V4L2_SEL_TGT_CROP:
#if IS_ENABLED(CONFIG_VIDEO_ZEDX)
		sel->r = *v4l2_subdev_state_get_crop(state, 0, sel->stream);
#else
		sel->r = *v4l2_subdev_state_get_crop(state, 0);
#endif
		break;

	case V4L2_SEL_TGT_NATIVE_SIZE:
		sel->r.top = 0;
		sel->r.left = 0;
		sel->r.width = AR0234_NATIVE_WIDTH;
		sel->r.height = AR0234_NATIVE_HEIGHT;
		break;

	default:
		return -EINVAL;
	}

	return 0;
}

static int ar0234_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct v4l2_subdev_format fmt = {
		.which = V4L2_SUBDEV_FORMAT_TRY,
		.pad = 0,
		.format = {
			.code = MEDIA_BUS_FMT_SGRBG10_1X10,
			.width = AR0234_NATIVE_WIDTH,
			.height = AR0234_NATIVE_HEIGHT,
		},
	};

#if IS_ENABLED(CONFIG_VIDEO_ZEDX)
	struct ar0234 *ar0234 = to_ar0234(sd);
	if (ar0234->routing_initialized)
		return 0;

	struct i2c_client *client = v4l2_get_subdevdata(sd);
	unsigned int streamid = ar0234_g_stream(sd);

	struct v4l2_subdev_route routes[] = {
		{
			.sink_pad = 0,
			.sink_stream = streamid,
			.source_pad = 0,
			.source_stream = streamid,
			.flags = V4L2_SUBDEV_ROUTE_FL_ACTIVE,
		},
	};

	struct v4l2_subdev_krouting routing = {
		.num_routes = ARRAY_SIZE(routes),
		.routes = routes,
	};

	// set default stream
	fmt.stream = streamid;

	dev_dbg(&client->dev, "%s(): default route %s on streamid=%u",__func__,
		sd->name, streamid);

	/* Needed by v4l2_subdev_s_stream_helper(), even with 1 stream only
	*/
	int ret = v4l2_subdev_set_routing(sd, sd_state, &routing);
	if (ret) {
		dev_warn(&client->dev, "%s(): failed to set default route on %s\n",
			__func__, sd->name);
		return ret;
	}

#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 18, 0)
	struct v4l2_subdev_route *route;
	for_each_active_route(&sd_state->routing, route) {
		struct v4l2_mbus_framefmt *mf;

		mf = v4l2_subdev_state_get_format(sd_state, route->source_pad,
						   route->source_stream);
		*mf = fmt.format;
	}
#else
	struct v4l2_subdev_stream_configs *stream_configs;
	unsigned int i;
	stream_configs = &sd_state->stream_configs;

	for (i = 0; i < stream_configs->num_configs; i++) {
		stream_configs->configs[i].fmt = fmt.format;
	}
#endif
	ar0234->routing_initialized = true;
#endif
	ar0234_set_format(sd, sd_state, &fmt);

	return 0;
}

static int ar0234_enable_streams(struct v4l2_subdev *subdev,
       struct v4l2_subdev_state *state,
       u32 pad, u64 streams_mask)
{

	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	dev_dbg(&client->dev, "%s(): enable: name=%s pad=%u streams_mask=%llu\n",
		__func__, subdev->name, pad, streams_mask);

	return ar0234_set_stream(subdev, true);
}

static int ar0234_disable_streams(struct v4l2_subdev *subdev,
        struct v4l2_subdev_state *state,
        u32 pad, u64 streams_mask)
{

	struct i2c_client *client = v4l2_get_subdevdata(subdev);
	dev_dbg(&client->dev, "%s(): disable: name=%s pad=%u streams_mask=%llu\n",
		__func__, subdev->name, pad, streams_mask);

	return ar0234_set_stream(subdev, false);
}

// TODO: Clean up for older kernel version
#if LINUX_VERSION_CODE >= KERNEL_VERSION(6, 10, 0)
static int ar0234_get_frame_desc(struct v4l2_subdev *sd,
        unsigned int pad, struct v4l2_mbus_frame_desc *desc)
{
        struct ar0234 *ar0234 = to_ar0234(sd);
#if IS_ENABLED(CONFIG_VIDEO_ZEDX)
	struct i2c_client *client = v4l2_get_subdevdata(sd);
#endif
	
        desc->type = V4L2_MBUS_FRAME_DESC_TYPE_CSI2;
        desc->num_entries = 0;

	desc->entry[desc->num_entries].flags = V4L2_MBUS_FRAME_DESC_FL_LEN_MAX;
#if IS_ENABLED(CONFIG_VIDEO_ZEDX)
	/*
	 * Append stream assignment: if the I2C subdevice address
	 * is not default 0x10 (secondary sensor), route via CSI-2 stream 1.
	 */
	desc->entry[desc->num_entries].stream = ar0234_g_stream(sd);
#else
	desc->entry[desc->num_entries].stream = 0;
#endif
	desc->entry[desc->num_entries].pixelcode = ar0234->cur_mode->code;
	desc->entry[desc->num_entries].length = 0;
	desc->entry[desc->num_entries].bus.csi2.vc = 0;
	desc->entry[desc->num_entries].bus.csi2.dt = ar0234->cur_mode->datatype;
#if IS_ENABLED(CONFIG_VIDEO_ZEDX)
	dev_dbg(&client->dev, "%s: set %s csi dt/vc=0x%x/0x%x",__func__,
		sd->name,
		desc->entry[desc->num_entries].bus.csi2.dt,
		desc->entry[desc->num_entries].bus.csi2.vc);
#endif
	desc->num_entries++;
	return 0;
}
#else
static int ar0234_get_frame_desc(struct v4l2_subdev *sd,
        unsigned int pad, struct v4l2_mbus_frame_desc *desc)
{
        unsigned int i;

        desc->num_entries = V4L2_FRAME_DESC_ENTRY_MAX;

        for (i = 0; i < desc->num_entries; i++) {
                desc->entry[desc->num_entries].flags = 0;
                desc->entry[desc->num_entries].pixelcode = MEDIA_BUS_FMT_FIXED;
                desc->entry[desc->num_entries].length = 0;
        }

        return 0;
}
#endif

static const struct v4l2_subdev_video_ops ar0234_video_ops = {
	.s_stream = ar0234_set_stream,
};

static const struct v4l2_subdev_pad_ops ar0234_pad_ops = {
	.set_fmt = ar0234_set_format,
	.get_fmt = v4l2_subdev_get_fmt,
	.enum_mbus_code = ar0234_enum_mbus_code,
	.enum_frame_size = ar0234_enum_frame_size,
	.enum_frame_interval = ar0234_enum_frame_interval,
	.get_frame_interval = ar0234_get_frame_interval,
	.set_frame_interval = ar0234_set_frame_interval,
	.get_selection = ar0234_get_selection,
	.enable_streams = ar0234_enable_streams,
	.disable_streams = ar0234_disable_streams,
	.get_frame_desc = ar0234_get_frame_desc,
};

static const struct v4l2_subdev_core_ops ar0234_core_ops = {
	.subscribe_event = v4l2_ctrl_subdev_subscribe_event,
	.unsubscribe_event = v4l2_event_subdev_unsubscribe,
};

static const struct v4l2_subdev_ops ar0234_subdev_ops = {
	.core = &ar0234_core_ops,
	.video = &ar0234_video_ops,
	.pad = &ar0234_pad_ops,
};

static const struct media_entity_operations ar0234_subdev_entity_ops = {
	.link_validate = v4l2_subdev_link_validate,
};

static const struct v4l2_subdev_internal_ops ar0234_internal_ops = {
	.init_state = ar0234_init_state,
};

static int ar0234_identify_module(struct ar0234 *ar0234)
{
	struct i2c_client *client = v4l2_get_subdevdata(&ar0234->sd);
	int ret;
	u64 val;

	ret = cci_read(ar0234->regmap, AR0234_REG_CHIP_ID, &val, NULL);
	if (ret)
		return ret;

	if (val != AR0234_CHIP_ID) {
		dev_err(&client->dev, "chip id mismatch: %x!=%llx",
			AR0234_CHIP_ID, val);
		return -ENXIO;
	}

	return 0;
}

static void ar0234_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0234 *ar0234 = to_ar0234(sd);

	v4l2_async_unregister_subdev(&ar0234->sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&ar0234->sd.entity);
	v4l2_ctrl_handler_free(&ar0234->ctrl_handler);
	pm_runtime_disable(&client->dev);
	pm_runtime_set_suspended(&client->dev);
}

static int ar0234_reset(struct gpio_desc *reset_gpio)
{
	if (!IS_ERR_OR_NULL(reset_gpio)) {
		gpiod_direction_output(reset_gpio, 0);

		gpiod_set_value_cansleep(reset_gpio, 1);
		msleep(10);

		gpiod_set_value_cansleep(reset_gpio, 0);
		msleep(10);

		return 0;
	}

	return -EINVAL;
}

static int ar0234_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct ar0234 *ar0234;
	u32 val;
	int ret;

	ar0234 = devm_kzalloc(&client->dev, sizeof(*ar0234), GFP_KERNEL);
	if (!ar0234)
		return -ENOMEM;

	ar0234->platform_data = client->dev.platform_data;
	if (!ar0234->platform_data)
		dev_warn(&client->dev, "No platform data provided\n");

	ar0234->regmap = devm_cci_regmap_init_i2c(client, 16);
	if (IS_ERR(ar0234->regmap))
		return dev_err_probe(dev, PTR_ERR(ar0234->regmap),
				     "failed to init CCI");

	v4l2_i2c_subdev_init(&ar0234->sd, client, &ar0234_subdev_ops);

	/* 0 or 1, as on the deserializer and serializer; absent means 0. */
	if (!fwnode_property_read_u32(dev_fwnode(dev), "gmsl-frame-sync-enable",
				      &val))
		ar0234->fw_frame_sync = !!val;

	ar0234->reset_gpio = devm_gpiod_get_optional(&client->dev, "reset",
							GPIOD_ASIS);
	if (IS_ERR(ar0234->reset_gpio))
		return -EPROBE_DEFER;
	else if (ar0234->reset_gpio == NULL)
		dev_warn(&client->dev, "Reset GPIO not found");
	else {
		/* desc_to_gpio returns the global GPIO integer namespace number */
		int gpio_num = desc_to_gpio(ar0234->reset_gpio);
		dev_dbg(&client->dev, "Found reset GPIO : %d", gpio_num);
		ar0234_reset(ar0234->reset_gpio);
	}

	/* Check module identity */
	ret = ar0234_identify_module(ar0234);
	if (ret) {
		dev_err(dev, "failed to find sensor: %d", ret);
		return ret;
	}

	ar0234->cur_mode = &supported_modes[0];
	ret = ar0234_init_controls(ar0234);
	if (ret) {
		dev_err(&client->dev, "failed to init controls: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ar0234->sd.internal_ops = &ar0234_internal_ops;
	ar0234->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE |
			    V4L2_SUBDEV_FL_HAS_EVENTS;
#if IS_ENABLED(CONFIG_VIDEO_ZEDX)
	ar0234->sd.flags |= V4L2_SUBDEV_FL_STREAMS;
#endif
	ar0234->sd.entity.ops = &ar0234_subdev_entity_ops;
	ar0234->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;

	ar0234->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&ar0234->sd.entity, 1, &ar0234->pad);
	if (ret) {
		dev_err(&client->dev, "failed to init entity pads: %d", ret);
		goto probe_error_v4l2_ctrl_handler_free;
	}

	ar0234->sd.state_lock = ar0234->ctrl_handler.lock;
	ret = v4l2_subdev_init_finalize(&ar0234->sd);
	if (ret < 0) {
		dev_err(dev, "v4l2 subdev init error: %d", ret);
		goto probe_error_media_entity_cleanup;
	}

	if (ar0234->platform_data && ar0234->platform_data->suffix[0])
		snprintf(ar0234->sd.name, sizeof(ar0234->sd.name), "ar0234 %s",
			ar0234->platform_data->suffix);

	if (ar0234->platform_data && ar0234->platform_data->lanes)
		ar0234->lanes = ar0234->platform_data->lanes;
	else {
		/* Read info from fwnode entrypoint bus cfg if no platform data */
		ret = ar0234_get_num_lane(ar0234, &client->dev);
		if (ret) {
			dev_err(&client->dev, "failed to get MIPI lane configuration");
			goto probe_error_media_entity_cleanup;
		}
	}

	/*
	 * Device is already turned on by i2c-core with ACPI domain PM.
	 * Enable runtime PM and turn off the device.
	 */
	pm_runtime_set_active(&client->dev);
	pm_runtime_enable(&client->dev);
	pm_runtime_idle(&client->dev);

#if LINUX_VERSION_CODE < KERNEL_VERSION(5, 13, 0)
	ret = v4l2_async_register_subdev_sensor_common(&ar0234->sd);
#else
	ret = v4l2_async_register_subdev_sensor(&ar0234->sd);
#endif
	if (ret < 0) {
		dev_err(&client->dev, "failed to register V4L2 subdev: %d",
			ret);
		goto probe_error_rpm;
	}

	return 0;
probe_error_rpm:
	pm_runtime_disable(&client->dev);
	v4l2_subdev_cleanup(&ar0234->sd);

probe_error_media_entity_cleanup:
	media_entity_cleanup(&ar0234->sd.entity);

probe_error_v4l2_ctrl_handler_free:
	v4l2_ctrl_handler_free(ar0234->sd.ctrl_handler);

	return ret;
}

static int __maybe_unused ar0234_suspend(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0234 *ar0234 = to_ar0234(sd);

	mutex_lock(&ar0234_mutex);

	// TODO: Add logic to support suspend during streaming later

	if (ar0234->streaming)
		ar0234_stop_streaming(ar0234);

	mutex_unlock(&ar0234_mutex);

	if (ar0234->reset_gpio) {
		gpiod_set_value_cansleep(ar0234->reset_gpio, 1);
		/* Reset discards sensor state; one-time regs must be reloaded */
		ar0234->common_regs_loaded = false;
	}

	return 0;
}

static int __maybe_unused ar0234_resume(struct device *dev)
{
	struct i2c_client *client = to_i2c_client(dev);
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct ar0234 *ar0234 = to_ar0234(sd);
	int ret;
	int count;

	mutex_lock(&ar0234_mutex);

	if (ar0234->reset_gpio) {
		for (count = 0; count < AR0234_PM_MAX_RETRY; count++) {
			gpiod_set_value_cansleep(ar0234->reset_gpio, 0);
			msleep(AR0234_REG_SLEEP_200MS);

			ret = gpiod_get_value_cansleep(ar0234->reset_gpio);
			if (ret == 0)
				break;
		}

		if (ret != 0) {
			dev_err(&client->dev, "failed to power on sensor in pm resume");
			mutex_unlock(&ar0234_mutex);
			return -ETIMEDOUT;
		}
	}

	// TODO: Add logic to support resume streaming after back from suspend later

	if (ar0234->streaming) {
		ret = ar0234_start_streaming(ar0234);
		if (ret) {
			ar0234_stop_streaming(ar0234);
			goto unlock;
		}
	}

unlock:
	mutex_unlock(&ar0234_mutex);
	return 0;
}

static const struct dev_pm_ops ar0234_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(ar0234_suspend, ar0234_resume)
};

static const struct i2c_device_id ar0234_id_table[] = {
	{ "ar0234", 0 },
	{}
};
MODULE_DEVICE_TABLE(i2c, ar0234_id_table);

static const struct acpi_device_id ar0234_acpi_ids[] = {
	{ "INTC10C0" },
	{}
};
MODULE_DEVICE_TABLE(acpi, ar0234_acpi_ids);

static struct i2c_driver ar0234_i2c_driver = {
	.driver = {
		.name = "ar0234",
		.acpi_match_table = ACPI_PTR(ar0234_acpi_ids),
		.pm = &ar0234_pm_ops,
	},
	.probe = ar0234_probe,
	.remove = ar0234_remove,
	.id_table = ar0234_id_table,
};

module_i2c_driver(ar0234_i2c_driver);

MODULE_DESCRIPTION("ON Semiconductor ar0234 sensor driver");
MODULE_AUTHOR("Dongcheng Yan <dongcheng.yan@intel.com>");
MODULE_AUTHOR("Hao Yao <hao.yao@intel.com>");
MODULE_LICENSE("GPL");
