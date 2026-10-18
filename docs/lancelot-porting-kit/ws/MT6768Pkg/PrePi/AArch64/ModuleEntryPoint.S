#include <Library/PcdLib.h>
#include <AsmMacroIoLibV8.h>
#include <Chipset/AArch64.h>

#define LoadConstantToReg(Data, Reg) \
  ldr  Reg, =Data

.text
.align 3

GCC_ASM_IMPORT (CEntryPoint)
GCC_ASM_IMPORT (ArmWriteCpacr)
GCC_ASM_IMPORT (ArmEnableInstructionCache)
GCC_ASM_IMPORT (ArmEnableDataCache)
GCC_ASM_IMPORT (ArmInvalidateTlb)
GCC_ASM_IMPORT (SecVectorTable)

GCC_ASM_EXPORT (_ModuleEntryPoint)

.global _StackBase 
.global _StackSize

_StackBase:
  .quad FixedPcdGet32(PcdPrePiStackBase)

_StackSize:
  .quad FixedPcdGet32(PcdPrePiStackSize)


_ModuleEntryPoint:
  msr daifset, #0xf
  mov x20, x0          /* save FDT pointer passed by LK in x0 */
  /* The known-good Mu-Silicium lancelot port enters UEFI at EL1. */
  /* If LK enters at EL2, switch to AArch64 EL1h before SEC/DXE. */
  bl _SwitchToEl1
  mov x0, #0

  /* Know about the current CPU index */
  mrs x3, mpidr_el1
  mov x2, x3
  mov x1, #0x0000100
  and x2, x2, x1
  cmp x2, #0x0
  mov x0, #0
  beq _CpuIndex

  mov x0, #4
_CpuIndex:
  mov x2, x3
  mov x1, #0xf
  and x2, x2, x1
  add x0, x0, x2

  /* Keep the CPU index in x19 */
  mov x19, x0


  /* First ensure all interrupts are disabled */
  bl ASM_PFX(ArmDisableInterrupts)

  /* Ensure that the MMU and caches are off */
  bl ASM_PFX(ArmDisableCachesAndMmu)
 
  /* Invalide I-Cache */
  bl ASM_PFX(ArmInvalidateInstructionCache)
  
  /* Invalidate TLB */
  bl ASM_PFX(ArmInvalidateTlb)

_SetupExceptionVector:
  LoadConstantToReg (SecVectorTable, x0)

  /* Install the vector table at the EL where SEC is actually running. */
  mrs x3, CurrentEl

  cmp x3, #0x8
  b.eq _SetupEl2VBar

  cmp x3, #0x4
  b.eq _SetupEl1VBar
  cmp x3, #0xc
  b.eq _SetupEl3VBar
  b _DonNotTrap_VFP_SIMD

_SetupEl2VBar:
  msr vbar_el2, x0
  b _DonNotTrap_VFP_SIMD

_SetupEl1VBar:
  msr vbar_el1, x0
  b _DonNotTrap_VFP_SIMD

_SetupEl3VBar:
  msr vbar_el3, x0
    
_DonNotTrap_VFP_SIMD:
  mrs x0, CPACR_EL1
  /* Bit 20 and 21 */
  orr x0, x0, #0x300000
  msr CPACR_EL1, x0
  isb
    
_SetupPrimaryCoreStack:
  msr spsel, #1
  ldr x0, _StackBase     /* Stack base arg0 */
  ldr x1, _StackSize     /* Stack size arg1 */
  
  add x4, x0, x1
  add sp, x4, #0

_EnableCache: 
  bl ArmInvalidateDataCache
  bl ASM_PFX(ArmEnableInstructionCache)
  /* ArmConfigureMmu enables D-cache with the identity map later. */

_PrepareArguments:
  mov x0, x20          /* pass saved FDT pointer to CEntryPoint() */

  bl CEntryPoint

.align 3
dead:  
  b dead                      /* We should never get here */

.align 3
_SwitchToEl1:
  mrs x0, CurrentEl
  cmp x0, #0x8
  b.ne 1f
  mov x0, #0x80000000          /* HCR_EL2.RW: AArch64 at EL1 */
  msr hcr_el2, x0
  /* Define EL1 state before ERET, rather than inheriting unknown MMU bits. */
  ldr x0, =0x30d00800         /* ARMv8 SCTLR_EL1 RES1, M/C/I disabled */
  msr sctlr_el1, x0
  mrs x0, cnthctl_el2
  orr x0, x0, #3             /* EL1 physical counter and timer access */
  msr cnthctl_el2, x0
  msr cntvoff_el2, xzr
  mrs x0, cptr_el2
  bic x0, x0, #0x400         /* do not trap EL1 FP/SIMD */
  msr cptr_el2, x0
  mov x0, x30
  msr elr_el2, x0
  mov x0, #0x3c5               /* EL1h, mask Debug/SError/IRQ/FIQ */
  msr spsr_el2, x0
  isb
  eret
1:
  ret
