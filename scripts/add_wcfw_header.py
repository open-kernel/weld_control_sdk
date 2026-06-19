#!/usr/bin/env python3
"""Add a WCFW header to a firmware payload.

The file is intentionally simple:
  - 64-byte metadata header for humans/tools to inspect.
  - firmware payload bytes, either from an input file or deterministic mock data.

The app reads the header, then sends only the payload over OTA. The payload
size and CRC fields are the values used by the OTA protocol.
"""

from __future__ import annotations

import argparse
import json
import struct
import time
import zlib
from pathlib import Path


MAGIC = b"WCFW"
HEADER_VERSION = 1
HEADER_SIZE = 64
DEFAULT_COMPANY_ID = 0xAAAA
FW_TYPE_BLE = 0x01
FW_TYPE_UART = 0x02
FW_TYPE_BY_NAME = {
    "ble": FW_TYPE_BLE,
    "uart": FW_TYPE_UART,
}


def crc16_ccitt_false(data: bytes) -> int:
    crc = 0xFFFF
    for value in data:
        crc ^= value << 8
        for _ in range(8):
            if crc & 0x8000:
                crc = ((crc << 1) ^ 0x1021) & 0xFFFF
            else:
                crc = (crc << 1) & 0xFFFF
    return crc & 0xFFFF


def parse_version(value: str) -> tuple[int, int, int]:
    parts = value.strip().lstrip("v").split(".")
    if len(parts) != 3:
        raise argparse.ArgumentTypeError("version must look like 1.2.3")
    try:
        major, minor, patch = (int(part) for part in parts)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("version parts must be integers") from exc
    for part in (major, minor, patch):
        if part < 0 or part > 255:
            raise argparse.ArgumentTypeError("version parts must be in 0..255")
    return major, minor, patch


def parse_int(value: str) -> int:
    return int(value, 0)


def build_payload(size: int, seed: int) -> bytes:
    if size <= 0:
        raise ValueError("payload size must be positive")
    state = seed & 0xFFFFFFFF
    data = bytearray(size)
    for index in range(size):
        state = (1664525 * state + 1013904223 + index) & 0xFFFFFFFF
        data[index] = (state >> 16) & 0xFF
    return bytes(data)


def build_header(
    *,
    company_id: int,
    fw_type: int,
    version: tuple[int, int, int],
    build_id: int,
    payload: bytes,
) -> bytes:
    payload_crc16 = crc16_ccitt_false(payload)
    payload_crc32 = zlib.crc32(payload) & 0xFFFFFFFF

    header = bytearray(HEADER_SIZE)
    struct.pack_into(
        "<4sBBBBBHIIIHH",
        header,
        0,
        MAGIC,
        HEADER_VERSION,
        fw_type & 0xFF,
        version[0] & 0xFF,
        version[1] & 0xFF,
        version[2] & 0xFF,
        company_id & 0xFFFF,
        build_id & 0xFFFFFFFF,
        len(payload) & 0xFFFFFFFF,
        payload_crc32,
        payload_crc16,
        HEADER_SIZE,
    )
    header_crc16 = crc16_ccitt_false(bytes(header[:HEADER_SIZE - 2]))
    struct.pack_into("<H", header, HEADER_SIZE - 2, header_crc16)
    return bytes(header)


def default_output_path(version: tuple[int, int, int], fw_type_name: str, build_id: int, mode: str) -> Path:
    filename = f"{mode}_firmware_{fw_type_name}_v{version[0]}.{version[1]}.{version[2]}_{build_id:08x}.bin"
    return Path(__file__).resolve().parents[1] / "generated" / filename


def write_manifest(path: Path, metadata: dict[str, object]) -> None:
    manifest_path = path.with_suffix(path.suffix + ".json")
    manifest_path.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> None:
    parser = argparse.ArgumentParser(description="Add a WeldControl WCFW header to a firmware payload.")
    source_group = parser.add_mutually_exclusive_group(required=True)
    source_group.add_argument("--input", type=Path, default=None, help="Input firmware payload file to wrap with a WCFW header.")
    source_group.add_argument("--mock", action="store_true", help="Generate deterministic mock payload data instead of reading --input.")
    parser.add_argument("--company-id", type=parse_int, default=DEFAULT_COMPANY_ID, help="Company ID, decimal or hex. Default: 0xAAAA")
    parser.add_argument("--type", choices=sorted(FW_TYPE_BY_NAME), default="ble", help="Firmware type.")
    parser.add_argument("--version", type=parse_version, default=parse_version("1.0.0"), help="Firmware version, e.g. 1.2.3.")
    parser.add_argument("--build-id", type=parse_int, default=int(time.time()), help="Build ID, decimal or hex. Default: current timestamp.")
    parser.add_argument("--payload-size", type=parse_int, default=32 * 1024, help="Mock payload bytes after the 64-byte header. Only used with --mock.")
    parser.add_argument("--seed", type=parse_int, default=0x20260605, help="Payload generator seed, decimal or hex.")
    parser.add_argument("--output", type=Path, default=None, help="Output firmware image path.")
    parser.add_argument("--no-manifest", action="store_true", help="Do not write the adjacent JSON manifest.")
    args = parser.parse_args()

    fw_type = FW_TYPE_BY_NAME[args.type]
    mode = "mock" if args.mock else "wrapped"
    output = args.output or default_output_path(args.version, args.type, args.build_id, mode)
    output.parent.mkdir(parents=True, exist_ok=True)

    if args.mock:
        payload = build_payload(args.payload_size, args.seed)
        source_path = None
    else:
        if not args.input or not args.input.is_file():
            raise FileNotFoundError(f"input firmware file not found: {args.input}")
        payload = args.input.read_bytes()
        if not payload:
            raise ValueError("input firmware file is empty")
        source_path = str(args.input)

    header = build_header(
        company_id=args.company_id,
        fw_type=fw_type,
        version=args.version,
        build_id=args.build_id,
        payload=payload,
    )
    image = header + payload
    image_crc16 = crc16_ccitt_false(image)

    output.write_bytes(image)
    metadata = {
        "path": str(output),
        "magic": MAGIC.decode("ascii"),
        "mode": mode,
        "source_path": source_path,
        "header_size": HEADER_SIZE,
        "company_id": f"0x{args.company_id & 0xFFFF:04X}",
        "firmware_type": args.type,
        "firmware_type_code": fw_type,
        "version": ".".join(str(part) for part in args.version),
        "build_id": args.build_id,
        "payload_size": len(payload),
        "file_size": len(image),
        "payload_crc16": f"0x{crc16_ccitt_false(payload):04X}",
        "file_crc16": f"0x{image_crc16:04X}",
        "file_crc32": f"0x{zlib.crc32(image) & 0xFFFFFFFF:08X}",
    }
    if not args.no_manifest:
        write_manifest(output, metadata)

    print(json.dumps(metadata, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
