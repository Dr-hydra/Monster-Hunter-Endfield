"""Compare exported own-palette matrices to direct original Blender LMT poses."""
from pathlib import Path
import importlib.util,json,struct
import bpy
from mathutils import Matrix
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('multi',ROOT/'tools/mhw_multi_bake.py');M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)
def load(p):return json.loads(p.read_text(encoding='utf-8-sig'))
def main():
 results=[]
 for mode in M.MODES:
  bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
  folder=ROOT/f'assets/modes/{mode}';manifest=load(folder/'source_skin_tracks.json');propid=next(iter(manifest['props']));prop=load(folder/manifest['props'][propid]['file']);source=ROOT/prop['source']
  options=dict(importArmatureOnly=True,importAllLODs=False,importBoundingBoxes=False);parsed=M.BASE.ParsedMod3File();parsed.parse(M.BASE.readMod3File(str(source),options),options)
  col=bpy.data.collections.new(mode+'.mod3');col['~TYPE']='MHW_MOD3_COLLECTION';bpy.context.scene.collection.children.link(col);rig,_=M.BASE.importSkeleton(parsed.skeleton.boneInfoList,mode,col,'STICK',5)
  mapping={int(b.name.split('_')[-1]):b.name for b in rig.data.bones if b.name.startswith('MhBone_')};root=rig.data.bones[prop['source_root_bone']];normal=M.CONVERT.to_4x4()@root.matrix_local.inverted();ci=M.CONVERT.to_4x4().inverted();helper=M.BASE.ensure_mhw_root_motion_bone(rig)
  banks=load(folder/'weapon_rig_tracks.json')['tracks'];cache={};checked=0;maximum=0;states=set()
  for slot,row in manifest['tracks'].items():
   if 'source_phases' in row:continue
   raw=(folder/row['file']).read_bytes();n,bones,fps=struct.unpack_from('<III',raw,4);frames=row['frame_map']
   selected={0,n//2,n-1}|{i for i in range(1,n) if frames[i].get('own_slot')!=frames[i-1].get('own_slot')}
   for frame in selected:
    entry=frames[frame];own=entry.get('own_slot')
    if own is None:continue
    key=row['own_bank']+':'+str(own);bank=banks[key];path=ROOT/bank['source']
    if str(path) not in cache:
     lmt=M.BASE.read_lmt_file(path);cache[str(path)]=(lmt,{a.id:a for a in lmt.actions})
    lmt,actions=cache[str(path)];action=actions[own];evaluation=M.BASE.compile_pose_evaluation(action,mapping,version=lmt.header.version)
    at=entry['own_frame'];count=action.header.frame_count;loop=action.header.loop_frame
    if at>=count and loop>=0:at=loop+(at-loop)%(count-loop)
    at=min(at,count-1)
    for pose in rig.pose.bones:pose.location=(0,0,0);pose.rotation_mode='QUATERNION';pose.rotation_quaternion=(1,0,0,0);pose.scale=(1,1,1)
    for (target,usage),value in evaluation.sample(at).items():
     target=target or helper.name;sem=M.BASE.get_usage_semantics(usage);value=M.BASE.adapt_track_frames_for_target_space(rig,M.BASE.TrackSpaceTarget('bone',target),sem,[(float(at),value)])[0][1];setattr(rig.pose.bones[target],sem.blender_path_hint,value)
    bpy.context.view_layer.update()
    for i,bone in enumerate(prop['rig_bones']):
     bind=Matrix([prop['bindposes'][i][r::4] for r in range(4)]);exact=normal@rig.pose.bones[bone['name']].matrix@ci@bind
     exported=struct.unpack_from('<16f',raw,16+(frame*bones+i)*64);error=max(abs(exact[r][c]-exported[c*4+r]) for r in range(4) for c in range(4));maximum=max(maximum,error)
     if error>2e-4:
      print('DEBUG',mode,slot,frame,entry,bone,'exact',list(exact),'exported',exported,'basis',list(rig.pose.bones[bone['name']].matrix_basis),'rest',list(rig.data.bones[bone['name']].matrix_local),flush=True)
      raise ValueError(f'Original own skin mismatch {mode}/{slot}/{frame}/{bone["name"]}: {error}')
    checked+=1;states.add(key)
  result=dict(mode=mode,original_pose_checks=checked,own_states=len(states),max_matrix_error=maximum);results.append(result);print(result,flush=True)
 M.dump(ROOT/'research/multiweapon/source-own-skin-direct-validation.json',dict(schema_version=1,modes=results,
  evidence='Exported native LSK deformation matrix vs direct Blender original MOD3+LMT evaluation, including original state switches/loops',game_launched=False,hashes_computed=False))
if __name__=='__main__':main()
