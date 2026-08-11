"""Raw-BIN firmware Manifest, request, and package verification."""

from __future__ import annotations

import hashlib
import json
import os
import re
import uuid
from pathlib import Path
from typing import Any

APP_RUNTIME_BASE = 0x90000000
APP_MAX_SIZE = 0x00100000
GUI_MAX_SIZE = 0x00800000
THERAPY_MAX_SIZE = 0x00080000

PRODUCT = "HMI"
HARDWARE = "STM32H743-W25Q256"
MANIFEST_FILE = "manifest.json"
REQUEST_FILE = "boot_update_request.json"
COMPONENT_FILES = {
    "app": "hmi.app.bin",
    "gui": "hmi.gui.bin",
    "therapy": "therapy.app.bin",
}
COMPONENT_MAX_SIZES = {
    "app": APP_MAX_SIZE,
    "gui": GUI_MAX_SIZE,
    "therapy": THERAPY_MAX_SIZE,
}
COMPONENT_MASKS = {"app": 1, "gui": 2, "therapy": 4}

PACKAGE_ID_PATTERN = re.compile(r"^[A-Za-z0-9._+-]{1,63}$")
VERSION_PATTERN = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


class PackageToolError(RuntimeError):
    """Raised when package input violates the firmware contract."""


def package_content_root(package_root: Path) -> Path:
    firmware = package_root / "firmware"
    return firmware if firmware.is_dir() else package_root


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    value: dict[str, Any] = {}
    for key, item in pairs:
        if key in value:
            raise PackageToolError(f"JSON has duplicate member {key!r}")
        value[key] = item
    return value


def _load_json(data: bytes, document_name: str) -> dict[str, Any]:
    try:
        value = json.loads(data.decode("ascii"), object_pairs_hook=_unique_object)
    except (UnicodeDecodeError, json.JSONDecodeError) as error:
        raise PackageToolError(f"invalid {document_name} JSON: {error}") from error
    if not isinstance(value, dict):
        raise PackageToolError(f"{document_name} root must be an object")
    return value


def _load_manifest_template(data: bytes) -> dict[str, Any]:
    """Parse a template, allowing only an empty numeric size placeholder."""
    try:
        text = data.decode("ascii")
    except UnicodeDecodeError as error:
        raise PackageToolError(f"invalid manifest template JSON: {error}") from error
    normalized = re.sub(r'("size"\s*:\s*)(?=,)', r"\g<1>0", text)
    return _load_json(normalized.encode("ascii"), "manifest template")


def _expect_keys(value: Any, expected: set[str], name: str) -> dict[str, Any]:
    if not isinstance(value, dict) or set(value) != expected:
        raise PackageToolError(f"{name} members do not match the firmware schema")
    return value


def _validate_u32(value: Any, name: str, maximum: int = 0xFFFFFFFF) -> int:
    if isinstance(value, bool) or not isinstance(value, int) or not 0 <= value <= maximum:
        raise PackageToolError(f"{name} must be an unsigned integer")
    return value


def _validate_version(value: Any, name: str) -> None:
    if not isinstance(value, str):
        raise PackageToolError(f"{name} is invalid")
    match = VERSION_PATTERN.fullmatch(value)
    if match is None or any(int(part) > 0xFFFF for part in match.groups()):
        raise PackageToolError(f"{name} must be major.minor.patch")


def _validate_package_id(package_id: Any) -> None:
    if not isinstance(package_id, str) or PACKAGE_ID_PATTERN.fullmatch(package_id) is None:
        raise PackageToolError("package_id must be 1..63 ASCII letters, digits, . _ + or -")


def _validate_app_vector(payload: bytes) -> None:
    if len(payload) < 8:
        raise PackageToolError("app image does not contain an eight-byte vector")
    reset_handler = int.from_bytes(payload[4:8], byteorder="little")
    reset_address = reset_handler & ~1
    if (reset_handler & 1) == 0:
        raise PackageToolError("app reset handler is not a Thumb address")
    if not APP_RUNTIME_BASE <= reset_address < APP_RUNTIME_BASE + APP_MAX_SIZE:
        raise PackageToolError("app reset handler is outside the APP runtime region")


