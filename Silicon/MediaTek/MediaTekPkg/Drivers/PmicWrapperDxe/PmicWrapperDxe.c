#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/MemoryMapHelperLib.h>
#include <Library/DebugLib.h>
#include <Library/TimerLib.h>
#include <Library/IoLib.h>

#include <Library/PmicWrapperImplLib.h>

#include <Protocol/MtkPmicWrapper.h>
#include <Protocol/MtkGpuMap.h>

typedef enum {
  WacsFsmIdle = 0,
  WacsFsmWfVldClr = 6,
} WACS_FSM_STATE;

STATIC EFI_MEMORY_REGION_DESCRIPTOR mPmicWrapperRegion;
STATIC EFI_LOCK mTransactionLock = EFI_INITIALIZE_LOCK_VARIABLE (TPL_NOTIFY);
STATIC BOOLEAN mPoisoned;
STATIC CONST MTK_GPU_MMIO_MAP mGpuMap = {
  MTK_GPU_MAP_REVISION, sizeof (MTK_GPU_MMIO_MAP),
  {{0x10006000, 0x1000}, {0x10001000, 0x1000}, {0x13000000, 0x1000},
   {0x13040000, 0x4000}, {0x10000000, 0x1000}, {0x1000C000, 0x1000}}
};

// Bounds apply to the whole transaction, including final completion.
#define PWRAP_TIMEOUT_NS 10000000ULL
#define PWRAP_MAX_POLLS 10000U
#define WACS_REQ BIT19
#define WACS_SYNC_IDLE BIT20
#define WACS_INIT_DONE BIT21

STATIC
UINT64
WacsNow ()
{
  return GetTimeInNanoSecond (GetPerformanceCounter ());
}

STATIC
BOOLEAN
WacsExpired (IN UINT64 Start)
{
  UINT64 Now = WacsNow ();
  return Now < Start || Now - Start >= PWRAP_TIMEOUT_NS;
}

STATIC
UINT32
PmicWrapperRead(
  IN  UINT16 Reg)
{
  return MmioRead32 (mPmicWrapperRegion.Address + gPlatformInfo.RegMap[Reg]);
}

STATIC
VOID
PmicWrapperWrite(
  IN  UINT16 Reg,
  IN  UINT32 Value)
{
  MmioWrite32 (mPmicWrapperRegion.Address + gPlatformInfo.RegMap[Reg], Value);
}

STATIC
WACS_FSM_STATE
WacsGetFsm (IN UINT32 Value)
{
  // Get FSM State
  if (gPlatformInfo.ArbCapabilities) {
    Value = (Value >> 1) & 0x7;
  } else {
    Value = (Value >> 16) & 0x7;
  }

  return Value;
}

STATIC
EFI_STATUS
WacsWaitFor (
  IN WACS_FSM_STATE Fsm,
  IN UINT64 Start,
  IN BOOLEAN OwnCommand,
  OUT UINT32 *Result OPTIONAL)
{
  for (UINTN Poll = 0; Poll < PWRAP_MAX_POLLS; Poll++) {
    if (WacsExpired (Start)) {
      return EFI_TIMEOUT;
    }
    UINT32 Value = PmicWrapperRead (PmicWrapperWacs2RData);
    WACS_FSM_STATE State = WacsGetFsm (Value);
    if (!gPlatformInfo.ArbCapabilities) {
      if ((Value & WACS_INIT_DONE) == 0) {
        return EFI_NOT_READY;
      }
      // A pre-existing request/result is foreign. Never consume or clear it.
      if (!OwnCommand && (State != WacsFsmIdle || (Value & WACS_REQ))) {
        return EFI_NOT_READY;
      }
      if (State == Fsm && (Value & WACS_SYNC_IDLE) &&
          (Fsm != WacsFsmIdle || !(Value & WACS_REQ))) {
        if (WacsExpired (Start)) {
          return EFI_TIMEOUT;
        }
        if (Result != NULL) {
          *Result = Value;
        }
        return EFI_SUCCESS;
      }
    } else if (State == Fsm) {
      if (WacsExpired (Start)) {
        return EFI_TIMEOUT;
      }
      if (Result != NULL) {
        *Result = Value;
      }
      return EFI_SUCCESS;
    }
    MicroSecondDelay (1);
  }
  return EFI_TIMEOUT; // Still bounded if the timer stalls.
}

