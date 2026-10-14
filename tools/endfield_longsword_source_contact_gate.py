"""Scratch-only per-character source-blade hilt contact promotion gate.

Actual target proximal knuckles and native fixed-length solver are measured
against the real own-skinned MHW sword hilt. No body/source-offset/asset weights
are changed. Masks remain geometric reconstructions, not original game flags.
"""
import argparse
import time
import numpy as np
from scipy.spatial.transform import Rotation
from endfield_profiles import ROOT, read, write
from endfield_mesh_audit import setup, oracle, fk, skin, render, OUT, probe_floor
from endfield_pose_projection import resources, ground_pose, posed_source_parts
from mhw_source_contact_audit import point_to_triangles, intervals


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--characters", nargs="+")
    parser.add_argument("--resume", action="store_true")
    parser.add_argument("--no-gallery", action="store_true")
    args = parser.parse_args()
    characters = args.characters or [row["character_id"] for row in read(ROOT / "profiles/index.json")["characters"]]
    candidates = read(ROOT / "research/multiweapon/longsword-contact-candidates.json")
    if candidates["enabled"] or candidates["authored_contact_flag"]:
        raise ValueError("Input must be unpromoted geometric source candidates")
    data = resources("longsword")
    tracks = data["weapon"]["tracks"]
    original = {slot: [float(frame["support_weight"]) for frame in row["frames"]] for slot,row in tracks.items()}
    record = next(row for row in data["props"]["props"] if row["role"] == "main")
    prop = read(data["root"] / record["file"])
    vertices = np.asarray(prop["vertices"],dtype=float)
    faces = np.asarray(prop["indices"],dtype=int).reshape(-1,3)
    hilt_faces = faces[np.max(vertices[faces,1],axis=1) < 0]
    if not len(hilt_faces):
        raise ValueError("Original indexed sword has no negative-blade-axis hilt faces")
    destination = OUT / "longsword-source-contact-candidate-gate.json"
    rows = read(destination)["characters"] if args.resume and destination.exists() else []
    gallery = OUT / "source-contact-candidate-gallery"
    gallery.mkdir(exist_ok=True)
    start = time.monotonic()

    def save():
        write(destination,dict(schema=2,model_id=record["id"],source_model=prop["source"],characters=rows,
            source_hilt_faces=len(hilt_faces),candidate_source="research/multiweapon/longsword-contact-candidates.json",
            contact_provenance="reconstructed_from_hilt_geometry",authored_contact_flag=False,
            assets_mutated=False,published=False,runtime_verified=False,
            policy="Exact native fixed-length solver; unchanged original source wrist offset/rotation; both actual target proximal knuckles within measured target half palm width of real own-skinned source hilt; residual<5mm, settled>=0.18s, no unreachable solve"))

    try:
        for slot,candidate in candidates["tracks"].items():
            for frame,mask in zip(tracks[slot]["frames"],candidate["candidate_mask"]):
                frame["support_weight"] = float(mask)
        for cid in characters:
            if any(row["character_id"] == cid for row in rows):
                print(cid,"reused existing exact source blade gate",flush=True)
                continue
            scene = setup(cid,update_profile=False)
            meta, clips = oracle(cid,"longsword")
            # Published per-model smooth masks must not replace the original
            # binary candidate being tested in this scratch reproduction.
            meta["contact_candidate_scratch"] = True
            ids = [scene["index"][scene["r"]["fingers"]["Bip001_L_Finger"+str(n)]] for n in (1,2)]
            index = scene["index"][scene["r"]["fingers"]["Bip001_L_Finger1"]]
            pinky = scene["index"][scene["r"]["fingers"]["Bip001_L_Finger4"]]
            tolerance = float(np.linalg.norm(scene["rest"][index,:3,3]-scene["rest"][pinky,:3,3])/2)
            right = scene["index"][meta["bone_paths"][12]]
            left = scene["index"][meta["bone_paths"][8]]
            basis = Rotation.from_quat(meta["source_basis_xyzw"]).as_matrix()
            char_rows = []
            for clip in clips:
                candidate = candidates["tracks"].get(str(clip["slot"]))
                if not candidate:
                    continue
                source_mask = np.asarray(candidate["candidate_mask"],bool)
                selected = np.flatnonzero(source_mask)
                passing = np.zeros(len(source_mask),np.int8)
                samples = []
                worst = maximum_residual = 0.0
                unreachable = 0
                for at in range(0,len(selected),12):
                    frames = selected[at:at+12]
                    world = fk(scene,meta,clip,frames)
                    world = ground_pose(scene,meta,clip,frames,world,probe_floor)
                    posed, clones = posed_source_parts(scene,meta,clip,frames,world)
                    blade = next(part for part in clones if part["name"] == record["id"])
                    actual_vertices = skin(blade,posed)
                    for k,frame in enumerate(frames):
                        distance = float(max(point_to_triangles(world[k,ids,:3,3],actual_vertices[k,hilt_faces])))
                        worst = max(worst,distance)
                        source = tracks[str(clip["slot"])]["frames"][int(frame)]
                        offset = basis@Rotation.from_quat(source["rotation_xyzw"]).as_matrix()@np.asarray(source["support_offset"])*meta["scale"]
                        residual = float(np.linalg.norm(world[k,left,:3,3]-world[k,right,:3,3]-offset))
                        maximum_residual = max(maximum_residual,residual)
                        failure = ("longsword",clip["slot"],int(frame)) in scene.get("_unreachable_contact",set())
                        unreachable += int(failure)
                        settled = float(frame)/clip["fps"] >= .18
                        passing[frame] = int(settled and not failure and residual<.005 and distance<=tolerance)
                        if (distance>tolerance or residual>.005 or failure) and len(samples)<8:
                            samples.append(dict(frame=int(frame),knuckle_hilt_max_m=distance,wrist_source_offset_residual_m=residual,
                                                settled=settled,unreachable=failure))
                char_rows.append(dict(slot=clip["slot"],frames=len(source_mask),candidate_frames=int(source_mask.sum()),
                    passing_frames=int(passing.sum()),pass_mask=passing.tolist(),pass_intervals=intervals(passing),
                    max_knuckle_hilt_m=worst,maximum_wrist_source_offset_residual_m=maximum_residual,
                    unreachable_frames=unreachable,rejected_samples=samples))
                if not args.no_gallery and cid in ("chr_0003_endminf","chr_0016_laevat","chr_0034_typhoea") and clip["slot"] in (102,307,311,1000) and passing.any():
                    frame = int(np.flatnonzero(passing)[len(np.flatnonzero(passing))//2])
                    sample = np.array([frame])
                    world = fk(scene,meta,clip,sample)
                    world = ground_pose(scene,meta,clip,sample,world,probe_floor)
                    posed, clones = posed_source_parts(scene,meta,clip,sample,world)
                    for view in ("rear","side","front"):
                        render(scene["parts"]+clones,posed,gallery/f'{cid}-{clip["slot"]}-{frame}-{view}.png',
                               cid+f'\nScratch source contact {clip["slot"]}:{frame}\nActual MHW own-skinned hilt',view)
            rows.append(dict(character_id=cid,target_palm_half_width_m=tolerance,tracks=char_rows,
                candidate_frames=sum(row["candidate_frames"] for row in char_rows),passing_frames=sum(row["passing_frames"] for row in char_rows),
                runtime_verified=False,published=False))
            save()
            print(cid,"candidate",rows[-1]["candidate_frames"],"passing",rows[-1]["passing_frames"],"elapsed",round(time.monotonic()-start),flush=True)
    finally:
        for slot,weights in original.items():
            for frame,weight in zip(tracks[slot]["frames"],weights):
                frame["support_weight"] = weight
    save()


if __name__ == "__main__":
    main()
