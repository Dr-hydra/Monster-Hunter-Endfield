"""Render exact original hunter/prop meshes on exported native tour timestamps.

Offline compatible materials and no cloth simulation; no game controller or
target-character output is claimed. Source pose keys and own palettes are real.
"""
from pathlib import Path
import importlib.util,json,math,struct,sys,time
import bpy
from mathutils import Matrix,Vector,Quaternion
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('multi',ROOT/'tools/mhw_multi_bake.py');M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)
spec=importlib.util.spec_from_file_location('source',ROOT/'tools/mhw_source_model.py');S=importlib.util.module_from_spec(spec);spec.loader.exec_module(S)
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def affine(frame):
 q=frame['rotation_xyzw'];mat=Quaternion((q[3],*q[:3])).to_matrix().to_4x4();mat.translation=Vector(frame['position']);return mat
def prop_mesh(doc):
 data=bpy.data.meshes.new(doc['id']);data.from_pydata(doc['vertices'],[],[doc['indices'][i:i+3] for i in range(0,len(doc['indices']),3)]);data.update()
 obj=bpy.data.objects.new(doc['id'],data);bpy.context.collection.objects.link(obj);mat=bpy.data.materials.new(doc['id']+' compatible material');mat.diffuse_color=(.08,.30,.68,1) if doc.get('role')!='shield' else (.65,.35,.08,1);data.materials.append(mat);return obj
