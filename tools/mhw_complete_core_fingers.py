"""Fill only missing core finger samples using their exact original LMT context.

Also records the original right middle distal-joint draw landmark for bow. This
is a anatomical joint, not an invented camera or sword-axis adjustment.
"""
from pathlib import Path
import importlib.util,json,struct,sys
import bpy
from mathutils import Quaternion
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('multi',ROOT/'tools/mhw_multi_bake.py');BASE=importlib.util.module_from_spec(spec);spec.loader.exec_module(BASE)
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def main():
 bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
 source=ROOT/'research/mhw-original/extracted/pl/m_equip/pl043_0000/body/mod/m_body043_0000.mod3'
 options=dict(importArmatureOnly=True,importAllLODs=False,importBoundingBoxes=False);parsed=BASE.BASE.ParsedMod3File();parsed.parse(BASE.BASE.readMod3File(str(source),options),options)
 col=bpy.data.collections.new('Original core finger context.mod3');col['~TYPE']='MHW_MOD3_COLLECTION';bpy.context.scene.collection.children.link(col)
 rig,_=BASE.BASE.importSkeleton(parsed.skeleton.boneInfoList,'Original hunter',col,'STICK',5);helper=BASE.BASE.ensure_mhw_root_motion_bone(rig)
 mapping={int(b.name.split('_')[-1]):b.name for b in rig.data.bones if b.name.startswith('MhBone_')}
 for p in rig.pose.bones:p.rotation_mode='QUATERNION'
 rest={f:BASE.world(rig.pose.bones[name]) for f,name in mapping.items()};audit=[]
 for mode,(num,cat,offset) in BASE.MODES.items():
  folder=ROOT/f'assets/modes/{mode}';manifest=load(folder/'finger_tracks.json');catalog={x['slot']:x for x in load(folder/'catalog.json')};moves=load(folder/'moves.json');core={s for slots in moves['commands'].values() for s in slots}
  missing=[s for s in sorted(core) if str(s) not in manifest['tracks']]
  selected=missing if mode!='bow' else sorted(set(missing)|{50110,50125,50136,50160,50161,50164})
  if not selected:continue
  path=(ROOT/'research/mhw-original/extracted' if mode=='longsword' else ROOT/'research/multiweapon/extracted')/f'hm/wp/wp{num}/mot/w{num}_00/w{num}_00.lmt'
  lmt=BASE.BASE.read_lmt_file(path);actions={a.id:a for a in lmt.actions};idle=BASE.BASE.compile_pose_evaluation(actions[1],mapping,version=lmt.header.version).sample(0)
  contacts={}
  for slot in selected:
   row=catalog[slot];action=actions[row['source_slot']];ev=BASE.BASE.compile_pose_evaluation(action,mapping,version=lmt.header.version);values=[];draw=[]
   for frame in range(row['frames']):
    source_frame=row.get('source_frame_start',0)+frame
    for p in rig.pose.bones:p.location=(0,0,0);p.rotation_quaternion=(1,0,0,0);p.scale=(1,1,1)
    for (target,usage),value in ev.sample(source_frame,base=idle).items():
     target=target or helper.name;sem=BASE.BASE.get_usage_semantics(usage);value=BASE.BASE.adapt_track_frames_for_target_space(rig,BASE.BASE.TrackSpaceTarget('bone',target),sem,[(float(source_frame),value)])[0][1];setattr(rig.pose.bones[target],sem.blender_path_hint,value)
    bpy.context.view_layer.update()
    for bone in manifest['bones']:
     f=bone['function'];q=BASE.world(rig.pose.bones[mapping[f]])['rotation_xyzw'];r=rest[f]['rotation_xyzw'];delta=Quaternion((q[3],*q[:3]))@Quaternion((r[3],*r[:3])).inverted();delta.normalize();values.extend((delta.x,delta.y,delta.z,delta.w))
    if mode=='bow':
     point=BASE.world(rig.pose.bones[mapping[56]])['position'];wrist=BASE.world(rig.pose.bones[mapping[12]]);q=Quaternion((wrist['rotation_xyzw'][3],*wrist['rotation_xyzw'][:3]));offset=q.inverted()@(BASE.Vector(point)-BASE.Vector(wrist['position']));draw.append(dict(position=point,right_wrist_local=list(offset)))
   if slot in missing:
    file=f'finger_{slot:05d}.lsf';(folder/file).write_bytes(struct.pack('<4sIII',b'LSF1',row['frames'],len(manifest['bones']),60)+struct.pack('<%df'%len(values),*values));manifest['tracks'][str(slot)]=dict(file=file,frames=row['frames'],fps=60,source=path.relative_to(ROOT).as_posix(),source_slot=row['source_slot'],context='Same exact original source idle stationary context as its body clip; source keys override base, no Legacy reuse')
   if draw:contacts[str(slot)]=dict(fps=60,frames=draw,source_slot=row['source_slot'])
  BASE.dump(folder/'finger_tracks.json',manifest)
  if contacts:
   for output,parts in [(51000,[(50160,0,208),(50161,0,100)]),(51001,[(50125,0,0)]*61)]:
    contacts[str(output)]=dict(fps=60,frames=[v for s,start,end in parts for v in contacts[str(s)]['frames'][start:end+1]],source_phases=parts)
   BASE.dump(folder/'bow_hand_contacts.json',dict(schema_version=1,source_function=56,semantic='right_middle_distal_joint',target_bone='Bip001_R_Finger22',
      evidence='Exact original MOD3 hand chain54→55→56 and matching original LMT pose+idle context',
      limitation='Joint landmark for string presentation; exact skin-surface/string collision contact is not native-controller certified',tracks=contacts))
  audit.append(dict(mode=mode,filled=missing,draw_landmark_tracks=len(contacts)));print(audit[-1],flush=True)
 BASE.dump(ROOT/'research/multiweapon/core-finger-completion.json',dict(schema_version=1,modes=audit,game_launched=False,hashes_computed=False))
if __name__=='__main__':main()
