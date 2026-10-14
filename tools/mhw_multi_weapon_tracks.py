"""Decode independent original weapon LMT and author skeleton dependencies.

The source weapon's own palette is used; no body palette or target bones are
substituted. Animations retain source bank/slot and unqualified body associations.
"""
from pathlib import Path
import importlib.util,json,sys
import bpy
from mathutils import Matrix
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('multi',ROOT/'tools/mhw_multi_bake.py');BASE=importlib.util.module_from_spec(spec);spec.loader.exec_module(BASE)

def main():
    for mode,(num,cat,offset) in BASE.MODES.items():
        root=ROOT/f'research/multiweapon/extracted/wp/{cat}'
        if mode=='longsword':root=ROOT/'research/mhw-original/extracted/wp/swo'
        files=list(root.glob('mot/**/*.lmt'))
        if not files:continue
        stems={'bow':'bow001','light_bowgun':'bs_lbg001','lance':'bs_lan001','greatsword':'bs_two001','sword_shield':'bs_one001','longsword':'bs_swo001'}
        source=next(root.glob(f'*/mod/{stems[mode]}.mod3'));options=dict(importArmatureOnly=True,importAllLODs=False,importBoundingBoxes=False)
        p=BASE.BASE.ParsedMod3File();p.parse(BASE.BASE.readMod3File(str(source),options),options)
        col=bpy.data.collections.new('Source weapon '+mode+'.mod3');col['~TYPE']='MHW_MOD3_COLLECTION';bpy.context.scene.collection.children.link(col)
        rig,_=BASE.BASE.importSkeleton(p.skeleton.boneInfoList,'Source weapon '+mode,col,'STICK',5)
        for pose in rig.pose.bones:pose.rotation_mode='QUATERNION'
        helper=BASE.BASE.ensure_mhw_root_motion_bone(rig)
        mapping={int(b.name.split('_')[-1]):b.name for b in rig.data.bones if b.name.startswith('MhBone_')};tracks={};skipped=[]
        for file in files:
            l=BASE.BASE.read_lmt_file(file)
            for action in l.actions:
                ev=BASE.BASE.compile_pose_evaluation(action,mapping,version=l.header.version)
                key=file.stem+':'+str(action.id)
                if not ev.supported:
                    skipped.append(dict(key=key,reason=[d.message for d in ev.diagnostics if d.level=='ERROR']));continue
                frames=[]
                for frame in range(action.header.frame_count):
                    for pose in rig.pose.bones:pose.location=(0,0,0);pose.rotation_quaternion=(1,0,0,0);pose.scale=(1,1,1)
                    for (target,usage),value in ev.sample(frame).items():
                        target=target or helper.name;sem=BASE.BASE.get_usage_semantics(usage)
                        converted=BASE.BASE.adapt_track_frames_for_target_space(rig,BASE.BASE.TrackSpaceTarget('bone',target),sem,[(float(frame),value)])[0][1]
                        setattr(rig.pose.bones[target],sem.blender_path_hint,converted)
                    bpy.context.view_layer.update();items={}
                    for f,name in mapping.items():
                        pose=rig.pose.bones[name];mat=pose.matrix_basis;q=(BASE.CONVERT@mat.to_3x3().normalized()@BASE.INVERSE).to_quaternion()
                        scale=mat.to_scale()
                        items[str(f)]=dict(position=list(BASE.CONVERT@mat.translation),rotation_xyzw=[q.x,q.y,q.z,q.w],scale=[scale.x,scale.z,scale.y])
                    frames.append(items)
                tracks[key]=dict(source=file.relative_to(ROOT).as_posix(),source_slot=action.id,frames=frames,fps=60,loop_frame=action.header.loop_frame,needs_parent=bool(action.header.flags2&1),body_association_verified=False,
                   association_evidence='Common draw/sheath source slots have matching body/weapon frame counts; association remains an inference' if action.id in (0,1,2,3) else 'Source body/weapon phase link not decoded; retain separate bank-slot identity')
        BASE.dump(ROOT/f'assets/modes/{mode}/weapon_rig_tracks.json',dict(schema_version=1,source_model=source.relative_to(ROOT).as_posix(),source_functions=mapping,coordinate='Unity Y-up pose-local basis delta after exact MOD3 import conversion',scale_basis='Unity axis permutation (sourceX,sourceZ,sourceY)',tracks=tracks,skipped=skipped))
        print(mode,'weapon tracks',len(tracks),'unsupported',len(skipped),flush=True)
        for o in list(col.all_objects):bpy.data.objects.remove(o,do_unlink=True)
        bpy.data.collections.remove(col)
    # Preserve complete source finger hierarchy, including helper functions.
    source=ROOT/'research/mhw-original/extracted/pl/m_equip/pl043_0000/body/mod/m_body043_0000.mod3'
    options=dict(importArmatureOnly=True,importAllLODs=False,importBoundingBoxes=False);p=BASE.BASE.ParsedMod3File();p.parse(BASE.BASE.readMod3File(str(source),options),options)
    col=bpy.data.collections.new('Source finger hierarchy.mod3');col['~TYPE']='MHW_MOD3_COLLECTION';bpy.context.scene.collection.children.link(col)
    rig,_=BASE.BASE.importSkeleton(p.skeleton.boneInfoList,'Source finger hierarchy',col,'STICK',5)
    rows={str(int(b.name.split('_')[-1])):dict(name=b.name,parent=b.parent.name if b.parent else None,position=list(BASE.CONVERT@b.matrix_local.translation)) for b in rig.data.bones if b.name.startswith('MhBone_')}
    BASE.dump(ROOT/'research/multiweapon/source-rig-full-hierarchy.json',dict(source=source.relative_to(ROOT).as_posix(),bones=rows))

if __name__=='__main__':main()
