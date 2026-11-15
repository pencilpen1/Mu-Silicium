/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef LANCELOT_CHARGER_HANDOFF_H
#define LANCELOT_CHARGER_HANDOFF_H
#define LANCELOT_CHARGER_NVS 0x512F0100U
VOID LancelotChargerInitialize (VOID);
VOID LancelotChargerPrepare (VOID);
VOID LancelotChargerExit (VOID);
#endif
