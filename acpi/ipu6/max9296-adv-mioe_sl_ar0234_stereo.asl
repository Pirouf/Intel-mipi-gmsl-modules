/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2026 Intel Corporation.
 *
 * SSDT overlay: D3 AR0234 GMSL camera configuration with MAX9296A deserializer on IPU6EPMTL platform.
 *   One MAX9296A deserializer (DES0, two GMSL links) with DPHY connection to IPU6EPMTL platform,
 *   carrying two AR0234 cameras over GMSL Links 0..1, fronted by MAX9295A serializers.
 *
 *
 * DES-level defines (set per DESx, undef'd at the end of each Device):
 *   DES_PHY_TYPE       - DES PHY type (0 for CPHY, 1 for DPHY)
 *   DES_I2C_ADDR       - DES I2C slave address (e.g. 0x0048 for MAX9296A)
 *   DES_LANES          - Number of MIPI data lanes (e.g. 2, 4)
 *   DES_INTERNAL_PHY   - DES internal PHY index (MAX9296A: 1 or 2)
 *   DES_TO_MIPI_PORT   - DES connected to IPU0 MIPI port (e.g. 0/1/2/3)
 *   DES_I2C_BUS        - DES I2C bus path (e.g. "\\_SB.PC00.I2C1")
 *   DES_PATH           - DES ACPI path string (e.g. "\\_SB.PC00.DES0")
 *   DES_REF            - DES ACPI namespace reference (e.g. \_SB.PC00.DES0)
 *
 * Channel-level defines (set per CHxx, undef'd by _des_ch_common_ar0234.asl):
 *   DESCH_LINK_NUM     - Channel/link number (0..1 for MAX9296A) - used for _ADR, reg, SER remote port
 *   DESCH_CH           - Channel device name (e.g. CH00)
 *   DESCH_SER          - Serializer device name (e.g. SER0)
 *   DESCH_CAM          - Camera device name (e.g. CAM0)
 *   DESCH_SER_I2C      - SER I2C slave address (e.g. 0x40 for MAX9295A)
 *   DESCH_CH_PATH      - CHxx ACPI path string (e.g. "\\_SB.PC00.DES0.CH00")
 *   DESCH_SER_PATH     - SERx ACPI path string (e.g. "\\_SB.PC00.DES0.CH00.SER0")
 *   DESCH_SER_REF      - SERx ACPI namespace reference (e.g. \_SB.PC00.DES0.CH00.SER0)
 *   DESCH_SER_GPIOREF  - SERx GPIO controller reference (e.g. ^^SER0)
 *   CAM_ALIAS          - Camera alias I2C address used in i2c-alias-pool of the SER
 *   CAM_LANES          - Number of MIPI data lanes for the camera (e.g. 2, 4)
 */

/* !! IMPORTANT !!
 * MAX9296 DPHY Advantech® MIOe-GMSL AIC (HFE-R360, ASR-A502) FAKRA connectors
 *
  +----|----+  +----|----+  +----|----+  +----|----+
  |   _^_   |  |   _^_   |  |   _^_   |  |   _^_   |
  |  /   \  |  |  /   \  |  |  /   \  |  |  /   \  |
  | | (A) | |  | | (B) | |  | | (C) | |  | | (D) | |
  |  \___/  |  |  \___/  |  |  \___/  |  |  \___/  |
  +---------+  +---------+  +---------+  +---------+
      MIPI-0 / I2C0		MIPI-4 / I2C1
      (GMSL A & B Links)  	(GMSL C & D Links)
 */

DefinitionBlock ("", "SSDT", 2, "", "IMG_IPU", 0x20261113)
{
    External (_SB.PC00, DeviceObj)

    Include ("_ipu.asl")

    Scope (\_SB.PC00)
    {
        Device (DES0)
        {
            #define EXTERNAL_FRAME_SYNC 1
            #define EXTERNAL_FRAME_FREQ_HZ 30
            #define DESCH_SER_FSYNC_RX_ID 9

	    //DES-level defines for DES0
            #define DES_PHY_TYPE 1
            #define DES_I2C_ADDR 0x0048
            #define DES_LANES 4
            #define DES_INTERNAL_PHY 2
            #define DES_TO_MIPI_PORT 0
            #define DES_I2C_BUS "\\_SB.PC00.I2C1"
            #define DES_PATH "\\_SB.PC00.DES0"
            #define DES_REF \_SB.PC00.DES0
            #define LINK_FREQ 700000000
            #include "_des_common_max9296.asl"

            // First populated serializer, connected to physical GMSL link 1
            #define DESCH_LINK_NUM 1
            #define DESCH_CH CH01
            #define DESCH_SER SER1
            #define DESCH_SER_I2C 0x62
            #define DESCH_CH_PATH "\\_SB.PC00.DES0.CH01"
            #define DESCH_SER_PATH "\\_SB.PC00.DES0.CH01.SER1"
            #define DESCH_SER_REF \_SB.PC00.DES0.CH01.SER1
            #define DESCH_SER_GPIOREF ^^^SER1
            #define DESCH_RESET_GPIO_PIN    7
            #define DESCH_RESET_GPIO_PIN2   8
            #define DESCH_SER_FSIN_GPIO 9
            #define DESCH_SER_FSIN_GPIO_2 10
            #define DESCH_SER_X_VC Package () { 1 }
            #define DESCH_SER_Y_VC Package () { 0 }
            #define CAM_ALIAS 0x66, 0x67
            #define CAM_LANES 2
            #include "_des_ch_common_zedx.asl"
            #undef DESCH_LINK_NUM
            #undef DESCH_CH
            #undef DESCH_SER
            #undef DESCH_SER_I2C
            #undef DESCH_CH_PATH
            #undef DESCH_SER_PATH
            #undef DESCH_SER_REF
            #undef DESCH_SER_GPIOREF
            #undef DESCH_RESET_GPIO_PIN
            #undef DESCH_RESET_GPIO_PIN2
            #undef DESCH_SER_X_VC
            #undef DESCH_SER_Y_VC
#ifdef DESCH_SER_FSIN_GPIO
            #undef DESCH_SER_FSIN_GPIO
#endif
#ifdef DESCH_SER_FSIN_GPIO_2
            #undef DESCH_SER_FSIN_GPIO_2
#endif
            #undef CAM_ALIAS
            #undef CAM_LANES

            // Second serializer, connected to physical GMSL link 2
            #define DESCH_LINK_NUM 2
            #define DESCH_CH CH02
            #define DESCH_SER SER2
            #define DESCH_SER_I2C 0x62
            #define DESCH_CH_PATH "\\_SB.PC00.DES0.CH02"
            #define DESCH_SER_PATH "\\_SB.PC00.DES0.CH02.SER2"
            #define DESCH_SER_REF \_SB.PC00.DES0.CH02.SER2
            #define DESCH_SER_GPIOREF ^^^SER2
            #define DESCH_RESET_GPIO_PIN    7
            #define DESCH_RESET_GPIO_PIN2   8
            #define DESCH_SER_FSIN_GPIO 9
            #define DESCH_SER_FSIN_GPIO_2 10
            #define DESCH_SER_X_VC Package () { 1 }
            #define DESCH_SER_Y_VC Package () { 0 }
            /* ATR links share the upstream bus; aliases must not overlap. */
            #define CAM_ALIAS 0x68, 0x6a
            #define CAM_LANES 2
            #include "_des_ch_common_zedx.asl"
            #undef DESCH_LINK_NUM
            #undef DESCH_CH
            #undef DESCH_SER
            #undef DESCH_SER_I2C
            #undef DESCH_CH_PATH
            #undef DESCH_SER_PATH
            #undef DESCH_SER_REF
            #undef DESCH_SER_GPIOREF
            #undef DESCH_RESET_GPIO_PIN
            #undef DESCH_RESET_GPIO_PIN2
            #undef DESCH_SER_X_VC
            #undef DESCH_SER_Y_VC
#ifdef DESCH_SER_FSIN_GPIO
            #undef DESCH_SER_FSIN_GPIO
#endif
#ifdef DESCH_SER_FSIN_GPIO_2
            #undef DESCH_SER_FSIN_GPIO_2
#endif
            #undef CAM_ALIAS
            #undef CAM_LANES

            // Channels 0 and 3 are not populated
            // Clean up DES-level defines
            #undef DES_PHY_TYPE
            #undef DES_I2C_ADDR
            #undef DES_LANES
            #undef DES_INTERNAL_PHY
            #undef DES_TO_MIPI_PORT
            #undef DES_I2C_BUS
            #undef DES_PATH
            #undef DES_REF
            #undef LINK_FREQ
        }
    }
}