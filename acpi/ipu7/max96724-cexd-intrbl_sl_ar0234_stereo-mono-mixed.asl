/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2026 Intel Corporation.
 *
 * SSDT overlay: ZEDX-ONE-GS (AR0234) GMSL camera configuration on PTL platform
 *   Two MAX96724 deserializers (DES0, DES1) with CPHY connection to PTL platform,
 *   each carrying four ZEDX-ONE-GS (AR0234) cameras over GMSL Links 0..3, fronted by MAX9295A serializers.
 *
 * DES-level defines (set per DESx, undef'd at the end of each Device):
 *   DES_PHY_TYPE       - DES PHY type (0 for CPHY, 1 for DPHY)
 *   DES_I2C_ADDR       - DES I2C slave address (e.g. 0x0027 for MAX96724)
 *   DES_LANES          - Number of MIPI data lanes (e.g. 2, 4)
 *   DES_INTERNAL_PHY   - DES internal PHY (PHY0 = 4, PHY1 = 5, PHY2 = 6, PHY3 = 7)
 *   DES_TO_MIPI_PORT   - DES connected to IPU0 MIPI port (e.g. 0/1/2/3)
 *   DES_I2C_BUS        - DES I2C bus path (e.g. "\\_SB.PC00.I2C1")
 *   DES_PATH           - DES ACPI path string (e.g. "\\_SB.PC00.DES0")
 *   DES_REF            - DES ACPI namespace reference (e.g. \_SB.PC00.DES0)
 *
 * Channel-level defines (set per CHxx, undef'd by _des_ch_common_ar0234.asl):
 *   DESCH_LINK_NUM     - Channel/link number (0..3) - used for _ADR, reg, SER remote port
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
 * cexd-intrbl (FabB) FAKRA connectors DES0 and DES1 max96724 GMSL A, B, C and D links pinout
      _______               _______
     /_ _ _ _\             /_ _ _ _\
    |         |           |         |
    | (B) (C) |           | (B) (C) |
    |         |           |         |
    | (D) (A) |           | (D) (A) |
    |____v____|           |____v____|

   MIPI-0 / I2C0          MIPI-2 / I2C1

 * cexd-intrbl (FabC) FAKRA connectors DES0 and DES1 max96724 GMSL A, B , C and D links pinout
      _______               _______
     /_ _ _ _\             /_ _ _ _\
    |         |           |         |
    | (C) (B) |           | (D) (B) |
    |         |           |         |
    | (D) (A) |           | (C) (A) |
    |____v____|           |____v____|

   MIPI-0 / I2C0          MIPI-2 / I2C1

*/
DefinitionBlock ("", "SSDT", 2, "", "IMG_IPU", 0x20260907)
{
    External (_SB.PC00, DeviceObj) // Root device

    Include ("_ipu.asl")

    Scope (\_SB.PC00)
    {
        Device (DES0)
        {
            /*
             * For detailed explanation of each define,
             * please refer to the comment block at the top of this file.
             */

            // DES-level defines for DES0.
            #define DES_PHY_TYPE 0
            #define DES_I2C_ADDR 0x0027
            #define DES_LANES 2
            #define DES_INTERNAL_PHY 4
            #define DES_TO_MIPI_PORT 0
            #define DES_I2C_BUS "\\_SB.PC00.I2C0"
            #define DES_PATH "\\_SB.PC00.DES0"
            #define DES_REF \_SB.PC00.DES0
            #include "_des_common_max96724.asl"

            // Channel-level defines for Channel 0 (CH00)
            #define DESCH_CH CH00
            #define DESCH_SER SER0
            #define DESCH_CAM CAM0
            #define DESCH_CH_PATH "\\_SB.PC00.DES0.CH00"
            #define DESCH_SER_PATH "\\_SB.PC00.DES0.CH00.SER0"
            #define DESCH_SER_REF \_SB.PC00.DES0.CH00.SER0
            #define DESCH_LINK_NUM 0
            #define DESCH_SER_I2C 0x42
            #define DESCH_SER_GPIOREF ^^SER0
            #define CAM_ALIAS 0x66
            #define CAM_LANES 2
            #include "_des_ch_common_ar0234.asl"
            #undef DESCH_CH
            #undef DESCH_SER
            #undef DESCH_CAM
            #undef DESCH_CH_PATH
            #undef DESCH_SER_PATH
            #undef DESCH_SER_REF
            #undef DESCH_LINK_NUM
            #undef DESCH_SER_I2C
            #undef DESCH_SER_GPIOREF
            #undef CAM_ALIAS
            #undef CAM_LANES

	    // Channel-level defines for Channel 1 (CH01)
            #define DESCH_LINK_NUM 1
            #define DESCH_CH CH01
            #define DESCH_SER SER1
            #define DESCH_CAM CAM1
            #define DESCH_SER_I2C 0x42
            #define DESCH_CH_PATH "\\_SB.PC00.DES0.CH01"
            #define DESCH_SER_PATH "\\_SB.PC00.DES0.CH01.SER1"
            #define DESCH_SER_REF \_SB.PC00.DES0.CH01.SER1
            #define DESCH_SER_GPIOREF ^^SER1
            #define CAM_ALIAS 0x67
            #define CAM_LANES 2
            #include "_des_ch_common_ar0234.asl"
            #undef DESCH_LINK_NUM
            #undef DESCH_CH
            #undef DESCH_SER
            #undef DESCH_CAM
            #undef DESCH_SER_I2C
            #undef DESCH_CH_PATH
            #undef DESCH_SER_PATH
            #undef DESCH_SER_REF
            #undef DESCH_SER_GPIOREF
            #undef CAM_ALIAS
            #undef CAM_LANES

            // Clean up DES0-level defines
            #undef DES_PHY_TYPE
            #undef DES_I2C_ADDR
            #undef DES_LANES
            #undef DES_INTERNAL_PHY
            #undef DES_TO_MIPI_PORT
            #undef DES_I2C_BUS
            #undef DES_PATH
            #undef DES_REF
        }
        Device (DES1)
        {
            #define EXTERNAL_FRAME_SYNC 1
            #define EXTERNAL_FRAME_FREQ_HZ 30

            // DES-level defines for DES1.
            #define DES_PHY_TYPE 0
            #define DES_I2C_ADDR 0x0027
            #define DES_LANES 2
            #define DES_INTERNAL_PHY 6
            #define DES_TO_MIPI_PORT 2
            #define DES_I2C_BUS "\\_SB.PC00.I2C1"
            #define DES_PATH "\\_SB.PC00.DES1"
            #define DES_REF \_SB.PC00.DES1
            #define DES_PIPE_STR_AUTOSELECT 0
            /*
             * max9x leaves physical links 1 and 2 at aliases 0x45 and 0x46
             * across a warm reboot. Keep those first so maxim-serdes can
             * find either the power-up address or the retained alias.
             */
            #define DES_I2C_ALIAS_POOL Package () { 0x45, 0x46, 0x44, 0x47 }
            #include "_des_common_max96724.asl"

            // Channel 0 is not populated

            // Channel 1 (zedx)
            #define DESCH_LINK_NUM 1
            #define DESCH_CH CH01
            #define DESCH_SER SER1
            #define DESCH_SER_I2C 0x62
            #define DESCH_CH_PATH "\\_SB.PC00.DES1.CH01"
            #define DESCH_SER_PATH "\\_SB.PC00.DES1.CH01.SER1"
            #define DESCH_SER_REF \_SB.PC00.DES1.CH01.SER1
            #define DESCH_SER_GPIOREF ^^^SER1
            #define DESCH_RESET_GPIO_PIN    7
            #define DESCH_RESET_GPIO_PIN2   8
            #define DESCH_SER_FSIN_GPIO 9
            #define DESCH_SER_FSIN_GPIO_2 10
            #define DESCH_SER_FSYNC_RX_ID 9
            #define CAM_ALIAS 0x66, 0x67
            #define CAM_LANES 2
            #define DESCH_SER_X_VC Package () { 1 }
            #define DESCH_SER_Y_VC Package () { 0 }
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
            #undef DESCH_SER_Z_VC
            #undef DESCH_SER_U_VC
#ifdef DESCH_SER_FSIN_GPIO
            #undef DESCH_SER_FSIN_GPIO
#endif
#ifdef DESCH_SER_FSIN_GPIO_2
            #undef DESCH_SER_FSIN_GPIO_2
#endif
#ifdef DESCH_SER_FSYNC_RX_ID
            #undef DESCH_SER_FSYNC_RX_ID
#endif
            #undef CAM_ALIAS
            #undef CAM_LANES

            // Channel 2 is not populated

            // Channel 3 is not populated

            // Clean up DES0-level defines
            #undef DES_PHY_TYPE
            #undef DES_I2C_ADDR
            #undef DES_I2C_BUS
            #undef DESCH_SER_I2C
            #undef DESCH_SER_GPIOREF
            #undef CAM_ALIAS
            #undef CAM_LANES
            #undef DESCH_LINK_NUM
#ifdef DESCH_SER_EXTRA_GPIO_PIN
            #undef DESCH_SER_EXTRA_GPIO_PIN
#endif
#ifdef DESCH_CAM_FSIN_GPIO
            #undef DESCH_CAM_FSIN_GPIO
#endif
            #undef DES_I2C_ALIAS_POOL
        }
   }
}