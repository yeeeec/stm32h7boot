#!/usr/bin/env python3
"""Build and verify the Manifest-driven raw APP/GUI SD package.

The APP input remains a raw image linked at SOURCE_BASE.  Relocation offsets
are exported by the final application ELF with --emit-relocs; the package
stores those offsets in a separate fixed-size binary table. No container header
is written to hmi.app.bin.
"""

from __future__ import annotations

import argparse
import base64
import datetime as dt
import hashlib
import os
import re
import shutil
import struct
import subprocess
import tempfile
import zlib
from pathlib import Path

SOURCE_BASE = 0x90000000
PAIR_BASES = {"app1": 0x90000000, "app2": 0x90A00000}
APP_MAX = 0x100000
GUI_MAX = 0x800000
MAX_RELOCATIONS = 4096
RELOC_ENTRY_SIZE = 8
RELOC_TYPE_ABS32_ADD_XIP_BASE = 1


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def write_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value & 0xFFFFFFFF)


def run_readelf(tool: str, mode: str, elf: Path) -> list[str]:
    try:
        completed = subprocess.run(
            [tool, mode, elf.as_posix()],
            check=True,
            capture_output=True,
            text=True,
        )
    except FileNotFoundError as exc:
        raise RuntimeError(
            f"{tool} was not found; pass --readelf with the ARM GNU readelf path"
        ) from exc
    except subprocess.CalledProcessError as exc:
        raise RuntimeError(exc.stderr.strip() or f"{tool} failed") from exc
    return completed.stdout.splitlines()


def parse_sections(lines: list[str]) -> dict[str, tuple[int, int]]:
    sections: dict[str, tuple[int, int]] = {}
    pattern = re.compile(
        r"^\s*\[\s*\d+\]\s+(\S+)\s+\S+\s+([0-9A-Fa-f]+)\s+\S+\s+([0-9A-Fa-f]+)"
    )
    for line in lines:
        match = pattern.match(line)
        if match:
            sections[match.group(1)] = (
                int(match.group(2), 16),
                int(match.group(3), 16),
            )
    return sections


def parse_load_segments(lines: list[str]) -> list[tuple[int, int, int, int, int]]:
    segments: list[tuple[int, int, int, int, int]] = []
    pattern = re.compile(
        r"^\s*LOAD\s+(?:0x)?([0-9A-Fa-f]+)\s+(?:0x)?([0-9A-Fa-f]+)\s+"
        r"(?:0x)?([0-9A-Fa-f]+)\s+(?:0x)?([0-9A-Fa-f]+)\s+"
        r"(?:0x)?([0-9A-Fa-f]+)"
    )
    for line in lines:
        match = pattern.match(line)
        if match:
            segments.append(tuple(int(value, 16) for value in match.groups()))
    return segments


def relocation_physical_address(
    virtual_address: int,
    section_name: str,
    sections: dict[str, tuple[int, int]],
    segments: list[tuple[int, int, int, int, int]],
) -> int | None:
    section = sections.get(section_name)
    if section is None:
        return None
    section_address, section_size = section
    if not (section_address <= virtual_address < section_address + section_size):
        return None
    for _, segment_vaddr, segment_paddr, segment_filesz, segment_memsz in segments:
        if segment_vaddr <= virtual_address < segment_vaddr + segment_memsz:
            delta = virtual_address - segment_vaddr
            if delta >= segment_filesz:
                return None
            return segment_paddr + delta
    return None


