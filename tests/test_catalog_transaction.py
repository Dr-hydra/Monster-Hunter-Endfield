"""A late catalog failure must roll back reference images, preview and metadata."""
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock
from PIL import Image
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import build_product_catalog as catalog


class CatalogTransactionTests(unittest.TestCase):
    def test_failure_on_last_audio_publish_restores_the_entire_prior_catalog_batch(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            def put(name,value):
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True)
                path.write_text(json.dumps(value),encoding='utf-8');return path
            entries=[];audits=[]
            for mode in catalog.MODE_ORDER:
                prefix='assets/modes/'+mode
                entries.append({'id':mode,'data_root':prefix,'audio_root':prefix+'/audio'})
                put(prefix+'/moves.json',{'commands':{'attack':[1]},'labels':{'1':'synthetic'}})
                put(prefix+'/catalog.json',[{'slot':1,'file':'clip.lsm','frames':2}])
                (root/prefix/'clip.lsm').write_bytes(b'synthetic source')
                put('preview/modes/'+mode+'/slot_00001.json',{'bones':['b'+str(i) for i in range(21)],'positions':[[[0,1,0]]*21]*2,'fps':60})
                image=root/'research/gallery'/('chr_a-'+mode+'.png');image.parent.mkdir(parents=True,exist_ok=True)
                Image.new('RGB',(2,2),'red').save(image)
                audits.append({'character_id':'chr_a','mode':mode,'status':'offline_mesh_checked','errors':[],
                               'evidence':[image.relative_to(root).as_posix()]})
            put('assets/modes/index.json',{'modes':entries})
            put('profiles/index.json',{'characters':[{'character_id':'chr_a','profile':'chr_a.json'}]})
            put('profiles/chr_a.json',{'character_id':'chr_a','bone_paths':{'hand':'root/hand'}})
            put('profiles/weapons/index.json',{'weapons':[]})
            put('research/character-coverage/coverage.json',{'combinations':audits})
            put('module.json',{'version':'synthetic'})
            names=['assets/'+name for name in ('product_catalog.json','audio_catalog.json','build_provenance.json','ui_modes_preview.json')]
            names+=['assets/ui_reference/chr_a-'+mode+'.webp' for mode in catalog.MODE_ORDER]
            before={}
            for name in names:
                path=root/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(('previous '+name).encode());os.utime(path,ns=(1000000000,1000000000));before[path]=path.read_bytes()
            replace=os.replace
            def fail_audio(source,destination):
                if Path(destination)==root/'assets/audio_catalog.json':raise OSError('injected final audio publication failure')
                replace(source,destination)
            with mock.patch.object(catalog,'validate_native_build',return_value={}),mock.patch.object(catalog,'verify_oracle_provenance',return_value={},create=True),mock.patch.object(catalog,'validate_current',return_value={}),mock.patch('resource_transaction.os.replace',side_effect=fail_audio):
                with self.assertRaisesRegex(OSError,'final audio'):catalog.build(root)
            self.assertEqual({path:path.read_bytes() for path in before},before)
            self.assertFalse(any((root/'output/resource-staging').rglob('*.building')))


if __name__=='__main__':unittest.main()
