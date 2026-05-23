#!/usr/bin/env python3
"""Hardware-in-the-loop UART tester for GloveAssist.

The firmware uses the shared 12-byte frame format from spi_protocol.c:
SOF, TYPE, SEQ, 8-byte XOR-obfuscated payload, CRC-8/CCITT.
"""

from __future__ import annotations

import argparse
import csv
import struct
import sys
import time
from dataclasses import dataclass

try:
    import serial
except ImportError:  # pragma: no cover - friendly CLI error
    serial = None


PROTO_SOF = 0xAA
PROTO_PAYLOAD_SIZE = 8
PROTO_FRAME_SIZE = 12
PROTO_XOR_KEY_BASE = 0x5A

MSG_TYPE_POLL = 0x00
MSG_TYPE_SENSOR_DATA = 0x01
MSG_TYPE_GESTURE = 0x02
MSG_TYPE_ACK = 0x03
MSG_TYPE_STATUS = 0x04
MSG_TYPE_CALIBRATION = 0x05
MSG_TYPE_COMMAND = 0x06
MSG_TYPE_HEARTBEAT = 0x07
MSG_TYPE_RAW_ADC = 0x08
MSG_TYPE_HONEYPOT = 0xFF

CMD_CALIBRATE = 0x01
CMD_SET_THRESH = 0x02
CMD_RESET = 0x03

FINGERS = {
    "index": 0,
    "middle": 1,
    "ring": 2,
    "pinky": 3,
}

GESTURES = {
    0: "NONE",
    1: "INDEX",
    2: "MIDDLE",
    3: "RING",
    4: "PINKY",
    5: "FIST",
    6: "HELP",
}

MSG_NAMES = {
    MSG_TYPE_POLL: "POLL",
    MSG_TYPE_SENSOR_DATA: "SENSOR_DATA",
    MSG_TYPE_GESTURE: "GESTURE",
    MSG_TYPE_ACK: "ACK",
    MSG_TYPE_STATUS: "STATUS",
    MSG_TYPE_CALIBRATION: "CALIBRATION",
    MSG_TYPE_COMMAND: "COMMAND",
    MSG_TYPE_HEARTBEAT: "HEARTBEAT",
    MSG_TYPE_RAW_ADC: "RAW_ADC",
    MSG_TYPE_HONEYPOT: "HONEYPOT",
}


@dataclass
class Frame:
    msg_type: int
    seq: int
    payload: bytes
    crc8: int


class ProtocolError(Exception):
    """Raised when a frame fails structural or CRC validation."""


class FrameBuilder:
    def __init__(self) -> None:
        self.seq = 0

    def build(self, msg_type: int, payload: bytes = b"") -> bytes:
        payload = payload[:PROTO_PAYLOAD_SIZE].ljust(PROTO_PAYLOAD_SIZE, b"\x00")
        seq = self.seq & 0xFF
        self.seq = (self.seq + 1) & 0xFF
        crc = crc8_ccitt(bytes([msg_type, seq]) + payload)
        key = seq ^ PROTO_XOR_KEY_BASE
        encoded = bytes(byte ^ key for byte in payload)
        return bytes([PROTO_SOF, msg_type, seq]) + encoded + bytes([crc])


def crc8_ccitt(data: bytes) -> int:
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            if crc & 0x80:
                crc = ((crc << 1) ^ 0x07) & 0xFF
            else:
                crc = (crc << 1) & 0xFF
    return crc


def decode_frame(raw: bytes) -> Frame:
    if len(raw) != PROTO_FRAME_SIZE:
        raise ProtocolError(f"bad length: {len(raw)}")
    if raw[0] != PROTO_SOF:
        raise ProtocolError(f"bad SOF: 0x{raw[0]:02X}")

    msg_type = raw[1]
    seq = raw[2]
    key = seq ^ PROTO_XOR_KEY_BASE
    payload = bytes(byte ^ key for byte in raw[3:11])
    crc = raw[11]
    expected = crc8_ccitt(bytes([msg_type, seq]) + payload)
    if crc != expected:
        raise ProtocolError(f"CRC mismatch rx=0x{crc:02X} calc=0x{expected:02X}")
    return Frame(msg_type=msg_type, seq=seq, payload=payload, crc8=crc)


