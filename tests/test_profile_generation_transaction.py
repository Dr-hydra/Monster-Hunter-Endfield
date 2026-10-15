"""The actual generator must leave the previous batch intact after late failures."""
import contextlib
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock
import numpy as np

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import endfield_profiles as generator
from resource_transaction import ResourceTransaction


class ProfileGenerationTransactionTests(unittest.TestCase):
    def test_external_workspace_environment_reaches_imported_legacy_cache(self):
        with tempfile.TemporaryDirectory() as folder:
            custom=Path(folder)/'external-tools'
            script='import json,sys;sys.path.insert(0,sys.argv[1]);import endfield_profiles as p;print(json.dumps({"be":str(p.BE),"cache":str(p.CACHE)}))'
            result=subprocess.run([sys.executable,'-c',script,str(Path(generator.__file__).parent)],
                env={**os.environ,'MHE_BE_ROOT':str(custom)},check=True,capture_output=True,text=True)
            values=json.loads(result.stdout)
            self.assertEqual(Path(values['be']),custom.resolve())
            self.assertEqual(Path(values['cache']),custom.resolve()/'research/custom_model/1.5.3/shared/postmodel-native-parser-history')

    def fixture(self,root,late_registry_failure):
        def put(name,value):
            path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(json.dumps(value),encoding='utf-8');return path
        cid='chr_0001_test';input_root=root/'input';graph_root=root/'graphs';raw_root=root/'raw'
        put('input/tables/source-metadata.json',{'gameVersion':'test','kind':'persistent+base'})
        chars={cid:{'weaponType':1,'name':{'id':'test'}}}
        if late_registry_failure:chars['chr_0002_other']={'weaponType':1}
        put('input/tables/Table/CharacterTable.json',chars)
        put('input/tables/Table/WeaponBasicTable.json',{})
        put('input/tables/Table/I18nTextTable_CN.json',{'test':'Test'})
        put('input/catalog/characters.json',{'characters':[{'characterId':cid}]})
        table=put('table-proof.json',{'game_version':'test','input_root':str(input_root),
            'overlay_order':['StreamingAssets','Persistent'],'tables':[{'table':name,'current_vfs_equal':True} for name in ('CharacterTable','WeaponBasicTable','I18nTextTable_CN')]})
        overlay=put('overlay.json',{'overlay_order':['StreamingAssets','Persistent'],
            'snapshots':[{'snapshot':str(raw_root/'extraction.json'),'current_vfs_verified':True,'current_closure_verified':True}]})
        put('graphs/'+cid+'.json',{'input_directory':str(raw_root),'snapshot':{'missing':[],'manifest_version':'test'}})
        old=put('profiles/'+cid+'.json',{'preserved':'old profile'})
        index=put('profiles/index.json',{'characters':[{'character_id':cid,'profile':cid+'.json'}]})
        put('profiles/weapons/equipment-index.json',{'equipment':[]})
        inspection=put('research/character-coverage/geometry/'+cid+'/inspection.json',[{'old':True}])
        bones={semantic:'root/'+semantic for semantic in generator.SEMANTICS}
        analyzed={'graph':SimpleNamespace(paths={path:i for i,path in enumerate(bones.values())}),
            'skeleton':'root','bones':bones,'fingers':{},'palette':list(range(21)),
            'sockets':{'wep_R':{'path':'root/weapon','position':[0,0,0],'rotation_xyzw':[0,0,0,1]}},
            'lengths':{},'renderers':[],'locals':{},'skin_rest':{path:np.eye(4) for path in bones.values()},
            'rest_evidence':{path:'synthetic' for path in bones.values()},'registration':np.eye(4),
            'registrations':{},'registration_error':{'test':0},'rest':{}}
        args=SimpleNamespace(input_root=input_root,game_version='test',graphs=graph_root,overlay_report=overlay,
            table_report=table,allow_backend_warnings=False,characters=[cid],export_geometry=not late_registry_failure)
        return args,analyzed,{path:path.read_bytes() for path in (old,index,inspection)}

    def test_late_roster_validation_keeps_old_profile_index_and_geometry(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);args,analyzed,before=self.fixture(root,True)
            with mock.patch.object(generator,'ROOT',root),mock.patch.object(generator,'analyze',return_value=analyzed),contextlib.redirect_stdout(io.StringIO()):
                with ResourceTransaction(root) as transaction,self.assertRaisesRegex(ValueError,'registry'):
                    generator.generate(args,transaction);transaction.commit()
            self.assertEqual({path:path.read_bytes() for path in before},before)
            self.assertFalse((root/'research/character-coverage/chr_0001_test-rig.json').exists())

    def test_partial_external_export_keeps_old_batch(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);args,analyzed,before=self.fixture(root,False)
            def fail(command,**_):
                (Path(command[3])/'partial-mesh.json').write_text('{}')
                raise subprocess.CalledProcessError(1,command)
            with mock.patch.object(generator,'ROOT',root),mock.patch.object(generator,'analyze',return_value=analyzed),mock.patch.object(generator.subprocess,'run',side_effect=fail):
                with ResourceTransaction(root) as transaction,self.assertRaises(subprocess.CalledProcessError):
                    generator.generate(args,transaction);transaction.commit()
            self.assertEqual({path:path.read_bytes() for path in before},before)
            self.assertFalse((root/'research/character-coverage/geometry/chr_0001_test/partial-mesh.json').exists())


if __name__=='__main__':unittest.main()
