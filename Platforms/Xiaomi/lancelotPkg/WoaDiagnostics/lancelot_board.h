/* SPDX-License-Identifier: BSD-2-Clause-Patent
 * lancelot/MT6768 board handoff. Only GPIO93 fields and SPI0's own GIC
 * enable bit are written. PMIC, PLL, shared mux and EINT are read-only.
 * GPIO93 masks come from the actual MT6768 pinctrl header, not MT6765.
 */
#ifndef LANCELOT_BOARD_H
#define LANCELOT_BOARD_H
typedef UINT32 (*LANCELOT_BOARD_READ)(UINTN Address);
typedef UINT32 (*LANCELOT_BOARD_WRITE)(UINTN Address, UINT32 Value);

#define LBOARD_GPIO_MODE93  0x100053B0U
#define LBOARD_GPIO_DIR93   0x10005020U
#define LBOARD_GPIO_IES93   0x10002860U
#define LBOARD_GPIO_SMT93   0x10002900U
#define LBOARD_GPIO_PUPD93  0x10002890U
#define LBOARD_GPIO_R093    0x100028B0U
#define LBOARD_GPIO_R193    0x100028C0U
#define LBOARD_SPI_GATE     0x10001094U
#define LBOARD_SPI_CLOCK    0x10000060U
#define LBOARD_SPI_COMMAND  0x1100A018U
#define LBOARD_SPI_ENABLE   0x0C000114U /* GICD_ISENABLER5: INTID170 = bit10 */
#define LBOARD_SPI_DISABLE  0x0C000194U /* GICD_ICENABLER5 */

STATIC inline BOOLEAN
LancelotKeysReady (LANCELOT_BOARD_READ Read)
{
  return ((Read (LBOARD_GPIO_MODE93) >> 20) & 15U) == 1U &&
         (Read (LBOARD_GPIO_DIR93) & (1U << 29)) == 0 &&
         (Read (LBOARD_GPIO_IES93) & (1U << 18)) != 0 &&
         (Read (LBOARD_GPIO_SMT93) & (1U << 9)) != 0 &&
         (Read (LBOARD_GPIO_PUPD93) & 1U) == 0 &&
         (Read (LBOARD_GPIO_R093) & 1U) == 0 &&
         (Read (LBOARD_GPIO_R193) & 1U) != 0;
}

STATIC inline BOOLEAN
LancelotPrepareKeys (LANCELOT_BOARD_READ Read, LANCELOT_BOARD_WRITE Write)
{
  /* MODE +0xc has a write-enable bit per nibble. Other fields use SET/CLR. */
  Write (LBOARD_GPIO_MODE93 + 12U, 9U << 20);
  Write (LBOARD_GPIO_DIR93 + 8U, 1U << 29);
  Write (LBOARD_GPIO_IES93 + 4U, 1U << 18);
  /* GPIO93/94 share this SMT field; both require input hysteresis on this board. */
  Write (LBOARD_GPIO_SMT93 + 4U, 1U << 9);
  Write (LBOARD_GPIO_PUPD93 + 8U, 1U);
  Write (LBOARD_GPIO_R093 + 8U, 1U);
  Write (LBOARD_GPIO_R193 + 4U, 1U);
  return LancelotKeysReady (Read);
}

STATIC inline BOOLEAN
LancelotBatteryReady (LANCELOT_BOARD_READ Read)
{
  UINT32 State = Read (0x1000DC24U);
  return (Read (0x1000D090U) & 1U) != 0 && (Read (0x1000D094U) & 1U) != 0 &&
         (State & 0x003F0000U) == 0x00300000U;
}

STATIC inline BOOLEAN
LancelotTouchReady (LANCELOT_BOARD_READ Read)
{
  UINT32 Clock = Read (LBOARD_SPI_CLOCK);
  if ((Clock & 0x80000000U) != 0) return FALSE;
  if (((Clock >> 24) & 3U) != 0) {
    UINT32 Pcw = Read (0x1000C25CU);
    if ((Read (0x1000C258U) & 1U) == 0 ||
        (Read (0x1000C264U) & 3U) != 1U ||
        (Pcw & 0x3FFFFFU) == 0 || Read (0x1000C25CU) != Pcw) return FALSE;
  }
  /* Never access a clock-gated SPI register window. No inherited DMA/ACT. */
  if ((Read (LBOARD_SPI_GATE) & 2U) == 0 &&
      (Read (LBOARD_SPI_COMMAND) & 0x00000C03U) != 0) return FALSE;
  return (Read (LBOARD_SPI_CLOCK) & 0x83000000U) == (Clock & 0x83000000U);
}

STATIC inline BOOLEAN
LancelotPrepareTouch (LANCELOT_BOARD_READ Read, LANCELOT_BOARD_WRITE Write)
{
  if (!LancelotTouchReady (Read)) return FALSE;
  /* Only this unused peripheral interrupt is masked; the shared EINT stays intact. */
  Write (LBOARD_SPI_DISABLE, 1U << 10);
  return (Read (LBOARD_SPI_ENABLE) & (1U << 10)) == 0;
}
#endif
