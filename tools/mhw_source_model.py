"""Inspect original hunter and sword geometry with source animation.
Mount candidates are labeled as unverified; no candidate is published to runtime.
"""
from pathlib import Path
import importlib.util,json,sys,math
import bpy
from mathutils import Matrix,Vector
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('mhe_source_bake',ROOT/'tools/mhw_bake_original.py')
bake=importlib.util.module_from_spec(spec);spec.loader.exec_module(bake)
base=bake.BASE
from MHW_Model_Editor.addons.MHW_Model_Editor.modules.mod3.mod3_functions import importLODGroup,createMaterialDict


def import_model(path,name,rig=None):
    options=dict(importArmatureOnly=False,importAllLODs=False,importBoundingBoxes=False)
    raw=base.readMod3File(str(path),options);parsed=base.ParsedMod3File();parsed.parse(raw,options)
    col=bpy.data.collections.new(name+'.mod3');col['~TYPE']='MHW_MOD3_COLLECTION';bpy.context.scene.collection.children.link(col)
    if rig is None:rig,_=base.importSkeleton(parsed.skeleton.boneInfoList,name,col,'STICK',5)
    mats=createMaterialDict(parsed.materialNameList)
    importLODGroup(parsed,col,mats,rig,set(),False)
    meshes=[o for o in col.all_objects if o.type=='MESH']
    for mesh in meshes:
        for material in mesh.data.materials:
            material.diffuse_color=(.30,.39,.54,1)
    return rig,meshes,col


def main():
    bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    extracted=ROOT/'research/mhw-original/extracted'
    body=extracted/'pl/m_equip/pl043_0000/body/mod/m_body043_0000.mod3'
    rig,meshes,col=import_model(body,'Original hunter')
    for path in sorted((extracted/'pl/m_equip/pl043_0000').glob('*/mod/*.mod3')):
        if path==body:continue
        _,parts,_=import_model(path,path.stem,rig);meshes+=parts
    weapon,weapon_meshes,_=import_model(extracted/'wp/swo/bs_swo001/mod/bs_swo001.mod3','Native sword detached')
    verts=[tuple(mesh.matrix_world@v.co) for mesh in weapon_meshes for v in mesh.data.vertices]
    lo=[min(v[a] for v in verts) for a in range(3)];hi=[max(v[a] for v in verts) for a in range(3)]
    # Record actual imported geometry and rest matrices; do not assume +Z tip.
    evidence=dict(source='original bs_swo001 MOD3',coordinate='Blender MOD3 import basis (+90 X, 0.01 scale)',
         bounds_min=lo,bounds_max=hi,spans=[hi[a]-lo[a] for a in range(3)],
         bones={b.name:dict(matrix=[list(r) for r in b.matrix_local],parent=b.parent.name if b.parent else None) for b in weapon.data.bones},
         mount_verified=False,limitation='No decoded drawn mounting matrix; candidate source attachment is not runtime blade data')
    (ROOT/'research/mhw-original/source-sword-geometry.json').write_text(json.dumps(evidence,indent=2),encoding='utf-8')
    # Candidate uses the actual imported weapon root basis, not an invented turn.
    # Its validity as the game's drawn attachment still needs native evidence.
    weapon_root=weapon.data.bones['MhBone_000'].matrix_local.copy()
    weapon.rotation_mode='QUATERNION'
    for pose in rig.pose.bones:pose.rotation_mode='QUATERNION'
    helper=base.ensure_mhw_root_motion_bone(rig)
    mapping={int(b.name.split('_')[-1]):b.name for b in rig.data.bones if b.name.startswith('MhBone_')}
    lmt=base.read_lmt_file(extracted/'hm/wp/wp03/mot/w03_00/w03_00.lmt')
    action=next(a for a in lmt.actions if a.id==172)
    evaluation=base.compile_pose_evaluation(action,mapping,version=95)
    for frame in range(action.header.frame_count):
        for p in rig.pose.bones:p.location=(0,0,0);p.rotation_quaternion=(1,0,0,0);p.scale=(1,1,1)
        for (target,usage),value in evaluation.sample(frame).items():
            target=target or helper.name;sem=base.get_usage_semantics(usage)
            converted=base.adapt_track_frames_for_target_space(rig,base.TrackSpaceTarget('bone',target),sem,[(float(frame),value)])[0][1]
            p=rig.pose.bones[target];setattr(p,sem.blender_path_hint,converted)
        for p in rig.pose.bones:
            p.keyframe_insert('location',frame=frame);p.keyframe_insert('rotation_quaternion',frame=frame);p.keyframe_insert('scale',frame=frame)
        bpy.context.view_layer.update()
        mount=rig.pose.bones[mapping[247]].matrix@weapon_root.inverted()
        weapon.matrix_world=mount
        weapon.keyframe_insert('location',frame=frame);weapon.keyframe_insert('rotation_quaternion',frame=frame);weapon.keyframe_insert('scale',frame=frame)
    scene=bpy.context.scene;scene.frame_start=0;scene.frame_end=action.header.frame_count-1;scene.render.fps=60
    scene.render.engine='BLENDER_EEVEE_NEXT';scene.render.resolution_x=720;scene.render.resolution_y=900;scene.render.resolution_percentage=100
    scene.world.color=(.6,.6,.6)
    bpy.ops.object.camera_add(location=(3.7,6.3,4.6));camera=bpy.context.object;camera.rotation_euler=(Vector((0,0,2.7))-camera.location).to_track_quat('-Z','Y').to_euler();camera.data.type='ORTHO';camera.data.ortho_scale=5.5;scene.camera=camera
    bpy.ops.object.light_add(type='AREA',location=(1,3,5));bpy.context.object.data.energy=1000;bpy.context.object.data.shape='DISK';bpy.context.object.data.size=5
    path=ROOT/'research/mhw-original/source-original-air172-candidate-mount.blend';scene.frame_set(30);bpy.ops.wm.save_as_mainfile(filepath=str(path))
    for frame in (0,18,32,48,64,90,133):
        scene.frame_set(frame);scene.render.filepath=str(ROOT/f'research/mhw-original/source-air172-candidate-{frame:03d}.png');bpy.ops.render.render(write_still=True)
    print('Native meshes',len(meshes),'sword bounds',lo,hi,'source action172 rendered; mount remains unverified',flush=True)


if __name__=='__main__':main()
