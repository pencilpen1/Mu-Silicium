#!/usr/bin/env python3
"""Compile actual Mu PWRAP/MT6358 sources with RAM doubles, never a phone.

Requires the independently checked Mu source tree; this does not apply a patch,
build firmware, flash, install a handoff, or prove secure-world exclusivity.
"""
from pathlib import Path
import argparse
import hashlib
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
ap = argparse.ArgumentParser(description=__doc__)
ap.add_argument('--mu', type=Path, default=ROOT)
args = ap.parse_args()
pkg = args.mu / 'Silicon/MediaTek/MediaTekPkg'
sources = {'transfer': pkg / 'Drivers/PmicWrapperDxe/PmicWrapperDxe.c',
           'consumer': pkg / 'Drivers/MT6358PmicDxe/MT6358PmicDxe.c'}
files = [*sources.values(), pkg / 'Include/Protocol/MtkPmicWrapper.h']
digests = lambda: [hashlib.sha256(p.read_bytes()).hexdigest() for p in files]
before = digests()
test = Path(__file__).resolve().parent
with tempfile.TemporaryDirectory(prefix='mu-pwrap-') as temp:
    temp = Path(temp)
    for name in ['UefiBootServicesTableLib', 'UefiLib', 'MemoryMapHelperLib',
                 'DebugLib', 'TimerLib', 'IoLib', 'BaseLib']:
        path = temp / 'Library' / (name + '.h')
        path.parent.mkdir(exist_ok=True)
        path.write_text('#include "host.h"\n')
    for cc in ['cc', 'clang']:
        for name, source in sources.items():
            binary = temp / (name + '-' + cc)
            macro = 'MU_PWRAP_SOURCE' if name == 'transfer' else 'MU_PMIC_SOURCE'
            subprocess.run([cc, '-std=gnu11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
                            '-Wno-unused-parameter', '-fsanitize=address,undefined',
                            '-I' + str(test), '-I' + str(temp), '-I' + str(pkg / 'Include'),
                            f'-D{macro}="{source}"', str(test / (name + '.c')),
                            '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
assert before == digests()
print('Actual Mu PWRAP/PMIC GCC+Clang ASan+UBSan PASS; source hashes unchanged; no OS mode, no device/handoff')
