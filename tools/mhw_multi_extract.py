"""Incremental, index-based extraction of original five-mode references.

No game launch, archive re-index, hash or whole-game extraction. Existing original
overlay metadata supplies the effective version of every resource.
"""
from pathlib import Path
import argparse, ctypes, importlib.util, json, re, struct

ROOT=Path(__file__).resolve().parents[1]
MODES={'sword_shield':('01','one',10000),'greatsword':('00','two',20000),
       'lance':('06','lan',30000),'light_bowgun':('13','lbg',40000),'bow':('11','bow',50000)}

def load_extractor():
    spec=importlib.util.spec_from_file_location('mhw_original_extractor',ROOT/'tools/mhw_extract_original.py')
    module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
    tooling=ROOT/'local/tools'
    source=(tooling/'mhw-nochunk/MHWNoChunk/Chunk.cs').read_text(encoding='utf-8-sig')
    keys=[bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})',row)) for row in re.findall(r'new byte\[\] \{([^}]+)\}',source)]
    raw=(tooling/'mhw-nochunk/MHWNoChunk/Resources/chunk.key').read_bytes()
    dll=ctypes.CDLL(str(tooling/'mhw-ooz-source/mhe_ooz.dll'))
    dll.MHE_Decompress.argtypes=[ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t]
    dll.MHE_Decompress.restype=ctypes.c_int
    return module,keys,raw[8:],dll

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game',type=Path,default=Path('G:/SteamLibrary/steamapps/common/Monster Hunter World'))
    p.add_argument('--select',action='append')
    args=p.parse_args()
    patterns=args.select or [
       r'^hm/wp/wp(00|01|03|06|11|13)/.*\.(lmt|fsm|mbd|col)$',
       r'^wp/(two/bs_two001|one/bs_one001|lan/bs_lan001|lbg/bs_lbg001|bow/bow001)/',
       r'^sound/wwise/Windows/(wp00_two_epvsp|wp01_01_one_epvsp|wp01_one_epvsp|wp03_swo_epvsp|wp06_lan_epvsp|wp11_bow_epvsp_shell|wp_bowgun_epvsp_shell|wp_(two|one|swo|lan|lbg|bow)_(cmn|001))\.nbnk$',
       r'^sound/pl/(hm_wp(00|01|03|06|11|13)_.*|hm_wp_bowgun_.*)\.wwev$']
    rows=json.loads((ROOT/'research/mhw-original/merged-index.json').read_text(encoding='utf-8'))['entries']
    selected=[r for r in rows if any(re.search(pattern,r['path'],re.I) for pattern in patterns)]
    module,keys,pattern,dll=load_extractor();archives={};out=ROOT/'research/multiweapon';done=[]
    for row in selected:
        target=out/'extracted'/row['path']
        if target.is_file() and target.stat().st_size==row['size']:done.append(dict(**row,reused=True));continue
        if row['archive'] not in archives:archives[row['archive']]=module.Archive(args.game/'chunk'/row['archive'],dll,keys,pattern)
        target.parent.mkdir(parents=True,exist_ok=True)
        target.write_bytes(archives[row['archive']].read(row['offset'],row['size']))
        done.append(dict(**row,reused=False))
    out.mkdir(parents=True,exist_ok=True)
    (out/'selection.json').write_text(json.dumps(dict(schema_version=1,source_index='research/mhw-original/merged-index.json',patterns=patterns,entries=done,total_bytes=sum(r['size'] for r in done),hashes_computed=False),ensure_ascii=False,indent=2),encoding='utf-8')
    print('selected',len(done),'bytes',sum(r['size'] for r in done),'new',sum(not r['reused'] for r in done),flush=True)

if __name__=='__main__':main()
