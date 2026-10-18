// Minimal SEC exception vectors adapted from Mu-Silicium's lancelot port.
#include <AsmMacroIoLibV8.h>
#include <Chipset/AArch64.h>
#include <Protocol/DebugSupport.h>
#include <Library/PcdLib.h>

.text

#define TO_HANDLER                         \
  EL1_OR_EL2 (x1)                          \
1:mrs  x1, elr_el1;                        \
  mrs  x2, esr_el1;                        \
  mrs  x3, far_el1;                        \
  b    3f;                                 \
2:mrs  x1, elr_el2;                        \
  mrs  x2, esr_el2;                        \
  mrs  x3, far_el2;                        \
3:b    SecFatalException;

#define EXCEPTION_VECTOR                                  \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_CUR_SP0_SYNC);   \
  mov x0, #EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS;         \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_CUR_SP0_IRQ);    \
  mov x0, #EXCEPT_AARCH64_IRQ;                           \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_CUR_SP0_FIQ);    \
  mov x0, #EXCEPT_AARCH64_FIQ;                           \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_CUR_SP0_SERR);   \
  mov x0, #EXCEPT_AARCH64_SERROR;                        \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_CUR_SPX_SYNC);   \
  mov x0, #EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS;         \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_CUR_SPX_IRQ);    \
  mov x0, #EXCEPT_AARCH64_IRQ;                           \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_CUR_SPX_FIQ);    \
  mov x0, #EXCEPT_AARCH64_FIQ;                           \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_CUR_SPX_SERR);   \
  mov x0, #EXCEPT_AARCH64_SERROR;                        \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_LOW_A64_SYNC);   \
  mov x0, #EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS;         \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_LOW_A64_IRQ);    \
  mov x0, #EXCEPT_AARCH64_IRQ;                           \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_LOW_A64_FIQ);    \
  mov x0, #EXCEPT_AARCH64_FIQ;                           \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_LOW_A64_SERR);   \
  mov x0, #EXCEPT_AARCH64_SERROR;                        \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_LOW_A32_SYNC);   \
  mov x0, #EXCEPT_AARCH64_SYNCHRONOUS_EXCEPTIONS;         \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_LOW_A32_IRQ);    \
  mov x0, #EXCEPT_AARCH64_IRQ;                           \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_LOW_A32_FIQ);    \
  mov x0, #EXCEPT_AARCH64_FIQ;                           \
  TO_HANDLER                                             \
VECTOR_ENTRY (SecVectorTable, ARM_VECTOR_LOW_A32_SERR);   \
  mov x0, #EXCEPT_AARCH64_SERROR;                        \
  TO_HANDLER

.section .text.SecVectorTable,"ax"
.align 11
.global SecVectorTable
SecVectorTable:
EXCEPTION_VECTOR
.org 0x800
.previous

// Fatal-only handler: use the reserved vector area as an emergency stack.
// It is disjoint from the normal SEC stack and the PrePi allocator.
SecFatalException:
  msr daifset, #0xf
  ldr x4, =FixedPcdGet64(PcdPrePiStackBase)
  mov sp, x4
  bl ASM_PFX (SecCommonExceptionEntry)
1:b 1b
