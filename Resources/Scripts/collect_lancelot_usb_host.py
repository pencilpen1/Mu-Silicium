#!/usr/bin/env python3
"""Capture Android USB host/role/power evidence using root ADB (including adb.exe).

Run once in each physical connection state, using wireless ADB for OTG.
Only reads device state; missing optional sysfs/debugfs files are recorded.
"""
import argparse
from datetime import datetime, timezone
import gzip
import hashlib
import json
from pathlib import Path
import re
import shlex
import struct
import subprocess
import zipfile
import zlib


def attributes(patterns, names):
    """Read only the listed attributes, not arbitrary driver register dumps."""
    return f'''
for p in {patterns}; do
  [ -e "$p" ] || continue
  echo "$p"
  for a in {names}; do
    [ -f "$p/$a" ] || continue
    echo "$a:"
    cat "$p/$a"
  done
done
'''


COMMANDS = {
    "identity.txt": "uname -a; cat /proc/version; getprop ro.product.device; "
                    "getprop ro.build.fingerprint; getprop ro.vendor.build.fingerprint; "
                    "getprop sys.usb.config; getprop sys.usb.state; getprop sys.usb.controller; "
                    "cat /proc/uptime; cat /proc/sys/kernel/random/boot_id",
    "role-before.txt": attributes(
        "/sys/class/udc/* /sys/class/usb_role/* /sys/class/typec/* /sys/class/extcon/*",
        "name state role data_role power_role port_type current_speed maximum_speed"),
    "dmesg-before.txt": "dmesg",
    "runtime.dtb": "cat /sys/firmware/fdt",
    "kernel.config.gz": "cat /proc/config.gz",
    "iomem.txt": "cat /proc/iomem",
    "interrupts.txt": "cat /proc/interrupts",
    "usb-topology.txt": "ls -l /sys/bus/usb/devices; " + attributes(
        "/sys/bus/usb/devices/*",
        "busnum devnum devpath speed version idVendor idProduct bDeviceClass "
        "bInterfaceClass bInterfaceSubClass bInterfaceProtocol bMaxPower "
        "bNumInterfaces bNumEndpoints manufacturer product uevent"),
    "usb-descriptors.txt": '''
for p in /sys/bus/usb/devices/*; do
  [ -f "$p/descriptors" ] || continue
  echo "$p/descriptors"
  od -An -v -tx1 "$p/descriptors"
done
''',
    "bindings.txt": '''
for p in /sys/bus/platform/devices/*usb* /sys/bus/platform/devices/*phy* /sys/bus/usb/devices/*; do
  [ -e "$p" ] || continue
  echo "$p"
  ls -ld "$p/driver" "$p/of_node"
  [ ! -f "$p/uevent" ] || cat "$p/uevent"
done
''',
    "i2c-bindings.txt": '''
ls -l /sys/bus/i2c/devices
for p in /sys/bus/i2c/devices/*-*; do
  [ -e "$p" ] || continue
  echo "$p"
  ls -ld "$p/driver" "$p/of_node"
  [ ! -f "$p/name" ] || cat "$p/name"
  [ ! -f "$p/modalias" ] || cat "$p/modalias"
done
ls -la /sys/bus/i2c/devices/7-0055/ /sys/bus/i2c/devices/7-0060/
''',
    "power-supply.txt": attributes(
        "/sys/class/power_supply/*",
        "type online present status scope usb_type real_type voltage_now current_now "
        "otg otg_online otg_enable otg_switch usb_otg typec_mode typec_power_role"),
    "regulators.txt": attributes(
        "/sys/class/regulator/*", "name state status microvolts num_users type"),
    "runtime-pm.txt": attributes(
        "/sys/bus/platform/devices/*usb*/power /sys/bus/platform/devices/*phy*/power "
        "/sys/bus/usb/devices/*/power", "control runtime_status runtime_usage"),
    "clock-summary.txt": "cat /sys/kernel/debug/clk/clk_summary",
    "regulator-summary.txt": "cat /sys/kernel/debug/regulator/regulator_summary",
    "gpio-summary.txt": "cat /sys/kernel/debug/gpio",
    "usb-debug-devices.txt": "cat /sys/kernel/debug/usb/devices",
    "debug-inventory.txt": "ls -l /sys/kernel/debug/usb /sys/kernel/debug/musb* "
                           "/sys/class/typec /sys/class/tcpc /sys/class/phy; cat /proc/mounts",
    "input-devices.txt": "cat /proc/bus/input/devices",
    "framework-usb.txt": "dumpsys usb",
    "modules.txt": "cat /proc/modules; ls /vendor/lib/modules /vendor_dlkm/lib/modules",
    "dmesg-after.txt": "dmesg",
    "role-after.txt": attributes(
        "/sys/class/udc/* /sys/class/usb_role/* /sys/class/typec/* /sys/class/extcon/*",
        "name state role data_role power_role port_type current_speed maximum_speed"),
}


