"""Decode original mode FSM/MBD into evidence, preserving unknown numeric fields."""
from pathlib import Path
import importlib.util,json,struct,sys
ROOT=Path(__file__).resolve().parents[1]
MODES={'sword_shield':'01','greatsword':'00','lance':'06','light_bowgun':'13','bow':'11'}

def mbd(path):
    data=path.read_bytes();magic,header,count=struct.unpack_from('<III',data);at=12;rows=[]
    for _ in range(count):
        a,index,n,d,e=struct.unpack_from('<5I',data,at);at+=20;arr=[]
        for _ in range(n):
            end=data.index(0,at);name=data[at:end].decode('ascii');at=end+1
            values=struct.unpack_from('<IIIIfIiI',data,at);at+=32
            arr.append(dict(unkname=name,values=values))
        tail,=struct.unpack_from('<I',data,at);at+=4
        rows.append(dict(unk1=a,lmtOffsetIndex=index,NumOfValues=n,unk4=d,unk5=e,arr1=arr,unk15=tail))
    end,=struct.unpack_from('<I',data,at);at+=4
    if at!=len(data):raise ValueError('MBD trailing bytes')
    return dict(source=path.relative_to(ROOT).as_posix(),magic=magic,header=header,entries=rows,end=end,exact_bytes_consumed=at)

def main():
    sys.path.insert(0,str(ROOT/'local/tools/mhw-construct-old/construct-2.10.56'))
    source=ROOT/'local/tools/mhw-fsm/fsm_handling.py'
    replacement="""try:
        return obj.decode(self.encoding, 'strict')
    except UnicodeDecodeError:
        try: return obj.decode('cp932', 'strict')
        except UnicodeDecodeError: return obj.decode(self.encoding, 'backslashreplace')"""
    code=source.read_text(encoding='utf-8').replace("return obj.decode(self.encoding,'ignore')",replacement)
    for mode,num in MODES.items():
        path=ROOT/f'research/multiweapon/extracted/hm/wp/wp{num}/wp{num}_action.fsm'
        sys.argv=['fsm_handling.py',str(path)]
        exec(compile(code,str(source),'exec'),dict(__name__='__main__'))
        doc=json.loads(path.with_suffix('.fsm.json').read_text(encoding='utf-8'))
        evidence=[]
        def walk(v,path='root'):
            if isinstance(v,dict):
                if any(k in v for k in ('mName','mContainerName','mMotionId','mMotNo','mActionId','mActionNo')):evidence.append(dict(path=path,values=v))
                for k,x in v.items():walk(x,path+'/'+k)
            elif isinstance(v,list):
                for i,x in enumerate(v):walk(x,path+'/'+str(i))
        walk(doc['root'])
        out=ROOT/f'research/multiweapon/{mode}';out.mkdir(parents=True,exist_ok=True)
        (out/'fsm-references.json').write_text(json.dumps(evidence,ensure_ascii=False,indent=2),encoding='utf-8')
        result=mbd(ROOT/f'research/multiweapon/extracted/hm/wp/wp{num}/mot/w{num}_00/w{num}_00.mbd')
        (out/'mbd-decoded.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
        print(mode,'FSM refs',len(evidence),'MBD entries',len(result['entries']),flush=True)

if __name__=='__main__':main()
