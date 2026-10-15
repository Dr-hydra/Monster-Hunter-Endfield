"""Tiny uncompressed TEX16 codec fixtures, never substitute production assets."""
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from PIL import Image

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from weapon_texture_decode import generate,resource_path


def tex(path,width,height,color):
    header=bytearray(0xc0);struct.pack_into('<II',header,0,5784916,16);struct.pack_into('<5I',header,0x14,1,width,height,1,7);struct.pack_into('<Q',header,0xb8,len(header))
    path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(header+bytes(color)*(width*height))


class TextureTests(unittest.TestCase):
    def fixture(self,root,names):
        mode=root/'assets/modes/test';(mode/'props').mkdir(parents=True)
        (mode/'props/index.json').write_text(json.dumps({'props':[{'file':'props/model.json'}]}))
        materials=[{'name':name,'resources':{'0':{'name':'AlbedoMap','value':name}}} for name in names]
        prop=mode/'props/model.json';prop.write_text(json.dumps({'id':'synthetic','materials':materials}));return mode,prop

    def test_same_basename_different_source_directories_never_collide(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);mode,prop=self.fixture(root,['first/shared','second/shared'])
            tex(root/'research/multiweapon/extracted/first/shared.tex',2,1,[255,0,0,255]);tex(root/'research/multiweapon/extracted/second/shared.tex',2,1,[0,255,0,255])
            generate(root);rows=json.loads(prop.read_text())['materials'];a,b=[r['runtime_textures']['albedo'] for r in rows]
            self.assertNotEqual(a['file'],b['file']);self.assertNotEqual((mode/a['file']).read_bytes(),(mode/b['file']).read_bytes())

    def test_rerun_updates_pixels_dimensions_png_and_metadata_together(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);mode,prop=self.fixture(root,['source/shared']);source=root/'research/multiweapon/extracted/source/shared.tex'
            tex(source,2,1,[255,0,0,255]);generate(root);tex(source,1,3,[0,0,255,255]);report=generate(root)
            row=json.loads(prop.read_text())['materials'][0]['runtime_textures']['albedo'];self.assertEqual((row['width'],row['height']),(1,3));self.assertEqual((mode/row['file']).read_bytes(),bytes([0,0,255,255])*3)
            with Image.open(mode/row['png']) as image:self.assertEqual(image.size,(1,3));self.assertEqual(image.getpixel((0,0)),(0,0,255,255))
            self.assertEqual(report['runtime_outputs'],1);self.assertEqual(generate(root)['runtime_outputs'],0)

    def test_missing_later_source_does_not_replace_earlier_pixels_or_metadata(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);mode,prop=self.fixture(root,['first/shared','second/shared']);first=root/'research/multiweapon/extracted/first/shared.tex';second=root/'research/multiweapon/extracted/second/shared.tex'
            tex(first,1,1,[1,2,3,255]);tex(second,1,1,[4,5,6,255]);generate(root);before={p:p.read_bytes() for p in mode.rglob('*') if p.is_file()}
            tex(first,1,1,[7,8,9,255]);second.unlink()
            with self.assertRaises(FileNotFoundError):generate(root)
            self.assertEqual(before,{p:p.read_bytes() for p in mode.rglob('*') if p.is_file()})

    def test_resource_paths_cannot_escape_output(self):
        for value in ('../x','C:/x','/x','a/../x','a//x'):
            with self.subTest(value=value),self.assertRaises(ValueError):resource_path(value)


if __name__=='__main__':unittest.main()
