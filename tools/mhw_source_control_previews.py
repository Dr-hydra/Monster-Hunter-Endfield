"""Render requested original hunter/weapon control frames, no guessed angles.

Blender --background --python tools/mhw_source_control_previews.py -- mode
slot:frame,frame ... . Current source LMT and actual authored30/47 attachments
are used, with source own-skin tracks when present. No game is launched.
"""
from pathlib import Path
import importlib.util
import json
import sys
import struct
import bpy
from mathutils import Vector, Matrix

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("source_tour",ROOT/"tools/mhw_source_tour_render.py")
T = importlib.util.module_from_spec(spec);spec.loader.exec_module(T)
M,S = T.M,T.S


def main():
    args = sys.argv[sys.argv.index("--")+1:]
    mode = args[0]
    requests = [(int(a.split(":")[0]),int(f)) for a in args[1:] for f in a.split(":")[1].split(",")]
    num = dict(greatsword="00",lance="06")[mode]
    folder = ROOT/"assets/modes"/mode
    destination = ROOT/"research/keyboard-defaults/source-control-previews"/mode
    destination.mkdir(parents=True,exist_ok=True)
    bpy.ops.object.select_all(action="SELECT");bpy.ops.object.delete(use_global=False)
    body=ROOT/"research/mhw-original/extracted/pl/m_equip/pl043_0000/body/mod/m_body043_0000.mod3"
    rig, meshes, _ = S.import_model(body,"Original control source hunter")
    for path in sorted(body.parents[2].glob("*/mod/*.mod3")):
        if path != body:
            _,more,_ = S.import_model(path,path.stem,rig);meshes += more
    helper = M.BASE.ensure_mhw_root_motion_bone(rig)
    mapping = {int(b.name.split("_")[-1]):b.name for b in rig.data.bones if b.name.startswith("MhBone_")}
    for bone in rig.pose.bones:bone.rotation_mode="QUATERNION"
    path = ROOT/f"research/multiweapon/extracted/hm/wp/wp{num}/mot/w{num}_00/w{num}_00.lmt"
    lmt = M.BASE.read_lmt_file(path);actions={a.id:a for a in lmt.actions}
    evaluations = {slot:M.BASE.compile_pose_evaluation(actions[slot],mapping,version=lmt.header.version) for slot in set(s for s,f in requests)|{1}}
    idle = evaluations[1].sample(0)
    props=[]
    for record in T.load(folder/"props/index.json")["props"]:
        if record["role"] in ("sheath","arrow"):continue
        doc=T.load(folder/record["file"]);props.append((doc,T.prop_mesh(doc)))
    skin=T.load(folder/"source_skin_tracks.json")["tracks"];skin_data={}
    C=M.CONVERT.to_4x4();CI=C.inverted()
    scene=bpy.context.scene;scene.render.engine="BLENDER_WORKBENCH";scene.render.resolution_x=640;scene.render.resolution_y=480;scene.render.resolution_percentage=100;scene.render.image_settings.file_format="PNG"
    scene.world.color=(.72,.76,.82);scene.display.shading.light="STUDIO";scene.display.shading.color_type="MATERIAL";scene.display.shading.show_shadows=True;scene.display.shading.show_cavity=True
    bpy.ops.object.camera_add();camera=bpy.context.object;camera.data.type="ORTHO";scene.camera=camera
    rows=[]
    for slot,frame in requests:
        if not 0<=frame<actions[slot].header.frame_count:raise ValueError("Requested source frame out of range")
        for bone in rig.pose.bones:bone.location=(0,0,0);bone.rotation_quaternion=(1,0,0,0);bone.scale=(1,1,1)
        for (target,usage),value in evaluations[slot].sample(frame,base=idle).items():
            target=target or helper.name;semantic=M.BASE.get_usage_semantics(usage);value=M.BASE.adapt_track_frames_for_target_space(rig,M.BASE.TrackSpaceTarget("bone",target),semantic,[(float(frame),value)])[0][1];setattr(rig.pose.bones[target],semantic.blender_path_hint,value)
        bpy.context.view_layer.update()
        for doc,obj in props:
            obj.matrix_world=(rig.matrix_world@rig.pose.bones[mapping[doc["source_attachment_function"]]].matrix)@CI
            own=skin.get(str(int(num)*0+slot+(20000 if mode=="greatsword" else 30000)))
            if own and own["prop_id"]==doc["id"]:
                if slot not in skin_data:
                    data=(folder/own["file"]).read_bytes();n,b,fps=struct.unpack_from("<III",data,4);skin_data[slot]=(data,n,b)
                data,n,b=skin_data[slot];mats=[Matrix([struct.unpack_from("<16f",data,16+(min(frame,n-1)*b+j)*64)[r::4] for r in range(4)])for j in range(b)]
                for j,v in enumerate(obj.data.vertices):
                    point=Vector((0,0,0));base=Vector(doc["vertices"][j])
                    for bone,weight in zip(doc["skin_bone_indices"][j],doc["skin_weights"][j]):
                        if weight:point+=(mats[bone]@base)*weight
                    v.co=point
            else:
                for j,v in enumerate(obj.data.vertices):v.co=doc["vertices"][j]
            obj.data.update()
        center=rig.matrix_world@rig.pose.bones[mapping[13]].matrix.translation
        camera.location=center+Vector((3,-5,1.6));camera.rotation_euler=(center-camera.location).to_track_quat("-Z","Y").to_euler()
        bpy.context.view_layer.update();dg=bpy.context.evaluated_depsgraph_get();bounds=[]
        for obj in meshes+[obj for doc,obj in props]:
            e=obj.evaluated_get(dg);bounds.extend(e.matrix_world@Vector(v)for v in e.bound_box)
        orientation=camera.rotation_euler.to_matrix();right=orientation@Vector((1,0,0));up=orientation@Vector((0,1,0));xx=[v.dot(right)for v in bounds];yy=[v.dot(up)for v in bounds]
        center+=right*((min(xx)+max(xx))/2-center.dot(right))+up*((min(yy)+max(yy))/2-center.dot(up));camera.location=center+Vector((3,-5,1.6));camera.data.ortho_scale=max(max(yy)-min(yy)+.3,(max(xx)-min(xx)+.3)/(640/480))*1.08
        filename=destination/f"slot_{slot:03d}-frame_{frame:03d}.png";scene.render.filepath=str(filename);bpy.ops.render.render(write_still=True)
        rows.append(dict(slot=slot,frame=frame,file=filename.relative_to(ROOT).as_posix(),source_frames=actions[slot].header.frame_count,source_loop_frame=actions[slot].header.loop_frame))
    (destination/"index.json").write_text(json.dumps(dict(schema_version=1,mode=mode,source=path.relative_to(ROOT).as_posix(),frames=rows,
        authored_hand_attachments=True,native_action_number_to_motion_mapping_verified=False,game_launched=False,hashes_computed=False),indent=2),encoding="utf-8")
    print("Original control frames",mode,len(rows),flush=True)


if __name__=="__main__":main()
