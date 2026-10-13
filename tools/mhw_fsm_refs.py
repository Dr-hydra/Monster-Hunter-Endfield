"""Summarize source FSM nodes/actions without confusing action IDs with LMT slots."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'research/mhw-original/extracted/hm/wp/wp03/wp03_action.fsm.json'
doc=json.loads(source.read_text(encoding='utf-8'))
rows=[]
def walk(value,path='root'):
    if isinstance(value,dict):
        if 'mContainerName' in value or any(k in value for k in ('mMotionId','mMotNo','mActionId','mActionNo')):
            rows.append(dict(path=path,values=value))
        for k,v in value.items():walk(v,path+'/'+k)
    elif isinstance(value,list):
        for i,v in enumerate(value):walk(v,path+'/'+str(i))
walk(doc['root'])
(ROOT/'research/mhw-original/fsm-references.json').write_text(json.dumps(rows,ensure_ascii=False,indent=2),encoding='utf-8')
print('structured action/link references',len(rows))
for row in rows:
    v=row['values']
    if v.get('mContainerName')=='Action_W03' and any(v.get(k) in range(87,103) for k in ('mActionId','mActionNo','mId')):
        print(row['path'],v)
