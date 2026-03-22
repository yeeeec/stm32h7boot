#!/usr/bin/env python3
"""Generate and patch the single firmware metadata struct."""

from __future__ import annotations

import argparse
import pathlib
import re
import struct
import subprocess
import sys
from datetime import datetime


STRING_FIELD_SPECS = (
    ("magic", 4, "PROJECT_MAGIC_HEAD"),
    ("project_name", 32, "PROJECT_NAME_STR"),
    ("image_tag", 4, "PROJECT_IMAGE_TAG"),
    ("git_hash", 8, "PROJECT_GIT_HASH"),
    ("build_time", 20, "PROJECT_BUILD_TIME"),
)
NUMERIC_FIELD_MACROS = (
    ("raw_bin_crc32", "PROJECT_RAW_BIN_CRC32"),
    ("write_address", "PROJECT_WRITE_ADDRESS"),
    ("valid_bin_size", "PROJECT_VALID_BIN_SIZE"),
)
RESERVED_SIZE = 16
STRUCT_SIZE = 96
CRC_FIELD_OFFSET = sum(field_size for _, field_size, _ in STRING_FIELD_SPECS)
WRITE_ADDRESS_OFFSET = CRC_FIELD_OFFSET + 4
VALID_BIN_SIZE_OFFSET = WRITE_ADDRESS_OFFSET + 4
DEFAULT_MAGIC_HEAD = "FWVH"
DEFAULT_IMAGE_TAG = "FWPK"
TARGET_SYMBOL = "g_fw_version_info"


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Generate and patch the firmware metadata struct"
    )
    subparsers = parser.add_subparsers(dest="command", required=True)

    generate_parser = subparsers.add_parser("generate")
    generate_parser.add_argument("--source-dir", required=True)
    generate_parser.add_argument("--project-name", required=True)
    generate_parser.add_argument("--linker-script", required=True)
    generate_parser.add_argument("--version-header", required=True)

    pack_parser = subparsers.add_parser("pack")
    pack_parser.add_argument("--version-header", required=True)
    pack_parser.add_argument("--linker-script", required=True)
    pack_parser.add_argument("--map-file", required=True)
    pack_parser.add_argument("--input-bin", required=True)
    pack_parser.add_argument("--header-bin", required=True)
    pack_parser.add_argument("--output-bin", required=True)

    return parser


def get_git_hash(source_dir: pathlib.Path) -> str:
    if not (source_dir / ".git").exists():
        return "unknown"

    result = subprocess.run(
        ["git", "rev-parse", "--short=7", "HEAD"],
        cwd=source_dir,
        capture_output=True,
        text=True,
        check=False,
    )
    if result.returncode != 0:
        return "unknown"

    git_hash = result.stdout.strip()
    return git_hash or "unknown"


def parse_flash_origin(linker_script: pathlib.Path) -> int:
    content = linker_script.read_text(encoding="utf-8", errors="ignore")
    match = re.search(
        r"FLASH\s*\([^)]+\)\s*:\s*ORIGIN\s*=\s*(0x[0-9A-Fa-f]+|\d+)", content
    )
    if not match:
        raise ValueError(f"Could not parse FLASH origin from {linker_script}")
    return int(match.group(1), 0)


def parse_symbol_address(map_file: pathlib.Path, symbol_name: str) -> int:
    pattern = re.compile(rf"^\s*(0x[0-9A-Fa-f]+)\s+{re.escape(symbol_name)}\s*$")
    for line in map_file.read_text(encoding="utf-8", errors="ignore").splitlines():
        match = pattern.match(line)
        if match:
            return int(match.group(1), 0)
    raise ValueError(f"Could not find symbol {symbol_name!r} in {map_file}")


def pack_string_field(value: str, size: int, macro_name: str) -> bytes:
    encoded = value.encode("utf-8")
    if len(encoded) > size:
        raise ValueError(f"{macro_name} is too long for {size} bytes: {value!r}")
    return encoded + b"\0" * (size - len(encoded))


def unpack_string_field(raw: bytes) -> str:
    return raw.split(b"\0", 1)[0].decode("utf-8", errors="replace")


def build_struct_blob(
    string_fields: dict[str, str],
    raw_bin_crc32: int,
    write_address: int,
    valid_bin_size: int,
) -> bytes:
    blob = bytearray()

    for field_name, field_size, macro_name in STRING_FIELD_SPECS:
        blob.extend(pack_string_field(string_fields[field_name], field_size, macro_name))

    blob.extend(struct.pack("<I", raw_bin_crc32))
    blob.extend(struct.pack("<I", write_address))
    blob.extend(struct.pack("<I", valid_bin_size))
    blob.extend(bytes(RESERVED_SIZE))

    if len(blob) != STRUCT_SIZE:
        raise ValueError(f"Metadata struct must be {STRUCT_SIZE} bytes")
    return bytes(blob)


def parse_struct_blob(blob: bytes) -> tuple[dict[str, str], int, int, int]:
    if len(blob) != STRUCT_SIZE:
        raise ValueError(f"Metadata struct must be {STRUCT_SIZE} bytes")

    string_fields: dict[str, str] = {}
    offset = 0
    for field_name, field_size, _ in STRING_FIELD_SPECS:
        string_fields[field_name] = unpack_string_field(blob[offset : offset + field_size])
        offset += field_size

    raw_bin_crc32, write_address, valid_bin_size = struct.unpack_from("<III", blob, offset)
    return string_fields, raw_bin_crc32, write_address, valid_bin_size


