/** @file
 *
 *  MemoryPeim for MT6768 (Redmi 9 / lancelot).
 *
 *  Builds the UEFI memory map DYNAMICALLY from the FDT that LK passes in x0.
 *  LK patches /memory (real DDR size) and /reserved-memory (real carveout
 *  addresses) at boot, then hands the FDT pointer to the "kernel" (= our
 *  BootShim) in x0 per the ARM64 boot protocol. BootShim forwards x0 to
 *  CEntryPoint(), which stores it in gFdtBase.
 *
 *  Rejects invalid/unsupported FDT layouts instead of guessing usable RAM.
 *
 *  Copyright (c) 2011-2015, ARM Limited. All rights reserved.
 *  SPDX-License-Identifier: BSD-2-Clause-Patent
 *
**/

#include <PiPei.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/ArmMmuLib.h>
#include <Library/ArmPlatformLib.h>
#include <Library/DebugLib.h>
#include <Library/HobLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/PcdLib.h>

// This varies by device (fallback map)
#include <Configuration/DeviceMemoryMap.h>
#include <Configuration/BootLogConfig.h>

// Set by CEntryPoint() from x0 (FDT pointer handed over by LK).
extern VOID *gFdtBase;

VOID BuildMemoryTypeInformationHob(VOID);

//
// UEFI own regions. MUST agree with the PCDs in Devices/lancelot.dsc and
// with the FD BaseAddress in MT6768Pkg.fdf. These ranges were checked against
// the captured runtime FDT; live reservations are checked again below.
//
#define UEFI_FD_BASE      0x4d100000ULL
#define UEFI_FD_SIZE      0x00200000ULL
#define UEFI_STACK_BASE   0x51300000ULL
#define UEFI_STACK_SIZE   0x00040000ULL
#define UEFI_POOL_BASE    0x4d300000ULL
#define UEFI_POOL_SIZE    0x03fc0000ULL
#define UEFI_VECTORS_BASE 0x512f0000ULL
#define UEFI_VECTORS_SIZE 0x00010000ULL
#define FB_BASE           0x7cdb0000ULL   // runtime mblock-14-framebuffer
#define FB_SIZE           0x02250000ULL   // runtime framebuffer reservation

//
// Minimal big-endian FDT reader (no libfdt dependency).
//
#define FDT_MAGIC       0xD00DFEEDU
#define FDT_BEGIN_NODE  1U
#define FDT_END_NODE    2U
#define FDT_PROP        3U
#define FDT_NOP         4U
#define FDT_END         9U

typedef struct {
  UINT32 Magic;
  UINT32 TotalSize;
  UINT32 OffDtStruct;
  UINT32 OffDtStrings;
  UINT32 OffMemRsvmap;
  UINT32 Version;
  UINT32 LastCompVersion;
  UINT32 BootCpuidPhys;
  UINT32 SizeDtStrings;
  UINT32 SizeDtStruct;
} FDT_HEADER;

STATIC
UINT32
Fdt32 (
  IN CONST VOID  *p
  )
{
  CONST UINT8 *b = (CONST UINT8 *)p;
  return ((UINT32)b[0] << 24) | ((UINT32)b[1] << 16) |
         ((UINT32)b[2] << 8) | (UINT32)b[3];
}

STATIC
UINT64
Fdt64 (
  IN CONST VOID  *p
  )
{
  return ((UINT64)Fdt32 (p) << 32) | Fdt32 ((CONST UINT8 *)p + 4);
}

#define MAX_RSV  48

typedef struct {
  UINT64  Addr;
  UINT64  Size;
} RANGE;

