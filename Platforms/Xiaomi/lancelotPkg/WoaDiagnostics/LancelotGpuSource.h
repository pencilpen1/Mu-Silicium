/* SPDX-License-Identifier: BSD-2-Clause-Patent -- no hardware control. */
#ifndef LANCELOT_GPU_SOURCE_H
#define LANCELOT_GPU_SOURCE_H
VOID LancelotGpuSourceInitialize(EFI_HANDLE ImageHandle,UINT64 BootId);
VOID LancelotGpuSourceReady(VOID);
VOID LancelotGpuSourceExit(VOID);
#endif
