"""Current asset identity must follow the current manifest, even if old bytes remain."""
import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from verify_resource_overlay import current_closure_gap


class CurrentClosureTests(unittest.TestCase):
    def setUp(self):
        self.snapshot = {"assets": [{"path": "assets/character.prefab"}],
                         "bundles": [{"path": "Bundles/Windows/old.ab"}]}

    @staticmethod
    def select(manifest, *, extra_assets):
        return [], sorted({bundle for asset in extra_assets for bundle in manifest[asset]})

    def test_old_bundle_still_present_does_not_validate_retargeted_asset(self):
        manifest = {"assets/character.prefab": ["Bundles/Windows/new.ab"],
                    "assets/unrelated.prefab": ["Bundles/Windows/old.ab"]}
        gap = current_closure_gap(self.snapshot, manifest, self.select)
        self.assertEqual(gap["reason"], "current-asset-closure-changed")
        self.assertEqual(gap["new_dependencies"], ["bundles/windows/new.ab"])

    def test_added_dependency_invalidates_old_snapshot(self):
        manifest = {"assets/character.prefab": ["Bundles/Windows/old.ab", "Bundles/Windows/skin.ab"]}
        self.assertIsNotNone(current_closure_gap(self.snapshot, manifest, self.select))

    def test_same_asset_closure_can_proceed_to_byte_verification(self):
        manifest = {"assets/character.prefab": ["Bundles/Windows/old.ab"]}
        self.assertIsNone(current_closure_gap(self.snapshot, manifest, self.select))

    def test_bundles_without_asset_identity_are_not_current_proof(self):
        self.assertEqual(current_closure_gap({"bundles": []}, {}, self.select)["reason"],
                         "snapshot-has-no-asset-identities")

    def test_recorded_missing_dependency_is_never_current_proof(self):
        self.snapshot["missing"] = ["Bundles/Windows/skin.ab"]
        self.assertEqual(current_closure_gap(self.snapshot, {}, self.select)["reason"],
                         "snapshot-records-missing-resources")


if __name__ == "__main__":
    unittest.main()