def read_frame(port, deadline: float) -> Frame | None:
    buf = bytearray()
    while time.monotonic() < deadline:
        byte = port.read(1)
        if not byte:
            continue
        value = byte[0]
        if not buf and value != PROTO_SOF:
            continue
        buf.append(value)
        if len(buf) == PROTO_FRAME_SIZE:
            try:
                return decode_frame(bytes(buf))
            except ProtocolError as exc:
                print(f"bad frame: {exc}", file=sys.stderr)
                buf.clear()
    return None


def describe_frame(frame: Frame) -> str:
    name = MSG_NAMES.get(frame.msg_type, f"0x{frame.msg_type:02X}")
    base = f"{name:<10} seq={frame.seq:3d} payload={frame.payload.hex(' ')}"

    if frame.msg_type == MSG_TYPE_GESTURE:
        flex = list(frame.payload[:4])
        gesture_id = frame.payload[4]
        conf = frame.payload[5]
        gesture = GESTURES.get(gesture_id, f"UNKNOWN({gesture_id})")
        return f"{base} flex={flex} gesture={gesture} conf={conf}%"

    if frame.msg_type == MSG_TYPE_RAW_ADC:
        raw = struct.unpack("<4H", frame.payload)
        return f"{base} raw={raw}"

    if frame.msg_type == MSG_TYPE_HEARTBEAT:
        status, ble_connected, rssi = frame.payload[:3]
        return f"{base} status=0x{status:02X} ble={ble_connected} rssi={rssi}"

    if frame.msg_type == MSG_TYPE_COMMAND:
        cmd_id, finger, value = unpack_command(frame.payload)
        return f"{base} cmd=0x{cmd_id:02X} finger={finger} value={value}"

    if frame.msg_type == MSG_TYPE_ACK:
        return f"{base} ack_status=0x{frame.payload[0]:02X}"

    return base


def unpack_command(payload: bytes) -> tuple[int, int, int]:
    cmd_id = payload[0]
    finger = payload[1]
    value = payload[2] | (payload[3] << 8)
    return cmd_id, finger, value


def make_command_payload(args) -> bytes | None:
    if args.cmd is None:
        return None
    if args.cmd == "calibrate":
        return bytes([CMD_CALIBRATE, 0, 0, 0]) + b"\x00" * 4
    if args.cmd == "reset":
        return bytes([CMD_RESET, 0, 0, 0]) + b"\x00" * 4
    if args.cmd == "set-thresh":
        finger = FINGERS[args.finger]
        return bytes([CMD_SET_THRESH, finger, args.value & 0xFF, args.value >> 8]) + b"\x00" * 4
    raise ValueError(f"unknown command: {args.cmd}")


def send_heartbeat(port, builder: FrameBuilder, ble_connected: int = 0) -> None:
    payload = bytes([0x01, ble_connected & 0x01, 0]) + b"\x00" * 5
    port.write(builder.build(MSG_TYPE_HEARTBEAT, payload))


def send_gesture(port, builder: FrameBuilder, gesture_id: int, confidence: int) -> None:
    payload = bytes([0, 0, 0, 0, gesture_id & 0xFF, confidence & 0xFF, 0, 0])
    port.write(builder.build(MSG_TYPE_GESTURE, payload))


def send_raw_adc(port, builder: FrameBuilder, values: list[int]) -> None:
    port.write(builder.build(MSG_TYPE_RAW_ADC, struct.pack("<4H", *values)))


def run_tap(args) -> int:
    rows = []
    start = time.monotonic()
    last_ts = None

    with open_serial(args) as port:
        print(f"tap: reading {args.port} at {args.baud} for {args.duration}s")
        deadline = time.monotonic() + args.duration
        count = 0
        while time.monotonic() < deadline:
            frame = read_frame(port, time.monotonic() + 0.5)
            if frame is not None:
                ts = time.monotonic() - start
                delta = None if last_ts is None else ts - last_ts
                last_ts = ts
                count += 1
                line = describe_frame(frame)
                if args.timestamps:
                    delta_txt = "" if delta is None else f" dt={delta * 1000:.1f}ms"
                    line = f"t={ts:.3f}s{delta_txt} {line}"
                print(line)
                rows.append(frame_row(ts, delta, frame))
        print(f"tap: decoded {count} valid frame(s)")
        if args.csv:
            write_csv(args.csv, rows)
        return 0 if count > 0 or not args.require_frames else 1


