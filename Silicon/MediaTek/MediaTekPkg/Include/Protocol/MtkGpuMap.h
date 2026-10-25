/** @file
  MT6768 firmware device-mapping metadata. No ownership or GPU readiness claim.
  SPDX-License-Identifier: BSD-2-Clause-Patent
**/
#ifndef MTK_GPU_MAP_H
#define MTK_GPU_MAP_H

#define MTK_GPU_MAP_REVISION 1U
#define MTK_GPU_MAP_WINDOWS 6U
typedef struct {
  UINT64 Base;
  UINT64 Size;
} MTK_GPU_MMIO_WINDOW;
typedef struct {
  UINT32 Revision;
  UINT32 Size;
  // SPM, INFRA, MFGCFG, GPU, TOP, APMIXED. GPU mapping is NOT power-on proof.
  MTK_GPU_MMIO_WINDOW Window[MTK_GPU_MAP_WINDOWS];
} MTK_GPU_MMIO_MAP;

extern EFI_GUID gMediaTekGpuMapProtocolGuid;
#endif
