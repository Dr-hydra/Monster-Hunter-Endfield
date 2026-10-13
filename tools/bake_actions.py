"""Bake evaluated MHW source poses; never install an addon or execute mod scripts.
Run with Blender --background --factory-startup --python this-file.
"""
from pathlib import Path
import sys, json, struct, math
import bpy
from mathutils import Matrix, Vector, Quaternion

ROOT = Path(__file__).resolve().parents[1]
RESEARCH = ROOT / 'research' / 'mhw-longsword' / '20261012'
sys.path.insert(0, str(RESEARCH / 'tooling'))
sys.path.insert(0, str(RESEARCH / 'tooling/mhw-anim-tools'))
from MHW_Model_Editor.addons.MHW_Model_Editor.modules.mod3.file_mod3 import readMod3File
from MHW_Model_Editor.addons.MHW_Model_Editor.modules.mod3.mod3_parser import ParsedMod3File
from MHW_Model_Editor.addons.MHW_Model_Editor.modules.mod3.mod3_functions import importSkeleton
from core.formats.lmt.reader import read_lmt_file
from core.formats.lmt.pose_evaluation import compile_pose_evaluation
from core.formats.lmt.semantics import get_usage_semantics
from blender_adapter.space import TrackSpaceTarget, adapt_track_frames_for_target_space
from blender_adapter.armature import ensure_mhw_root_motion_bone

# Semantic functions confirmed by MOD3 rest hierarchy and positions.
SEMANTIC = {'hips':13, 'spine':1, 'chest':2, 'neck':3, 'head':4,
            'left_shoulder':5, 'left_upper_arm':6, 'left_lower_arm':7, 'left_hand':8,
            'right_shoulder':9, 'right_upper_arm':10, 'right_lower_arm':11, 'right_hand':12,
            'left_upper_leg':14, 'left_lower_leg':15, 'left_foot':16, 'left_toes':17,
            'right_upper_leg':18, 'right_lower_leg':19, 'right_foot':20, 'right_toes':21}