def run_stm32(args) -> int:
    builder = FrameBuilder()
    cmd_payload = make_command_payload(args)
    command_sent = False
    bad_crc_sent = False
    saw_gesture = False
    saw_ack = False
    rows = []
    start = time.monotonic()
    last_rx_ts = None

    with open_serial(args) as port:
        print(f"stm32: simulating ESP32 on {args.port} at {args.baud}")
        deadline = time.monotonic() + args.duration
        next_hb = 0.0

        while time.monotonic() < deadline:
            now = time.monotonic()
            heartbeat_allowed = (
                args.drop_heartbeat_after is None
                or (now - start) < args.drop_heartbeat_after
            )
            if heartbeat_allowed and now >= next_hb:
                send_heartbeat(port, builder, ble_connected=0)
                next_hb = now + args.heartbeat_interval

            if (args.drop_heartbeat_after is not None
                    and (now - start) >= args.drop_heartbeat_after
                    and next_hb != float("inf")):
                print("heartbeat stopped: STM32 should enter local alarm after timeout")
                next_hb = float("inf")

            if cmd_payload is not None and not command_sent and now >= deadline - args.duration + 1.0:
                wire = builder.build(MSG_TYPE_COMMAND, cmd_payload)
                if args.bad_command_crc:
                    wire = wire[:-1] + bytes([wire[-1] ^ 0xFF])
                    print(f"sent command with bad CRC: {args.cmd}")
                    bad_crc_sent = True
                else:
                    print(f"sent command: {args.cmd}")
                port.write(wire)
                command_sent = True

            frame = read_frame(port, time.monotonic() + 0.1)
            if frame is None:
                continue
            ts = time.monotonic() - start
            delta = None if last_rx_ts is None else ts - last_rx_ts
            last_rx_ts = ts
            print(timestamped_description(args, ts, delta, frame))
            rows.append(frame_row(ts, delta, frame))
            if frame.msg_type == MSG_TYPE_GESTURE:
                saw_gesture = True
            if frame.msg_type == MSG_TYPE_ACK:
                saw_ack = True

    if args.cmd is not None and not args.bad_command_crc and not saw_ack:
        print("FAIL: command was sent but no ACK was received", file=sys.stderr)
        return 1
    if bad_crc_sent and saw_ack:
        print("FAIL: bad-CRC command unexpectedly received ACK", file=sys.stderr)
        return 1
    if args.drop_heartbeat_after is not None:
        print("manual check: STM32 OLED/motor/buzzer should show local alarm")
    if args.require_frames and not saw_gesture:
        print("FAIL: no STM32 gesture frame received", file=sys.stderr)
        return 1
    if args.csv:
        write_csv(args.csv, rows)
    print("stm32: PASS")
    return 0


def run_esp32(args) -> int:
    builder = FrameBuilder()
    saw_heartbeat = False
    rows = []
    start = time.monotonic()
    last_rx_ts = None

    with open_serial(args) as port:
        print(f"esp32: simulating STM32 on {args.port} at {args.baud}")
        deadline = time.monotonic() + args.duration
        next_tx = 0.0

        while time.monotonic() < deadline:
            now = time.monotonic()
            if now >= next_tx:
                if args.raw_adc is not None:
                    send_raw_adc(port, builder, args.raw_adc)
                    print(f"sent RAW_ADC: {args.raw_adc}")
                else:
                    send_gesture(port, builder, args.gesture, args.confidence)
                    print(f"sent gesture: {GESTURES.get(args.gesture, args.gesture)}")
                next_tx = now + args.tx_interval

            frame = read_frame(port, time.monotonic() + 0.1)
            if frame is None:
                continue
            ts = time.monotonic() - start
            delta = None if last_rx_ts is None else ts - last_rx_ts
            last_rx_ts = ts
            print(timestamped_description(args, ts, delta, frame))
            rows.append(frame_row(ts, delta, frame))
            if frame.msg_type == MSG_TYPE_HEARTBEAT:
                saw_heartbeat = True

    if args.csv:
        write_csv(args.csv, rows)
    if args.require_frames and not saw_heartbeat:
        print("FAIL: no ESP32 heartbeat received", file=sys.stderr)
        return 1
    print("esp32: PASS")
    return 0


