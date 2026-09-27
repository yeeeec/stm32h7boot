from pathlib import Path
import re
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
            "VersionPolicy_Check",
            "VersionPolicy_ValidateMinimumBootloader",
            "UPDATE_OUTCOME_NO_CHANGE",
            "UPDATE_OUTCOME_INSTALLED",
            "UPDATE_OUTCOME_RESET",
            "BOOT_FLOW_RESET",
        ):
            self.assertNotIn(removed_name, source)

        types = (UPDATE / "include" / "update" / "update_types.h").read_text(
            encoding="utf-8", errors="replace"
        )
        result = re.search(r"typedef struct\s*\{([^}]*)\}\s*update_result_t", types, re.S)
        self.assertIsNotNone(result)
        self.assertRegex(result.group(1), r"update_outcome_t\s+outcome;")
        self.assertRegex(result.group(1), r"firmware_status_t\s+status;")
        self.assertNotIn("failure", result.group(1))

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


if __name__ == "__main__":
    unittest.main()
