from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]
UPDATE = ROOT / "Services" / "Update"


class UpdateStructureTest(unittest.TestCase):
    def test_package_reader_has_one_complete_validation_mode(self):
        header = (UPDATE / "src" / "update_package.h").read_text(encoding="utf-8", errors="replace")
        source = (UPDATE / "src" / "package_reader.c").read_text(encoding="utf-8", errors="replace")

        self.assertRegex(
            header,
            r"PackageReader_Validate\(const char \*root,\s*update_package_t \*package\)",
        )
        self.assertNotIn("expected_manifest_digest", header + source)
        self.assertNotIn("verify_payload_hashes", header + source)
        self.assertNotIn("manifest_sha256", header + source)
        self.assertNotIn("UpdateManifest_Digest", header + source)
        self.assertNotIn("UpdateManifest_Canonicalize", header + source)
        self.assertRegex(source, r"status = verify_payload\(root,\s*&package->manifest.components\[index\]\)")
        self.assertIn("UpdateVersion_Compare(&bootloader_version,", source)

    def test_duplicate_failure_models_are_removed(self):
        source_files = [
            path.read_text(encoding="utf-8", errors="replace")
            for directory in (UPDATE / "src", UPDATE / "include", ROOT / "Application" / "src")
            for path in directory.rglob("*")
            if path.is_file() and path.suffix in {".c", ".h"}
        ]
        source = "\n".join(source_files)
        for removed_name in (
            "update_operation_result_t",
            "update_failure_t",
            "update_result_t",
            "update_outcome_t",
            "VersionPolicy_Check",
            "VersionPolicy_ValidateMinimumBootloader",
            "UPDATE_OUTCOME_",
            "boot_flow_result_t",
            "BOOT_FLOW_LAUNCH",
            "BOOT_FLOW_FATAL",
            "BOOT_FLOW_RESET",
            "UpdateComponent_IsEnabled",
            "IMAGE_TARGET_RESOURCE",
        ):
            self.assertNotIn(removed_name, source)

    def test_update_service_declares_module_dependencies_directly(self):
        source = (UPDATE / "src" / "update_service.c").read_text(encoding="utf-8", errors="replace")
        for header in (
            "current_store.h",
            "image_installer.h",
            "runtime_verifier.h",
            "update_journal_internal.h",
            "update_package.h",
        ):
            self.assertIn(f'#include "{header}"', source)
        self.assertNotIn("update_internal.h", source)

        cmake = (UPDATE / "CMakeLists.txt").read_text(encoding="utf-8", errors="replace")
        self.assertNotIn("version_policy.c", cmake)

    def test_status_based_public_interfaces_and_debug_error_split(self):
        update_service = (UPDATE / "src" / "update_service.c").read_text(
            encoding="utf-8", errors="replace"
        )
        service_header = (UPDATE / "include" / "update" / "update_service.h").read_text(
            encoding="utf-8", errors="replace"
        )
        boot_flow = (ROOT / "Application" / "src" / "boot_flow.c").read_text(
            encoding="utf-8", errors="replace"
        )
        boot_header = (ROOT / "Application" / "src" / "boot_flow.h").read_text(
            encoding="utf-8", errors="replace"
        )
        self.assertIn("firmware_status_t UpdateService_Process(void);", service_header)
        self.assertIn("firmware_status_t BootFlow_Run(uint32_t *vector_address);", boot_header)
        self.assertIn("return FIRMWARE_STATUS_INVALID_STATE;", update_service)
        self.assertIn("debug_package_status_is_ignorable", update_service)
        self.assertIn("default:", update_service)
        self.assertIn("return status;", boot_flow)


if __name__ == "__main__":
    unittest.main()