def check_binary(name, data):
    if name == "runtime.dtb":
        return (len(data) >= 40 and data[:4] == b"\xd0\x0d\xfe\xed"
                and struct.unpack_from(">I", data, 4)[0] == len(data))
    if name == "kernel.config.gz":
        try:
            gzip.decompress(data)
            return data[:2] == b"\x1f\x8b"
        except (OSError, EOFError, zlib.error):
            return False
    return None


def command_errors(stdout, stderr):
    # Some Android su/adb combinations merge stderr into stdout and return 0
    # even for missing files. Do not mislabel those optional captures as OK.
    return re.findall(rb"(?m)^(?:cat|ls|od|sh|/system/bin/sh|dumpsys):[^\r\n]+",
                      stdout + b"\n" + stderr)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--serial", required=True, help="ADB serial or wireless IP:port")
    parser.add_argument("--state", required=True, choices=["device", "host", "disconnected"],
                        help="Physical setup label; actual role is read separately")
    parser.add_argument("--note", default="", help="Attached peripheral/OTG adapter and observed behavior")
    parser.add_argument("--out", required=True, type=Path, help="New output directory (also creates .zip)")
    args = parser.parse_args()
    adb = [args.adb, "-s", args.serial]

    def run(argv, timeout=30):
        return subprocess.run(adb + argv, capture_output=True, timeout=timeout)

    def root(command):
        return run(["exec-out", "su -c " + shlex.quote(command)])

    state = run(["get-state"], 10)
    if state.returncode or state.stdout.strip() != b"device":
        parser.exit(1, "Selected ADB device is not connected/authorized.\n")
    user = root("id")
    if user.returncode or b"uid=0" not in user.stdout:
        parser.exit(1, "Authorize root for ADB on the phone, then rerun.\n")
    device = root("getprop ro.product.device")
    if device.returncode or device.stdout.strip() != b"lancelot":
        parser.exit(1, "Selected device does not report ro.product.device=lancelot.\n")

    archive = args.out.with_name(args.out.name + ".zip")
    if archive.exists():
        parser.exit(1, "Output archive already exists; choose a new --out.\n")
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out / "root-id.txt").write_bytes(user.stdout + user.stderr)
    manifest = {"started_utc": datetime.now(timezone.utc).isoformat(),
                "serial": args.serial, "setup_label": args.state,
                "note": args.note, "commands": {}}
    for name, command in COMMANDS.items():
        try:
            result = root(command)
            (args.out / name).write_bytes(result.stdout)
            (args.out / (name + ".stderr.txt")).write_bytes(result.stderr)
            valid = check_binary(name, result.stdout)
            errors = command_errors(result.stdout, result.stderr)
            manifest["commands"][name] = {
                "command": command, "returncode": result.returncode,
                "bytes": len(result.stdout), "stderr_bytes": len(result.stderr),
                "sha256": hashlib.sha256(result.stdout).hexdigest(),
                "binary_valid": valid,
                "command_errors": [line.decode(errors="replace") for line in errors],
            }
            ok = result.returncode == 0 and valid is not False and not result.stderr and not errors
            print(name, "OK" if ok else "partial/unavailable", len(result.stdout), "bytes")
        except subprocess.TimeoutExpired:
            manifest["commands"][name] = {"command": command, "error": "timeout"}
            print(name, "timeout")
    manifest["finished_utc"] = datetime.now(timezone.utc).isoformat()
    (args.out / "manifest.json").write_text(json.dumps(manifest, indent=2), encoding="utf-8")
    with zipfile.ZipFile(archive, "x", zipfile.ZIP_DEFLATED) as output:
        for path in sorted(args.out.iterdir()):
            output.write(path, args.out.name + "/" + path.name)
    print("Capture:", archive.resolve())


if __name__ == "__main__":
    main()
