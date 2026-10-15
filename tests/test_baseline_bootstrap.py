"""First original-source publication must not depend on an earlier local catalog."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import mhw_publish_baseline as publisher
from mhw_publish_baseline import previous_catalog,original_catalog,validate_publication_controls


class BaselineBootstrapTests(unittest.TestCase):
    def test_clean_directory_needs_no_previous_catalog(self):
        with tempfile.TemporaryDirectory() as folder:self.assertEqual(previous_catalog(Path(folder)),[])

    def test_old_catalog_is_not_silently_consumed(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'assets').mkdir();(root/'assets/catalog.json').write_text('not valid JSON')
            self.assertEqual(previous_catalog(root),[])

    def test_explicit_legacy_preservation_requires_real_existing_input(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            with self.assertRaises(FileNotFoundError):previous_catalog(root,True)
            (root/'assets').mkdir();rows=[{'slot':99,'file':'legacy.lsm','source':'Legacy explicitly recorded source'}];(root/'assets/catalog.json').write_text(json.dumps(rows))
            with self.assertRaises(FileNotFoundError):previous_catalog(root,True)
            (root/'assets/legacy.lsm').write_bytes(b'synthetic clip')
            self.assertEqual(previous_catalog(root,True),rows)

    def test_original_or_unknown_old_entries_are_not_relabelled_legacy(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'assets').mkdir()
            (root/'assets/catalog.json').write_text(json.dumps([{'slot':99,'file':'source.lsm','source':'Original MHW'}, {'slot':100}]))
            self.assertEqual(previous_catalog(root,True),[])

    def test_old_publisher_legacy_pollution_is_not_a_fresh_original_input(self):
        original={'slot':1,'source':'Original source'}
        self.assertEqual(original_catalog([original,{'slot':2,'source':'Legacy preserved'}, {'slot':1000}]),[original])

    def test_removed_legacy_cannot_leave_command_or_experimental_references(self):
        catalog=[{'slot':1,'frames':3}]
        good={'idle':1,'commands':{'attack':[1]},'experimental_commands':['attack']}
        validate_publication_controls(good,catalog)
        for invalid in ({**good,'commands':{'attack':[344]}},
                        {**good,'experimental_commands':['missing']},
                        {**good,'hold_commands':{'attack':{'releases':[{'slot':344,'seconds':0}]}}}):
            with self.assertRaises(ValueError):validate_publication_controls(invalid,catalog)

    def test_main_composes_in_a_copy_and_leaves_baked_input_unchanged_on_failure(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);stage=root/'input';stage.mkdir();source=stage/'source.json';source.write_bytes(b'original')
            def fail(previous,scratch):
                self.assertEqual(previous,[]);self.assertNotEqual(scratch,stage)
                (scratch/'source.json').write_bytes(b'changed')
                raise RuntimeError('composition failed')
            with mock.patch.object(publisher,'ROOT',root),mock.patch.object(publisher,'STAGE',stage),mock.patch.object(publisher,'publish',side_effect=fail),mock.patch.object(sys,'argv',['mhw_publish_baseline.py']):
                with self.assertRaisesRegex(RuntimeError,'composition'):publisher.main()
            self.assertEqual(source.read_bytes(),b'original')
            self.assertFalse((root/'assets').exists())


if __name__=='__main__':unittest.main()
