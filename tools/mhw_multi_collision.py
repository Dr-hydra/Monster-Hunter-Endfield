"""Join original COL author move names to original TIML collision-node IDs.

Names binary layout is sourced from Synthlight/MHW-Editor col.bt. The final
name-record uint is the stable node ID, not the name-array position or MoveId.
Node slots map to flag bits (0->1, 1->2, 2->4). Auxiliary bank selectors are
preserved, so shield/tackle hits are not mislabeled as weapon slashes.
"""
from pathlib import Path
import json,re,struct
ROOT=Path(__file__).resolve().parents[1]
MODES={'greatsword':'00','sword_shield':'01','lance':'06','bow':'11','light_bowgun':'13','longsword':'03'}

def read_col(path):
    d=path.read_bytes()
    if d[4:8]!=b'COL\0':raise ValueError(f'Invalid COL {path}')
    count=struct.unpack_from('<I',d,20)[0];at=24;rows=[]
    for index in range(count):
        end=d.index(0,at);name=d[at:end].decode('utf-8');at=end+1
        geom,move,unk,unk2,unk3,node=struct.unpack_from('<iiiHBI',d,at);at+=19
        rows.append(dict(index=index,name=name,geometry_id=geom,move_id=move,node_id=node,unknown=[unk,unk2,unk3]))
    return dict(source=path.relative_to(ROOT).as_posix(),names=rows,names_bytes_end=at)

def fields(t):
    # Stable suffixes survive upstream Japanese-label mojibake. Numeric hashes
    # and original spellings are retained in the audit; no ID arithmetic.
    nodes={};banks={};flag=None
    for f in t['transforms']:
        name=f['name']
        if name=='mFlag':flag=f['keys'];continue
        if not name.endswith(']'):continue
        lane=int(re.search(r'(\d+)\]$',name).group(1))
        if 'D[' in name:nodes[lane]=f['keys']
        else:banks[lane]=f['keys']
    return nodes,banks,flag or []

def at(keys,frame,default=0):
    value=default
    for k in keys:
        if k['frame']>frame:break
        value=int(k['value'])
    return value

def main():
    for mode,num in MODES.items():
        source=ROOT/f'research/multiweapon/extracted/hm/wp/wp{num}/collision'
        banks={}
        for path in sorted(source.glob('*.col')):
            suffix=path.stem.removeprefix('wp'+num)
            if suffix and not suffix.startswith('_'):continue
            bank=int(suffix[1:]) if suffix else 0;banks[bank]=read_col(path)
        timl=json.loads((ROOT/f'research/multiweapon/{mode}/timeline-decoded.json').read_text())['actions'];actions={}
        for slot,doc in timl.items():
            hits=[]
            if not doc:continue
            for t in doc['types']:
                if t['name']!='CollisionTimelineObject':continue
                nodes,selectors,flags=fields(t)
                for i,key in enumerate(flags):
                    frame=key['frame'];mask=int(key['value']);end=flags[i+1]['frame'] if i+1<len(flags) else doc['animation_length']
                    if end<=frame:continue
                    for lane,nkeys in nodes.items():
                        if not mask&(1<<lane):continue
                        bank=at(selectors.get(lane,[]),frame);node=at(nkeys,frame)
                        name=next((r for r in banks.get(bank,{}).get('names',[]) if r['node_id']==node),None)
                        hits.append(dict(start_frame=frame,end_frame=end,lane=lane,bank=bank,node_id=node,flag=mask,author_name=name['name'] if name else None,collision_record=name))
            if hits:actions[slot]=hits
        out=ROOT/f'research/multiweapon/{mode}/collision-evidence.json'
        out.write_text(json.dumps(dict(schema_version=1,layout_source='https://github.com/Synthlight/MHW-Editor/blob/master/010%20Templates/col.bt',banks=banks,actions=actions),ensure_ascii=False,separators=(',',':')),encoding='utf-8')
        print(mode,'COL banks',len(banks),'named source motions',len(actions))
if __name__=='__main__':main()
