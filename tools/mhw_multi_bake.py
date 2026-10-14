"""Sample original five-mode source poses, anatomical contacts and source props.

Run with project Blender --background --factory-startup --python this-file.
Sparse NeedsParent actions are not silently evaluated against rest. Their source
components and controller context remain in the audit for qualified composition.
"""
from pathlib import Path
import importlib.util,json,math,struct,sys,zlib
import bpy
from mathutils import Matrix,Quaternion,Vector
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('mhe_base_bake',ROOT/'tools/bake_actions.py')
BASE=importlib.util.module_from_spec(spec);spec.loader.exec_module(BASE)
from MHW_Model_Editor.addons.MHW_Model_Editor.modules.mod3.mod3_functions import importLODGroup,createMaterialDict
from MHW_Model_Editor.addons.MHW_Model_Editor.modules.mrl3.file_mrl3 import Mrl3File
from core.formats.timl.reader import read_timl_data_bytes
from core.formats.timl.semantics import format_timeline_parameter_label,format_datatype_hash_label
MODES={'sword_shield':('01','one',10000),'greatsword':('00','two',20000),'lance':('06','lan',30000),'light_bowgun':('13','lbg',40000),'bow':('11','bow',50000),'longsword':('03','swo',0)}
CONVERT=Matrix(((-1,0,0),(0,0,1),(0,-1,0)));INVERSE=CONVERT.inverted()
ORDER=list(BASE.SEMANTIC)

def dump(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,ensure_ascii=False,separators=(',',':')),encoding='utf-8')

def world(pose):
    mat=pose.matrix;q=(CONVERT@mat.to_3x3().normalized()@INVERSE).to_quaternion()
    return dict(position=list(CONVERT@mat.translation),rotation_xyzw=[q.x,q.y,q.z,q.w])

def timl(raw,action):
    if not action.header.timl_offset:return None
    d=read_timl_data_bytes(raw,data_offset=action.header.timl_offset)
    return dict(animation_length=d.animation_length,loop_start=d.loop_start_point,loop_control=d.loop_control,
      types=[dict(name=format_timeline_parameter_label(t.timeline_parameter_hash),hash=t.timeline_parameter_hash,
       transforms=[dict(name=format_datatype_hash_label(x.datatype_hash),hash=x.datatype_hash,type=x.data_type,
         keys=[dict(frame=k.frame_timing,value=k.value,interpolation=k.interpolation,easing=k.easing) for k in x.keyframes]) for x in t.transforms]) for t in d.types])

