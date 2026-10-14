"""Sample original animation clearance from actual weighted source shoe soles.

LSM root curves are preserved. Neutral mesh sole floor is recorded separately
from the old bone-foot root-height reference. Composite clearance follows the
existing verified source-stage map plus the stored LSM translation difference.
"""
from pathlib import Path
import dataclasses,importlib.util,json,struct,sys
import bpy
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('multi',ROOT/'tools/mhw_multi_bake.py');M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)
spec=importlib.util.spec_from_file_location('source',ROOT/'tools/mhw_source_model.py');S=importlib.util.module_from_spec(spec);spec.loader.exec_module(S)

def select_probes(rig,meshes,mapping):
    inverse={v:k for k,v in mapping.items()};candidates={'left':[],'right':[]}
    for mesh in meshes:
        groups={g.index:g.name for g in mesh.vertex_groups}
        for vertex in mesh.data.vertices:
            weights=[(groups[g.group],g.weight) for g in vertex.groups if groups.get(g.group) in inverse and g.weight>1e-7]
            if not weights:continue
            functions={inverse[b]:w for b,w in weights};side='left' if sum(functions.get(f,0) for f in (16,17))>.65 else 'right' if sum(functions.get(f,0) for f in (20,21))>.65 else None
            if not side:continue
            total=sum(w for b,w in weights);p=mesh.matrix_world@vertex.co;point=M.CONVERT@p
            candidates[side].append(dict(mesh=mesh.name,vertex=vertex.index,rest_position=list(point),influences=[dict(function=inverse[b],bone=b,weight=w/total,local_point=list(rig.data.bones[b].matrix_local.inverted()@p)) for b,w in weights]))
    probes=[]
    for side,rows in candidates.items():
        floor=min(p['rest_position'][1] for p in rows);bottom=[p for p in rows if p['rest_position'][1]<=floor+.025]
        selected=[min(bottom,key=lambda p:p['rest_position'][1])]
        while len(selected)<min(16,len(bottom)):
            candidate=max(bottom,key=lambda p:min(sum((p['rest_position'][j]-q['rest_position'][j])**2 for j in (0,2)) for q in selected))
            if candidate in selected:break
            selected.append(candidate)
        probes += [dict(p,side=side) for p in selected]
    return probes

