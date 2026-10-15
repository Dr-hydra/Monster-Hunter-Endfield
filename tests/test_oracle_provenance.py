"""Oracle generation provenance tests use isolated files and a fake native process."""
import copy
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from unittest import mock


spec = importlib.util.spec_from_file_location('standalone_oracle_provenance', Path(__file__).resolve().parents[1]/'tools/audit_oracle_provenance.py')
oracle = importlib.util.module_from_spec(spec)
spec.loader.exec_module(oracle)


def put(root, name, value):
    path = root/name
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(value if isinstance(value, bytes) else json.dumps(value).encode())
    return path


def fixture(root):
    modes = sorted(oracle._contract.MODES)
    put(root, 'profiles/index.json', {'characters': [{'character_id': 'chr_test', 'profile': 'test.json'}], 'modes': modes})
    put(root, 'profiles/test.json', {'bone_paths': {'hip': 'root/hip'}, 'finger_bones': {},
                                  'ground_foot_mesh_rest_min_y': -0.02, 'display_name': 'first'})
    put(root, 'research/character-coverage/chr_test-rig.json', {'registered_model_rest': {}})
    entries = []
    for mode in modes:
        # Exercise the native hard-coded longsword finger root independently of
        # the registry's source-body asset root.
        data_root = 'assets/alternate-longsword' if mode == 'longsword' else 'assets/modes/'+mode
        entries.append({'id': mode, 'data_root': data_root})
        put(root, data_root+'/source_rig.json', {'order': []})
        put(root, data_root+'/moves.json', {'commands': {'attack': [7]}, 'idle': 8})
        put(root, data_root+'/catalog.json', [{'slot': 7, 'file': 'attack.lsm'}, {'slot': 8, 'file': 'idle.lsm'}])
        put(root, data_root+'/attack.lsm', b'synthetic source body')
        put(root, data_root+'/idle.lsm', b'synthetic source idle')
        finger_root = 'assets/modes/longsword' if mode == 'longsword' else data_root
        put(root, finger_root+'/finger_tracks.json', {'bones': [], 'tracks': {'7': {'file': 'finger.lsf'}}})
        put(root, finger_root+'/finger.lsf', b'synthetic source finger')
        put(root, oracle.ORACLE_NAME+'/chr_test/'+mode+'.json', {'character_id': 'chr_test', 'mode': mode})
        put(root, oracle.ORACLE_NAME+'/chr_test/'+mode+'.ltp', b'LTP1 synthetic oracle poses')
    put(root, 'assets/modes/index.json', {'modes': entries})
    stamp = 1_700_000_000_000_000_000
    for name in oracle._contract.NATIVE_INPUTS:
        path = put(root, name, b'synthetic native source')
        os.utime(path, ns=(stamp, stamp))
    for name in ('endfield_pose_solver.dll', 'endfield_retarget_oracle.exe'):
        path = put(root, 'build/offline-retarget/Release/'+name, b'synthetic native binary')
        os.utime(path, ns=(stamp+100, stamp+100))
    return modes


def copy_generation(command, **kwargs):
    root, staging = map(Path, command[1:])
    for path in (root/oracle.ORACLE_NAME).rglob('*'):
        if path.is_file():
            dest = staging/path.relative_to(root/oracle.ORACLE_NAME)
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(path, dest)
    return subprocess.CompletedProcess(command, 0)


