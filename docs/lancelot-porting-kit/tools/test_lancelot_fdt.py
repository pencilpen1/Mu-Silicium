#!/usr/bin/env python3
"""Run the actual SEC FDT parser on captured and malformed DTBs with sanitizers."""
import os
from pathlib import Path
import struct
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "ws/MT6768Pkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c"


def make_dtb(*, depth=0, name="test", reservations=1, reg=None):
    strings = b"#address-cells\0#size-cells\0reg\0"
    def word(n):
        return struct.pack(">I", n)
    def node(n):
        b = n.encode() + b"\0"
        return word(1) + b + b"\0" * (-len(b) % 4)
    def prop(n, value):
        return word(3) + word(len(value)) + word(strings.index(n.encode())) + value + b"\0" * (-len(value) % 4)
    body = node("") + prop("#address-cells", word(2)) + prop("#size-cells", word(2))
    body += node("memory@40000000") + prop("reg", reg if reg is not None else struct.pack(">QQ", 0x40000000, 0x100000000)) + word(2)
    body += node("reserved-memory") + prop("#address-cells", word(2)) + prop("#size-cells", word(2))
    body += node(name) + prop("reg", b"".join(struct.pack(">QQ", 0x70000000 + i * 0x1000, 0x1000) for i in range(reservations))) + word(2) + word(2)
    body += node("child") * depth + word(2) * depth + word(2) + word(9)
    return struct.pack(">10I", 0xd00dfeed, 56 + len(body) + len(strings), 56, 56 + len(body), 40, 17, 16, 0, len(strings), len(body)) + b"\0" * 16 + body + strings


def main():
    source = SOURCE.read_text()
    # Compile the firmware function itself; only UEFI types/string primitives
    # are replaced with host equivalents. No duplicate Python parser is tested.
    parser = source[source.index("#define UEFI_FD_BASE"):source.index("// A carveout")]
    preamble = r"""
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
typedef uint8_t UINT8;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef size_t UINTN;
typedef char CHAR8;
typedef void VOID;
typedef bool BOOLEAN;
#define STATIC static
#define CONST const
#define IN
#define OUT
#define TRUE true
#define FALSE false
#define MAX_UINT64 UINT64_MAX
#define ZeroMem(p,n) memset(p,0,n)
#define CopyMem memcpy
#define AsciiStrCmp strcmp
#define AsciiStrnCmp strncmp
"""
    harness = r"""
int main(int argc, char **argv) {
  for (int i=1; i<argc; i++) {
    FILE *f=fopen(argv[i],"rb"); assert(f);
    fseek(f,0,SEEK_END); long n=ftell(f); rewind(f);
    unsigned char *b=malloc(n); assert(b);
    assert(fread(b,1,n,f)==(size_t)n); fclose(f);
    UINT64 base=0,size=0,fb=0,fbs=0; UINTN count=0; RANGE ranges[MAX_RSV];
    bool ok=FdtParse(b,&base,&size,ranges,&count,&fb,&fbs);
    bool expected=strstr(argv[i],".ok")!=NULL;
    if(ok!=expected) {fprintf(stderr,"unexpected result %d: %s\n",ok,argv[i]);return 1;}
    if(ok) {
      assert(base==0x40000000ULL && size==0x100000000ULL);
      assert(count>0 && count<=MAX_RSV);
      if(strstr(argv[i],"multi-reservation")) assert(count==2);
      if(strstr(argv[i],"runtime")) {
        assert(fb==0x7cdb0000 && fbs==0x2250000);
        bool tee=false;
        for(UINTN j=0;j<count;j++) tee |= ranges[j].Addr==0x70000000 && ranges[j].Size==0x7000000;
        assert(tee);
      }
    }
    free(b);
  }
  printf("PASS: %d FDT cases (actual C parser, ASan/UBSan)\n",argc-1);
}
"""
    valid = make_dtb()
    cases = {
        "runtime.ok": (ROOT / "analysis/runtime-2026-10-17/lancelot-runtime.dtb").read_bytes(),
        "minimal.ok": valid,
        "multi-reservation.ok": make_dtb(reservations=2),
        "deep.bad": make_dtb(depth=17),
        "long-name.bad": make_dtb(name="x" * 64),
        "capacity.bad": make_dtb(reservations=49),
        "multi-bank.bad": make_dtb(reg=struct.pack(">QQQQ", 0x40000000, 0x10000000, 0x90000000, 0x10000000)),
    }
    for name, offset, value in [
        ("bad-struct", 8, 0xfffffff0), ("bad-strings", 12, len(valid)+4),
        ("struct-length", 36, 0xfffffff0), ("old-version", 20, 16),
        ("missing-reserve-end", 40, 1), ("unknown-token", 56, 0x1234),
    ]:
        b = bytearray(valid)
        struct.pack_into(">I", b, offset, value)
        cases[name + ".bad"] = b
    b = bytearray(valid)
    strings_offset = struct.unpack_from(">I", b, 12)[0]
    b[strings_offset:] = b"x" * (len(b)-strings_offset)
    cases["unterminated-strings.bad"] = b
    # First root property: corrupt its length and property-name offset.
    for name, offset in [("property-length", 68), ("property-name", 72)]:
        b = bytearray(valid)
        struct.pack_into(">I", b, offset, 0xffffffff)
        cases[name + ".bad"] = b
    with tempfile.TemporaryDirectory(prefix="fdt-test-", dir="/tmp/opencode") as temp:
        temp = Path(temp)
        cfile = temp / "test.c"
        cfile.write_text(preamble + parser + harness)
        exe = temp / "test"
        subprocess.run(["gcc", "-std=c11", "-g", "-O1", "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie", str(cfile), "-o", str(exe)], check=True)
        paths = []
        for name, data in cases.items():
            path = temp / name
            path.write_bytes(data)
            paths.append(str(path))
        subprocess.run([str(exe), *paths], check=True, timeout=30,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
        include_dirs = [".", "ws/edk2/MdePkg/Include", "ws/edk2/MdePkg/Include/AArch64",
                        "ws/edk2/ArmPkg/Include", "ws/edk2/ArmPlatformPkg/Include",
                        "ws/edk2/MdeModulePkg/Include", "ws/MT6768Pkg/Include"]
        memory_exe = temp / "memory-test"
        subprocess.run(["gcc", "-std=c11", "-g", "-O1", "-fshort-wchar",
                        "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-no-pie",
                        *("-I" + str(ROOT / d) for d in include_dirs),
                        str(ROOT / "tools/test_lancelot_memory.c"), "-o", str(memory_exe)], check=True)
        subprocess.run([str(memory_exe), str(temp / "runtime.ok")], check=True, timeout=30,
                       env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})


if __name__ == "__main__":
    main()