def main():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    source=ROOT/'research/mhw-original/extracted/pl/m_equip/pl043_0000/body/mod/m_body043_0000.mod3'
    rig,meshes,_=S.import_model(source,'Source actual soles')
    _,legmeshes,_=S.import_model(source.parents[2]/'leg/mod/m_leg043_0000.mod3','Original shoe mesh',rig);meshes+=legmeshes
    helper=M.BASE.ensure_mhw_root_motion_bone(rig);mapping={int(b.name.split('_')[-1]):b.name for b in rig.data.bones if b.name.startswith('MhBone_')}
    for p in rig.pose.bones:p.rotation_mode='QUATERNION'
    probes=select_probes(rig,meshes,mapping);neutral=min(p['rest_position'][1] for p in probes)
    required={helper.name}
    for p in probes:
        for influence in p['influences']:
            bone=rig.data.bones[influence['bone']]
            while bone:required.add(bone.name);bone=bone.parent
    # Remove renderable meshes once true bind-derived probes have been captured.
    # The numerical samples retain their full skin weights and source palette.
    for mesh in meshes:bpy.data.objects.remove(mesh,do_unlink=True)
    M.dump(ROOT/'research/multiweapon/source-sole-probes.json',dict(schema_version=1,probes=probes,neutral_floor=neutral,source=source.relative_to(ROOT).as_posix(),actual_skinned_shoe_vertices=True))
    requested=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
    for mode,(num,cat,offset) in M.MODES.items():
        if requested and mode not in requested:continue
        folder=ROOT/f'assets/modes/{mode}';catalog=json.loads((folder/'catalog.json').read_text(encoding='utf-8'));known={r['source_slot']:r for r in catalog if 'source_slot' in r and 'source_phases' not in r}
        path=(ROOT/'research/mhw-original/extracted' if mode=='longsword' else ROOT/'research/multiweapon/extracted')/f'hm/wp/wp{num}/mot/w{num}_00/w{num}_00.lmt'
        lmt=M.BASE.read_lmt_file(path);idle=M.BASE.compile_pose_evaluation(next(a for a in lmt.actions if a.id==1),mapping,version=lmt.header.version).sample(0);idle={k:v for k,v in idle.items() if k[0] is None or k[0] in required}
        tracks={};physical={}
        for action in lmt.actions:
            if action.id not in known:continue
            ev=M.BASE.compile_pose_evaluation(action,mapping,version=lmt.header.version)
            if not ev.supported:continue
            ev=dataclasses.replace(ev,bindings=tuple(b for b in ev.bindings if b.target is None or b.target in required))
            clearance=[];raw=[]
            for frame in range(action.header.frame_count):
                for name in required:
                    p=rig.pose.bones[name];p.location=(0,0,0);p.rotation_quaternion=(1,0,0,0);p.scale=(1,1,1)
                for (name,usage),value in ev.sample(frame,base=idle).items():
                    name=name or helper.name;sem=M.BASE.get_usage_semantics(usage);value=M.BASE.adapt_track_frames_for_target_space(rig,M.BASE.TrackSpaceTarget('bone',name),sem,[(float(frame),value)])[0][1];setattr(rig.pose.bones[name],sem.blender_path_hint,value)
                bpy.context.view_layer.update();floor=1e9
                for probe in probes:
                    point=Vector((0,0,0))
                    for inf in probe['influences']:point+=(rig.pose.bones[inf['bone']].matrix@Vector(inf['local_point']))*inf['weight']
                    floor=min(floor,(M.CONVERT@point).y)
                raw.append(floor);clearance.append(max(0.,floor-neutral))
            tracks[str(offset+action.id)]=dict(fps=60,clearance=clearance);physical[str(action.id)]=raw
        idle_floor=physical['1'][0]
        for slot,rows in physical.items():tracks[str(offset+int(slot))]['clearance']=[max(0.,y-idle_floor) for y in rows]
        output=dict(schema_version=2,tracks=tracks,ground_evidence='Original actual shoe sole skinning; spatially distributed bottom vertices per foot',neutral_source_sole_floor=idle_floor,source_bind_sole_floor=neutral,root_reference='Existing bone-foot-referenced LSM1 Y unchanged',source_probes='research/multiweapon/source-sole-probes.json')
        M.dump(folder/'ground_tracks.json',output)
        source_rig=json.loads((folder/'source_rig.json').read_text(encoding='utf-8'));bind_hip_height=source_rig['bones']['hips']['position'][1]-neutral
        source_rig['sole_geometry']=dict(neutral_floor=idle_floor,bind_floor=neutral,probes=len(probes),bind_hip_height=bind_hip_height,hip_height=bind_hip_height,
          hip_height_evidence='Bind-rest hip above actual skinned shoe sole; not animated idle hip height')
        M.dump(folder/'source_rig.json',source_rig)
        if mode=='longsword':
            root=ROOT/'assets';existing=json.loads((root/'ground_tracks.json').read_text(encoding='utf-8'));runtime={r['slot']:r for r in json.loads((root/'catalog.json').read_text(encoding='utf-8'))}
            for slot,row in runtime.items():
                if str(row.get('source','')).startswith('Legacy') or str(slot) not in tracks:continue
                start=row.get('source_frame_start',0);existing['tracks'][str(slot)]=dict(fps=60,clearance=tracks[str(slot)]['clearance'][start:start+row['frames']])
            composition=json.loads((ROOT/'research/mhw-original/helmbreaker-composition.json').read_text(encoding='utf-8'))
            if 1000 in runtime:
                raw=(root/runtime[1000]['file']).read_bytes();n,b,fps=struct.unpack_from('<III',raw,4);values=struct.unpack_from('<%df'%(n*(b*4+3)),raw,16);out=[]
                for i,stage in enumerate(composition['stage_map']):
                    slot=stage['slot'];frame=stage['frame'];src=(folder/known[slot]['file']).read_bytes();sf=struct.unpack_from('<f',src,16+(frame*87+85)*4)[0];output_y=values[i*87+85]
                    out.append(max(0.,physical[str(slot)][frame]-idle_floor+output_y-sf))
                existing['tracks']['1000']=dict(fps=60,clearance=out,source='Exact existing source stage map and LSM hip-Y differences')
            existing.update({k:v for k,v in output.items() if k!='tracks'});M.dump(root/'ground_tracks.json',existing)
        print(mode,'actual source soles tracks',len(tracks),'neutral floor',round(neutral,6),'probes',len(probes),flush=True)
if __name__=='__main__':main()
