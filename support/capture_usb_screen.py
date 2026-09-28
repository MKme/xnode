"""Capture the visible T-Deck Plus or Ultra LVGL frame over its existing USB serial port.

No device settings, filesystem or navigation are changed. Review the resulting
image for private information before sharing it. Requires existing pyserial/Pillow.
"""
import argparse
import re
import time
from pathlib import Path

import serial
from PIL import Image


def capture(port, output, timeout=45):
    if output.exists():
        raise FileExistsError(f"Refusing to overwrite {output}")
    connection = serial.Serial()
    connection.port = port
    connection.baudrate = 115200
    connection.timeout = 1
    connection.dtr = False
    connection.rts = False
    connection.open()
    try:
        connection.reset_input_buffer()
        connection.write(b"XNODE SCREENSHOT\n")
        deadline = time.monotonic() + timeout
        rows = {}
        started = False
        pending = bytearray()
        while time.monotonic() < deadline:
            if b"\n" not in pending:
                pending.extend(connection.read(max(1, connection.in_waiting)))
                if b"\n" not in pending:
                    continue
            boundary = pending.index(b"\n") + 1
            line = bytes(pending[:boundary])
            del pending[:boundary]
            begin = re.search(rb"XNODE_SCREEN_BEGIN (\d+) (\d+) RGB888", line)
            if begin:
                width, height = int(begin[1]), int(begin[2])
                if (width, height) not in [(320, 240), (362, 440)]:
                    raise RuntimeError("Unsupported framebuffer size")
                started = True
                rows.clear()
            elif started:
                match = re.search(rb"XNODE_SCREEN_ROW (\d+) ([0-9a-f]+)\r?\n", line)
                if match:
                    index = int(match[1])
                    if 0 <= index < height and len(match[2]) == width * 6:
                        rows[index] = bytes.fromhex(match[2].decode("ascii"))
                if b"XNODE_SCREEN_END" in line:
                    if len(rows) != height:
                        raise RuntimeError(f"Incomplete capture: {len(rows)}/{height} rows")
                    output.parent.mkdir(parents=True, exist_ok=True)
                    Image.frombytes("RGB", (width, height), b"".join(rows[i] for i in range(height))).save(output)
                    print(f"Captured device framebuffer: {output} ({width}x{height})")
                    return
            if b"XNODE_SCREEN_ERROR" in line:
                raise RuntimeError("Device could not capture its visible frame")
        raise TimeoutError("No complete XNODE screen received; ensure the device is awake and running capture-capable firmware")
    finally:
        connection.close()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    capture(args.port, args.output)
