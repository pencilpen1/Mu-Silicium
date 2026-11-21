/* SPDX-License-Identifier: BSD-2-Clause-Patent */
#ifndef MTK_PMIC_WRAPPER_HANDOFF_H
#define MTK_PMIC_WRAPPER_HANDOFF_H

typedef struct {
  // Irreversibly close this boot-services WACS2 protocol under its transaction
  // lock. Success certifies this DXE producer only, not other privilege domains.
  EFI_STATUS (EFIAPI *CloseNormalAp)(VOID);
} MTK_PMIC_WRAPPER_HANDOFF_PROTOCOL;

extern EFI_GUID gMediaTekPmicWrapperHandoffProtocolGuid;
#endif
