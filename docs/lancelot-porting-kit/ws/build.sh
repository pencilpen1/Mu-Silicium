#!/bin/bash
# Build Redmi 9 (lancelot / MT6768) UEFI image.
# Layout: ws/ contains edk2/, edk2-platforms/, MT6768Pkg/, BootShim/, blobs/
set -e
. build_common.sh
./build_bootshim.sh

GCC5_AARCH64_PREFIX=aarch64-linux-gnu- build -j$(nproc) -s -n 0 -a AARCH64 -t GCC5 -p MT6768Pkg/Devices/lancelot.dsc

cat BootShim/BootShim.bin workspace/Build/MT6768Pkg/DEBUG_GCC5/FV/MT6768PKG_UEFI.fd > workspace/UEFI
gzip -f workspace/UEFI

# mkbootimg parameters derived from the stock lancelot boot.img header:
#   kernel_addr=0x40080000 ramdisk_addr=0x47c80000 tags_addr=0x4bc80000
#   => base=0x40078000, kernel_offset=0x8000, ramdisk_offset=0x07c08000,
#      tags_offset=0x0bc08000
python3 mkbootimg.py \
	--cmdline "bootopt=64S3,32N2,64N2 androidboot.serialconsole=0" \
	--kernel workspace/UEFI.gz \
	--ramdisk blobs/initramfs \
	--base 0x40078000 \
	--kernel_offset 0x00008000 \
	--ramdisk_offset 0x07c08000 \
	--second_offset 0x00e88000 \
	--tags_offset 0x0bc08000 \
	--dtb_offset 0x0bc08000 \
	--header_version 2 \
	--dtb blobs/dtb \
	-o boot-uefi.img

echo "==> boot-uefi.img ready ($(stat -c%s boot-uefi.img) bytes)"