def parse_elf_relocations(
    elf: Path,
    app: bytes,
    readelf: str,
    source_base: int,
) -> list[int]:
    sections = parse_sections(run_readelf(readelf, "-SW", elf))
    segments = parse_load_segments(run_readelf(readelf, "-lW", elf))
    lines = run_readelf(readelf, "-rW", elf)
    current_section: str | None = None
    offsets: set[int] = set()
    relocation_pattern = re.compile(
        r"^\s*([0-9A-Fa-f]+)\s+[0-9A-Fa-f]+\s+(R_ARM_\S+)\s+"
        r"([0-9A-Fa-f]+)\s+.*$"
    )
    section_pattern = re.compile(r"^Relocation section '([^']+)'\s")

    for line in lines:
        section_match = section_pattern.match(line)
        if section_match:
            name = section_match.group(1)
            current_section = name[4:] if name.startswith(".rel.") else None
            continue
        if current_section is None:
            continue
        match = relocation_pattern.match(line)
        if match is None or match.group(2) != "R_ARM_ABS32":
            continue
        relocation_address = int(match.group(1), 16)
        symbol_value = int(match.group(3), 16)
        if not (source_base <= symbol_value < source_base + 0x02000000):
            continue
        physical = relocation_physical_address(
            relocation_address, current_section, sections, segments
        )
        if physical is None or physical < source_base:
            continue
        offset = physical - source_base
        if offset % 4 != 0 or offset + 4 > len(app):
            continue
        raw_word = u32(app, offset)
        if not (source_base <= raw_word < source_base + 0x02000000):
            raise RuntimeError(
                f"ELF relocation at 0x{offset:08X} does not match raw APP word "
                f"0x{raw_word:08X}"
            )
        offsets.add(offset)

    result = sorted(offsets)
    if len(result) > MAX_RELOCATIONS:
        raise RuntimeError(
            f"ELF exported {len(result)} relocations, product limit is {MAX_RELOCATIONS}"
        )
    if not result:
        raise RuntimeError("ELF did not produce any usable APP relocations")
    return result


def parse_offset_file(path: Path, app: bytes) -> list[int]:
    offsets: list[int] = []
    for line_number, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        value = line.split("#", 1)[0].strip()
        if not value:
            continue
        try:
            offset = int(value, 0)
        except ValueError as exc:
            raise RuntimeError(f"invalid relocation offset at {path}:{line_number}") from exc
        offsets.append(offset)
    offsets = sorted(set(offsets))
    for offset in offsets:
        if offset <= 0 or offset % 4 or offset + 4 > len(app):
            raise RuntimeError(f"relocation offset out of range: 0x{offset:08X}")
        if not (SOURCE_BASE <= u32(app, offset) < SOURCE_BASE + 0x02000000):
            raise RuntimeError(f"raw APP word is not an XIP address at 0x{offset:08X}")
    if len(offsets) > MAX_RELOCATIONS:
        raise RuntimeError(f"relocation count exceeds {MAX_RELOCATIONS}")
    return offsets


def make_relocation_table(offsets: list[int]) -> bytes:
    table = bytearray()
    for offset in offsets:
        table += struct.pack("<IHH", offset, RELOC_TYPE_ABS32_ADD_XIP_BASE, 0)
    return bytes(table)


def apply_relocations(app: bytes, offsets: list[int], target_base: int) -> bytes:
    image = bytearray(app)
    for offset in offsets:
        raw_word = u32(image, offset)
        if not SOURCE_BASE <= raw_word < SOURCE_BASE + 0x02000000:
            raise RuntimeError(f"relocation source is outside QSPI at 0x{offset:08X}")
        relocated = raw_word - SOURCE_BASE + target_base
        if relocated >= SOURCE_BASE + 0x02000000:
            raise RuntimeError(f"relocation target is outside QSPI at 0x{offset:08X}")
        write_u32(image, offset, relocated)
    return bytes(image)