/**
  Parse the FDT for /memory reg (DDR) and all reserved ranges:
  - the top-level reservation map (ATF / TEE / lca_reserved_mem)
  - /reserved-memory/<child> reg (LK patches real addresses in at boot)

  Returns TRUE if /memory was found (DdrBase/DdrSize set).
**/
STATIC
BOOLEAN
FdtParse (
  IN  CONST VOID  *Fdt,
  OUT UINT64      *DdrBase,
  OUT UINT64      *DdrSize,
  OUT RANGE       *Rsv,
  OUT UINTN       *RsvCount,
  OUT UINT64      *FbBase,
  OUT UINT64      *FbSize
  )
{
  CONST FDT_HEADER  *H;
  CONST UINT8       *Blob;
  CONST UINT8       *Str;
  CONST UINT8       *p;
  UINT32            OffStruct;
  UINT32            OffStrings;
  UINT32            OffRsv;
  UINT32            TotalSize;
  UINT32            StructSize;
  UINT32            StringsSize;
  UINT32            RsvEnd;
  UINT32            StructEnd;
  UINT32            StringsEnd;
  UINTN             rc;
  CHAR8             NameStack[16][64];
  UINTN             Depth;
  BOOLEAN           MemFound;
  BOOLEAN           RsvTerminated;

  if (Fdt == NULL) {
    return FALSE;
  }

  H = (CONST FDT_HEADER *)Fdt;
  if (Fdt32 (&H->Magic) != FDT_MAGIC) {
    return FALSE;
  }

  TotalSize   = Fdt32 (&H->TotalSize);
  OffStruct   = Fdt32 (&H->OffDtStruct);
  OffStrings  = Fdt32 (&H->OffDtStrings);
  OffRsv      = Fdt32 (&H->OffMemRsvmap);
  StructSize  = Fdt32 (&H->SizeDtStruct);
  StringsSize = Fdt32 (&H->SizeDtStrings);

  // Bound offsets/strings to the declared blob size. The caller still relies
  // on LK handing over a readable DTB pointer and the complete declared blob.
  if ((TotalSize < sizeof (FDT_HEADER)) ||
      (TotalSize > 16U * 1024U * 1024U) ||
      (Fdt32 (&H->Version) < 17) ||
      (Fdt32 (&H->LastCompVersion) > 17) ||
      (OffRsv < sizeof (FDT_HEADER)) || (OffRsv & 7) ||
      (OffStruct < sizeof (FDT_HEADER)) || (OffStruct & 3) ||
      (OffStrings < sizeof (FDT_HEADER)) ||
      (OffRsv > TotalSize) ||
      (OffStruct > TotalSize) ||
      (OffStrings > TotalSize) ||
      (StructSize > TotalSize - OffStruct) ||
      (StringsSize > TotalSize - OffStrings))
  {
    return FALSE;
  }

  RsvEnd     = OffStruct;
  StructEnd  = OffStruct + StructSize;
  StringsEnd = OffStrings + StringsSize;

  *DdrBase = 0;
  *DdrSize = 0;
  *FbBase = FB_BASE;
  *FbSize = FB_SIZE;

  Blob       = (CONST UINT8 *)Fdt;
  Str        = Blob + OffStrings;

  // 1) top-level reservation map
  rc = 0;
  RsvTerminated = FALSE;
  for (UINT32 o = OffRsv; (o <= RsvEnd) && (RsvEnd - o >= 16); o += 16) {
    UINT64  a = Fdt64 (Blob + o);
    UINT64  s = Fdt64 (Blob + o + 8);
    if ((a == 0) && (s == 0)) {
      RsvTerminated = TRUE;
      break;
    }
    if ((s != 0) && (a <= MAX_UINT64 - s) && (rc < MAX_RSV)) {
      Rsv[rc].Addr = a;
      Rsv[rc].Size = s;
      rc++;
    } else if (s != 0) {
      return FALSE;
    }
  }
  if (!RsvTerminated) {
    return FALSE;
  }

  // 2) walk the struct block
  p         = Blob + OffStruct;
  Depth      = 0;
  MemFound   = FALSE;
  ZeroMem (NameStack, sizeof (NameStack));

  while (p < Blob + StructEnd) {
    if ((UINTN)(Blob + StructEnd - p) < sizeof (UINT32)) {
      return FALSE;
    }
    UINT32  tok = Fdt32 (p);
    p += 4;

    if (tok == FDT_BEGIN_NODE) {
      CONST CHAR8  *nm   = (CONST CHAR8 *)p;
      UINTN        len   = 0;
      if (Depth >= 16) {
        return FALSE;
      }
      while ((p + len < Blob + StructEnd) && (nm[len] != '\0')) {
        len++;
      }
      if ((p + len >= Blob + StructEnd) || (len >= 64)) {
        return FALSE;
      }
      p += ((len + 1) + 3) & ~3u;
      if (p > Blob + StructEnd) {
        return FALSE;
      }
      if (Depth < 16) {
        CopyMem (NameStack[Depth], nm, len + 1);
      }
      Depth++;
    } else if (tok == FDT_END_NODE) {
      if (Depth == 0) {
        return FALSE;
      }
      Depth--;
    } else if (tok == FDT_PROP) {
      if ((UINTN)(Blob + StructEnd - p) < 8) {
        return FALSE;
      }
      UINT32        plen = Fdt32 (p); p += 4;
      UINT32        noff = Fdt32 (p); p += 4;
      UINT32        padded;
      BOOLEAN       NameTerminated;
      CONST UINT8   *data = p;
      if ((Depth == 0) || (plen > 0xfffffffcU) ||
          ((padded = (plen + 3) & ~3u) > StructEnd - (UINT32)(p - Blob)) ||
          (noff >= StringsSize))
      {
        return FALSE;
      }
      p += padded;
      CONST CHAR8   *pname  = (CONST CHAR8 *)(Str + noff);
      if ((CONST UINT8 *)pname >= Blob + StringsEnd) {
        return FALSE;
      }
      NameTerminated = FALSE;
      for (UINT32 StringOffset = noff; StringOffset < StringsSize; StringOffset++) {
        if (Str[StringOffset] == '\0') {
          NameTerminated = TRUE;
          break;
        }
      }
      if (!NameTerminated) {
        return FALSE;
      }
      CONST CHAR8   *cur    = (Depth > 0) ? NameStack[Depth - 1] : "";
      CONST CHAR8   *parent = (Depth > 1) ? NameStack[Depth - 2] : "";

      // This platform's root and reserved-memory use two 32-bit cells each.
      // Refuse other encodings rather than interpreting them as 64-bit tuples.
      if (((Depth == 1) || ((Depth == 2) &&
           (AsciiStrCmp (cur, "reserved-memory") == 0))) &&
          ((AsciiStrCmp (pname, "#address-cells") == 0) ||
           (AsciiStrCmp (pname, "#size-cells") == 0))) {
        if ((plen != 4) || (Fdt32 (data) != 2)) {
          return FALSE;
        }
      }

      // /memory (root child): Depth==2 inside it, cur begins with "memory"
      if ((Depth == 2) &&
          ((AsciiStrCmp (cur, "memory") == 0) ||
           (AsciiStrnCmp (cur, "memory@", 7) == 0)) &&
          (AsciiStrCmp (pname, "reg") == 0))
      {
        // The current board has one bank. Do not silently drop extra banks.
        if (MemFound || (plen != 16)) {
          return FALSE;
        }
        *DdrBase   = Fdt64 (data);
        *DdrSize   = Fdt64 (data + 8);
        MemFound   = TRUE;
      }
      // child of /reserved-memory: Depth==3, parent == "reserved-memory"
      else if ((Depth == 3) &&
               (AsciiStrCmp (parent, "reserved-memory") == 0) &&
                (AsciiStrCmp (pname, "reg") == 0))
      {
        if ((plen == 0) || ((plen % 16) != 0)) {
          return FALSE;
        }
        if ((AsciiStrCmp (cur, "mblock-14-framebuffer") == 0) ||
            (AsciiStrCmp (cur, "framebuffer") == 0))
        {
          *FbBase = Fdt64 (data);
          *FbSize = Fdt64 (data + 8);
        }
        for (UINTN Tuple = 0; Tuple < plen; Tuple += 16) {
          UINT64 A = Fdt64 (data + Tuple);
          UINT64 S = Fdt64 (data + Tuple + 8);
          if (S == 0) {
            continue;
          }
          if ((rc == MAX_RSV) || (A > MAX_UINT64 - S)) {
            return FALSE;
          }
          Rsv[rc].Addr = A;
          Rsv[rc].Size = S;
          rc++;
        }
      }
    } else if (tok == FDT_NOP) {
      continue;
    } else if (tok == FDT_END) {
      *RsvCount = rc;
      return MemFound && (Depth == 0);
    } else {
      // Unknown token: do not continue with a desynchronized parser.
      break;
    }
  }

  *RsvCount = rc;
  return FALSE;
}