def write_version_header(
    version_header: pathlib.Path,
    string_fields: dict[str, str],
    raw_bin_crc32: int,
    write_address: int,
    valid_bin_size: int,
) -> None:
    macro_values = {
        "PROJECT_RAW_BIN_CRC32": f"0x{raw_bin_crc32:08X}U",
        "PROJECT_WRITE_ADDRESS": f"0x{write_address:08X}U",
        "PROJECT_VALID_BIN_SIZE": f"{valid_bin_size}U",
    }

    lines = [
        "#ifndef FW_VERSION_GENERATED_H",
        "#define FW_VERSION_GENERATED_H",
        "",
    ]

    for field_name, _, macro_name in STRING_FIELD_SPECS:
        lines.append(f'#define {macro_name:<22} "{string_fields[field_name]}"')

    for _, macro_name in NUMERIC_FIELD_MACROS:
        lines.append(f"#define {macro_name:<22} {macro_values[macro_name]}")

    lines.extend(
        (
            "",
            "#define FW_VERSION_INFO_INITIALIZER \\",
            "  { \\",
            "    .magic = PROJECT_MAGIC_HEAD, \\",
            "    .project_name = PROJECT_NAME_STR, \\",
            "    .image_tag = PROJECT_IMAGE_TAG, \\",
            "    .git_hash = PROJECT_GIT_HASH, \\",
            "    .build_time = PROJECT_BUILD_TIME, \\",
            "    .raw_bin_crc32 = PROJECT_RAW_BIN_CRC32, \\",
            "    .write_address = PROJECT_WRITE_ADDRESS, \\",
            "    .valid_bin_size = PROJECT_VALID_BIN_SIZE, \\",
            "    .reserved = {0}, \\",
            "  }",
            "",
            "#endif // FW_VERSION_GENERATED_H",
            "",
        )
    )

    version_header.write_text("\n".join(lines), encoding="utf-8")


def stm32_crc32(data: bytes) -> int:
    crc = 0xFFFFFFFF
    poly = 0x04C11DB7

    for offset in range(0, len(data), 4):
        word = int.from_bytes(data[offset : offset + 4], byteorder="little", signed=False)
        crc ^= word

        for _ in range(32):
            if crc & 0x80000000:
                crc = ((crc << 1) ^ poly) & 0xFFFFFFFF
            else:
                crc = (crc << 1) & 0xFFFFFFFF

    return crc


def generate_assets(args: argparse.Namespace) -> int:
    source_dir = pathlib.Path(args.source_dir)
    linker_script = pathlib.Path(args.linker_script)
    version_header = pathlib.Path(args.version_header)

    string_fields = {
        "magic": DEFAULT_MAGIC_HEAD,
        "project_name": args.project_name,
        "image_tag": DEFAULT_IMAGE_TAG,
        "git_hash": get_git_hash(source_dir),
        "build_time": datetime.now().strftime("%Y-%m-%d %H:%M:%S"),
    }
    write_address = parse_flash_origin(linker_script)

    version_header.parent.mkdir(parents=True, exist_ok=True)
    write_version_header(version_header, string_fields, 0, write_address, 0)

    print(f"Generated version header: {version_header}")
    return 0


def pack_versioned_bin(args: argparse.Namespace) -> int:
    version_header = pathlib.Path(args.version_header)
    linker_script = pathlib.Path(args.linker_script)
    map_file = pathlib.Path(args.map_file)
    input_bin = pathlib.Path(args.input_bin)
    header_bin = pathlib.Path(args.header_bin)
    output_bin = pathlib.Path(args.output_bin)

    write_address = parse_flash_origin(linker_script)
    symbol_address = parse_symbol_address(map_file, TARGET_SYMBOL)
    struct_offset = symbol_address - write_address
    firmware_bin = bytearray(input_bin.read_bytes())

    if struct_offset < 0 or struct_offset + STRUCT_SIZE > len(firmware_bin):
        raise ValueError(
            f"Metadata struct offset {struct_offset} is outside firmware bin size {len(firmware_bin)}"
        )

    original_blob = bytes(firmware_bin[struct_offset : struct_offset + STRUCT_SIZE])
    string_fields, _, _, _ = parse_struct_blob(original_blob)
    valid_bin_size = len(firmware_bin)

    crc_input_blob = build_struct_blob(string_fields, 0, write_address, valid_bin_size)
    firmware_bin[struct_offset : struct_offset + STRUCT_SIZE] = crc_input_blob
    raw_bin_crc32 = stm32_crc32(firmware_bin)

    final_blob = build_struct_blob(
        string_fields,
        raw_bin_crc32,
        write_address,
        valid_bin_size,
    )
    firmware_bin[struct_offset : struct_offset + STRUCT_SIZE] = final_blob

    input_bin.write_bytes(firmware_bin)
    header_bin.parent.mkdir(parents=True, exist_ok=True)
    output_bin.parent.mkdir(parents=True, exist_ok=True)
    header_bin.write_bytes(final_blob)
    output_bin.write_bytes(final_blob + firmware_bin)
    write_version_header(
        version_header,
        string_fields,
        raw_bin_crc32,
        write_address,
        valid_bin_size,
    )

    print(
        "Generated versioned bin: "
        f"{output_bin} (crc32=0x{raw_bin_crc32:08X}, "
        f"write_address=0x{write_address:08X}, valid_bin_size={valid_bin_size}, "
        f"struct_offset=0x{struct_offset:X})"
    )
    return 0


def main() -> int:
    args = build_parser().parse_args()
    if args.command == "generate":
        return generate_assets(args)
    if args.command == "pack":
        return pack_versioned_bin(args)
    raise ValueError(f"Unknown command: {args.command}")


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover - build helper
        print(f"error: {exc}", file=sys.stderr)
        raise SystemExit(1)
