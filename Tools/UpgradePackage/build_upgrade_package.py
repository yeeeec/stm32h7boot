#!/usr/bin/env python3
"""Create, request, and verify a raw STM32H7 firmware package."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from package_builder import (
    PackageToolError,
    create_request,
    package_content_root,
    parse_request_bytes,
    sha256_bytes,
    validate_request,
    update_manifest_from_files,
    verify_package_root,
)


def create_manifest_command(args: argparse.Namespace) -> None:
    package_root = args.package_root.resolve()
    manifest, _ = update_manifest_from_files(package_root)
    print(
        json.dumps(
            {
                "manifest": str(package_content_root(package_root) / "manifest.json"),
                "components": sorted(manifest["components"]),
                "created": True,
            },
            indent=2,
        )
    )


def create_request_command(args: argparse.Namespace) -> None:
    request_path = create_request(args.package_root.resolve())
    print(json.dumps({"request": str(request_path), "created": True}, indent=2))


def verify_command(args: argparse.Namespace) -> None:
    manifest, manifest_bytes = verify_package_root(args.package_root.resolve())
    result = {
        "package_id": manifest["package_id"],
        "manifest_sha256": sha256_bytes(manifest_bytes),
        "components": {},
        "verified": True,
    }
    root = args.package_root.resolve()
    firmware = root / "firmware" if (root / "firmware").is_dir() else root
    for name, component in manifest["components"].items():
        payload = (firmware / component["file"]).read_bytes()
        result["components"][name] = {
            "file": component["file"],
            "size": len(payload),
            "sha256": sha256_bytes(payload),
        }
    request_path = root / "boot_update_request.json"
    if request_path.is_file():
        request = parse_request_bytes(request_path.read_bytes())
        validate_request(request, manifest, manifest_bytes)
        result["component_mask"] = request["component_mask"]
    print(json.dumps(result, indent=2))


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)

    manifest = subparsers.add_parser("create-manifest", help="fill sizes and SHA-256 values")
    manifest.add_argument("--package-root", type=Path, required=True)
    manifest.set_defaults(function=create_manifest_command)

    request = subparsers.add_parser("create-request", help="create boot_update_request.json")
    request.add_argument("--package-root", type=Path, required=True)
    request.set_defaults(function=create_request_command)

    verify = subparsers.add_parser("verify", help="verify every package SHA-256")
    verify.add_argument("--package-root", type=Path, required=True)
    verify.set_defaults(function=verify_command)

    args = parser.parse_args()
    args.function(args)
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except PackageToolError as error:
        print(f"error: {error}")
        raise SystemExit(2)
