"""Exercise the real complete-product validator with small synthetic package files."""
import copy
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

spec=importlib.util.spec_from_file_location('mhe_package_mesh_gate',Path(__file__).resolve().parents[1]/'tools/package.py')
package=importlib.util.module_from_spec(spec);spec.loader.exec_module(package)


class ProductMeshGateTests(unittest.TestCase):
    def fixture(self, root):
        entries={}
        def put(name, value):
            path=root/name;path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(value if isinstance(value,bytes) else json.dumps(value).encode())
            entries[name]=path
        modes=sorted(package.MODES)
        source={'game_version':'synthetic','overlay_order':['StreamingAssets','Persistent'],
                'character_ids':['chr_a'],'equipment_ids':['equipment_a']}
        put('profiles/index.json',{'characters':[{'character_id':'chr_a','profile':'chr_a.json'}],'current_source':source})
        weapon_ids=['equipment_a','laevatain_blade','typhoea_bow']
        put('profiles/weapons/index.json',{'weapons':[{'id':key,'profile':key+'.json'} for key in weapon_ids]})
        put('profiles/weapons/equipment-index.json',{'equipment':[{'weapon_id':'equipment_a'}]})
        profile={'character_id':'chr_a','bone_paths':['root/hand']}
        put('profiles/chr_a.json',profile)
        put('assets/preview.json',{})
        put('assets/reference.webp',b'synthetic image fixture')
        put('assets/sound.wav',b'synthetic audio fixture')
        status={'offline_status':'mesh_checked','audit_status':'offline_mesh_checked','errors':[],
                'current_inputs_verified':True,'reference_images':[{'file':'reference.webp'}]}
        tours=[]
        for mode in modes:
            name='previews/source-tours/'+mode+'.mp4'
            put('assets/'+name,b'\0\0\0\x20ftyp'+b'\0'*1024)
            tours.append({'id':mode,'file':name,'seconds':1,'fps':10,'source_mesh_frames':1,'distinct_pcm':1,
                          'game_launched':False,'visual_quality_certified':False,'evidence':'synthetic','limitations':['test only']})
        put('assets/tours.json',{'schema_version':1,'modes':tours})
        put('assets/build_provenance.json',{'version':'test','modes':[{'id':mode} for mode in modes],
                                          'current_endfield_source':source,'characters_pending_current_vfs_validation':[]})
        put('assets/audio_catalog.json',{'modes':{mode:{'clips':[{'file':'sound.wav'}]} for mode in modes}})
        weapons=[]
        for key in weapon_ids:
            value={'resource_id':key};name='profiles/weapons/'+key+'.json';put(name,value)
            row={'id':key,'model_profile':name,'modes':['bow' if key=='typhoea_bow' else 'longsword']}
            if key!='equipment_a':
                row.update(offline_status='mesh_checked',audit_weapon_profile=value,
                    character_audits={'chr_a':{'status':'offline_special_mesh_checked','errors':[],
                                             'current_inputs_verified':True,'character_profile_ref':'chr_a'}})
            weapons.append(row)
        product={'version':'test','modes':[{'id':mode} for mode in modes], 'preview':{'file':'preview.json'},
                 'source_tours':{'file':'tours.json','modes':tours},'weapons':weapons,
                 'characters':[{'id':'chr_a','audit_profile_inputs':profile,'modes':{mode:copy.deepcopy(status) for mode in modes}}]}
        put('assets/product_catalog.json',product)
        return entries,product,put

    def test_complete_product_rejects_bad_ordinary_audit_with_valid_reference_image(self):
        with tempfile.TemporaryDirectory() as folder:
            entries,product,put=self.fixture(Path(folder))
            package.validate_product(entries,{'version':'test'})
            for changes in ({'offline_status':'failed'},{'offline_status':'pending'},
                            {'audit_status':'offline_mesh_failed'},{'errors':['nonfinite skin']},
                            {'errors':None},{'current_inputs_verified':False}):
                invalid=copy.deepcopy(product);invalid['characters'][0]['modes']['bow'].update(changes)
                put('assets/product_catalog.json',invalid)
                with self.subTest(changes=changes),self.assertRaises(ValueError): package.validate_product(entries,{'version':'test'})
            put('assets/product_catalog.json',product)
            put('profiles/chr_a.json',{'character_id':'chr_a','bone_paths':['root/different_hand']})
            with self.assertRaises(ValueError):package.validate_product(entries,{'version':'test'})

    def test_both_special_weapons_require_every_target_and_empty_error_list(self):
        with tempfile.TemporaryDirectory() as folder:
            entries,product,put=self.fixture(Path(folder))
            for index in (1,2):
                for changes in ({'status':'offline_special_mesh_failed'},{'status':'pending'},
                                {'errors':['unbounded indexed skin']},{'errors':None},{'current_inputs_verified':False},
                                {'character_profile_ref':'wrong'}):
                    invalid=copy.deepcopy(product);invalid['weapons'][index]['character_audits']['chr_a'].update(changes)
                    put('assets/product_catalog.json',invalid)
                    with self.subTest(weapon=index,changes=changes),self.assertRaises(ValueError):package.validate_product(entries,{'version':'test'})
                invalid=copy.deepcopy(product);invalid['weapons'][index]['character_audits']={}
                put('assets/product_catalog.json',invalid)
                with self.assertRaises(ValueError):package.validate_product(entries,{'version':'test'})
            put('assets/product_catalog.json',product)
            put('profiles/weapons/laevatain_blade.json',{'resource_id':'changed'})
            with self.assertRaises(ValueError):package.validate_product(entries,{'version':'test'})


if __name__=='__main__':unittest.main()
