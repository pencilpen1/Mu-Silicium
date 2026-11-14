//
// Copyright (c) 2012-2013, ARM Limited. All rights reserved.
// SPDX-License-Identifier: BSD-2-Clause-Patent
//

#include <AsmMacroLib.h>

ASM_FUNC (PlatformAssemblyInitialize)
  EL1_OR_EL2 (x0)
1:ret
// Retain the incoming EL2. Generic SEC runs SetupExceptionLevel2 next.
// Start with AArch64 EL1 and no stage 2 or inherited HCR traps.
2:mov x0, #(1 << 31)
  msr hcr_el2, x0
  isb
  ret
