"""Fast software-rasterized source mesh boards for motion semantic review.

Actual MOD3 skinning and original LMT pose samples are evaluated by Blender.
Weapon 247/248 mounting is a resource-derived candidate, explicitly unverified;
boards never claim Endfield runtime output or fit blade directions by hand.
"""
from pathlib import Path
import importlib.util,json,math,sys
import bpy
from mathutils import Vector,Matrix
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('multi',ROOT/'tools/mhw_multi_bake.py');BASE=importlib.util.module_from_spec(spec);spec.loader.exec_module(BASE)
spec2=importlib.util.spec_from_file_location('source',ROOT/'tools/mhw_source_model.py');SRC=importlib.util.module_from_spec(spec2);spec2.loader.exec_module(SRC)
sys.path.insert(0,str(ROOT/'local/python-packages'))
from PIL import Image,ImageDraw
SELECT={'greatsword':('00','two','bs_two001',[101,102,104,106,107,114,115,122,123,135,141,144,145,150]),
        'lance':('06','lan','bs_lan001',[1,11,101,102,103,104,105,106,107,110,118,120,125,128,138]),
        'sword_shield':('01','one','bs_one001',[1,44,47,101,102,103,110,112,116,118,120,121,135,136,170,173,360,361,362]),
        'light_bowgun':('13','lbg','bs_lbg001',[1,82,85,88,92,100,102,103,107,110]),
        'bow':('11','bow','bow001',[1,110,125,136,160,161,164])}

def render_mesh(meshes,view,center,width=245,height=245):
    dg=bpy.context.evaluated_depsgraph_get();polys=[];forward=np.array([.72,0,.69]) if view=='front-side' else np.array([0,0,-1.]) if view=='back' else np.array([1.,0,0])
    right=np.cross(forward,[0,1,0]);up=np.array([0,1,0]);light=np.array([.5,.8,.3]);light/=np.linalg.norm(light)
    for mesh,color in meshes:
        ev=mesh.evaluated_get(dg);data=ev.to_mesh();data.calc_loop_triangles();mat=BASE.CONVERT.to_4x4()@mesh.matrix_world
        vertices=np.array([list(mat@v.co) for v in data.vertices]);tri=np.array([t.vertices for t in data.loop_triangles],dtype=np.int32)
        if not len(tri):ev.to_mesh_clear();continue
        points=vertices[tri];normal=np.cross(points[:,1]-points[:,0],points[:,2]-points[:,0]);normal/=np.maximum(1e-9,np.linalg.norm(normal,axis=1))[:,None]
        shade=.35+.65*np.abs(normal@light);depth=points.mean(axis=1)@forward
        rel=points-np.array(center);xy=np.stack((rel@right,rel@up),axis=2);xy[:,:,0]=width*.5+xy[:,:,0]*63;xy[:,:,1]=height*.52-xy[:,:,1]*63
        for i in range(len(points)):polys.append((depth[i],xy[i].tolist(),tuple(int(max(0,min(255,c*shade[i]))) for c in color)))
        ev.to_mesh_clear()
    im=Image.new('RGB',(width,height),'#f0f3f7');draw=ImageDraw.Draw(im)
    for _,points,color in sorted(polys,key=lambda x:x[0],reverse=True):draw.polygon([tuple(p) for p in points],fill=color)
    return im

