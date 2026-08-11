"""Fixed-runtime release image assembly, Manifest generation, and package writing."""

from __future__ import annotations

import hashlib
import json
import os
import re
import shutil
import tempfile
import uuid
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable

from elf_image import LoadSegment, PackageToolError

APP_BASE = 0x90000000
APP_MAX_SIZE = 0x00100000
APP_END = APP_BASE + APP_MAX_SIZE
GUI_BASE = 0x90200000
GUI_MAX_SIZE = 0x00800000
GUI_END = GUI_BASE + GUI_MAX_SIZE
THERAPY_MAX_SIZE = 0x00080000
FILL_BYTE = 0xFF

PRODUCT = "HMI"
HARDWARE = "STM32H743-W25Q256"
MANIFEST_FILE = "manifest.json"
APP_FILE = "hmi.app.bin"
GUI_FILE = "hmi.gui.bin"
THERAPY_FILE = "therapy.app.bin"
COMPONENT_FILES = {"app": APP_FILE, "gui": GUI_FILE, "therapy": THERAPY_FILE}
FIRMWARE_FILES = frozenset((MANIFEST_FILE, APP_FILE, GUI_FILE, THERAPY_FILE))
PACKAGE_ID_PATTERN = re.compile(r"^[A-Za-z0-9._+-]{1,63}$")
VERSION_PATTERN = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


def package_content_root(package_root: Path) -> Path:
    firmware = package_root / "firmware"
    return firmware if firmware.is_dir() else package_root


@dataclass(frozen=True)
class ReleaseImages:
    app: bytes
    gui: bytes
    therapy: bytes | None = None


@dataclass(frozen=True)
class ClassifiedSegments:
    app: tuple[LoadSegment, ...]
    gui: tuple[LoadSegment, ...]


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def parse_release_version(value: str, argument_name: str) -> tuple[int, int, int]:
    match = VERSION_PATTERN.fullmatch(value)
    if match is None:
        raise PackageToolError(f"{argument_name} must be major.minor.patch without leading zeroes")
    parts = tuple(int(part) for part in match.groups())
    if any(part > 0xFFFF for part in parts):
        raise PackageToolError(f"{argument_name} components must be at most 65535")
    return parts


def validate_package_id(package_id: str) -> None:
    if PACKAGE_ID_PATTERN.fullmatch(package_id) is None:
        raise PackageToolError("package_id must be 1..63 ASCII letters, digits, . _ + or -")


def _range_is_within(address: int, length: int, base: int, limit: int) -> bool:
    return address >= base and length > 0 and address + length <= limit


def classify_load_segments(segments: Iterable[LoadSegment]) -> ClassifiedSegments:
    """Classify every file-backed segment by physical load address."""

    app: list[LoadSegment] = []
    gui: list[LoadSegment] = []
    for segment in segments:
        if not segment.data:
            continue
        if _range_is_within(segment.physical_address, len(segment.data), APP_BASE, APP_END):
            app.append(segment)
        elif _range_is_within(segment.physical_address, len(segment.data), GUI_BASE, GUI_END):
            gui.append(segment)
        else:
            raise PackageToolError(
                "file-backed PT_LOAD range "
                f"0x{segment.physical_address:08X}..0x{segment.end_address:08X} "
                "is outside one fixed release region"
            )
    return ClassifiedSegments(tuple(app), tuple(gui))


def _assemble_region(
    segments: Iterable[LoadSegment], base: int, maximum_size: int, name: str
) -> bytes:
    ordered = tuple(sorted(segments, key=lambda segment: segment.physical_address))
    if not ordered:
        raise PackageToolError(f"ELF contains no file-backed {name} PT_LOAD segment")

    highest = max(segment.end_address - base for segment in ordered)
    if highest <= 0 or highest > maximum_size:
        raise PackageToolError(f"{name} image size is outside its fixed region")

    image = bytearray([FILL_BYTE]) * highest
    written = bytearray(highest)
    for segment in ordered:
        offset = segment.physical_address - base
        data = segment.data
        occupied = written[offset : offset + len(data)]
        if any(occupied):
            previous = image[offset : offset + len(data)]
            for index, is_written in enumerate(occupied):
                if is_written and previous[index] != data[index]:
                    raise PackageToolError(
                        f"conflicting PT_LOAD data in {name} at offset 0x{offset + index:X}"
                    )
        image[offset : offset + len(data)] = data
        written[offset : offset + len(data)] = b"\x01" * len(data)
    return bytes(image)