//
// A carveout to place inside the DDR walk: UEFI regions, framebuffer, and
// reserved ranges, each tagged with its resource/memory/cache attributes.
//
typedef struct {
  UINT64                       Addr;
  UINT64                       Size;
  CONST CHAR8                  *Name;
  EFI_RESOURCE_TYPE            RType;
  EFI_RESOURCE_ATTRIBUTE_TYPE  RAttr;
  EFI_MEMORY_TYPE              MemType;
  ARM_MEMORY_REGION_ATTRIBUTES ArmAttr;
} CARVEOUT;

STATIC
VOID
SortCarveouts (
  IN OUT CARVEOUT  *C,
  IN     UINTN     Count
  )
{
  // simple insertion sort by Addr
  for (UINTN i = 1; i < Count; i++) {
    CARVEOUT  v = C[i];
    UINTN     j = i;
    while ((j > 0) && (C[j - 1].Addr > v.Addr)) {
      C[j] = C[j - 1];
      j--;
    }
    C[j] = v;
  }
}

STATIC
UINTN
AddReg (
  IN OUT ARM_MEMORY_REGION_DESCRIPTOR_EX  *Map,
  IN OUT UINTN                            *N,
  IN     CONST CHAR8                      *Name,
  IN     UINT64                           Addr,
  IN     UINT64                           Size,
  IN     DeviceMemoryAddHob               Hob,
  IN     EFI_RESOURCE_TYPE                RType,
  IN     EFI_RESOURCE_ATTRIBUTE_TYPE      RAttr,
  IN     EFI_MEMORY_TYPE                  MemType,
  IN     ARM_MEMORY_REGION_ATTRIBUTES     ArmAttr
  )
{
  ARM_MEMORY_REGION_DESCRIPTOR_EX  *e;
  UINTN                            len;

  if (*N >= MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT - 1) {
    return *N;
  }

  e = &Map[*N];
  ZeroMem (e, sizeof (*e));
  len = AsciiStrLen (Name);
  if (len >= MEMORY_REGION_NAME_MAX_LENGTH) {
    len = MEMORY_REGION_NAME_MAX_LENGTH - 1;
  }
  CopyMem (e->Name, Name, len);
  e->Address        = Addr;
  e->Length         = Size;
  e->HobOption      = Hob;
  e->ResourceType   = RType;
  e->ResourceAttribute = RAttr;
  e->MemoryType     = MemType;
  e->ArmAttributes  = ArmAttr;
  (*N)++;
  return *N;
}