def _validate_component(value: Any, name: str) -> None:
    component = _expect_keys(value, {"file", "format", "size", "sha256"}, name)
    if component["file"] != COMPONENT_FILES[name] or component["format"] != "raw-bin-v1":
        raise PackageToolError(f"components.{name} has an invalid fixed identity")
    if _validate_u32(component["size"], f"components.{name}.size", COMPONENT_MAX_SIZES[name]) == 0:
        raise PackageToolError(f"components.{name}.size must not be zero")
    if not isinstance(component["sha256"], str) or SHA256_PATTERN.fullmatch(
        component["sha256"]
    ) is None:
        raise PackageToolError(f"components.{name}.sha256 is invalid")


def validate_manifest(manifest: dict[str, Any]) -> None:
    root = _expect_keys(
        manifest,
        {"format_version", "package_id", "release", "target", "components"},
        "manifest root",
    )
    if root["format_version"] != 1:
        raise PackageToolError("manifest format_version is invalid")
    _validate_package_id(root["package_id"])

    release = _expect_keys(root["release"], {"major", "minor", "patch", "build"}, "release")
    for name in ("major", "minor", "patch"):
        _validate_u32(release[name], f"release.{name}", 0xFFFF)
    _validate_u32(release["build"], "release.build")

    target = _expect_keys(
        root["target"],
        {"product", "hardware", "minimum_bootloader_version"},
        "target",
    )
    if target["product"] != PRODUCT or target["hardware"] != HARDWARE:
        raise PackageToolError("manifest target does not match this Bootloader")
    _validate_version(target["minimum_bootloader_version"], "minimum_bootloader_version")

    components = root["components"]
    if (
        not isinstance(components, dict)
        or not components
        or set(components) - set(COMPONENT_FILES)
    ):
        raise PackageToolError("manifest components must contain a non-empty app/gui/therapy subset")
    for name, component in components.items():
        _validate_component(component, name)


def parse_manifest_bytes(data: bytes) -> dict[str, Any]:
    manifest = _load_json(data, "manifest")
    validate_manifest(manifest)
    return manifest


def _write_bytes_sync(path: Path, data: bytes) -> None:
    with path.open("wb") as stream:
        stream.write(data)
        stream.flush()
        os.fsync(stream.fileno())


def _write_json_sync(path: Path, value: dict[str, Any]) -> None:
    _write_bytes_sync(path, (json.dumps(value, indent=2) + "\n").encode("ascii"))


def _component_entry(name: str, payload: bytes) -> dict[str, Any]:
    return {
        "file": COMPONENT_FILES[name],
        "format": "raw-bin-v1",
        "size": len(payload),
        "sha256": sha256_bytes(payload),
    }


def update_manifest_from_files(package_root: Path) -> tuple[dict[str, Any], bytes]:
    """Fill component size/hash fields and remove components whose BIN is absent."""
    content_root = package_content_root(package_root)
    manifest_path = content_root / MANIFEST_FILE
    try:
        manifest = _load_manifest_template(manifest_path.read_bytes())
    except OSError as error:
        raise PackageToolError(f"cannot read manifest template: {error}") from error
    components = manifest.get("components")
    if not isinstance(components, dict):
        raise PackageToolError("manifest template must contain a components object")

    for name, filename in COMPONENT_FILES.items():
        if name not in components:
            continue
        path = content_root / filename
        if not path.is_file():
            del components[name]
            continue
        payload = path.read_bytes()
        if not 0 < len(payload) <= COMPONENT_MAX_SIZES[name]:
            raise PackageToolError(f"{name} file size is outside the supported region")
        components[name] = _component_entry(name, payload)

    if not components:
        raise PackageToolError("manifest must contain at least one component with a BIN file")
    temporary_path = manifest_path.with_name(f".{MANIFEST_FILE}-{uuid.uuid4().hex}.new")
    _write_json_sync(temporary_path, manifest)
    os.replace(temporary_path, manifest_path)
    return verify_firmware_directory(content_root)