def main():
 requested=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [];mode=requested[0] if requested else 'longsword';limit=int(requested[1]) if len(requested)>1 else 0
 selected_indices=None
 if '--only-indices' in requested:
  selection_file=Path(requested[requested.index('--only-indices')+1]);selected_indices=set(load(selection_file)['indices'])
 directory=ROOT/f'research/multiweapon/{mode}/source-tour';timeline=load(directory/'timeline.json');folder=ROOT/timeline['data_root'];catalog={x['slot']:x for x in load(folder/'catalog.json')};original=ROOT/('research/mhw-original/extracted' if mode=='longsword' else 'research/multiweapon/extracted')
 numbers={'longsword':'03','sword_shield':'01','greatsword':'00','lance':'06','light_bowgun':'13','bow':'11'};num=numbers[mode]
 bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
 body=ROOT/'research/mhw-original/extracted/pl/m_equip/pl043_0000/body/mod/m_body043_0000.mod3';rig,meshes,_=S.import_model(body,'Original source hunter')
 for path in sorted(body.parents[2].glob('*/mod/*.mod3')):
  if path==body:continue
  _,more,_=S.import_model(path,path.stem,rig);meshes+=more
 helper=M.BASE.ensure_mhw_root_motion_bone(rig);mapping={int(b.name.split('_')[-1]):b.name for b in rig.data.bones if b.name.startswith('MhBone_')}
 for bone in rig.pose.bones:bone.rotation_mode='QUATERNION'
 lmt=M.BASE.read_lmt_file(original/f'hm/wp/wp{num}/mot/w{num}_00/w{num}_00.lmt');actions={a.id:a for a in lmt.actions};compiled={s:M.BASE.compile_pose_evaluation(a,mapping,version=lmt.header.version) for s,a in actions.items()};idle=compiled[1].sample(0)
 prop_index=load(folder/'props/index.json')['props'];props=[]
 for row in prop_index:
  doc=load(folder/row['file'])
  if doc.get('role') in ('sheath','arrow'):continue
  props.append((doc,prop_mesh(doc)))
 skin=load(folder/'source_skin_tracks.json');skin_data={};C=M.CONVERT.to_4x4();CI=C.inverted();source_rig=load(folder/'source_rig.json');hip_height=source_rig['source_rest_hip_height'];floor_y=source_rig['sole_geometry']['neutral_floor'];lsmap=load(ROOT/'research/mhw-original/helmbreaker-composition.json')['stage_map'] if mode=='longsword' else None
 scene=bpy.context.scene;scene.render.engine='BLENDER_WORKBENCH';scene.render.resolution_x=640;scene.render.resolution_y=480;scene.render.resolution_percentage=100;scene.render.image_settings.file_format='PNG';scene.world.color=(.72,.76,.82);scene.display.shading.light='STUDIO';scene.display.shading.color_type='MATERIAL';scene.display.shading.show_shadows=True;scene.display.shading.show_cavity=True
 bpy.ops.object.camera_add(location=(4,-6,3));camera=bpy.context.object;camera.data.type='ORTHO';camera.data.ortho_scale=3.5;scene.camera=camera
 bpy.ops.mesh.primitive_plane_add(size=100,location=(0,0,-.8));plane=bpy.context.object;floor=bpy.data.materials.new('Preview ground');floor.diffuse_color=(.56,.60,.64,1);plane.data.materials.append(floor)
 frames=directory/'frames';frames.mkdir(exist_ok=True);records=timeline['samples'][:limit or None];last_motion=None;previous={};blend_from={};start=time.monotonic()
 def apply(slot,frame):
  for bone in rig.pose.bones:bone.location=(0,0,0);bone.rotation_quaternion=(1,0,0,0);bone.scale=(1,1,1)
  for (target,usage),value in compiled[slot].sample(frame,base=idle).items():
   target=target or helper.name;sem=M.BASE.get_usage_semantics(usage);value=M.BASE.adapt_track_frames_for_target_space(rig,M.BASE.TrackSpaceTarget('bone',target),sem,[(float(frame),value)])[0][1];setattr(rig.pose.bones[target],sem.blender_path_hint,value)
 for i,record in enumerate(records):
  slot=record['slot'];row=catalog[slot];at=record['seconds']*60;source=row.get('source_slot');sf=row.get('source_frame_start',0)+at
  if slot==1000 and lsmap:
   stage=lsmap[min(len(lsmap)-1,int(at))];source=stage['slot'];sf=stage['frame']
  elif mode=='bow' and slot==51000:
   source=160 if at<209 else 161;sf=at if at<209 else at-209
  elif mode=='bow' and slot==51001:source=125;sf=0
  apply(source,min(sf,actions[source].header.frame_count-1))
  current={b.name:(b.location.copy(),b.rotation_quaternion.copy(),b.scale.copy()) for b in rig.pose.bones}
  if record['motion']!=last_motion:blend_from=previous;last_motion=record['motion']
  t=min(1.,record['seconds']/.10)
  if blend_from and t<1:
   for bone in rig.pose.bones:
    a=blend_from[bone.name];b=current[bone.name];bone.location=a[0].lerp(b[0],t);bone.rotation_quaternion=a[1].slerp(b[1],t);bone.scale=a[2].lerp(b[2],t)
  previous=current;bpy.context.view_layer.update();hip=C@rig.pose.bones[mapping[13]].matrix.translation;desired=Vector(record['hip'])+Vector((0,hip_height,0));rig.location=CI.to_3x3()@(desired-hip);bpy.context.view_layer.update()
  for doc,obj in props:
   obj.hide_render=not record.get('weapons_visible',True)
   function=doc['source_attachment_function'];pose=rig.matrix_world@rig.pose.bones[mapping[function]].matrix;obj.matrix_world=pose@CI
   track=skin['tracks'].get(str(slot))
   if track and track['prop_id']==doc['id']:
    if slot not in skin_data:
     raw=(folder/track['file']).read_bytes();n,b,fps=struct.unpack_from('<III',raw,4);skin_data[slot]=(raw,n,b)
    raw,n,b=skin_data[slot];sample=min(n-1,int(at));mats=[Matrix([struct.unpack_from('<16f',raw,16+(sample*b+j)*64)[r::4] for r in range(4)]) for j in range(b)]
    for j,v in enumerate(obj.data.vertices):
     point=Vector((0,0,0));base=Vector(doc['vertices'][j])
     for bone,weight in zip(doc['skin_bone_indices'][j],doc['skin_weights'][j]):
      if weight:point+=(mats[bone]@base)*weight
     v.co=point
   else:
    for j,v in enumerate(obj.data.vertices):v.co=doc['vertices'][j]
   obj.data.update()
  center=Vector(desired);center.y=max(.25,center.y-.1);center_blender=CI.to_3x3()@center;camera.location=center_blender+Vector((3,-5,1.6));camera.rotation_euler=(center_blender-camera.location).to_track_quat('-Z','Y').to_euler()
  bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get();bounds=[]
  for obj in meshes+[o for d,o in props if not o.hide_render]:
   evaluated=obj.evaluated_get(dg);bounds.extend(evaluated.matrix_world@Vector(v) for v in evaluated.bound_box)
  bounds.append(CI.to_3x3()@Vector((desired.x,floor_y,desired.z)))
  orientation=camera.rotation_euler.to_matrix();right=orientation@Vector((1,0,0));up=orientation@Vector((0,1,0));xx=[p.dot(right) for p in bounds];yy=[p.dot(up) for p in bounds];cx=(min(xx)+max(xx))/2;cy=(min(yy)+max(yy))/2
  center_blender+=right*(cx-center_blender.dot(right))+up*(cy-center_blender.dot(up));camera.location=center_blender+Vector((3,-5,1.6));camera.data.ortho_scale=max(max(yy)-min(yy)+.3,(max(xx)-min(xx)+.3)/(640/480))*1.08
  plane.location=CI.to_3x3()@Vector((desired.x,floor_y,desired.z));scene.render.filepath=str(frames/f'{i:05d}.png')
  if selected_indices is None or i in selected_indices:bpy.ops.render.render(write_still=True)
  if i%100==0:print(mode,'source tour frame',i,'/',len(records),round(time.monotonic()-start,1),'s',flush=True)
 M.dump(directory/'render-evidence.json',dict(schema_version=1,mode=mode,frames=len(records),fps=10,source_mesh=str(body.relative_to(ROOT)),source_pose='Original LMT with explicit source idle context; exact published composite phase map',
  changed_frames_rendered=len(selected_indices) if selected_indices is not None else len(records),unchanged_frames_reused=selected_indices is not None,weapon_visibility='Actual exported native normalSheathed state; hide independent props after completed source sheath',
  controller_timing='Exported actual native TickDemo/controller120Hz sampled10fps',own_skin='Original TIML mWeaponMotionNo and exact own palette; unknown native carry/interpolation not emulated',
  limitation='Compatible preview shading; no source clothing physics or unproven sheath/arrow installation; source hunter preview, not Endfield game validation',game_launched=False,hashes_computed=False))
 print(mode,'rendered',len(records),'source mesh frames',round(time.monotonic()-start,1),'s',flush=True)
if __name__=='__main__':main()