STATIC
EFI_STATUS
InitMmu (
  IN ARM_MEMORY_REGION_DESCRIPTOR  *MemoryTable
  )
{
  VOID            *TranslationTableBase;
  UINTN           TranslationTableSize;
  RETURN_STATUS   Status;

  Status = ArmConfigureMmu (MemoryTable, &TranslationTableBase, &TranslationTableSize);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Error: Failed to enable MMU: %r\n", Status));
  }
  return Status;
}

STATIC
VOID
AddHob (
  IN PARM_MEMORY_REGION_DESCRIPTOR_EX  Desc
  )
{
  BuildResourceDescriptorHob (Desc->ResourceType, Desc->ResourceAttribute, Desc->Address, Desc->Length);
  // Resource HOBs describe available RAM; allocation HOBs describe USED RAM.
  // In particular, never allocate the entire PrePi pool or conventional RAM.
  // BuildStackHob in PrePi.c owns the stack allocation HOB.
  if ((Desc->ResourceType == EFI_RESOURCE_SYSTEM_MEMORY) &&
      (Desc->MemoryType != EfiConventionalMemory) &&
      (Desc->Address != UEFI_STACK_BASE))
  {
    BuildMemoryAllocationHob (Desc->Address, Desc->Length, Desc->MemoryType);
  }
}

