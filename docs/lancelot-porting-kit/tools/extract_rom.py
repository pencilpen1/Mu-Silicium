#!/usr/bin/env python3
"""Extract selected members from a ROM zip into a fresh directory.

Usage: python3 -I extract_rom.py <rom.zip> <outdir> [member ...]
Reads only; never executes anything from the archive.
"""
import os
import sys
import zipfile

DEFAULT = [
    "boot.img",
    "dtbo.img",
    "lk.img",
    "preloader_emmc.img",
    "tee.img",
    "sspm.img",
    "scp.img",
    "spmfw.img",
    "logo.img",
    "vbmeta.img",
    "dynamic_partitions_op_list",
    "META-INF/com/android/metadata",
]


def main():
    if len(sys.argv) < 3:
        print(__doc__)
        return 1
    zip_path, outdir = sys.argv[1], sys.argv[2]
    wanted = sys.argv[3:] or DEFAULT

    os.makedirs(outdir, exist_ok=True)
    with zipfile.ZipFile(zip_path) as z:
        names = set(z.namelist())
        for m in wanted:
            if m not in names:
                print(f"  MISSING  {m}")
                continue
            info = z.getinfo(m)
            dest = os.path.join(outdir, os.path.basename(m))
            with z.open(m) as src, open(dest, "wb") as dst:
                while chunk := src.read(1 << 20):
                    dst.write(chunk)
            print(f"  OK  {info.file_size:>12,}  -> {dest}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
