"""Read rMotBlendData according to Synthlight/MHW-Editor authors' template.
Unqualified fields keep upstream unk names; this does not invent action timing.
"""
from pathlib import Path
import json,struct
ROOT=Path(__file__).resolve().parents[1]
path=ROOT/'research/mhw-original/extracted/hm/wp/wp03/mot/w03_00/w03_00.mbd'
data=path.read_bytes();magic,header,count=struct.unpack_from('<III',data);at=12;rows=[]
for _ in range(count):
    unknown,index,number,unknown4,unknown5=struct.unpack_from('<5I',data,at);at+=20;arr=[]
    for j in range(number):
        end=data.index(0,at);name=data[at:end].decode('ascii');at=end+1
        unknowns=struct.unpack_from('<IIIIfIiI',data,at);at+=32
        arr.append(dict(unkname=name,values=unknowns))
    endval,=struct.unpack_from('<I',data,at);at+=4
    rows.append(dict(unk1=unknown,lmtOffsetIndex=index,NumOfValues=number,unk4=unknown4,unk5=unknown5,arr1=arr,unk15=endval))
end,=struct.unpack_from('<I',data,at);at+=4
if at!=len(data):raise ValueError(f'MBD cursor {at}, file {len(data)}')
out=ROOT/'research/mhw-original/mbd-decoded.json'
out.write_text(json.dumps(dict(template='https://github.com/Synthlight/MHW-Editor/wiki/.mbd',magic=magic,header=header,count=count,entries=rows,end=end),indent=2),encoding='utf-8')
print('MBD entries',len(rows),'exact cursor',at)
print([r for r in rows if r['lmtOffsetIndex'] in (170,171,172,173,174)])
