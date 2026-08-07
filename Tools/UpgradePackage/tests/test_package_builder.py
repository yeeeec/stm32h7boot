from __future__ import annotations

import json
import sys
import tempfile
import unittest
from pathlib import Path

TOOL_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(TOOL_ROOT))

from elf_image import LoadSegment, PackageToolError
from package_builder import (
    APP_BASE,
    APP_END,
    GUI_BASE,
    GUI_END,
    FILL_BYTE,
    ReleaseImages,
    assemble_images,
    build_manifest,
    classify_load_segments,
    create_development_request,
    parse_manifest_bytes,
    sha256_bytes,
    validate_development_request,
    verify_package_root,
    write_release_package,
)


def app_vector(reset_handler: int = APP_BASE | 1) -> bytes:
    return (0x20010000).to_bytes(4, "little") + reset_handler.to_bytes(4, "little")


def valid_segments(app: bytes | None = None, gui: bytes = b"GUI") -> list[LoadSegment]:
    return [
        LoadSegment(APP_BASE, 0x20000000, app or app_vector()),
        LoadSegment(GUI_BASE, 0x24000000, gui),
    ]


class PackageBuilderTest(unittest.TestCase):
    def test_physical_address_selects_app_when_virtual_address_is_ram(self) -> None:
        images = assemble_images(valid_segments())
        self.assertEqual(images.app, app_vector())
        self.assertEqual(images.gui, b"GUI")

    def test_app_gap_is_filled_with_ff(self) -> None:
        segments = valid_segments()
        segments.insert(1, LoadSegment(APP_BASE + 0x1000, 0x30000000, b"OK"))
        images = assemble_images(segments)
        self.assertEqual(images.app[:8], app_vector())
        self.assertEqual(images.app[8:0x1000], bytes([FILL_BYTE]) * (0x1000 - 8))
        self.assertEqual(images.app[0x1000:], b"OK")

    def test_gui_base_maps_to_offset_zero(self) -> None:
        images = assemble_images(valid_segments(gui=b"\x11\x22"))
        self.assertEqual(images.gui, b"\x11\x22")

    def test_fixed_region_boundaries_and_outside_range(self) -> None:
        classified = classify_load_segments(
            [LoadSegment(APP_END - 1, 0x20000000, b"A"), LoadSegment(GUI_END - 1, 0, b"G")]
        )
        self.assertEqual(classified.app[0].physical_address, APP_END - 1)
        self.assertEqual(classified.gui[0].physical_address, GUI_END - 1)
        for segment in (
            LoadSegment(APP_END, 0, b"A"),
            LoadSegment(APP_END - 1, 0, b"AB"),
            LoadSegment(GUI_END, 0, b"G"),
        ):
            with self.assertRaises(PackageToolError):
                classify_load_segments([segment])

    def test_identical_overlap_is_accepted_and_conflicting_overlap_is_rejected(self) -> None:
        identical = valid_segments()
        identical.insert(1, LoadSegment(APP_BASE + 4, 0, app_vector()[4:]))
        self.assertEqual(assemble_images(identical).app, app_vector())

        conflicting = valid_segments()
        conflicting.insert(1, LoadSegment(APP_BASE + 4, 0, b"\x03\x00\x00\x90"))
        with self.assertRaises(PackageToolError):
            assemble_images(conflicting)

    def test_vector_requires_thumb_reset_inside_app(self) -> None:
        for reset_handler in (APP_BASE, GUI_BASE | 1):
            with self.assertRaises(PackageToolError):
                assemble_images(valid_segments(app=app_vector(reset_handler)))

    def test_manifest_has_exact_firmware_v1_shape(self) -> None:
        images = ReleaseImages(app=app_vector(), gui=b"GUI")
        manifest = build_manifest(images, "hmi-1.2.3+7", (1, 2, 3), 7, (1, 0, 0))
        self.assertEqual(
            set(manifest), {"format_version", "package_id", "release", "target", "components"}
        )
        self.assertEqual(set(manifest["components"]["app"]), {"file", "format", "size", "sha256"})
        self.assertEqual(set(manifest["components"]["gui"]), {"file", "format", "size", "sha256"})
        self.assertEqual(manifest["components"]["app"]["file"], "hmi.app.bin")
        self.assertEqual(manifest["components"]["gui"]["file"], "hmi.gui.bin")
        self.assertEqual(manifest["components"]["app"]["sha256"], sha256_bytes(images.app))
        self.assertEqual(manifest["components"]["gui"]["sha256"], sha256_bytes(images.gui))

    def test_package_write_and_self_verification(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_release_package(
                root, ReleaseImages(app=app_vector(), gui=b"GUI"), "hmi-1.2.3+7", (1, 2, 3), 7, (1, 0, 0)
            )
            self.assertEqual(
                {item.name for item in (root / "firmware").iterdir()},
                {"manifest.json", "hmi.app.bin", "hmi.gui.bin"},
            )
            self.assertFalse((root / "boot_update_request.json").exists())
            manifest, _ = verify_package_root(root)
            self.assertEqual(manifest["package_id"], "hmi-1.2.3+7")

    def test_development_request_binds_raw_manifest_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            write_release_package(
                root, ReleaseImages(app=app_vector(), gui=b"GUI"), "hmi-1.2.3+7", (1, 2, 3), 7, (1, 0, 0)
            )
            request_path = create_development_request(root)
            manifest, raw_manifest = verify_package_root(root)
            request = json.loads(request_path.read_text(encoding="ascii"))
            self.assertEqual(request["manifest_sha256"], sha256_bytes(raw_manifest))
            changed_manifest = raw_manifest + b"\n"
            with self.assertRaises(PackageToolError):
                validate_development_request(request, manifest, changed_manifest)
            self.assertEqual(parse_manifest_bytes(changed_manifest)["package_id"], manifest["package_id"])


if __name__ == "__main__":
    unittest.main()
