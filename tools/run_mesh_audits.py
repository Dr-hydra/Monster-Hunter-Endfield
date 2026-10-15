"""Run unchanged-precision mesh audits with one writer per character.

Each worker writes separate reports/gallery directories. The two canonical
coverage reports are replaced only after all expected identities and their
current input contracts validate. Prepare/rebuild the native oracle first.
"""
from __future__ import annotations
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
from pathlib import Path
import subprocess
import sys
from resource_audit_contract import read, MODES, SPECIAL, NATIVE_INPUTS, validate_current, validate_special_current
from audit_native_build import validate_native_build
from audit_oracle_provenance import verify_oracle_provenance
from resource_transaction import ResourceTransaction

P0 = {'chr_0016_laevat', 'chr_0034_typhoea', 'chr_0018_dapan'}


def groups(characters, workers):
    if not characters or workers < 1: raise ValueError('Nonempty character list and positive worker count required')
    if len(set(characters)) != len(characters): raise ValueError('Duplicate character registry')
    result = [[] for _ in range(min(workers, len(characters)))]
    ordered = sorted(characters, key=lambda cid: (cid not in P0, cid))
    for i, character in enumerate(ordered): result[i % len(result)].append(character)
    return result


def merge_rows(reports, expected):
    result = {}
    for report in reports:
        for row in report['combinations']:
            key = row['character_id'], row['mode']
            if key in result: raise ValueError('Duplicate worker audit identity: ' + str(key))
            result[key] = row
    if set(result) != expected: raise ValueError('Worker reports do not cover exactly the expected audit identities')
    return [result[key] for key in sorted(result)]


def run_worker(root, folder, characters):
    folder.mkdir(parents=True)
    for script, report, gallery in (
        ('endfield_mesh_audit.py', 'coverage.json', 'gallery'),
        ('endfield_special_mesh_audit.py', 'special-cross-coverage.json', 'special-gallery'),
    ):
        with (folder/(script+'.log')).open('w',encoding='utf-8') as stream:
            subprocess.run([sys.executable,str(root/'tools'/script),'--characters',*characters,
                '--report',str(folder/report),'--gallery',str(folder/gallery)],cwd=root,
                stdout=stream,stderr=subprocess.STDOUT,check=True)
    return folder


def run(root, output, workers):
    root,output=Path(root).resolve(),Path(output).resolve();output.relative_to(root)
    validate_native_build(root, NATIVE_INPUTS)
    verify_oracle_provenance(root)
    if not 1 <= workers <= 16: raise ValueError('Worker count must be 1..16')
    if output.exists() and any(output.iterdir()):raise ValueError('Audit output must be empty; preceding coverage is never reused')
    index=read(root/'profiles/index.json')
    if len(index['modes'])!=len(MODES) or set(index['modes'])!=MODES:raise ValueError('All six modes are required')
    characters=[item['character_id'] for item in index['characters']]
    assignments=groups(characters,workers)
    output.mkdir(parents=True,exist_ok=True)
    (output/'assignments.json').write_text(json.dumps(assignments,indent=2)+'\n',encoding='utf-8')
    with ThreadPoolExecutor(max_workers=len(assignments)) as pool:
        futures=[pool.submit(run_worker,root,output/f'worker-{i+1}',part) for i,part in enumerate(assignments)]
        folders=[future.result() for future in futures]
    if read(root/'profiles/index.json') != index:raise ValueError('Character registry changed during audit')
    ordinary_reports=[read(folder/'coverage.json') for folder in folders]
    special_reports=[read(folder/'special-cross-coverage.json') for folder in folders]
    ordinary=merge_rows(ordinary_reports,{(cid,mode) for cid in characters for mode in MODES})
    special=merge_rows(special_reports,{(cid,spec[0]) for cid in characters for spec in SPECIAL.values()})
    profiles={item['character_id']:read(root/'profiles'/item['profile']) for item in index['characters']}
    validate_native_build(root, NATIVE_INPUTS)
    verify_oracle_provenance(root)
    for row in ordinary:validate_current(root,row['character_id'],row['mode'],profiles[row['character_id']],row)
    for row in special:
        weapon=next(key for key,value in SPECIAL.items() if value[0]==row['mode'])
        validate_special_current(root,row['character_id'],weapon,profiles[row['character_id']],row)
    normal={**ordinary_reports[0],'combinations':ordinary,
        'summary':{'combinations':len(ordinary),'offline_mesh_checked':len(ordinary),'current_revision':len(ordinary),'runtime_verified':0,'visual_quality_certified':0}}
    specials={**special_reports[0],'combinations':special,
        'summary':{'combinations':len(special),'checked':len(special),'current_revision':len(special),'runtime_verified':0,'visual_quality_certified':0}}
    with ResourceTransaction(root) as transaction:
        transaction.write_json(root/'research/character-coverage/coverage.json',normal)
        transaction.write_json(root/'research/character-coverage/special-cross-coverage.json',specials)
        transaction.commit()
    result={'ordinary':len(ordinary),'special':len(special),'workers':len(assignments),
            'errors':0,'canonical_reports_published':True,'game_launched':False,'precision':'existing P0 every-core-frame / remaining stride12 plus boundaries'}
    (output/'result.json').write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root',type=Path,default=Path(__file__).resolve().parents[1])
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--workers',type=int,default=4)
    args=parser.parse_args();print(json.dumps(run(args.root,args.output,args.workers)))


if __name__=='__main__':main()
