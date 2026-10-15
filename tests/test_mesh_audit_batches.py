from pathlib import Path
import json
import subprocess
import sys
import tempfile
import unittest
from unittest import mock
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from run_mesh_audits import groups,merge_rows,P0
import run_mesh_audits as batches


class MeshAuditBatchTests(unittest.TestCase):
    def test_worker_failure_never_replaces_previous_complete_reports(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);(root/'profiles').mkdir()
            (root/'profiles/index.json').write_text(json.dumps({'modes':sorted(batches.MODES),
                'characters':[{'character_id':'one','profile':'one.json'}]}))
            old=root/'research/character-coverage';old.mkdir(parents=True)
            previous={old/'coverage.json':b'previous ordinary',old/'special-cross-coverage.json':b'previous special'}
            for path,data in previous.items():path.write_bytes(data)
            with mock.patch.object(batches,'validate_native_build',return_value={}),mock.patch.object(batches,'verify_oracle_provenance',return_value={}),mock.patch.object(batches,'run_worker',side_effect=subprocess.CalledProcessError(1,'synthetic worker')):
                with self.assertRaises(subprocess.CalledProcessError):batches.run(root,root/'output/new-audit',2)
            self.assertEqual({path:path.read_bytes() for path in previous},previous)

    def test_each_character_has_one_writer_and_priority_costs_are_distributed(self):
        characters=[*P0,*['chr_'+str(i) for i in range(31)]]
        result=groups(characters,4)
        self.assertEqual(sorted(sum(result,[])),sorted(characters))
        self.assertTrue(all(len(set(part)&P0)<=1 for part in result))
        with self.assertRaises(ValueError):groups(['same','same'],2)

    def test_partial_duplicate_or_extra_reports_never_form_a_complete_batch(self):
        first={'character_id':'one','mode':'longsword'};second={'character_id':'two','mode':'bow'}
        expected={('one','longsword'),('two','bow')}
        self.assertEqual(merge_rows([{'combinations':[second]},{'combinations':[first]}],expected),[first,second])
        for rows in ([first],[first,first,second],[first,second,{'character_id':'extra','mode':'bow'}]):
            with self.assertRaises(ValueError):merge_rows([{'combinations':rows}],expected)


if __name__=='__main__':unittest.main()
