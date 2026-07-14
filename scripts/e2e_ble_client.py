#!/usr/bin/env python3
"""Optional BLE client check for GloveAssist hardware E2E.

Exit codes:
  0 - BLE scan/connect/notify succeeded
  1 - BLE check ran but failed
  2 - optional Python dependency is missing
"""

from __future__ import annotations

import argparse
import asyncio
import sys
from pathlib import Path


SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"
RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"
TX_UUID = "6e400003-b5a3-f393-e0a9-e50e24dcca9e"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Check GloveAssist BLE service and notifications.")
    parser.add_argument("--name", default="GloveAssist", help="BLE device name to scan for.")
    parser.add_argument("--timeout", type=float, default=35.0, help="Scan timeout in seconds.")
    parser.add_argument("--notify-timeout", type=float, default=8.0, help="Notification wait timeout.")
    parser.add_argument("--log", help="Path to write BLE client log.")
    return parser.parse_args()


class Logger:
    def __init__(self, path: str | None) -> None:
        self.path = Path(path) if path else None
        if self.path:
            self.path.parent.mkdir(parents=True, exist_ok=True)
            self.path.write_text("", encoding="utf-8")

    def write(self, message: str) -> None:
        print(message, flush=True)
        if self.path:
            with self.path.open("a", encoding="utf-8") as handle:
                handle.write(message + "\n")


async def run_check(args: argparse.Namespace, log: Logger) -> int:
    try:
        from bleak import BleakClient, BleakScanner
    except ImportError:
        log.write("Python package 'bleak' is not installed.")
        return 2

    log.write(f"Scanning for BLE device named '{args.name}' for {args.timeout:.0f}s...")
    devices = await BleakScanner.discover(timeout=args.timeout)
    device = None
    for candidate in devices:
        log.write(f"Seen BLE device: name={candidate.name!r} address={candidate.address}")
        if candidate.name == args.name:
            device = candidate
            break

    if device is None:
        log.write(f"FAIL: BLE device '{args.name}' not found.")
        return 1

    notifications: list[bytes] = []

    def on_notify(_sender: object, data: bytearray) -> None:
        payload = bytes(data)
        notifications.append(payload)
        try:
            text = payload.decode("utf-8", errors="replace")
        except Exception:
            text = repr(payload)
        log.write(f"BLE notify TX: {text!r}")

    log.write(f"Connecting to {device.address}...")
    async with BleakClient(device) as client:
        if not client.is_connected:
            log.write("FAIL: BLE client did not connect.")
            return 1

        services = client.services
        service_uuids = {str(service.uuid).lower() for service in services}
        if SERVICE_UUID not in service_uuids:
            log.write(f"FAIL: NUS service {SERVICE_UUID} not found.")
            return 1

        char_uuids = {
            str(char.uuid).lower()
            for service in services
            for char in service.characteristics
        }
        if TX_UUID not in char_uuids or RX_UUID not in char_uuids:
            log.write("FAIL: NUS TX/RX characteristics not found.")
            return 1

        try:
            value = await client.read_gatt_char(TX_UUID)
            log.write(f"BLE read TX: {bytes(value).decode('utf-8', errors='replace')!r}")
        except Exception as exc:
            log.write(f"BLE read TX failed, continuing to notify check: {exc}")

        await client.start_notify(TX_UUID, on_notify)
        try:
            deadline = asyncio.get_running_loop().time() + args.notify_timeout
            while not notifications and asyncio.get_running_loop().time() < deadline:
                await asyncio.sleep(0.2)
        finally:
            await client.stop_notify(TX_UUID)

    if not notifications:
        log.write("FAIL: no BLE TX notification received.")
        return 1

    log.write("PASS: BLE service and TX notification verified.")
    return 0


def main() -> int:
    args = parse_args()
    log = Logger(args.log)
    return asyncio.run(run_check(args, log))


if __name__ == "__main__":
    sys.exit(main())