def package_manifest(
    app: bytes,
    gui: bytes,
    reloc_table: bytes,
    offsets: list[int],
    package_id: str,
    build_number: int,
    release_version: str,
    created_utc: str,
) -> dict:
    reset = u32(app, 4)
    if (reset & 1) == 0 or reset < SOURCE_BASE:
        raise RuntimeError(f"invalid raw APP reset vector: 0x{reset:08X}")
    entry_offset = (reset & ~1) - SOURCE_BASE
    if entry_offset >= len(app) or entry_offset % 2:
        raise RuntimeError(f"invalid APP entry offset: 0x{entry_offset:08X}")
    target_crc = {
        name: f"{crc32(apply_relocations(app, offsets, base)):08X}"
        for name, base in PAIR_BASES.items()
    }
    return {
        "format_version": 2,
        "package_id": package_id,
        "product_id": "HMI",
        "hardware_id": "STM32H743-W25Q256",
        "build_number": build_number,
        "created_utc": created_utc,
        "minimum_bootloader_version": "1.0.0",
        "release_groups": [
            {"id": "app-gui", "version": release_version, "atomic": True, "components": ["app", "gui"]}
        ],
        "transaction": {
            "release_group_id": "app-gui",
            "strategy": "inactive-pair",
            "commit_store": "eeprom",
            "commit_condition": "all-components-crc-valid",
        },
        "crc32_parameters": {
            "name": "CRC-32/ISO-HDLC",
            "polynomial": "0x04C11DB7",
            "initial_value": "0xFFFFFFFF",
            "reflect_input": True,
            "reflect_output": True,
            "xor_output": "0xFFFFFFFF",
        },
        "components": [
            {
                "id": "app",
                "file": "hmi.app.bin",
                "format": "raw-xip-reloc-v2",
                "target": "inactive-app-slot",
                "maximum_image_size_bytes": APP_MAX,
                "file_size_bytes": len(app),
                "image_size_bytes": len(app),
                "source_crc32": f"{crc32(app):08X}",
                "target_crc32": target_crc,
                "sha256": sha256(app),
                "vector_offset": 0,
                "entry_offset": entry_offset,
                "link_address": SOURCE_BASE,
                "relocation": {
                    "file": "hmi.app.reloc.bin",
                    "count": len(offsets),
                    "crc32": f"{crc32(reloc_table):08X}",
                    "format": "hmi-reloc-v1",
                },
            },
            {
                "id": "gui",
                "file": "hmi.gui.bin",
                "format": "raw",
                "target": "inactive-gui-slot",
                "maximum_image_size_bytes": GUI_MAX,
                "file_size_bytes": len(gui),
                "crc32": f"{crc32(gui):08X}",
                "sha256": sha256(gui),
            },
        ],
        "signature": {
            "algorithm": "ECDSA-P256-SHA256",
            "key_id": "hmi-production-key-01",
            "canonicalization": "RFC8785",
            "scope": "all-fields-except-signature.value",
            "encoding": "base64",
            "value": base64.b64encode(bytes(64)).decode("ascii"),
        },
    }


def write_json(path: Path, value: dict) -> None:
    import json

    path.write_text(json.dumps(value, indent=2) + "\n", encoding="ascii")


