/*
 * SPDX-License-Identifier: GPL-2.0
 * Copyright (c) 2026 Intel Corporation.
 *
 * SSDT overlay: ZEDX (AR0234) GMSL camera configuration on PTL platform.
 *   Advantech MIO-5381 (PTL): two MAX96724 deserializers, each using
 *   a 2-trio C-PHY output to IPU7:
 *     DES0: I2C1, 7-bit 0x27 (8-bit 0x4E/0x4F), PHY2 -> IPU MIPI port 0
 *     DES1: I2C2, 7-bit 0x27 (8-bit 0x4E/0x4F), PHY0 -> IPU MIPI port 2
 *   Each deserializer carries ZED X (MAX9295 + 2x AR0234) cameras on
 *   GMSL links B (1) and C (2). Each serializer carries two AR0234 streams.
 *
 * I2C address notes (maxim-serdes, see max_des.c):
 *   - The DES i2c-atr uses I2C_ATR_F_PASSTHROUGH, so DES_I2C_ALIAS_POOL only
 *     remaps serializer addresses (both ZED X serializers are 0x62). The
 *     camera aliases (CAM_ALIAS) are translated by each serializer ATR and
 *     then pass through the DES unchanged onto the parent I2C bus.
 *   - Therefore, per I2C bus, DES_I2C_ALIAS_POOL, CAM_ALIAS of every link
 *     and the DES address (0x27) must all be unique and must not clash
 *     with any other device on that bus. DES0 and DES1 sit on different
 *     I2C controllers, so they may reuse the same alias sets.
 *
 * DES-level defines (set per DESx, undef'd at the end of each Device):
 *   DES_PHY_TYPE             - DES PHY type (0 for CPHY, 1 for DPHY)
 *   DES_I2C_ADDR             - DES I2C slave address (e.g. 0x0027 for MAX96724)
 *   DES_INTERNAL_PHY         - DES internal PHY as CSI2Bus local port (PHY0 = 4, PHY1 = 5, PHY2 = 6, PHY3 = 7)
 *   DES_TO_MIPI_PORT         - DES connected to IPU0 MIPI port (e.g. 0/1/2/3)
 *   DES_I2C_BUS              - DES I2C bus path (e.g. "\\_SB.PC00.I2C0")
 *   DES_PATH                 - DES ACPI path string (e.g. "\\_SB.PC00.DES0")
 *   DES_REF                  - DES ACPI namespace reference (e.g. \_SB.PC00.DES0)
 *   DES_PIPE_STR_AUTOSELECT  - DES pipe stream autoselect (0/1)
 *   DES_I2C_ALIAS_POOL       - Serializer aliases, ordered by populated links
 *
 * Channel-level defines (set per CHxx, undef'd by _des_ch_common_d457.asl):
 *   DESCH_LINK_NUM           - Channel/link number (0..3) - used for _ADR, reg, SER remote port
 *   DESCH_CH                 - Channel device name (e.g. CH00)
 *   DESCH_SER                - Serializer device name (e.g. SER0)
 *   DESCH_CAM                - Camera device name (e.g. CAM0)
 *   DESCH_SER_I2C            - SER I2C slave address (e.g. 0x64 for MAX9295D)
 *   DESCH_CH_PATH            - CHxx ACPI path string (e.g. "\\_SB.PC00.DES0.CH00")
 *   DESCH_SER_PATH           - SERx ACPI path string (e.g. "\\_SB.PC00.DES0.CH00.SER0")
 *   DESCH_SER_REF            - SERx ACPI namespace reference (e.g. \_SB.PC00.DES0.CH00.SER0)
 *   DESCH_SER_GPIOREF        - SERx GPIO controller reference (e.g. ^^SER0)
 *   CAM_ALIAS                - Camera alias I2C address used in i2c-alias-pool of the SER
 *   CAM_LANES                - Number of MIPI data lanes for the camera (e.g. 2, 4)
 *   DESCH_SER_X_VC           - Serializer X-pipe virtual channel mapping
 *   DESCH_SER_Y_VC           - Serializer Y-pipe virtual channel mapping
 *   DESCH_SER_Z_VC           - Serializer Z-pipe virtual channel mapping
 *   DESCH_SER_U_VC           - Serializer U-pipe virtual channel mapping
 *   DESCH_SER_EXTRA_GPIO_PIN - Optional: extra SER GPIO pin number
 *   DESCH_CAM_FSIN_GPIO      - Optional: camera FSIN GPIO index on the SER
 */

/* !! IMPORTANT !!
 * MAX96724 CPHY Advantech® Mio-5381 GMSL AIC (RevA) eight FAKRA connectors
 *

  +----|----+  +----|----+  +----|----+  +----|----+      +----|----+  +----|----+  +----|----+  +----|----+
  |   _^_   |  |   _^_   |  |   _^_   |  |   _^_   |      |   _^_   |  |   _^_   |  |   _^_   |  |   _^_   |
  |  /   \  |  |  /   \  |  |  /   \  |  |  /   \  |      |  /   \  |  |  /   \  |  |  /   \  |  |  /   \  |
  | | (A) | |  | | (B) | |  | | (C) | |  | | (D) | |      | | (A) | |  | | (B) | |  | | (C) | |  | | (D) | |
  |  \___/  |  |  \___/  |  |  \___/  |  |  \___/  |      |  \___/  |  |  \___/  |  |  \___/  |  |  \___/  |
  +---------+  +---------+  +---------+  +---------+      +---------+  +---------+  +---------+  +---------+
 		MIPI-0 / I2C1					         MIPI-2 / I2C2
 
 */