def validate_app_vector(app: bytes) -> None:
    if len(app) < 8:
        raise PackageToolError("APP image does not contain an eight-byte vector")
    reset_handler = int.from_bytes(app[4:8], byteorder="little")
    reset_address = reset_handler & ~1
    if (reset_handler & 1) == 0:
        raise PackageToolError("APP reset handler is not a Thumb address")
    if not APP_BASE <= reset_address < APP_END:
        raise PackageToolError("APP reset handler is outside the APP runtime region")


def assemble_images(segments: Iterable[LoadSegment]) -> ReleaseImages:
    classified = classify_load_segments(segments)
    app = _assemble_region(classified.app, APP_BASE, APP_MAX_SIZE, "APP")
    gui = _assemble_region(classified.gui, GUI_BASE, GUI_MAX_SIZE, "GUI")
    validate_app_vector(app)
    return ReleaseImages(app=app, gui=gui)


def build_manifest(
    images: ReleaseImages,
    package_id: str,
    release_version: tuple[int, int, int],
    build_number: int,
    minimum_bootloader_version: tuple[int, int, int],
) -> dict[str, Any]:
    validate_package_id(package_id)
    if isinstance(build_number, bool) or not 0 <= build_number <= 0xFFFFFFFF:
        raise PackageToolError("build_number must be an unsigned 32-bit integer")
    if not 0 < len(images.app) <= APP_MAX_SIZE:
        raise PackageToolError("APP image size is outside the 1 MiB runtime region")
    if not 0 < len(images.gui) <= GUI_MAX_SIZE:
        raise PackageToolError("GUI image size is outside the 8 MiB runtime region")
    validate_app_vector(images.app)

    return {
        "format_version": 1,
        "package_id": package_id,
        "release": {
            "major": release_version[0],
            "minor": release_version[1],
            "patch": release_version[2],
            "build": build_number,
        },
        "target": {
            "product": PRODUCT,
            "hardware": HARDWARE,
            "minimum_bootloader_version": ".".join(
                str(part) for part in minimum_bootloader_version
            ),
        },
        "components": {
            "app": {
                "file": APP_FILE,
                "format": "raw-bin-v1",
                "size": len(images.app),
                "sha256": sha256_bytes(images.app),
            },
            "gui": {
                "file": GUI_FILE,
                "format": "raw-bin-v1",
                "size": len(images.gui),
                "sha256": sha256_bytes(images.gui),
            },
        },
    }


def component_manifest_entry(name: str, payload: bytes) -> dict[str, Any]:
    if name not in COMPONENT_FILES:
        raise PackageToolError(f"unknown component: {name}")
    return {
        "file": COMPONENT_FILES[name],
        "format": "raw-bin-v1",
        "size": len(payload),
        "sha256": sha256_bytes(payload),
    }


def update_manifest_from_files(package_root: Path) -> tuple[dict[str, Any], bytes]:
    """Update a hand-authored manifest using the BIN files in package_root."""
    package_root = package_content_root(package_root)
    manifest_path = package_root / MANIFEST_FILE
    try:
        manifest = json.loads(
            manifest_path.read_text(encoding="ascii"), object_pairs_hook=_unique_object
        )
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PackageToolError(f"invalid manifest template: {error}") from error
    if not isinstance(manifest, dict) or not isinstance(manifest.get("components"), dict):
        raise PackageToolError("manifest template must contain a components object")
    components = manifest["components"]
    for name, filename in COMPONENT_FILES.items():
        path = package_root / filename
        if name in components:
            if not path.is_file():
                del components[name]
            else:
                payload = path.read_bytes()
                maximum = {
                    "app": APP_MAX_SIZE,
                    "gui": GUI_MAX_SIZE,
                    "therapy": THERAPY_MAX_SIZE,
                }[name]
                if not 0 < len(payload) <= maximum:
                    raise PackageToolError(f"{name} file size is outside the fixed runtime region")
                components[name] = component_manifest_entry(name, payload)
    if not components:
        raise PackageToolError("manifest must contain at least one component with a BIN file")
    _write_json_sync(manifest_path, manifest)
    return verify_firmware_directory(package_root)


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise PackageToolError(f"manifest has duplicate member {key!r}")
        value[key] = item
    return value


def parse_manifest_bytes(data: bytes) -> dict[str, Any]:
    try:
        value = json.loads(data.decode("ascii"), object_pairs_hook=_unique_object)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PackageToolError(f"invalid manifest JSON: {error}") from error
    if not isinstance(value, dict):
        raise PackageToolError("manifest root must be an object")
    validate_manifest(value)
    return value