def timestamped_description(args, ts: float, delta: float | None, frame: Frame) -> str:
    line = describe_frame(frame)
    if args.timestamps:
        delta_txt = "" if delta is None else f" dt={delta * 1000:.1f}ms"
        line = f"t={ts:.3f}s{delta_txt} {line}"
    return line


def frame_row(ts: float, delta: float | None, frame: Frame) -> dict[str, str | int | float]:
    row: dict[str, str | int | float] = {
        "time_s": f"{ts:.6f}",
        "delta_ms": "" if delta is None else f"{delta * 1000.0:.3f}",
        "type": MSG_NAMES.get(frame.msg_type, f"0x{frame.msg_type:02X}"),
        "type_hex": f"0x{frame.msg_type:02X}",
        "seq": frame.seq,
        "payload_hex": frame.payload.hex(),
    }

    if frame.msg_type == MSG_TYPE_GESTURE:
        row["gesture_id"] = frame.payload[4]
        row["gesture_name"] = GESTURES.get(frame.payload[4], "UNKNOWN")
        row["confidence"] = frame.payload[5]
    elif frame.msg_type == MSG_TYPE_RAW_ADC:
        raw = struct.unpack("<4H", frame.payload)
        row["raw_index"] = raw[0]
        row["raw_middle"] = raw[1]
        row["raw_ring"] = raw[2]
        row["raw_pinky"] = raw[3]
    elif frame.msg_type == MSG_TYPE_HEARTBEAT:
        row["heartbeat_status"] = frame.payload[0]
        row["ble_connected"] = frame.payload[1]

    return row


def write_csv(path: str, rows: list[dict[str, str | int | float]]) -> None:
    if not rows:
        print(f"csv: no rows to write to {path}")
        return

    fields: list[str] = []
    for row in rows:
        for key in row:
            if key not in fields:
                fields.append(key)

    with open(path, "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)
    print(f"csv: wrote {len(rows)} row(s) to {path}")


def open_serial(args):
    if serial is None:
        print("pyserial is missing. Install with: python -m pip install pyserial", file=sys.stderr)
        sys.exit(2)
    return serial.Serial(args.port, args.baud, timeout=args.timeout)


def add_serial_args(parser) -> None:
    parser.add_argument("--port", required=True, help="Serial port, e.g. COM7")
    parser.add_argument("--baud", type=int, default=115200, help="UART baud rate")
    parser.add_argument("--timeout", type=float, default=0.05, help="Serial read timeout")
    parser.add_argument("--duration", type=float, default=10.0, help="Test duration in seconds")
    parser.add_argument("--require-frames", action="store_true", help="Fail if expected frames are absent")
    parser.add_argument("--timestamps", action="store_true", help="Print receive timestamps and inter-frame delta")
    parser.add_argument("--csv", help="Write decoded frames to CSV")


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="mode", required=True)

    tap = sub.add_parser("tap", help="Passive decode of one UART direction")
    add_serial_args(tap)
    tap.set_defaults(func=run_tap)

    stm32 = sub.add_parser("stm32", help="Test STM32 by simulating ESP32")
    add_serial_args(stm32)
    stm32.add_argument("--heartbeat-interval", type=float, default=0.5)
    stm32.add_argument(
        "--drop-heartbeat-after",
        type=float,
        help="Stop sending heartbeat after N seconds; STM32 should enter local alarm",
    )
    stm32.add_argument("--cmd", choices=["calibrate", "reset", "set-thresh"])
    stm32.add_argument("--finger", choices=sorted(FINGERS), default="index")
    stm32.add_argument("--value", type=int, default=800)
    stm32.add_argument(
        "--bad-command-crc",
        action="store_true",
        help="Corrupt the command CRC; STM32 should reject it and not ACK",
    )
    stm32.set_defaults(func=run_stm32)

    esp32 = sub.add_parser("esp32", help="Test ESP32 by simulating STM32")
    add_serial_args(esp32)
    esp32.add_argument("--tx-interval", type=float, default=1.0)
    esp32.add_argument("--gesture", type=int, default=6, choices=sorted(GESTURES))
    esp32.add_argument("--confidence", type=int, default=90)
    esp32.add_argument("--raw-adc", type=int, nargs=4, metavar=("I", "M", "R", "P"))
    esp32.set_defaults(func=run_esp32)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