/**
  Run an EX descriptor table through the MMU + HOB machinery (same loop as the
  original mt6765 MemoryPeim).
**/
STATIC
EFI_STATUS
ApplyMap (
  IN ARM_MEMORY_REGION_DESCRIPTOR_EX  *Map
  )
{
  PARM_MEMORY_REGION_DESCRIPTOR_EX  Ex;
  ARM_MEMORY_REGION_DESCRIPTOR      Mmu[MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT];
  UINTN                             Index;
  RETURN_STATUS                     Status;

  Index = 0;
  Ex    = Map;
  while (Ex->Length != 0) {
    switch (Ex->HobOption) {
    case AddMem:
    case AddDev:
    case HobOnlyNoCacheSetting:
      AddHob (Ex);
      break;
    case NoHob:
    default:
      goto update;
    }

    if (Ex->HobOption == HobOnlyNoCacheSetting) {
      Ex++;
      continue;
    }

  update:
    Mmu[Index].PhysicalBase = Ex->Address;
    Mmu[Index].VirtualBase  = Ex->Address;
    Mmu[Index].Length       = Ex->Length;
    Mmu[Index].Attributes   = Ex->ArmAttributes;
    Index++;
    Ex++;
  }

  Mmu[Index].PhysicalBase = 0;
  Mmu[Index].VirtualBase  = 0;
  Mmu[Index].Length       = 0;
  Mmu[Index].Attributes   = 0;

  DEBUG ((EFI_D_INFO, "Configure MMU In \n"));
  Status = InitMmu (Mmu);
  DEBUG ((EFI_D_INFO, "Configure MMU Out \n"));
  return Status;
}