STATIC
EFI_STATUS
WacsTransfer (
  IN UINT16 Address,
  IN UINT16 Data,
  IN BOOLEAN IsWrite,
  OUT UINT16 *Result OPTIONAL,
  IN UINT64 Start)
{
  EFI_STATUS Status;
  UINT32 Command;
  UINT32 Value = 0;
  BOOLEAN Pending = FALSE;
  if (mPoisoned) {
    return EFI_DEVICE_ERROR;
  }
  if ((Address & 1) || (!IsWrite && Result == NULL)) {
    return EFI_INVALID_PARAMETER;
  }
  Status = WacsWaitFor (WacsFsmIdle, Start, FALSE, NULL);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  if (WacsExpired (Start)) {
    return EFI_TIMEOUT;
  }

  // Encode Address, data and write mode
  if (gPlatformInfo.ArbCapabilities) {
    Command = Address;
    if (IsWrite) {
      Pending = TRUE;
      PmicWrapperWrite (PmicWrapperSwinf2WData31, Data);
      Command |= (1 << 29);
    }
  } else {
    Command = ((UINT32)IsWrite << 31) | ((UINT32)(Address >> 1) << 16) | Data;
  }

  Pending = TRUE;
  PmicWrapperWrite (PmicWrapperWacs2Cmd, Command);
  if (!IsWrite) {
    Status = WacsWaitFor (WacsFsmWfVldClr, Start, TRUE, &Value);
    if (EFI_ERROR (Status)) {
      goto Done;
    }
    if (gPlatformInfo.ArbCapabilities) {
      Value = PmicWrapperRead (PmicWrapperSwinf2RData31);
    }
    if (WacsExpired (Start)) {
      Status = EFI_TIMEOUT;
      goto Done;
    }
    PmicWrapperWrite (PmicWrapperWacs2VldClr, 1);
  }
  Status = WacsWaitFor (WacsFsmIdle, Start, TRUE, NULL);
  if (!EFI_ERROR (Status)) {
    Pending = FALSE;
    if (!IsWrite) {
      *Result = (UINT16)Value;
    }
  }
Done:
  if (Pending) {
    // No automatic recovery, channel reset, or late-result consumption.
    mPoisoned = TRUE;
  }
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
PmicWrapperImplRead (
  IN UINT16 Address,
  OUT UINT16 *Value)
{
  EFI_STATUS Status = EfiAcquireLockOrFail (&mTransactionLock);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = WacsTransfer (Address, 0, FALSE, Value, WacsNow ());
  EfiReleaseLock (&mTransactionLock);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
PmicWrapperImplWrite (
  IN  UINT16 Address,
  OUT UINT16 Value)
{
  EFI_STATUS Status = EfiAcquireLockOrFail (&mTransactionLock);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  Status = WacsTransfer (Address, Value, TRUE, NULL, WacsNow ());
  EfiReleaseLock (&mTransactionLock);
  return Status;
}

STATIC
EFI_STATUS
EFIAPI
PmicWrapperImplUpdateBits (
  IN UINT16 Address,
  IN UINT16 Mask,
  IN UINT16 Value)
{
  if (!Mask || (Value & ~Mask)) {
    return EFI_INVALID_PARAMETER;
  }
  EFI_STATUS Status = EfiAcquireLockOrFail (&mTransactionLock);
  if (EFI_ERROR (Status)) {
    return Status;
  }
  UINT64 Start = WacsNow ();
  UINT16 Old;
  Status = WacsTransfer (Address, 0, FALSE, &Old, Start);
  if (!EFI_ERROR (Status)) {
    UINT16 New = (Old & ~Mask) | Value;
    if (New != Old) {
      Status = WacsTransfer (Address, New, TRUE, NULL, Start);
    }
  }
  EfiReleaseLock (&mTransactionLock);
  return Status;
}

STATIC MTK_PMIC_WRAPPER_PROTOCOL mPmicWrapper = {
  PmicWrapperImplRead,
  PmicWrapperImplWrite,
  PmicWrapperImplUpdateBits
};

EFI_STATUS
EFIAPI
InitPmicWrapper (
  IN EFI_HANDLE        ImageHandle,
  IN EFI_SYSTEM_TABLE *SystemTable)
{
  EFI_STATUS Status;
  UINT32     InitState;

  // Locate PMIC Wrapper Memory Region
  Status = LocateMemoryRegionByName ("PMIC Wrapper", &mPmicWrapperRegion);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Locate PMIC Wrapper Memory Region! Status = %r\n", Status));
    return Status;
  }

  // Read init state of PMIC Wrapper
  InitState = PmicWrapperRead (PmicWrapperInitDone2);
  if (InitState != 1) {
    DEBUG ((EFI_D_ERROR, "PMIC Wrapper Not Initialized!\n"));
    return EFI_NOT_READY;
  }

  // Register PMIC Wrapper Protocol
  Status = gBS->InstallMultipleProtocolInterfaces (&ImageHandle,
             &gMediaTekPmicWrapperProtocolGuid, &mPmicWrapper,
             &gMediaTekPmicWrapperCheckedProtocolGuid, &mPmicWrapper, NULL);
  if (EFI_ERROR (Status)) {
    DEBUG ((EFI_D_ERROR, "Failed to Register PMIC Wrapper Protocol! Status = %r\n", Status));
    return Status;
  }

  // Advertise only already-installed board mappings. Does not map a window,
  // access GPU/SPM, power a domain or grant AP/secure-world exclusivity.
  STATIC CHAR8 Names[MTK_GPU_MAP_WINDOWS][24] = {
    "SPM Control", "Infra Cfg AO", "MFG Configuration", "Mali Registers",
    "Top Ck Gen", "AP Mixed"
  };
  if (mPmicWrapperRegion.Address == 0x1000D000 && !gPlatformInfo.ArbCapabilities) {
    for (UINTN Index = 0; Index < MTK_GPU_MAP_WINDOWS; Index++) {
      EFI_MEMORY_REGION_DESCRIPTOR Region;
      Status = LocateMemoryRegionByName (Names[Index], &Region);
      if (EFI_ERROR (Status) || Region.Address != mGpuMap.Window[Index].Base ||
          Region.Length != mGpuMap.Window[Index].Size) {
        return EFI_SUCCESS; // Normal PMIC remains usable without a GPU map contract.
      }
    }
    Status = gBS->InstallMultipleProtocolInterfaces (&ImageHandle,
               &gMediaTekGpuMapProtocolGuid, (VOID *)&mGpuMap, NULL);
    if (EFI_ERROR (Status)) {
      DEBUG ((EFI_D_ERROR, "GPU device mapping contract publication failed: %r\n", Status));
      return EFI_SUCCESS; // Optional GPU metadata must not disable normal PMIC boot clients.
    }
  }

  return Status;
}