class OracleProvenanceTests(unittest.TestCase):
    def make_proof(self, root):
        with mock.patch.object(oracle.subprocess, 'run', side_effect=copy_generation) as process:
            result = oracle.rebuild_compare_provenance(root)
        self.assertEqual(process.call_count, 1)
        return result

    def test_real_compare_path_is_required_and_audited_outputs_are_untouched(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); fixture(root)
            before = {str(p.relative_to(root)): (p.read_bytes(), p.stat().st_mtime_ns)
                      for p in (root/oracle.ORACLE_NAME).rglob('*') if p.is_file()}
            with self.assertRaisesRegex(ValueError, 'proof missing'):
                oracle.verify_oracle_provenance(root)
            self.assertEqual(self.make_proof(root)['pairs'], 6)
            self.assertTrue(oracle.verify_oracle_provenance(root)['verified'])
            self.assertEqual(before, {str(p.relative_to(root)): (p.read_bytes(), p.stat().st_mtime_ns)
                                    for p in (root/oracle.ORACLE_NAME).rglob('*') if p.is_file()})

    def test_track_rig_and_native_binary_changes_invalidate_proof(self):
        names = ['assets/alternate-longsword/attack.lsm', 'assets/alternate-longsword/idle.lsm',
                 'assets/modes/longsword/finger.lsf', 'assets/alternate-longsword/source_rig.json',
                 'research/character-coverage/chr_test-rig.json',
                 'build/offline-retarget/Release/endfield_retarget_oracle.exe']
        for name in names:
            with self.subTest(input=name), tempfile.TemporaryDirectory() as folder:
                root = Path(folder); fixture(root); self.make_proof(root)
                with (root/name).open('ab') as stream:
                    stream.write(b' ')
                with self.assertRaisesRegex(ValueError, 'inputs changed'):
                    oracle.verify_oracle_provenance(root)

    def test_only_actual_native_profile_fields_are_bound(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); fixture(root); self.make_proof(root)
            profile = oracle._read(root/'profiles/test.json')
            profile['display_name'] = 'renamed'; profile['ground_foot_probes'] = [{'irrelevant_to_oracle': True}]
            put(root, 'profiles/test.json', profile)
            self.assertTrue(oracle.verify_oracle_provenance(root)['verified'])
            original = copy.deepcopy(profile)
            for key, value in [('bone_paths', {'hip': 'wrong/hip'}), ('finger_bones', {'finger': 'wrong'}),
                               ('ground_foot_mesh_rest_min_y', -0.1)]:
                changed = {**original, key: value}; put(root, 'profiles/test.json', changed)
                with self.subTest(field=key), self.assertRaisesRegex(ValueError, 'inputs changed'):
                    oracle.verify_oracle_provenance(root)

    def test_regenerated_difference_never_replaces_last_valid_proof(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); fixture(root); self.make_proof(root)
            path = root/oracle.PROOF_NAME; original = path.read_bytes()
            def changed(command, **kwargs):
                copy_generation(command, **kwargs)
                (Path(command[2])/'chr_test/bow.ltp').write_bytes(b'LTP1 DIFFERENT pose result')
            with mock.patch.object(oracle.subprocess, 'run', side_effect=changed):
                with self.assertRaisesRegex(ValueError, 'differs from audited'):
                    oracle.rebuild_compare_provenance(root)
            self.assertEqual(path.read_bytes(), original)

    def test_native_failure_or_incomplete_batch_never_creates_proof(self):
        for failure in ('native', 'missing', 'extra'):
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as folder:
                root = Path(folder); fixture(root)
                def run(command, **kwargs):
                    if failure == 'native':
                        raise subprocess.CalledProcessError(1, command)
                    copy_generation(command, **kwargs)
                    if failure == 'missing':
                        (Path(command[2])/'chr_test/bow.ltp').unlink()
                    else:
                        (Path(command[2])/'unexpected.json').write_text('{}')
                with mock.patch.object(oracle.subprocess, 'run', side_effect=run):
                    with self.assertRaises((ValueError, subprocess.CalledProcessError)):
                        oracle.rebuild_compare_provenance(root)
                self.assertFalse((root/oracle.PROOF_NAME).exists())

    def test_generation_input_change_during_native_run_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); fixture(root)
            def changed(command, **kwargs):
                copy_generation(command, **kwargs)
                with (root/'assets/alternate-longsword/attack.lsm').open('ab') as stream:
                    stream.write(b' changed during native run')
            with mock.patch.object(oracle.subprocess, 'run', side_effect=changed):
                with self.assertRaisesRegex(ValueError, 'inputs changed during rebuild'):
                    oracle.rebuild_compare_provenance(root)
            self.assertFalse((root/oracle.PROOF_NAME).exists())

    def test_output_change_after_proof_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); fixture(root); self.make_proof(root)
            with (root/oracle.ORACLE_NAME/'chr_test/bow.ltp').open('ab') as stream:
                stream.write(b' changed')
            with self.assertRaisesRegex(ValueError, 'outputs changed'):
                oracle.verify_oracle_provenance(root)

    def test_optional_finger_manifest_and_unredirected_longsword_root_are_bound(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); fixture(root)
            inputs = oracle.generation_inputs(root)
            names = {row['path'] for row in inputs['files']}
            self.assertIn('assets/modes/longsword/finger.lsf', names)
            self.assertNotIn('assets/alternate-longsword/finger.lsf', names)
            manifest = root/'assets/modes/bow/finger_tracks.json'; original = manifest.read_bytes()
            manifest.unlink(); self.make_proof(root); manifest.write_bytes(original)
            with self.assertRaisesRegex(ValueError, 'inputs changed'):
                oracle.verify_oracle_provenance(root)


if __name__ == '__main__':
    unittest.main()