def main():
    requested=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
    for mode,(num,cat,stem,slots) in SELECT.items():
        if requested and mode not in requested:continue
        bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
        body=ROOT/'research/mhw-original/extracted/pl/m_equip/pl043_0000/body/mod/m_body043_0000.mod3';rig,meshes,_=SRC.import_model(body,'Source hunter')
        for path in sorted(body.parents[2].glob('*/mod/*.mod3')):
            if path==body:continue
            _,more,_=SRC.import_model(path,path.stem,rig);meshes+=more
        wppath=next((ROOT/f'research/multiweapon/extracted/wp/{cat}').glob(f'*/mod/{stem}.mod3'));weapon,wm,_=SRC.import_model(wppath,'Source weapon')
        mounted=[(weapon,247)];allmeshes=[(m,(154,165,182)) for m in meshes]+[(m,(47,101,186)) for m in wm]
        if mode in ('lance','sword_shield'):
            spath=wppath.with_name('bs_sld001.mod3');shield,sm,_=SRC.import_model(spath,'Source shield');mounted.append((shield,248));allmeshes += [(m,(205,150,63)) for m in sm]
        for p in rig.pose.bones:p.rotation_mode='QUATERNION'
        helper=BASE.BASE.ensure_mhw_root_motion_bone(rig);mapping={int(b.name.split('_')[-1]):b.name for b in rig.data.bones if b.name.startswith('MhBone_')}
        lmt=BASE.BASE.read_lmt_file(ROOT/f'research/multiweapon/extracted/hm/wp/wp{num}/mot/w{num}_00/w{num}_00.lmt');by={a.id:a for a in lmt.actions}
        idle=BASE.BASE.compile_pose_evaluation(by[1],mapping,version=lmt.header.version).sample(0)
        rootmats={o.name:next(b.matrix_local.copy() for b in o.data.bones if b.parent is None) for o,_ in mounted}
        board=Image.new('RGB',(1120,65+len(slots)*270),'#f0f3f7');draw=ImageDraw.Draw(board);draw.text((12,12),mode+' ORIGINAL BODY/WEAPON MESH | CANDIDATE SOURCE SOCKET MOUNT | NOT ENDFIELD GAMEPLAY',fill='black')
        evidence=[]
        for r,slot in enumerate(slots):
            a=by[slot];ev=BASE.BASE.compile_pose_evaluation(a,mapping,version=lmt.header.version)
            frames=list(dict.fromkeys([0,round((a.header.frame_count-1)*.28),round((a.header.frame_count-1)*.55),a.header.frame_count-1]));draw.text((6,55+r*270),f'SOURCE {slot} {a.header.frame_count}f / loop{a.header.loop_frame}',fill='black')
            for j,frame in enumerate(frames):
                for p in rig.pose.bones:p.location=(0,0,0);p.rotation_quaternion=(1,0,0,0);p.scale=(1,1,1)
                for (target,usage),value in ev.sample(frame,base=idle).items():
                    target=target or helper.name;sem=BASE.BASE.get_usage_semantics(usage)
                    cv=BASE.BASE.adapt_track_frames_for_target_space(rig,BASE.BASE.TrackSpaceTarget('bone',target),sem,[(float(frame),value)])[0][1]
                    setattr(rig.pose.bones[target],sem.blender_path_hint,cv)
                bpy.context.view_layer.update()
                for obj,function in mounted:obj.matrix_world=rig.pose.bones[mapping[function]].matrix@rootmats[obj.name].inverted()
                bpy.context.view_layer.update();center=BASE.CONVERT@rig.pose.bones[mapping[13]].matrix.translation;center.y+=.18
                image=render_mesh(allmeshes,'front-side',list(center));board.paste(image,(135+j*245,55+r*270));draw.text((140+j*245,302+r*270),'frame '+str(frame),fill='black')
            evidence.append(dict(slot=slot,frames=frames,source_header_frames=a.header.frame_count,loop=a.header.loop_frame))
        out=ROOT/f'research/multiweapon/{mode}/source-mesh-board.png';board.save(out);BASE.dump(out.with_suffix('.json'),dict(mode=mode,samples=evidence,body_source=body.relative_to(ROOT).as_posix(),weapon_source=wppath.relative_to(ROOT).as_posix(),mount_verified=False,source_wrist_rotations='unchanged',gameplay=False))
        print(mode,'actual source skinned mesh board',len(evidence),'motions',flush=True)

if __name__=='__main__':main()