def verify_firmware_directory(content_root: Path) -> tuple[dict[str, Any], bytes]:
    if not content_root.is_dir():
        raise PackageToolError(f"package directory does not exist: {content_root}")
    try:
        manifest_bytes = (content_root / MANIFEST_FILE).read_bytes()
    except OSError as error:
        raise PackageToolError(f"cannot read manifest: {error}") from error
    manifest = parse_manifest_bytes(manifest_bytes)

    expected = {MANIFEST_FILE} | {
        COMPONENT_FILES[name] for name in manifest["components"]
    }
    names = {entry.name for entry in content_root.iterdir()}
    extras = names - expected
    if extras and extras != {REQUEST_FILE}:
        raise PackageToolError("package directory files do not match manifest components")

    for name, component in manifest["components"].items():
        path = content_root / component["file"]
        try:
            payload = path.read_bytes()
        except OSError as error:
            raise PackageToolError(f"cannot read {component['file']}: {error}") from error
        if not 0 < len(payload) <= COMPONENT_MAX_SIZES[name]:
            raise PackageToolError(f"{name} file size is outside the supported region")
        if name == "app":
            _validate_app_vector(payload)
        if len(payload) != component["size"]:
            raise PackageToolError(f"{name} file size does not match manifest")
        if sha256_bytes(payload) != component["sha256"]:
            raise PackageToolError(f"{name} file SHA-256 does not match manifest")
    return manifest, manifest_bytes


def verify_package_root(package_root: Path) -> tuple[dict[str, Any], bytes]:
    return verify_firmware_directory(package_content_root(package_root))


def parse_request_bytes(data: bytes) -> dict[str, Any]:
    request = _load_json(data, "request")
    _expect_keys(
        request,
        {"format_version", "requested", "package_id", "manifest_sha256", "component_mask"},
        "request root",
    )
    if request["format_version"] != 1 or request["requested"] is not True:
        raise PackageToolError("request format_version or requested is invalid")
    _validate_package_id(request["package_id"])
    if not isinstance(request["manifest_sha256"], str) or SHA256_PATTERN.fullmatch(
        request["manifest_sha256"]
    ) is None:
        raise PackageToolError("request manifest_sha256 is invalid")
    if (
        isinstance(request["component_mask"], bool)
        or not isinstance(request["component_mask"], int)
        or not 1 <= request["component_mask"] <= 7
    ):
        raise PackageToolError("request component_mask is invalid")
    return request


def component_mask_for_manifest(manifest: dict[str, Any]) -> int:
    return sum(COMPONENT_MASKS[name] for name in manifest["components"])


def validate_request(
    request: dict[str, Any], manifest: dict[str, Any], manifest_bytes: bytes
) -> None:
    if request["package_id"] != manifest["package_id"]:
        raise PackageToolError("request package_id does not match manifest")
    if request["manifest_sha256"] != sha256_bytes(manifest_bytes):
        raise PackageToolError("request does not bind the current manifest bytes")
    if request["component_mask"] != component_mask_for_manifest(manifest):
        raise PackageToolError("request component_mask does not match manifest")


def create_request(package_root: Path) -> Path:
    manifest, manifest_bytes = verify_package_root(package_root)
    request = {
        "format_version": 1,
        "requested": True,
        "package_id": manifest["package_id"],
        "manifest_sha256": sha256_bytes(manifest_bytes),
        "component_mask": component_mask_for_manifest(manifest),
    }
    request_path = package_root / REQUEST_FILE
    temporary_path = package_root / f".{REQUEST_FILE}-{uuid.uuid4().hex}.new"
    _write_json_sync(temporary_path, request)
    os.replace(temporary_path, request_path)
    persisted = parse_request_bytes(request_path.read_bytes())
    validate_request(persisted, manifest, manifest_bytes)
    return request_path