DefinitionBlock ("", "SSDT", 2, "", "IMG_IPU", 0x20260922)
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

            // DES0: I2C1, 2-trio C-PHY on MAX96724 PHY2 -> IPU MIPI port 0
            #define DES_PHY_TYPE 0
            #define DES_I2C_ADDR 0x004F
            #define DES_LANES 2
            #define DES_INTERNAL_PHY 4
            #define DES_TO_MIPI_PORT 0
            #define DES_I2C_BUS "\\_SB.PC00.I2C1"
            #define DES_PATH "\\_SB.PC00.DES0"
            #define DES_REF \_SB.PC00.DES0
            #define DES_PIPE_STR_AUTOSELECT 0
            /*
             * Serializer aliases on I2C1, assigned to populated links in
             * order (link 1 -> 0x45, link 2 -> 0x46). Must not overlap the
             * DES address (0x27), CAM_ALIAS below, or other I2C1 devices.
             */
            #define DES_I2C_ALIAS_POOL Package () { 0x45, 0x46, 0x44, 0x47 }
            #include "_des_common_max96724.asl"

            // First populated serializer, physical GMSL link B (1)

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

            // Second populated serializer, physical GMSL link C (2)

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

            // Links A (0) and D (3) are not populated

            #undef DES_PHY_TYPE
            #undef DES_I2C_ADDR
            #undef DES_LANES
            #undef DES_INTERNAL_PHY
            #undef DES_TO_MIPI_PORT
            #undef DES_I2C_BUS
            #undef DES_PATH
            #undef DES_REF
            #undef DES_PIPE_STR_AUTOSELECT
            #undef DES_I2C_ALIAS_POOL
#ifdef EXTERNAL_FRAME_SYNC
            #undef EXTERNAL_FRAME_SYNC
#endif
#ifdef EXTERNAL_FRAME_FREQ_HZ
            #undef EXTERNAL_FRAME_FREQ_HZ
#endif
#ifdef DESCH_SER_FSYNC_RX_ID
            #undef DESCH_SER_FSYNC_RX_ID
#endif
        }

        Device (DES1)
        {
            #define EXTERNAL_FRAME_SYNC 1
            #define EXTERNAL_FRAME_FREQ_HZ 30
            #define DESCH_SER_FSYNC_RX_ID 9

            // DES1: I2C2, 2-trio C-PHY on MAX96724 PHY0 -> IPU MIPI port 2
            #define DES_PHY_TYPE 0
            #define DES_I2C_ADDR 0x004F
            #define DES_LANES 2
            #define DES_INTERNAL_PHY 4
            #define DES_TO_MIPI_PORT 2
            #define DES_I2C_BUS "\\_SB.PC00.I2C2"
            #define DES_PATH "\\_SB.PC00.DES1"
            #define DES_REF \_SB.PC00.DES1
            #define DES_PIPE_STR_AUTOSELECT 0
            /*
             * Serializer aliases on I2C2, assigned to populated links in
             * order (link 1 -> 0x45, link 2 -> 0x46). Must not overlap the
             * DES address (0x27), CAM_ALIAS below, or other I2C2 devices.
             */
            #define DES_I2C_ALIAS_POOL Package () { 0x45, 0x46, 0x44, 0x47 }
            #include "_des_common_max96724.asl"

            // First populated serializer, physical GMSL link B (1)

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

            // Second populated serializer, physical GMSL link C (2)

            #define DESCH_LINK_NUM 2
            #define DESCH_CH CH02
            #define DESCH_SER SER2
            #define DESCH_SER_I2C 0x62
            #define DESCH_CH_PATH "\\_SB.PC00.DES1.CH02"
            #define DESCH_SER_PATH "\\_SB.PC00.DES1.CH02.SER2"
            #define DESCH_SER_REF \_SB.PC00.DES1.CH02.SER2
            #define DESCH_SER_GPIOREF ^^^SER2
            #define DESCH_RESET_GPIO_PIN    7
            #define DESCH_RESET_GPIO_PIN2   8
            #define DESCH_SER_FSIN_GPIO 9
            #define DESCH_SER_FSIN_GPIO_2 10
            #define DESCH_SER_X_VC Package () { 1 }
            #define DESCH_SER_Y_VC Package () { 0 }
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

            // Links A (0) and D (3) are not populated

            #undef DES_PHY_TYPE
            #undef DES_I2C_ADDR
            #undef DES_LANES
            #undef DES_INTERNAL_PHY
            #undef DES_TO_MIPI_PORT
            #undef DES_I2C_BUS
            #undef DES_PATH
            #undef DES_REF
            #undef DES_PIPE_STR_AUTOSELECT
            #undef DES_I2C_ALIAS_POOL
#ifdef EXTERNAL_FRAME_SYNC
            #undef EXTERNAL_FRAME_SYNC
#endif
#ifdef EXTERNAL_FRAME_FREQ_HZ
            #undef EXTERNAL_FRAME_FREQ_HZ
#endif
#ifdef DESCH_SER_FSYNC_RX_ID
            #undef DESCH_SER_FSYNC_RX_ID
#endif
        }
    }
}