def _expect_keys(value: Any, expected: set[str], name: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != expected:
        raise PackageToolError(f"manifest {name} members do not match the firmware schema")
    return value


def _validate_u32(value: Any, name: str, maximum: int = 0xFFFFFFFF) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise PackageToolError(f"manifest {name} must be an unsigned integer")
    return value


def _validate_component(
    value: Any, name: str, filename: str, maximum_size: int
) -> dict[str, Any]:
    component = _expect_keys(value, {"file", "format", "size", "sha256"}, name)
    if component["file"] != filename or component["format"] != "raw-bin-v1":
        raise PackageToolError(f"manifest {name} has an invalid fixed component identity")
    size = _validate_u32(component["size"], f"{name}.size", maximum_size)
    if size == 0:
        raise PackageToolError(f"manifest {name}.size must not be zero")
    if not isinstance(component["sha256"], str) or SHA256_PATTERN.fullmatch(component["sha256"]) is None:
        raise PackageToolError(f"manifest {name}.sha256 is invalid")
    return component


def validate_manifest(manifest: dict[str, Any]) -> None:
    root = _expect_keys(
        manifest, {"format_version", "package_id", "release", "target", "components"}, "root"
    )
    if root["format_version"] != 1 or not isinstance(root["package_id"], str):
        raise PackageToolError("manifest format_version or package_id is invalid")
    validate_package_id(root["package_id"])

    release = _expect_keys(root["release"], {"major", "minor", "patch", "build"}, "release")
    for name in ("major", "minor", "patch"):
        _validate_u32(release[name], f"release.{name}", 0xFFFF)
    _validate_u32(release["build"], "release.build")

    target = _expect_keys(
        root["target"], {"product", "hardware", "minimum_bootloader_version"}, "target"
    )
    if target["product"] != PRODUCT or target["hardware"] != HARDWARE:
        raise PackageToolError("manifest target does not match this Bootloader")
    if not isinstance(target["minimum_bootloader_version"], str):
        raise PackageToolError("manifest minimum_bootloader_version is invalid")
    parse_release_version(target["minimum_bootloader_version"], "minimum_bootloader_version")

    components = root["components"]
    if (
        not isinstance(components, dict)
        or not components
        or set(components) - set(COMPONENT_FILES)
    ):
        raise PackageToolError("manifest components must contain a non-empty app/gui/therapy subset")
    limits = {
        "app": (APP_FILE, APP_MAX_SIZE),
        "gui": (GUI_FILE, GUI_MAX_SIZE),
        "therapy": (THERAPY_FILE, THERAPY_MAX_SIZE),
    }
    for name, component in components.items():
        filename, maximum = limits[name]
        _validate_component(component, f"components.{name}", filename, maximum)


def _write_bytes_sync(path: Path, data: bytes) -> None:
    with path.open("wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def _write_json_sync(path: Path, value: dict[str, Any]) -> None:
    encoded = (json.dumps(value, indent=2) + "\n").encode("ascii")
    _write_bytes_sync(path, encoded)


def verify_firmware_directory(firmware: Path) -> tuple[dict[str, Any], bytes]:
    if not firmware.is_dir():
        raise PackageToolError(f"firmware directory does not exist: {firmware}")
    names = {entry.name for entry in firmware.iterdir()}
    manifest_bytes = (firmware / MANIFEST_FILE).read_bytes()
    manifest = parse_manifest_bytes(manifest_bytes)
    expected = {MANIFEST_FILE} | {COMPONENT_FILES[name] for name in manifest["components"]}
    extras = names - expected
    if extras != set() and extras != {"boot_update_request.json"}:
        raise PackageToolError("package directory files do not match manifest components")

    for name, component in manifest["components"].items():
        payload = (firmware / component["file"]).read_bytes()
        maximum_size = {
            "app": APP_MAX_SIZE,
            "gui": GUI_MAX_SIZE,
            "therapy": THERAPY_MAX_SIZE,
        }[name]
        if name == "app":
            validate_app_vector(payload)
        component = manifest["components"][name]
        if not 0 < len(payload) <= maximum_size:
            raise PackageToolError(f"{name} file size is outside the fixed runtime region")
        if len(payload) != component["size"]:
            raise PackageToolError(f"{name} file size does not match manifest")
        if sha256_bytes(payload) != component["sha256"]:
            raise PackageToolError(f"{name} file SHA-256 does not match manifest")
    return manifest, manifest_bytes


def _replace_firmware_directory(staged: Path, package_root: Path) -> Path:
    destination = package_root / "firmware"
    if destination.exists() and not destination.is_dir():
        raise PackageToolError(f"firmware output is not a directory: {destination}")
    backup = package_root / f".firmware-previous-{uuid.uuid4().hex}"
    moved_previous = False
    try:
        if destination.exists():
            os.replace(destination, backup)
            moved_previous = True
        os.replace(staged, destination)
    except OSError as error:
        if moved_previous and backup.exists() and not destination.exists():
            os.replace(backup, destination)
        raise PackageToolError(f"cannot finalize firmware directory: {error}") from error
    if backup.exists():
        shutil.rmtree(backup)
    return destination


def write_release_package(
    package_root: Path,
    images: ReleaseImages,
    package_id: str,
    release_version: tuple[int, int, int],
    build_number: int,
    minimum_bootloader_version: tuple[int, int, int],
) -> Path:
    """Write all release files to staging, verify them, then replace firmware/."""

    package_root = package_root.resolve()
    package_root.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".hmi-package-", dir=package_root.parent) as temporary:
        staged_root = Path(temporary)
        staged = staged_root / "firmware"
        staged.mkdir()
        _write_bytes_sync(staged / APP_FILE, images.app)
        _write_bytes_sync(staged / GUI_FILE, images.gui)

        final_images = ReleaseImages(
            app=(staged / APP_FILE).read_bytes(), gui=(staged / GUI_FILE).read_bytes()
        )
        manifest = build_manifest(
            final_images,
            package_id,
            release_version,
            build_number,
            minimum_bootloader_version,
        )
        _write_json_sync(staged / MANIFEST_FILE, manifest)
        verify_firmware_directory(staged)
        destination = _replace_firmware_directory(staged, package_root)

    verify_firmware_directory(destination)
    return destination


def verify_package_root(package_root: Path) -> tuple[dict[str, Any], bytes]:
    return verify_firmware_directory(package_content_root(package_root))


def _parse_request_bytes(data: bytes) -> dict[str, Any]:
    try:
        request = json.loads(data.decode("ascii"), object_pairs_hook=_unique_object)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PackageToolError(f"invalid development request JSON: {error}") from error
    if not isinstance(request, dict) or set(request) != {
        "format_version",
        "requested",
        "package_id",
        "manifest_sha256",
        "component_mask",
    }:
        raise PackageToolError("development request members do not match the firmware schema")
    if request["format_version"] != 1 or request["requested"] is not True:
        raise PackageToolError("development request format_version or requested is invalid")
    if not isinstance(request["package_id"], str):
        raise PackageToolError("development request package_id is invalid")
    validate_package_id(request["package_id"])
    if not isinstance(request["manifest_sha256"], str) or SHA256_PATTERN.fullmatch(
        request["manifest_sha256"]
    ) is None:
        raise PackageToolError("development request manifest_sha256 is invalid")
    if (
        isinstance(request["component_mask"], bool)
        or not isinstance(request["component_mask"], int)
        or request["component_mask"] not in (1, 2, 3, 4, 5, 6, 7)
    ):
        raise PackageToolError("development request component_mask is invalid")
    return request


def component_mask_for_manifest(manifest: dict[str, Any]) -> int:
    return sum(
        {"app": 1, "gui": 2, "therapy": 4}[name] for name in manifest["components"]
    )


def validate_development_request(
    request: dict[str, Any], manifest: dict[str, Any], manifest_bytes: bytes
) -> None:
    if request["package_id"] != manifest["package_id"]:
        raise PackageToolError("development request package_id does not match manifest")
    if request["manifest_sha256"] != sha256_bytes(manifest_bytes):
        raise PackageToolError("development request does not bind the current manifest bytes")
    if request["component_mask"] != component_mask_for_manifest(manifest):
        raise PackageToolError("development request component_mask does not match manifest")


def create_development_request(package_root: Path) -> Path:
    manifest, manifest_bytes = verify_package_root(package_root)
    request = {
        "format_version": 1,
        "requested": True,
        "package_id": manifest["package_id"],
        "manifest_sha256": sha256_bytes(manifest_bytes),
        "component_mask": component_mask_for_manifest(manifest),
    }
    request_path = package_root / "boot_update_request.json"
    temporary_path = package_root / f".boot-update-request-{uuid.uuid4().hex}.new"
    _write_json_sync(temporary_path, request)
    os.replace(temporary_path, request_path)
    persisted = _parse_request_bytes(request_path.read_bytes())
    validate_development_request(persisted, manifest, manifest_bytes)
    return request_path