EFI_STATUS
EFIAPI
MemoryPeim (
  IN EFI_PHYSICAL_ADDRESS  UefiMemoryBase,
  IN UINT64                UefiMemorySize
  )
{
  ARM_MEMORY_REGION_DESCRIPTOR_EX  Map[MAX_ARM_MEMORY_REGION_DESCRIPTOR_COUNT];
  UINTN                            N;
  UINT64                           DdrBase;
  UINT64                           DdrSize;
  RANGE                            Rsv[MAX_RSV];
  UINTN                            RsvCount;
  CARVEOUT                         Cb[MAX_RSV + 8];
  UINTN                            CbCount;
  UINT64                           Cursor;
  UINT64                           DdrEnd;
  UINT64                           FbBase;
  UINT64                           FbSize;
  EFI_STATUS                       Status;

  N = 0;
  ZeroMem (Map, sizeof (Map));

  // Peripheral MMIO window (0x0 .. DDR base). Marked device.
  AddReg (Map, &N, "Periphs", 0x00000000, 0x40000000, AddMem,
          MMAP_IO, UNCACHEABLE, MmIO, NS_DEVICE);

  if (gFdtBase == NULL) {
    DEBUG ((EFI_D_ERROR, "No FDT: refusing unsafe static memory fallback\n"));
    return EFI_NOT_FOUND;
  }

  if (!FdtParse (gFdtBase, &DdrBase, &DdrSize, Rsv, &RsvCount, &FbBase, &FbSize) ||
      (DdrBase < 0x40000000ULL) ||
      (DdrSize == 0) ||
      (DdrBase > MAX_UINT64 - DdrSize) ||
      (FbSize == 0) ||
      (FbBase > MAX_UINT64 - FbSize))
  {
    DEBUG ((EFI_D_ERROR, "Invalid FDT memory description\n"));
    return EFI_COMPROMISED_DATA;
  }

  {
    if ((UefiMemoryBase != UEFI_POOL_BASE) || (UefiMemorySize != UEFI_POOL_SIZE) ||
        (FbBase != FB_BASE) || (FbSize < FB_SIZE) ||
        (FbBase < DdrBase) || (FbBase + FbSize > DdrBase + DdrSize)) {
      return EFI_UNSUPPORTED;
    }
    DEBUG ((EFI_D_INFO, "FDT: DDR 0x%llx + 0x%llx, %u reserved\n",
            DdrBase, DdrSize, (UINT32)RsvCount));

    // Build the carveout list (regions that are NOT conventional HLOS RAM).
    CbCount = 0;
    Cb[CbCount++] = (CARVEOUT){ UEFI_STACK_BASE,   UEFI_STACK_SIZE,   "UEFI Stack",   SYS_MEM, SYS_MEM_CAP, BsData, WRITE_BACK };
    Cb[CbCount++] = (CARVEOUT){ UEFI_FD_BASE,      UEFI_FD_SIZE,      "UEFI FD",      SYS_MEM, SYS_MEM_CAP, BsCode, WRITE_BACK };
    Cb[CbCount++] = (CARVEOUT){ UEFI_POOL_BASE,    UEFI_POOL_SIZE,    "UEFI MemPool", SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK };
    Cb[CbCount++] = (CARVEOUT){ UEFI_VECTORS_BASE, UEFI_VECTORS_SIZE, "CPU Vectors",  SYS_MEM, SYS_MEM_CAP, BsCode, WRITE_BACK };
    Cb[CbCount++] = (CARVEOUT){ LANCELOT_LOG_BASE, LANCELOT_LOG_SIZE, "Boot Log", SYS_MEM, SYS_MEM_CAP, Reserv, WRITE_BACK_XN };
    Cb[CbCount++] = (CARVEOUT){ FbBase,             FbSize,             "Display",      MEM_RES, SYS_MEM_CAP, Reserv, WRITE_THROUGH_XN };
    for (UINTN i = 0; i < RsvCount; i++) {
      // The framebuffer is already represented by the Display carveout.
      if ((Rsv[i].Addr == FbBase) && (Rsv[i].Size == FbSize)) {
        continue;
      }
      if ((Rsv[i].Addr >= DdrBase) && (Rsv[i].Addr < DdrBase + DdrSize) &&
          (CbCount < MAX_RSV + 8))
      {
        Cb[CbCount++] = (CARVEOUT){ Rsv[i].Addr, Rsv[i].Size, "Reserved",
                                    MEM_RES, SYS_MEM_CAP, Reserv, WRITE_BACK_XN };
      }
    }

    // Never allow a fixed UEFI region to overlap a runtime reservation.
    for (UINTN i = 0; i < 5; i++) {
      UINT64 UefiEnd = Cb[i].Addr + Cb[i].Size;
      if ((Cb[i].Addr < DdrBase) || (UefiEnd > DdrBase + DdrSize)) {
        return EFI_UNSUPPORTED;
      }
      for (UINTN j = 0; j < RsvCount; j++) {
        UINT64 RsvEnd = Rsv[j].Addr + Rsv[j].Size;
        if ((Cb[i].Addr < RsvEnd) && (Rsv[j].Addr < UefiEnd)) {
          DEBUG ((EFI_D_ERROR,
                  "UEFI region %a [0x%llx,0x%llx) overlaps runtime reservation [0x%llx,0x%llx)\n",
                  Cb[i].Name, Cb[i].Addr, UefiEnd, Rsv[j].Addr, RsvEnd));
          return EFI_SECURITY_VIOLATION;
        }
      }
    }

    SortCarveouts (Cb, CbCount);

    // Walk DDR, emitting conventional RAM for the gaps and the carveouts
    // in between.
    Cursor  = DdrBase;
    DdrEnd  = DdrBase + DdrSize;
    for (UINTN i = 0; i < CbCount; i++) {
      UINT64  a = Cb[i].Addr;
      UINT64  s = Cb[i].Size;
      if (a >= DdrEnd) {
        break;
      }
      if (a < Cursor) {
        // clip overlap with a previously-emitted region
        UINT64  overlap = Cursor - a;
        if (s <= overlap) {
          continue;
        }
        a += overlap;
        s -= overlap;
      }
      if (a > Cursor) {
        AddReg (Map, &N, "HLOS RAM", Cursor, a - Cursor, AddMem,
                SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK_XN);
      }
      if (s > DdrEnd - a) {
        s = DdrEnd - a;
      }
      // Keep firmware/TEE reservations out of the CPU translation tables.
      // They remain reserved in the resource HOBs. Display is the exception.
      AddReg (Map, &N, Cb[i].Name, a, s,
              AsciiStrCmp (Cb[i].Name, "Reserved") == 0 ? HobOnlyNoCacheSetting : AddMem,
              Cb[i].RType, Cb[i].RAttr, Cb[i].MemType, Cb[i].ArmAttr);
      Cursor = a + s;
      if (Cursor >= DdrEnd) {
        break;
      }
    }
    if (Cursor < DdrEnd) {
      AddReg (Map, &N, "HLOS RAM", Cursor, DdrEnd - Cursor, AddMem,
              SYS_MEM, SYS_MEM_CAP, Conv, WRITE_BACK_XN);
    }
  }

  // Terminator
  AddReg (Map, &N, "Terminator", 0, 0, 0, 0, 0, 0, 0);

  Status = ApplyMap (Map);
  if (EFI_ERROR (Status)) {
    return Status;
  }

  if (FeaturePcdGet (PcdPrePiProduceMemoryTypeInformationHob)) {
    BuildMemoryTypeInformationHob ();
  }

  return EFI_SUCCESS;
}