def bake_props(mode,cat,output):
    directory=ROOT/f'research/multiweapon/extracted/wp/{cat}'
    if mode=='longsword':directory=ROOT/'research/mhw-original/extracted/wp/swo'
    names={'sword_shield':['bs_one001','bs_sld001'],'greatsword':['bs_two001'],
      'lance':['bs_lan001','bs_sld001'],'light_bowgun':['bs_lbg001'],'bow':['bow001','ya001'],'longsword':['bs_swo001','bs_saya001']}[mode]
    results=[]
    for stem in names:
        paths=list(directory.glob(f'*/mod/{stem}.mod3'))
        if not paths:raise RuntimeError('Missing source prop '+stem)
        path=paths[0];options=dict(importArmatureOnly=False,importAllLODs=False,importBoundingBoxes=False)
        parsed=BASE.ParsedMod3File();parsed.parse(BASE.readMod3File(str(path),options),options)
        col=bpy.data.collections.new(mode+' '+stem+'.mod3');col['~TYPE']='MHW_MOD3_COLLECTION';bpy.context.scene.collection.children.link(col)
        rig,_=BASE.importSkeleton(parsed.skeleton.boneInfoList,stem,col,'STICK',5)
        mats=createMaterialDict(parsed.materialNameList);importLODGroup(parsed,col,mats,rig,set(),False)
        meshes=[o for o in col.all_objects if o.type=='MESH'];vertices=[];normals=[];uv=[];indices=[];submeshes=[];skin_indices=[];skin_weights=[]
        root=next(b for b in rig.data.bones if b.parent is None);rootinverse=root.matrix_local.inverted()
        transform=CONVERT.to_4x4()@rootinverse
        palette={b.name:i for i,b in enumerate(rig.data.bones)}
        for mesh in meshes:
            mesh.data.calc_loop_triangles();base=len(vertices);mesh_indices=[]
            mat=transform@mesh.matrix_world
            groups={g.index:g.name for g in mesh.vertex_groups};cache={};layer=mesh.data.uv_layers.active
            # Read actual loop UV and split seams. A vertex-index guess changes
            # material sampling on source weapons and is explicitly avoided.
            for triangle in mesh.data.loop_triangles:
                for loop_index in triangle.loops:
                    loop=mesh.data.loops[loop_index];v=mesh.data.vertices[loop.vertex_index]
                    texcoord=tuple(layer.data[loop_index].uv) if layer else (0.,0.)
                    normal=mesh.data.corner_normals[loop_index].vector
                    key=(loop.vertex_index,texcoord,tuple(normal),triangle.material_index)
                    if key not in cache:
                        cache[key]=len(vertices);vertices.append(list(mat@v.co));normals.append(list((mat.to_3x3()@normal).normalized()));uv.append(list(texcoord))
                        weights=sorted([(palette[groups[g.group]],g.weight) for g in v.groups if groups.get(g.group) in palette],key=lambda x:-x[1])[:4]
                        total=sum(w for _,w in weights)
                        if total<=0:weights=[(palette[root.name],1.)];total=1.
                        weights=[(i,w/total) for i,w in weights]+[(0,0.)]*(4-len(weights))
                        skin_indices.append([i for i,w in weights]);skin_weights.append([w for i,w in weights])
                    mesh_indices.append(cache[key]);indices.append(cache[key])
            submeshes.append(dict(name=mesh.name,indices=mesh_indices,material_names=[m.name for m in mesh.data.materials],source_mesh_index=mesh.data.get('Mod3_Mesh_Index'),source_render_mode=mesh.data.get('Mod3_Mesh_RenderMode')))
        bones=[]
        for b in rig.data.bones:
            mat=rootinverse@b.matrix_local if not b.parent else b.parent.matrix_local.inverted()@b.matrix_local
            q=(CONVERT@mat.to_3x3().normalized()@INVERSE).to_quaternion()
            bones.append(dict(name=b.name,parent=b.parent.name if b.parent else None,position=list(CONVERT@mat.translation),rotation_xyzw=[q.x,q.y,q.z,q.w],scale=list(mat.to_scale())))
        attachment=248 if 'sld' in stem or mode=='bow' else 247
        materials=[];mrlpath=path.with_suffix('.mrl3')
        if mrlpath.exists():
            mrl=Mrl3File()
            with mrlpath.open('rb') as f:mrl.read(f)
            # This CRC is the game's material identifier, not an artifact hash.
            mrl.parser({zlib.crc32(n.encode())^0xffffffff:n for n in parsed.materialNameList})
            for m in mrl.materialList:
                materials.append(dict(name=m.matInfo.materialName,source_material_hash=m.matInfo.materialNameHash,shader=m.matInfo.mmtrName,resources=m.resourceDict))
        bindposes=[]
        for b in rig.data.bones:
            bind=(CONVERT.to_4x4()@rootinverse@b.matrix_local@CONVERT.to_4x4().inverted()).inverted()
            bindposes.append([bind[r][c] for c in range(4) for r in range(4)])
        role='sheath' if 'saya' in stem else 'shield' if 'sld' in stem else 'arrow' if stem=='ya001' else 'main'
        doc=dict(schema_version=1,id=mode+'_'+stem,role=role,source=path.relative_to(ROOT).as_posix(),
          vertices=vertices,normals=normals,uv=uv,indices=indices,submeshes=submeshes,rig_bones=bones,
          skin_bone_indices=skin_indices,skin_weights=skin_weights,bindposes=bindposes,
          materials=materials,material_source=mrlpath.relative_to(ROOT).as_posix() if mrlpath.exists() else None,
          coordinate='Unity Y up Z forward meters; imported MOD3 root bind transform removed',
          attachment_source_bone=attachment,attachment_verified=False,
          limitation='Actual source mesh and root bind; native drawn mounting offset is not decoded. No guessed blade/arrow axis correction.',
          bounds_min=[min(p[a] for p in vertices) for a in range(3)],bounds_max=[max(p[a] for p in vertices) for a in range(3)],
          skin_evidence=dict(palette_count=len(parsed.skeleton.boneInfoList),static_rest_only=True))
        dump(output/f'props/{stem}.json',doc);results.append(dict(id=doc['id'],role=role,file=f'props/{stem}.json',vertices=len(vertices),triangles=len(indices)//3))
        for obj in list(col.all_objects):bpy.data.objects.remove(obj,do_unlink=True)
        bpy.data.collections.remove(col)
    dump(output/'props/index.json',dict(schema_version=1,props=results))
    return results

def main():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    source=ROOT/'research/mhw-original/extracted/pl/m_equip/pl043_0000/body/mod/m_body043_0000.mod3'
    options=dict(importArmatureOnly=True,importAllLODs=False,importBoundingBoxes=False)
    parsed=BASE.ParsedMod3File();parsed.parse(BASE.readMod3File(str(source),options),options)
    col=bpy.data.collections.new('Original shared hunter.mod3');col['~TYPE']='MHW_MOD3_COLLECTION';bpy.context.scene.collection.children.link(col)
    rig,_=BASE.importSkeleton(parsed.skeleton.boneInfoList,'Original hunter',col,'STICK',5)
    root=BASE.ensure_mhw_root_motion_bone(rig)
    if root.error:raise RuntimeError(root.error)
    mapping={int(b.name.split('_')[-1]):b.name for b in rig.data.bones if b.name.startswith('MhBone_')}
    for pose in rig.pose.bones:pose.rotation_mode='QUATERNION'
    rest={name:dict(function=function,**world(rig.pose.bones[mapping[function]])) for name,function in BASE.SEMANTIC.items()}
    for b in rest.values():b['rotation']=b.pop('rotation_xyzw')
    floor=min(rest[n]['position'][1] for n in ('left_foot','right_foot','left_toes','right_toes'));height=rest['hips']['position'][1]-floor
    palms={}
    for side,functions in [('left',(8,37,34,44)),('right',(12,54,51,61))]:
        wrist,middle,index,pinky=[CONVERT@rig.data.bones[mapping[f]].matrix_local.translation for f in functions]
        palms[side]=dict(forward=list(middle-wrist),across=list(index-pinky),functions=dict(zip(('wrist','middle','index','pinky'),functions)),evidence='Original MOD3 anatomical bind positions')
    finger_functions=[f for f in mapping if 31<=f<=64 and f not in (47,)]
    finger_rest={str(f):dict(name=mapping[f],**world(rig.pose.bones[mapping[f]])) for f in finger_functions}
    requested=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [];props_only='--props-only' in requested
    requested=[a for a in requested if not a.startswith('--')]
    for mode,(num,cat,offset) in MODES.items():
        if requested and mode not in requested:continue
        output=ROOT/f'assets/modes/{mode}';preview=ROOT/f'preview/modes/{mode}'
        output.mkdir(parents=True,exist_ok=True);preview.mkdir(parents=True,exist_ok=True)
        if props_only:
            print(mode,'props',len(bake_props(mode,cat,output)),flush=True);continue
        dump(output/'source_rig.json',dict(bones=rest,order=ORDER,fps=60,hand_anatomy=palms,up_axis='y',forward_axis='z',source_rest_hip_height=height,source=source.relative_to(ROOT).as_posix()))
        path=(ROOT/'research/mhw-original/extracted' if mode=='longsword' else ROOT/'research/multiweapon/extracted')/f'hm/wp/wp{num}/mot/w{num}_00/w{num}_00.lmt';raw=path.read_bytes();lmt=BASE.read_lmt_file(path)
        idle_action=next(a for a in lmt.actions if a.id==1)
        idle_evaluation=BASE.compile_pose_evaluation(idle_action,mapping,version=lmt.header.version)
        if not idle_evaluation.supported or idle_action.header.flags2&1:raise RuntimeError('No qualified full stationary source idle')
        stationary_context=idle_evaluation.sample(0)
        catalog=[];audit=[];skipped=[];ground={};grip={};weapon={};timeline={};fingers={}
        for action in lmt.actions:
            ev=BASE.compile_pose_evaluation(action,mapping,version=lmt.header.version)
            timeline[str(action.id)]=timl(raw,action)
            if action.header.frame_count<2 or not ev.supported:
                skipped.append(dict(source_slot=action.id,frames=action.header.frame_count,needs_parent=bool(action.header.flags2&1),reason='NeedsParent requires qualified base context' if action.header.flags2&1 else 'not directly sampleable',diagnostics=[d.message for d in ev.diagnostics if d.level=='ERROR']))
                continue
            count=action.header.frame_count;slot=offset+action.id;samples=[];positions=[];rotations=[];contacts=[];clear=[];wframes=[];fingerframes=[];first=None
            for frame in range(count):
                for p in rig.pose.bones:p.location=(0,0,0);p.rotation_quaternion=(1,0,0,0);p.scale=(1,1,1)
                for (target,usage),value in ev.sample(frame,base=stationary_context).items():
                    target=target or root.name;sem=BASE.get_usage_semantics(usage)
                    value=BASE.adapt_track_frames_for_target_space(rig,BASE.TrackSpaceTarget('bone',target),sem,[(float(frame),value)])[0][1]
                    setattr(rig.pose.bones[target],sem.blender_path_hint,value)
                bpy.context.view_layer.update();points=[];quats=[];row=[]
                for name,function in BASE.SEMANTIC.items():
                    result=world(rig.pose.bones[mapping[function]]);points.append(result['position']);q=Quaternion((result['rotation_xyzw'][3],*result['rotation_xyzw'][:3]));quats.append(result['rotation_xyzw'])
                    r=rest[name]['rotation'];delta=q@Quaternion((r[3],*r[:3])).inverted();delta.normalize();row.extend((delta.x,delta.y,delta.z,delta.w))
                if first is None:first=points[0][:]
                row.extend((points[0][0]-first[0],points[0][1]-height,points[0][2]-first[2]));samples.extend(row);positions.append(points);rotations.append(quats)
                main=8 if mode=='bow' else 12;support=12 if mode=='bow' else 8;separation=Vector(points[support])-Vector(points[main])
                weight=max(0.,min(1.,(.40-separation.length)/.12)) if mode in ('greatsword','light_bowgun') else 0.
                rq=Quaternion((quats[main][3],*quats[main][:3]));contacts.extend((*separation,weight))
                item=dict(rotation_xyzw=quats[main],position=points[main],support_offset=list(rq.inverted()@separation),support_weight=weight,
                     left=dict(position=points[8],rotation_xyzw=quats[8]),right=dict(position=points[12],rotation_xyzw=quats[12]),
                     attachments={str(f):world(rig.pose.bones[mapping[f]]) for f in (30,47,247,248,251) if f in mapping})
                wframes.append(item);clear.append(max(0.,min(points[j][1] for j in (15,16,19,20))))
                fr=[]
                for f in finger_functions:
                    q=world(rig.pose.bones[mapping[f]])['rotation_xyzw'];r=finger_rest[str(f)]['rotation_xyzw']
                    delta=Quaternion((q[3],*q[:3]))@Quaternion((r[3],*r[:3])).inverted();delta.normalize();fr.extend((delta.x,delta.y,delta.z,delta.w))
                fingerframes.append(fr)
            filename=f'clip_{slot:05d}.lsm';(output/filename).write_bytes(struct.pack('<4sIII',b'LSM1',count,21,60)+struct.pack('<%df'%len(samples),*samples))
            gripname=f'grip_{slot:05d}.lsg';(output/gripname).write_bytes(struct.pack('<4sIII',b'LSG1',count,60,4)+struct.pack('<%df'%len(contacts),*contacts))
            holes=len(set(stationary_context)-{(b.target,b.usage) for b in ev.bindings})
            catalog.append(dict(slot=slot,file=filename,frames=count,duration=(count-1)/60,source=path.relative_to(ROOT).as_posix(),source_slot=action.id,source_frame_start=0,source_frame_end=count-1,loop_frame=action.header.loop_frame,needs_parent=bool(action.header.flags2&1),source_evaluation='mhw-v95-pose-1',
                base_context=dict(source_slot=1,source_frame=0,components_filled=holes,evidence='Original drawn idle explicit stationary presentation context; existing source keys always override base',native_parent_replayed=False)))
            grip[str(slot)]=gripname;ground[str(slot)]=dict(fps=60,clearance=clear);weapon[str(slot)]=dict(fps=60,frames=wframes)
            if fingerframes:fingers[str(slot)]=dict(fps=60,frames=fingerframes)
            dump(preview/f'slot_{slot:05d}.json',dict(bones=ORDER,positions=positions,rotations_xyzw=rotations,up_axis='y',fps=60,source_slot=action.id))
            audit.append(dict(slot=slot,source_slot=action.id,frames=count,bound_components=len(ev.bindings),stationary_base_components=holes,needs_parent=bool(action.header.flags2&1),min_ground=min(clear),max_ground=max(clear),loop_frame=action.header.loop_frame))
        dump(output/'catalog.json',catalog)
        dump(output/'ground_tracks.json',dict(schema_version=1,source='Original sampled lowest ankle/toe; no invented flight',tracks=ground))
        dump(output/'grip_tracks.json',dict(schema_version=1,source='Source wrist separation; support disabled for independently-held weapon/shield/bow',tracks=grip))
        dump(output/'source_weapon_tracks.json',dict(schema_version=2,frame_basis='Left hand' if mode=='bow' else 'Right hand',dominant_hand='left' if mode=='bow' else 'right',source_mount_verified=False,tracks=weapon))
        dump(output/'finger_tracks.json',dict(schema_version=1,functions=finger_functions,rest=finger_rest,frame_basis='world-space rest quaternion delta xyzw by source function',tracks=fingers))
        dump(ROOT/f'research/multiweapon/{mode}/timeline-decoded.json',dict(schema_version=1,source=path.relative_to(ROOT).as_posix(),fps=60,actions=timeline))
        props=bake_props(mode,cat,output)
        dump(ROOT/f'research/multiweapon/{mode}/bake-audit.json',dict(source=path.relative_to(ROOT).as_posix(),version=lmt.header.version,actions=audit,skipped=skipped,props=props,hashes_computed=False))
        print(mode,'baked',len(catalog),'skipped',len(skipped),'props',len(props),flush=True)
    print('All mode source sampling complete; command mapping still requires qualified source evidence',flush=True)

if __name__=='__main__':main()