def main():
    out = ROOT / 'assets'; out.mkdir(parents=True, exist_ok=True)
    (ROOT / 'preview').mkdir(exist_ok=True)
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    mod = RESEARCH / 'appearance-zxc/nativePC/pl/m_equip/pl043_0000/body/mod/m_body043_0000.mod3'
    options = dict(importArmatureOnly=True, importAllLODs=False, importBoundingBoxes=False)
    raw = readMod3File(str(mod), options)
    parsed = ParsedMod3File(); parsed.parse(raw, options)
    collection = bpy.data.collections.new('Hunter.mod3'); collection['~TYPE']='MHW_MOD3_COLLECTION'
    bpy.context.scene.collection.children.link(collection)
    rig, _ = importSkeleton(parsed.skeleton.boneInfoList, 'Hunter source', collection, 'STICK', 5)
    helper = ensure_mhw_root_motion_bone(rig)
    if helper.error: raise RuntimeError(helper.error)
    root_name = helper.name
    for p in rig.pose.bones: p.rotation_mode='QUATERNION'
    mapping = {int(b.name.split('_')[-1]):b.name for b in rig.data.bones if b.name.startswith('MhBone_')}
    ordered = list(SEMANTIC)
    # Blender Z-up -> Unity Y-up, reflection across Z; rotations converted as matrices.
    # Hunter faces Blender -Y. Rotate 180 degrees so Unity forward is +Z.
    convert = Matrix(((-1,0,0),(0,0,1),(0,-1,0)))
    rest = {}
    for name, func in SEMANTIC.items():
        b=rig.data.bones[mapping[func]]
        q=(convert @ b.matrix_local.to_3x3() @ convert.inverted()).to_quaternion()
        p=convert @ b.matrix_local.translation
        rest[name]=dict(function=func, rotation=[q.x,q.y,q.z,q.w], position=list(p))
    floor_y=min(rest[n]['position'][1] for n in ('left_foot','right_foot','left_toes','right_toes'))
    rest_hip_height=rest['hips']['position'][1]-floor_y
    hand_anatomy={}
    for side,functions in [('left',(8,37,34,44)),('right',(12,54,51,61))]:
        wrist,middle,index,pinky=[convert @ rig.data.bones[mapping[f]].matrix_local.translation for f in functions]
        hand_anatomy[side]=dict(forward=list(middle-wrist),across=list(index-pinky),
            functions=dict(wrist=functions[0],middle=functions[1],index=functions[2],pinky=functions[3]),
            evidence='Unmodified donor MOD3 bind positions; palm X wrist->middle, Z pinky->index. No video angle fitting.')
    (out/'source_rig.json').write_text(json.dumps(dict(bones=rest,order=ordered,fps=60,hand_anatomy=hand_anatomy,
        up_axis='y',forward_axis='z',source_rest_hip_height=rest_hip_height,
        hip_translation='XZ relative to clip first frame; Y relative to ground-referenced rest hip height'),indent=2),encoding='utf-8')
    lmt=read_lmt_file(RESEARCH/'rise/nativePC/hm/wp/wp03/mot/w03_00/w03_00.lmt')
    catalog=[]; skipped=[]
    for action in lmt.actions:
        if action.header.frame_count<2: continue
        evaluation=compile_pose_evaluation(action,mapping,version=lmt.header.version)
        if not evaluation.supported or action.header.flags2 & 1:
            skipped.append(dict(slot=action.id,reason=('NeedsParent: explicit base pose required' if action.header.flags2 & 1 else '; '.join(d.message for d in evaluation.diagnostics))))
            continue
        count=action.header.frame_count
        # Export world-space rest deltas. Runtime target rig supplies its own proportions.
        samples=[]; positions=[]; attachment_frames=[]; first_hip=None
        for frame in range(count):
            for p in rig.pose.bones:
                p.location=(0,0,0); p.rotation_quaternion=(1,0,0,0); p.scale=(1,1,1)
            for (target, usage),value in evaluation.sample(frame).items():
                target=target or root_name
                semantics=get_usage_semantics(usage)
                converted=adapt_track_frames_for_target_space(rig,TrackSpaceTarget('bone',target),semantics,[(float(frame),value)])[0][1]
                setattr(rig.pose.bones[target],semantics.blender_path_hint,converted)
            bpy.context.view_layer.update()
            row=[]; points=[]
            for name in ordered:
                pose=rig.pose.bones[mapping[SEMANTIC[name]]].matrix
                r=(convert @ pose.to_3x3().normalized() @ convert.inverted()).to_quaternion()
                r0=Quaternion((rest[name]['rotation'][3],*rest[name]['rotation'][:3]))
                delta=r @ r0.inverted(); delta.normalize()
                row.extend((delta.x,delta.y,delta.z,delta.w))
                points.append(list(convert @ pose.translation))
            hip=points[0]
            if first_hip is None: first_hip=hip[:]
            # The MOD3 hips rest at Y=0 while the animation feet rest near Y=0.
            # Subtract rest leg height, NOT rest hips Y, to avoid a second hip lift.
            # XZ motion is clip-relative and may be accumulated by the runtime.
            row.extend((hip[0]-first_hip[0],hip[1]-rest_hip_height,hip[2]-first_hip[2]))
            samples.extend(row); positions.append(points)
            attachments={}
            for func in (247,248):
                pose=rig.pose.bones[mapping[func]].matrix
                attachments[str(func)]={'position':list(convert @ pose.translation),
                    'axes':[list(convert @ pose.to_3x3().col[a].normalized()) for a in range(3)]}
            attachment_frames.append(attachments)
        filename=f'clip_{action.id:03d}.lsm'
        (out/filename).write_bytes(struct.pack('<4sIII',b'LSM1',count,len(ordered),60)+struct.pack('<%df'%len(samples),*samples))
        catalog.append(dict(slot=action.id,file=filename,frames=count,duration=(count-1)/60))
        (ROOT/'preview'/f'slot_{action.id:03d}.json').write_text(json.dumps(dict(bones=ordered,
            positions=positions,attachments=attachment_frames,up_axis='y',fps=60)),encoding='utf-8')
        print(f'BAKED {action.id} frames={count}',flush=True)
    (out/'catalog.json').write_text(json.dumps(catalog,indent=2),encoding='utf-8')
    (ROOT/'preview/bake_report.json').write_text(json.dumps(dict(blender=bpy.app.version_string,
        baked=len(catalog),skipped=skipped,source_rest_hip_height=rest_hip_height,
        coordinate_system='Unity: Y up, Z forward; left bones at negative X',
        hip_reference='Y relative to ground-referenced rest hip height; XZ relative to first frame',
        hashes_computed=False),indent=2),encoding='utf-8')
    bpy.ops.wm.save_as_mainfile(filepath=str(ROOT/'preview/hunter_source.blend'))
    print(f'COMPLETE baked={len(catalog)} skipped={len(skipped)}',flush=True)

if __name__=='__main__': main()
