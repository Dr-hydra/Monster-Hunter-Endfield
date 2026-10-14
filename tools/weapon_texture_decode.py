"""Decode source MRL3-linked MHW TEX16 textures to local PNG + runtime RGBA.

Uses Pillow's BCn decoder. Keeps source color transfer and UV orientation;
all resource paths come from the actual MRL3 resource dictionary, not names.
"""
from pathlib import Path
import json,struct
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
def write(path,j):path.write_text(json.dumps(j,ensure_ascii=False,separators=(',',':'))+'\n',encoding='utf-8')
def main():
 count=0;paths=set();audit=[]
 for index in (ROOT/'assets/modes').glob('*/props/index.json'):
  mode=index.parent.parent;textures=mode/'props/textures';textures.mkdir(exist_ok=True)
  for item in json.loads(index.read_text(encoding='utf-8'))['props']:
   prop_path=mode/item['file'];prop=json.loads(prop_path.read_text(encoding='utf-8'))
   for material in prop.get('materials',[]):
    runtime={}
    for resource in material.get('resources',{}).values():
     use=resource['name'].lower();category='albedo' if 'albedomap' in use else 'normal' if 'normalmap' in use else 'rmt' if 'rmtmap' in use else 'emissive' if 'emissivemap' in use else None
     if category is None:continue
     relative=resource['value'].replace('\\','/')+'.tex';src=ROOT/'research/multiweapon/extracted'/relative
     if not src.exists():src=ROOT/'research/mhw-original/extracted'/relative
     if not src.exists():continue
     image,evidence=decode(src);stem=Path(relative).stem
     if max(image.size)>512:image.thumbnail((512,512),Image.Resampling.LANCZOS)
     png=textures/(stem+'.png');rgba=textures/(stem+'.rgba')
     if not rgba.exists():
      image.save(png);image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes();rgba.write_bytes(image.transpose(Image.Transpose.FLIP_TOP_BOTTOM).tobytes());count+=1
     runtime[category]={'file':str(rgba.relative_to(mode)).replace('\\','/'),'png':str(png.relative_to(mode)).replace('\\','/'),'width':image.width,'height':image.height,'format':'RGBA32','source_path':relative,**evidence}
     paths.add(relative);audit.append({'prop':prop['id'],'material':material['name'],'category':category,**runtime[category]})
    material['runtime_textures']=runtime
   prop['material_policy']='MRL3 actual mapped texture/UV; compatible private Unity material; native Capcom shader not transplanted'
   write(prop_path,prop)
 write(ROOT/'research/weapon-coverage/source-texture-validation.json',{'schema':1,'decoded_resource_count':len(paths),'runtime_outputs':count,'materials':audit,'runtime_verified':False})
 print('MRL3-linked TEX resources',len(paths),'runtime textures',count)
if __name__=='__main__':main()
