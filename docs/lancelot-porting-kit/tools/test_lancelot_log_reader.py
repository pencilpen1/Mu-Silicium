#!/usr/bin/env python3
"""Host reader framing, reconnect/cursor handling and CLI process integration."""
from contextlib import redirect_stderr
import io
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

from read_lancelot_log import (
    Batch, Capture, ProtocolError, UnsupportedFirmware, fetch_batch, parse_reply,
)


def reply(payload, boot=1, start=0, advertised=None):
    end = start + (len(payload) if advertised is None else advertised)
    lines = [f"(bootloader) LLOG1 {boot:016x} {start:016x} {end:016x}\n"]
    lines += ["(bootloader) LLOGD " + payload[i:i+24].hex() + "\n"
              for i in range(0, len(payload), 24)]
    return "".join(lines).encode() + b"OKAY [ 0.010s]\nFinished. Total time: 0.011s\n"


class LogReaderTest(unittest.TestCase):
    def test_binary_framing_and_invalid_responses(self):
        payload = bytes(range(256))
        self.assertEqual(parse_reply(reply(payload)).payload, payload)
        self.assertEqual(parse_reply(reply(b"a\r\nb\0c")).payload, b"a\r\nb\0c")
        for bad in (
            b"OKAY\n", reply(b"a", advertised=2), reply(b"ab", advertised=1),
            reply(b"", advertised=4097), reply(b"a") + reply(b"b"),
            b"(bootloader) LLOGD 01\n" + reply(b""),
            reply(b"a").replace(b"LLOGD 61", b"LLOGD 6z"),
        ):
            with self.subTest(bad=bad[:90]), self.assertRaises(ProtocolError):
                parse_reply(bad)

    def test_partial_transfer_does_not_advance_past_received_bytes(self):
        output = reply(b"abc", advertised=100).split(b"OKAY")[0]
        batch = parse_reply(output + b"(bootloader) LLOGD 123", partial=True)
        self.assertEqual(batch.payload, b"abc")
        self.assertEqual(batch.next_cursor, 3)
        with patch("read_lancelot_log.subprocess.run", side_effect=
                   subprocess.TimeoutExpired(["fastboot"], 1, output=output)):
            batch, warning = fetch_batch("fastboot", "lancelot-uefi", 0, 1)
        self.assertEqual(batch.next_cursor, 3)
        self.assertIsNotNone(warning)

    def test_reconnect_overlap_loss_and_new_boot(self):
        output = io.BytesIO()
        capture = Capture(output, echo=False)
        with redirect_stderr(io.StringIO()):
            self.assertTrue(capture.consume(Batch(1, 0, 3, b"SEC")))
            # Retry/reconnect can overlap earlier bytes: append only new bytes.
            capture.consume(Batch(1, 1, 6, b"ECDXE"))
            self.assertEqual(capture.cursor, 6)
            self.assertTrue(output.getvalue().endswith(b"SECDXE"))
            capture.consume(Batch(1, 10, 13, b"BDS"))
            self.assertIn(b"LOST 4 bytes", output.getvalue())
            self.assertEqual(capture.cursor, 13)
            # Old cursor happens to fit inside the new log: request zero first.
            self.assertFalse(capture.consume(Batch(2, 13, 17, b"tail")))
            self.assertEqual(capture.cursor, 0)
            self.assertNotIn(b"tail", output.getvalue())
            capture.consume(Batch(2, 0, 4, b"S01\n"))
            self.assertTrue(output.getvalue().endswith(b"S01\n"))

    def test_command_selection_and_old_firmware(self):
        with patch("read_lancelot_log.subprocess.run", return_value=
                   subprocess.CompletedProcess([], 0, reply(b"ok"))) as run:
            batch, warning = fetch_batch("/opt/android platform-tools/fastboot", "unit-2", 17, 2)
        self.assertEqual(run.call_args.args[0],
                         ["/opt/android platform-tools/fastboot", "-s", "unit-2", "oem", "log", "0000000000000011"])
        self.assertEqual(batch.payload, b"ok")
        self.assertIsNone(warning)
        with patch("read_lancelot_log.subprocess.run", return_value=
                   subprocess.CompletedProcess([], 1, b"FAILED (remote: 'diagnostic getvar only')")):
            with self.assertRaises(UnsupportedFirmware):
                fetch_batch("fastboot", "unit-2", 0, 1)

    def test_linux_permission_failures_are_not_retried_as_disconnects(self):
        message = b"no permissions (user in plugdev group; are your udev rules wrong?)\n"
        for result, error in (
            (subprocess.CompletedProcess([], 1, message), None),
            (None, subprocess.TimeoutExpired(["fastboot"], 1, output=message)),
        ):
            with self.subTest(timeout=error is not None), patch(
                "read_lancelot_log.subprocess.run", return_value=result, side_effect=error
            ):
                with self.assertRaisesRegex(PermissionError, "udev"):
                    fetch_batch("fastboot", "lancelot-uefi", 0, 1)

    def test_cli_drains_multiple_snapshots(self):
        with tempfile.TemporaryDirectory(prefix="log-reader-test-", dir="/tmp/opencode") as tmp:
            tmp = Path(tmp)
            fake = tmp / "fake fastboot"
            # Fake only the PC executable/USB boundary. Exercise real argument
            # construction, process I/O, parsing, cursor progression and file I/O.
            fake.write_text(f"#!{sys.executable}\n" + r'''
import sys
assert sys.argv[1:5] == ['-s', 'lancelot-uefi', 'oem', 'log']
start = int(sys.argv[5], 16)
payload = (b'A' * 4096 + b'END\r\n\0')[start:start+4096]
print(f'(bootloader) LLOG1 {7:016x} {start:016x} {start+len(payload):016x}', file=sys.stderr)
for i in range(0, len(payload), 24):
    print('(bootloader) LLOGD ' + payload[i:i+24].hex(), file=sys.stderr)
print('OKAY [ 0.010s]', file=sys.stderr)
''')
            fake.chmod(0o755)
            output = tmp / "boot.log"
            output.write_bytes(b"existing\n")
            result = subprocess.run(
                [sys.executable, str(Path(__file__).with_name("read_lancelot_log.py")),
                 "--fastboot", str(fake), "--once", "--quiet", "-o", str(output)],
                capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 0, result.stderr)
            data = output.read_bytes()
            self.assertTrue(data.startswith(b"existing\n"))
            self.assertTrue(data.endswith(b"A" * 4096 + b"END\r\n\0"))


if __name__ == "__main__":
    unittest.main()
