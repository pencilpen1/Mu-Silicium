# MT6768 PWRAP: bounded firmware transactions

## Checked-contract discovery follow-up

The same driver object is also advertised as
`gMediaTekPmicWrapperCheckedProtocolGuid`
(`9d0d6b42-7d76-4c3f-b19e-91c88e633202`). This identifies the bounded,
status-returning contract without calling an older void-only interface to guess
its behavior. It is not an OS mode, alternate implementation, mailbox backend,
or ownership proof. The original GUID and normal firmware clients are unchanged.

Host tests verify both GUIDs refer to the identical driver object. The amended
formal RELEASE build returned exit 0; FD SHA-256:
`a6312edd0a99d1e0212ea9ff4ed386abd873f09521ae8f5065240428d78f9a3e`.
A Boot-v2 package rebuilt from the preserved working template changes only the
kernel payload/size/image ID, preserving ramdisk, DTB and all other header fields.
The live test uses a separate loader read-only SWCID/rail observation before XNU;
this is not GPU power or DMA acceptance. Hardware results are recorded separately.

## Scope

`PmicWrapperDxe` previously waited indefinitely for WACS2 FSM transitions and
its void read/write API could not report failed transactions to MT6358 callers.
The firmware driver now has one uniform implementation, not Windows/Linux/iOS
compatibility modes:

- `Read` and `Write` return `EFI_STATUS`; failed reads leave the output unchanged.
- A firmware `EFI_LOCK` serializes transactions and rejects recursive entry.
- `UpdateBits` holds the same lock across read/modify/write, preserving other bits.
- The complete transaction has a 10 ms deadline plus a 10,000-poll limit.
- Initial non-idle FSM/REQ is rejected without CMD or VLDCLR writes.
- Own read ACK is consumed only after data is read; final IDLE/REQ completion is
  verified before publishing success. Writes also wait for completion.
- Unknown own completion persistently quarantines this firmware driver. It does
  not clear foreign/late data, reset PWRAP, or retry to pretend recovery.
- MT6358 callers propagate read/update errors. Key failures are logged and do not
  assert a key. The LDO selector uses the descriptor's `Mask`, not its voltage.
- An uninitialized wrapper is not published as a usable protocol.

The GUID and first two pointer offsets remain unchanged. A caller rebuilt against
the header can check the returned status; an old void caller ignores that return.
The third pointer is the new atomic update operation. No alternate legacy driver
or OS-selection branch has been added.

## Validation

Run `python3 Tests/PmicWrapper/test.py` from this repository. It compiles the
**actual firmware driver and actual MT6358 consumer** with GCC and Clang using
ASan/UBSan. All four compile/run combinations passed, including:

- successful read and atomic register updates;
- wrong address/null output, init rejection, foreign pending REQ/result;
- timeout before/after CMD, stuck/frozen timer, clock rollback and final ACK;
- persistent quarantine, zero MMIO on later denied requests, recursive locking;
- consumer error propagation, unchanged failed outputs and LDO selector fields.

Formal AARCH64 RELEASE build/link/FV generation returned exit 0 with the unchanged
Lancelot build recipe. Resulting FD is 2,097,152 bytes, SHA-256:
`cc5f3c50cf875922c4791685d5b9b230a3b94bf268150aec15a06dae014b2dea`.
Build retains the existing unsupported reset-library warning and ignored missing
PDB-copy messages; this is not a zero-warning claim.

## Limits and boot-path preservation

This is a firmware robustness fix, **not a GPU/handoff acceptance result**. The
firmware lock covers firmware protocol callers only; it does not prove ownership
against secure-world, SSPM, SCP, or another OS. No XNU ownership provider or
mailbox ABI proof is created by these changes.

The Lancelot DSC/FDF, memory-map descriptor, Android image configuration, storage
driver and existing build-script edits were unchanged and hash-audited during the
build. There are no changes to GPT, OS devices, Linux DT, Windows ACPI, PLLs or
GPU power domains. The preserved working Mu boot image was not replaced or
flashed by this build. This does **not** substitute for real Windows/Linux boot
regression tests, which have not yet been performed for this FD.

Raw host/device logs, source-before snapshots, source hashes and original working
firmware are retained privately; raw device logs, account data and binary vendor
firmware are not published. Next hardware work must retain the recoverable
original Boot/Mu and independently verify PMIC identity, access and handoff before
GPU rail/power or private DMA tests. GPU rendering is still unverified.