def build_package(args: argparse.Namespace) -> None:
    import json

    app = args.app.read_bytes()
    gui = args.gui.read_bytes()
    if not 0 < len(app) <= APP_MAX:
        raise RuntimeError(f"APP size is outside the 1 MiB slot: {len(app)}")
    if not 0 < len(gui) <= GUI_MAX:
        raise RuntimeError(f"GUI size is outside the 8 MiB slot: {len(gui)}")
    if args.relocations:
        offsets = parse_offset_file(args.relocations, app)
    else:
        if not args.elf:
            raise RuntimeError("pass --elf or --relocations; relocation scanning is disabled")
        offsets = parse_elf_relocations(args.elf, app, args.readelf, SOURCE_BASE)
    reloc_table = make_relocation_table(offsets)
    now = args.created_utc or dt.datetime.now(dt.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")
    manifest = package_manifest(
        app, gui, reloc_table, offsets, args.package_id, args.build_number,
        args.release_version, now,
    )
    with tempfile.TemporaryDirectory(prefix="hmi-upgrade-") as temporary:
        staged = Path(temporary) / "firmware"
        staged.mkdir()
        (staged / "hmi.app.bin").write_bytes(app)
        (staged / "hmi.gui.bin").write_bytes(gui)
        (staged / "hmi.app.reloc.bin").write_bytes(reloc_table)
        write_json(staged / "manifest.json", manifest)
        verify_directory(staged.parent, manifest)
        destination = args.output / "firmware"
        destination.mkdir(parents=True, exist_ok=True)
        for name in ("hmi.app.bin", "hmi.gui.bin", "hmi.app.reloc.bin", "manifest.json"):
            temporary_destination = destination / (name + ".new")
            shutil.copyfile(staged / name, temporary_destination)
            os.replace(temporary_destination, destination / name)
        request_temporary = args.output / "boot_update_request.json.new"
        request_temporary.write_text("{}\n", encoding="ascii")
        os.replace(request_temporary, args.output / "boot_update_request.json")
    print(json.dumps({
        "output": str(args.output),
        "app_bytes": len(app),
        "gui_bytes": len(gui),
        "relocation_count": len(offsets),
        "app_source_crc32": manifest["components"][0]["source_crc32"],
        "app_target_crc32": manifest["components"][0]["target_crc32"],
        "gui_crc32": manifest["components"][1]["crc32"],
    }, indent=2))


def verify_directory(root: Path, manifest: dict | None = None) -> None:
    import json

    firmware = root / "firmware"
    if manifest is None:
        manifest = json.loads((firmware / "manifest.json").read_text(encoding="ascii"))
    app = (firmware / "hmi.app.bin").read_bytes()
    gui = (firmware / "hmi.gui.bin").read_bytes()
    reloc = (firmware / "hmi.app.reloc.bin").read_bytes()
    app_component, gui_component = manifest["components"]
    if len(app) != app_component["file_size_bytes"] or len(app) != app_component["image_size_bytes"]:
        raise RuntimeError("APP size does not match Manifest")
    if sha256(app) != app_component["sha256"] or f"{crc32(app):08X}" != app_component["source_crc32"]:
        raise RuntimeError("APP source integrity does not match Manifest")
    if len(gui) != gui_component["file_size_bytes"] or sha256(gui) != gui_component["sha256"]:
        raise RuntimeError("GUI integrity does not match Manifest")
    relocation = app_component["relocation"]
    if len(reloc) != relocation["count"] * RELOC_ENTRY_SIZE or f"{crc32(reloc):08X}" != relocation["crc32"]:
        raise RuntimeError("relocation table integrity does not match Manifest")
    offsets = []
    for index in range(0, len(reloc), RELOC_ENTRY_SIZE):
        offset, relocation_type, reserved = struct.unpack_from("<IHH", reloc, index)
        if relocation_type != RELOC_TYPE_ABS32_ADD_XIP_BASE or reserved != 0:
            raise RuntimeError(f"invalid relocation entry at byte offset {index}")
        offsets.append(offset)
    if offsets != sorted(set(offsets)):
        raise RuntimeError("relocation offsets are not strictly sorted")
    if not offsets or offsets[0] != 4:
        raise RuntimeError("reset vector relocation at offset 4 is required")
    for offset in offsets:
        if offset <= 0 or offset + 4 > len(app) or offset % 4:
            raise RuntimeError(f"relocation offset out of range: 0x{offset:08X}")
    for name, base in PAIR_BASES.items():
        target = apply_relocations(app, offsets, base)
        expected = app_component["target_crc32"][name]
        actual = f"{crc32(target):08X}"
        if actual != expected:
            raise RuntimeError(f"{name} target CRC mismatch: {actual} != {expected}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    build = subparsers.add_parser("build")
    build.add_argument("--app", type=Path, required=True)
    build.add_argument("--gui", type=Path, required=True)
    build.add_argument("--elf", type=Path)
    build.add_argument("--relocations", type=Path)
    build.add_argument("--readelf", default="arm-none-eabi-readelf")
    build.add_argument("--output", type=Path, required=True)
    build.add_argument("--package-id", default="hmi-app-gui-2.0.0+1")
    build.add_argument("--release-version", default="2.0.0")
    build.add_argument("--build-number", type=int, default=1)
    build.add_argument("--created-utc")
    build.set_defaults(function=build_package)
    verify = subparsers.add_parser("verify")
    verify.add_argument("--root", type=Path, required=True)
    verify.set_defaults(function=lambda args: verify_directory(args.root))
    args = parser.parse_args()
    args.function(args)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as error:
        print(f"error: {error}")
        raise SystemExit(2)
