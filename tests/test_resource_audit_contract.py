"""Current audit proof is based on actual inputs, never file/image existence alone."""
import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
import resource_audit_contract as contract


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding='utf-8')


def fixture(root):
    profile = {'character_id': 'chr_a', 'bone_paths': ['root/hand'], 'evidence': {'source_graph': 'graph.json'}}
    write_json(root/'assets/modes/index.json', {'modes': [{'id': 'longsword', 'data_root': 'assets/modes/longsword'}]})
    write_json(root/'profiles/index.json', {'characters': [{'character_id': 'chr_a', 'profile': 'chr_a.json'}]})
    write_json(root/'research/character-coverage/chr_a-rig.json', {'rest': []})
    write_json(root/'assets/modes/longsword/catalog.json', [])
    write_json(root/'graph.json', {'input': 'current'})
    write_json(root/'research/character-coverage/geometry/chr_a/inspection.json', [{'kind': 'Mesh', 'file': 'mesh.json'}])
    write_json(root/'research/character-coverage/geometry/chr_a/mesh.json', {'vertices': [0, 1, 2]})
    for name in ('research/character-coverage/oracle/chr_a/longsword.json',
                 'research/character-coverage/oracle/chr_a/longsword.ltp',
                 *contract.PYTHON_INPUTS, *contract.NATIVE_INPUTS,
                 'build/offline-retarget/Release/endfield_retarget_oracle.exe',
                 'build/offline-retarget/Release/endfield_pose_solver.dll'):
        path=root/name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(b'synthetic')
    return profile


class AuditContractTests(unittest.TestCase):
    def test_registry_cannot_reduce_audit_requirements_to_one_mode(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            write_json(root/'profiles/index.json',{'characters':[{'character_id':'chr_a'}],'modes':['longsword']})
            write_json(root/'research/character-coverage/coverage.json',{'combinations':[]})
            with self.assertRaisesRegex(ValueError,'six'):contract.validate_all(root)

    def test_failed_pending_missing_errors_and_old_proof_never_qualify(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder); profile=fixture(root)
            good={'status':'offline_mesh_checked','errors':[], 'input_contract':contract.input_contract(root,'chr_a','longsword',profile)}
            contract.validate_current(root,'chr_a','longsword',profile,good)
            invalids=[{**good,'status':'offline_mesh_failed'}, {**good,'status':'pending'},
                      {**good,'errors':['nonfinite skin']}, {k:v for k,v in good.items() if k!='errors'},
                      {k:v for k,v in good.items() if k!='input_contract'}]
            for row in invalids:
                with self.subTest(row=row.get('status')), self.assertRaises(ValueError):
                    contract.validate_current(root,'chr_a','longsword',profile,row)

    def test_graph_geometry_track_or_profile_change_invalidates_real_proof(self):
        paths=['graph.json', 'research/character-coverage/geometry/chr_a/mesh.json',
               'assets/modes/longsword/catalog.json', 'research/character-coverage/oracle/chr_a/longsword.ltp',
               'native/retarget.h', 'native/core.h', 'tools/endfield_profiles.py', 'tools/laevat_profile.py',
               'tools/offline_retarget/pose_solver.cpp', 'tools/offline_retarget/main.cpp',
               'research/character-coverage/chr_a-rig.json',
               'build/offline-retarget/Release/endfield_pose_solver.dll']
        for name in paths:
            with self.subTest(input=name), tempfile.TemporaryDirectory() as folder:
                root=Path(folder); profile=fixture(root)
                row={'status':'offline_mesh_checked','errors':[], 'input_contract':contract.input_contract(root,'chr_a','longsword',profile)}
                with (root/name).open('ab') as stream: stream.write(b' changed')
                with self.assertRaises(ValueError): contract.validate_current(root,'chr_a','longsword',profile,row)
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);profile=fixture(root)
            row={'status':'offline_mesh_checked','errors':[], 'input_contract':contract.input_contract(root,'chr_a','longsword',profile)}
            profile['bone_paths']=['root/wrong_hand']
            with self.assertRaises(ValueError): contract.validate_current(root,'chr_a','longsword',profile,row)

    def test_numerical_library_environment_changes_invalidate_the_audit(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);profile=fixture(root)
            row={'status':'offline_mesh_checked','errors':[], 'input_contract':contract.input_contract(root,'chr_a','longsword',profile)}
            with mock.patch.object(contract,'numerical_environment',return_value={'packages':{'numpy':'changed'}}):
                with self.assertRaises(ValueError):contract.validate_current(root,'chr_a','longsword',profile,row)

    def test_special_contract_covers_weapon_graph_mesh_and_profile(self):
        for changed in ('graph', 'mesh', 'profile'):
            with self.subTest(changed=changed), tempfile.TemporaryDirectory() as folder:
                root=Path(folder);profile=fixture(root)
                write_json(root/'profiles/weapons/index.json',{'weapons':[{'id':'laevatain_blade','profile':'blade.json'}]})
                weapon=root/'profiles/weapons/blade.json';write_json(weapon,{'resource_id':'wpn_misc_0016'})
                graph=root/'research/expansion-20261013/laevat-weapon-raw.json';write_json(graph,{'graph':1})
                geo=root/'research/weapon-coverage/laevat-geometry'
                write_json(geo/'inspection.json',[{'file':'blade.json'}]);write_json(geo/'blade.json',{'vertices':[]})
                for script in ('endfield_special_mesh_audit.py','endfield_special_previews.py'):
                    (root/'tools'/script).write_text('synthetic')
                row={'character_id':'chr_a','mode':'longsword','weapon':'wpn_misc_0016',
                     'status':'offline_special_mesh_checked','errors':[],
                     'input_contract':contract.special_input_contract(root,'chr_a','laevatain_blade',profile)}
                contract.validate_special_current(root,'chr_a','laevatain_blade',profile,row)
                write_json({'graph':graph,'mesh':geo/'blade.json','profile':weapon}[changed],{'changed':True})
                with self.assertRaises(ValueError): contract.validate_special_current(root,'chr_a','laevatain_blade',profile,row)


if __name__ == '__main__': unittest.main()
