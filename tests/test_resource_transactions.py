"""Synthetic staging failures must preserve the preceding complete output batch."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from resource_transaction import ResourceTransaction


class TransactionTests(unittest.TestCase):
    def test_export_failure_leaves_old_outputs_and_index_unchanged(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'profiles').mkdir();old=root/'profiles/one.json';old.write_bytes(b'old-profile');index=root/'profiles/index.json';index.write_bytes(b'old-index')
            with self.assertRaisesRegex(RuntimeError,'inspector'):
                with ResourceTransaction(root) as batch:
                    batch.write_json(old,{'new':1});batch.write_json(index,{'new':2})
                    partial=batch.directory(root/'research/geometry');(partial/'partial.json').write_text('{}')
                    raise RuntimeError('inspector failed after a partial export')
            self.assertEqual(old.read_bytes(),b'old-profile');self.assertEqual(index.read_bytes(),b'old-index');self.assertFalse((root/'research/geometry').exists())

    def test_publish_failure_rolls_back_replaced_and_new_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);old=root/'old.json';old.write_bytes(b'old');new=root/'new.json';replace=os.replace;calls=0
            def fail_second(source,destination):
                nonlocal calls
                calls+=1
                if calls==2:raise OSError('injected replace failure')
                return replace(source,destination)
            with ResourceTransaction(root) as batch:
                batch.write_json(old,{'new':1});batch.write_json(new,{'new':2})
                with mock.patch('resource_transaction.os.replace',side_effect=fail_second),self.assertRaises(OSError):batch.commit()
            self.assertEqual(old.read_bytes(),b'old');self.assertFalse(new.exists())

    def test_complete_batch_publishes_generated_directory_and_json(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            with ResourceTransaction(root) as batch:
                staging=batch.directory('geometry');(staging/'mesh.json').write_text('{"vertices": []}')
                batch.add_directory('geometry');batch.write_json('profiles/index.json',{'characters':['one']});batch.commit()
            self.assertTrue((root/'geometry/mesh.json').is_file());self.assertEqual(json.loads((root/'profiles/index.json').read_text()),{'characters':['one']})

    def test_outside_workspace_is_never_a_publish_target(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)/'workspace';root.mkdir()
            with ResourceTransaction(root) as batch,self.assertRaises(ValueError):batch.write_json(root.parent/'outside.json',{})


if __name__=='__main__':unittest.main()
