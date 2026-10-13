"""Bake original long-sword skeletal samples and evidence-backed contact data.

Blender portable --background --factory-startup --python this-file
Always writes research staging. Publish after inspecting source poses using
tools/mhw_publish_baseline.py. Native layer/control/FSM replay is not claimed.
"""
from pathlib import Path
import sys, json, struct, importlib.util, math, shutil
import bpy
from mathutils import Matrix, Quaternion, Vector
ROOT=Path(__file__).resolve().parents[1]
SPEC=importlib.util.spec_from_file_location('mhe_existing_bake',ROOT/'tools/bake_actions.py')
BASE=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(BASE)
SEMANTIC=BASE.SEMANTIC
RESEARCH=ROOT/'research/mhw-original'
SELECTED={1,3,9,11,101,102,103,104,105,106,107,108,110,111,112,127,128,160,161,170,171,172,173,174,306,307,308}


def dump(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,ensure_ascii=False,separators=(',',':')),encoding='utf-8')


def main():
    stage=RESEARCH/'baked';stage.mkdir(parents=True,exist_ok=True)
    output=stage/'assets';output.mkdir(exist_ok=True)
    preview=stage/'preview';preview.mkdir(exist_ok=True)
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    source=RESEARCH/'extracted/pl/m_equip/pl043_0000/body/mod/m_body043_0000.mod3'
    options=dict(importArmatureOnly=True,importAllLODs=False,importBoundingBoxes=False)
    raw=BASE.readMod3File(str(source),options)
    parsed=BASE.ParsedMod3File();parsed.parse(raw,options)
    collection=bpy.data.collections.new('Original Hunter.mod3');collection['~TYPE']='MHW_MOD3_COLLECTION'
    bpy.context.scene.collection.children.link(collection)
    rig,_=BASE.importSkeleton(parsed.skeleton.boneInfoList,'Original Hunter source',collection,'STICK',5)
    helper=BASE.ensure_mhw_root_motion_bone(rig)
    if helper.error:raise RuntimeError(helper.error)
    root_name=helper.name
    for pose in rig.pose.bones:pose.rotation_mode='QUATERNION'
    mapping={int(b.name.split('_')[-1]):b.name for b in rig.data.bones if b.name.startswith('MhBone_')}
    ordered=list(SEMANTIC)
    convert=Matrix(((-1,0,0),(0,0,1),(0,-1,0)))
    inverse=convert.inverted()
    rest={}
    for name,function in SEMANTIC.items():
        bone=rig.data.bones[mapping[function]]
        q=(convert@bone.matrix_local.to_3x3()@inverse).to_quaternion()
        rest[name]=dict(function=function,rotation=[q.x,q.y,q.z,q.w],position=list(convert@bone.matrix_local.translation))
    floor=min(rest[n]['position'][1] for n in ('left_foot','right_foot','left_toes','right_toes'))
    hip_height=rest['hips']['position'][1]-floor
    palms={}
    for side,functions in [('left',(8,37,34,44)),('right',(12,54,51,61))]:
        wrist,middle,index,pinky=[convert@rig.data.bones[mapping[f]].matrix_local.translation for f in functions]
        palms[side]=dict(forward=list(middle-wrist),across=list(index-pinky),
                        functions=dict(zip(('wrist','middle','index','pinky'),functions)),
                        evidence='Original pl043 MOD3 bind positions, anatomical palm basis; no video-fitted angles')
    dump(output/'source_rig.json',dict(bones=rest,order=ordered,fps=60,hand_anatomy=palms,
         up_axis='y',forward_axis='z',source_rest_hip_height=hip_height,
         hip_translation='XZ relative to clip first frame; Y relative to ground-referenced rest hip height',
         source=source.relative_to(ROOT).as_posix()))
    path=RESEARCH/'extracted/hm/wp/wp03/mot/w03_00/w03_00.lmt'
    lmt=BASE.read_lmt_file(path)
    catalog=[];audit=[];skip=[];grip_manifest={};ground={};weapon={};full={}
    for action in lmt.actions:
        evaluation=BASE.compile_pose_evaluation(action,mapping,version=lmt.header.version)
        if action.header.frame_count<2 or not evaluation.supported or action.header.flags2&1:
            skip.append(dict(slot=action.id,frame_count=action.header.frame_count,
                             reason='NeedsParent requires explicit base context' if action.header.flags2&1 else 'not directly sampleable',
                             diagnostics=[d.message for d in evaluation.diagnostics if d.level=='ERROR']))
            continue
        count=action.header.frame_count;samples=[];positions=[];attachments=[];absolute=[];contact=[];clearance=[];source_tracks=[];first_hip=None
        for frame in range(count):
            for pose in rig.pose.bones:
                pose.location=(0,0,0);pose.rotation_quaternion=(1,0,0,0);pose.scale=(1,1,1)
            for (target,usage),value in evaluation.sample(frame).items():
                target=target or root_name
                semantics=BASE.get_usage_semantics(usage)
                converted=BASE.adapt_track_frames_for_target_space(rig,BASE.TrackSpaceTarget('bone',target),semantics,[(float(frame),value)])[0][1]
                setattr(rig.pose.bones[target],semantics.blender_path_hint,converted)
            bpy.context.view_layer.update()
            row=[];points=[];rotations=[]
            for name in ordered:
                matrix=rig.pose.bones[mapping[SEMANTIC[name]]].matrix
                q=(convert@matrix.to_3x3().normalized()@inverse).to_quaternion()
                restq=Quaternion((rest[name]['rotation'][3],*rest[name]['rotation'][:3]))
                delta=q@restq.inverted();delta.normalize()
                row.extend((delta.x,delta.y,delta.z,delta.w));rotations.append([q.x,q.y,q.z,q.w])
                points.append(list(convert@matrix.translation))
            hip=points[0]
            if first_hip is None:first_hip=hip[:]
            row.extend((hip[0]-first_hip[0],hip[1]-hip_height,hip[2]-first_hip[2]));samples.extend(row)
            positions.append(points);absolute.append(rotations)
            right=Vector(points[12]);left=Vector(points[8]);separation=left-right
            # Distance-only support estimate. Rotation is actual dominant hand,
            # not guessed blade/socket axes. Source socket mount is unresolved.
            distance=separation.length
            weight=max(0.,min(1.,(.40-distance)/.12))
            contact.extend((*separation,weight))
            rq=Quaternion((rotations[12][3],*rotations[12][:3]))
            source_tracks.append(dict(rotation_xyzw=rotations[12],support_offset=list(rq.inverted()@separation),support_weight=weight))
            clearance.append(max(0.,min(points[i][1] for i in (15,16,19,20))))
            if action.id in SELECTED:
                attached={}
                for function in (30,47,247,248,251):
                    matrix=rig.pose.bones[mapping[function]].matrix
                    attached[str(function)]=dict(position=list(convert@matrix.translation),
                        axes=[list(convert@matrix.to_3x3().col[a].normalized()) for a in range(3)])
                attachments.append(attached)
        filename=f'clip_{action.id:03d}.lsm'
        (output/filename).write_bytes(struct.pack('<4sIII',b'LSM1',count,21,60)+struct.pack('<%df'%len(samples),*samples))
        catalog.append(dict(slot=action.id,file=filename,frames=count,duration=(count-1)/60,source='original MHW v95 w03_00.lmt',source_slot=action.id))
        dump(preview/f'slot_{action.id:03d}.json',dict(bones=ordered,positions=positions,rotations_xyzw=absolute,attachments=attachments,up_axis='y',fps=60))
        ground[str(action.id)]=dict(fps=60,clearance=clearance)
        if action.id in SELECTED:
            gripname=f'grip_{action.id:03d}.lsg'
            (output/gripname).write_bytes(struct.pack('<4sIII',b'LSG1',count,60,4)+struct.pack('<%df'%len(contact),*contact))
            grip_manifest[str(action.id)]=gripname
            weapon[str(action.id)]=dict(fps=60,frames=source_tracks)
            full[action.id]=dict(positions=positions,rotations_xyzw=absolute,attachments=attachments)
        maxcurve=max(max(binding.curve.frames) for binding in evaluation.bindings)
        audit.append(dict(slot=action.id,header_frames=count,max_curve_frame=maxcurve,needs_parent=False,
            raw_track_count=len(action.tracks),bound_components=len(evaluation.bindings),min_foot_y=min(clearance),max_foot_y=max(clearance),
            control_records=[dict(bone_id=t.header.bone_id,usage=t.header.usage,buffer_type=t.header.buffer_type) for t in action.tracks if t.header.bone_id in (249,250,251,252,253)]))
        if action.id in SELECTED:print('original',action.id,'frames',count,'foot_peak',round(max(clearance),3),flush=True)
    if 170 not in full:raise RuntimeError('Original 170 cannot be sampled')
    dump(output/'catalog.json',catalog)
    dump(output/'ground_tracks.json',dict(schema_version=1,source='Original pose lowest ankle/toe Y in source meters; no fabricated flight arc',tracks=ground))
    dump(output/'grip_tracks.json',dict(schema_version=1,source='Original left minus right wrist, showcase contact distance threshold 0.28..0.40m',tracks=grip_manifest))
    dump(output/'source_weapon_tracks.json',dict(schema_version=1,frame_basis='Original right hand world rotation',
         limitation='Hand support only; does not describe blade or native MHW weapon mount',tracks=weapon))
    moves=dict(idle=1,sheathe_hold_frame=80,source='Original MHW slots; transitions remain showcase logic',
       commands=dict(slash=[101,102,103],thrust=[110,111],spirit=[104,105,106,107,108],fade=[112],foresight=[127,128],
                     sheathe=[306],iai=[307],iai_spirit=[308],helm=[170,171,172,173],dodge=[9,11],normal_sheathe=[3]),
       experimental_commands=['helm'],limitation='MHW FSM control/flight/weapon mount not fully reconstructed')
    dump(output/'moves.json',moves)
    # Practice windows are kinematic presentation estimates, not actual game attacks.
    windows={}
    for row in catalog:
        if row['slot'] in SELECTED and row['slot'] in (101,102,103,104,105,106,107,108,110,111,112,127,128,307,308):
            end=min(row['duration'],.80);begin=min(.22,end*.35)
            if end>begin:windows[str(row['slot'])]=[[begin,end,1.]]
    dump(output/'hit_windows.json',dict(schema_version=1,evidence='Explicit local showcase simulation intervals; not decoded MHW damage windows',windows=windows))
    dump(RESEARCH/'original-bake-audit.json',dict(source=path.relative_to(ROOT).as_posix(),version=lmt.header.version,
         actions=audit,skipped=skip,catalog_count=len(catalog),experimental_control=True,source_mount_verified=False,
         alias1000='Not generated: original 170 is not a complete Helmbreaker; controller flight/phase linkage must be decoded'))
    print('bake complete',len(catalog),'skip',len(skip),'staging only',flush=True)


if __name__=='__main__':main()
