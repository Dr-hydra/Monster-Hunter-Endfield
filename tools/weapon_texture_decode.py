"""Decode source MRL3-linked MHW TEX16 textures to local PNG + runtime RGBA.

Uses Pillow's BCn decoder. Keeps source color transfer and UV orientation;
all resource paths come from the actual MRL3 resource dictionary, not names.
"""
from pathlib import Path, PurePosixPath
import argparse, io, json, struct
from resource_transaction import ResourceTransaction
from PIL import Image
ROOT=Path(__file__).resolve().parents[1]
BC={22:(1,'BC1','RGBA'),23:(1,'BC1','RGBA'),24:(4,'BC4','L'),26:(5,'BC5','RGB'),28:(6,'BC6H','RGB'),29:(6,'BC6HS','RGB'),30:(7,'BC7','RGBA'),31:(7,'BC7','RGBA')}
def decode(path):
 data=path.read_bytes();magic,version=struct.unpack_from('<II',data)
 if (magic,version)!=(5784916,16):raise ValueError('unsupported TEX header')
 mip,w,h,depth,fmt=struct.unpack_from('<5I',data,0x14);offset=struct.unpack_from('<Q',data,0xb8)[0]
 if depth!=1 or not(0<w<=16384 and 0<h<=16384):raise ValueError('unsupported volume/size')
 if fmt in BC:
  kind,name,mode=BC[fmt];image=Image.frombytes(mode,(w,h),data[offset:],'bcn',(kind,name)).convert('RGBA')
 elif fmt in (7,9):image=Image.frombytes('RGBA',(w,h),data[offset:offset+w*h*4])
 else:raise ValueError('unsupported format '+str(fmt))
 return image,{'source_format':fmt,'source_width':w,'source_height':h,'source_mip_count':mip,'color_space':'sRGB' if fmt in(9,23,31) else 'linear'}
def resource_path(value):
 relative=value.replace('\\','/')
 if not relative.lower().endswith('.tex'):relative+='.tex'
 path=PurePosixPath(relative)
 if path.is_absolute() or ':' in relative or any(p in ('','.', '..') for p in relative.split('/')) or any(ord(c)<32 for c in relative):
  raise ValueError('Unsafe source texture path: '+relative)
 return path

def generate(root):
 root=Path(root).resolve();count=0;paths=set();audit=[]
 with ResourceTransaction(root) as transaction:
  for index in sorted((root/'assets/modes').glob('*/props/index.json')):
   mode=index.parent.parent;textures=mode/'props/textures'
   for item in json.loads(index.read_text(encoding='utf-8'))['props']:
    prop_path=(mode/item['file']).resolve();prop_path.relative_to(mode.resolve());prop=json.loads(prop_path.read_text(encoding='utf-8'))
    for material in prop.get('materials',[]):
     runtime={}
     for resource in material.get('resources',{}).values():
      use=resource['name'].lower();category='albedo' if 'albedomap' in use else 'normal' if 'normalmap' in use else 'rmt' if 'rmtmap' in use else 'emissive' if 'emissivemap' in use else None
      if category is None:continue
      relative=resource_path(resource['value']);src=root/'research/multiweapon/extracted'/relative
      if not src.exists():src=root/'research/mhw-original/extracted'/relative
      if not src.is_file():raise FileNotFoundError('Referenced source texture is unavailable: '+str(relative))
      image,evidence=decode(src)
      if max(image.size)>512:image.thumbnail((512,512),Image.Resampling.LANCZOS)
      # Preserve the entire source identity. Equal basenames in different resource
      # directories must never alias the same published texture.
      png=textures/relative.with_suffix('.png');rgba=textures/relative.with_suffix('.rgba')
      pixels=image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes()
      if len(pixels)!=image.width*image.height*4:raise ValueError('Decoded RGBA size mismatch')
      if not rgba.is_file() or rgba.read_bytes()!=pixels:count+=1
      encoded=io.BytesIO();image.save(encoded,format='PNG')
      transaction.write_bytes(png,encoded.getvalue());transaction.write_bytes(rgba,pixels)
      runtime[category]={'file':rgba.relative_to(mode).as_posix(),'png':png.relative_to(mode).as_posix(),'width':image.width,'height':image.height,'format':'RGBA32','source_path':relative.as_posix(),**evidence}
      paths.add(relative.as_posix());audit.append({'prop':prop['id'],'material':material['name'],'category':category,**runtime[category]})
     material['runtime_textures']=runtime
    prop['material_policy']='MRL3 actual mapped texture/UV; compatible private Unity material; native Capcom shader not transplanted'
    transaction.write_json(prop_path,prop)
  report={'schema':2,'decoded_resource_count':len(paths),'runtime_outputs':count,'materials':audit,'runtime_verified':False,'output_identity':'complete source-relative path; current decoded bytes atomically replace old bytes'}
  transaction.write_json(root/'research/weapon-coverage/source-texture-validation.json',report)
  transaction.commit()
 return report

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--root',type=Path,default=ROOT);args=parser.parse_args()
 report=generate(args.root);print('MRL3-linked TEX resources',report['decoded_resource_count'],'updated runtime textures',report['runtime_outputs'])
if __name__=='__main__':main()
