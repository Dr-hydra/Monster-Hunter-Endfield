"""Read-only source wrist, attachment and actual longsword-hilt audit.

The output is geometric contact evidence, not an authored contact/IK flag.
No support weights or animation assets are rewritten by this investigation.
"""
from pathlib import Path
import json
import struct
import sys
import numpy as np
from scipy.spatial.transform import Rotation

ROOT = Path(__file__).resolve().parents[1]
MODES = ("longsword", "sword_shield", "lance", "bow", "greatsword", "light_bowgun")


def read(path):
    return json.loads(path.read_text("utf-8"))


def summary(values):
    a = np.asarray(values, dtype=float)
    return dict(min=float(a.min()), median=float(np.median(a)), p95=float(np.percentile(a, 95)), max=float(a.max()))


def intervals(mask):
    starts = np.where(np.diff(np.r_[0, mask.astype(np.int8), 0]) == 1)[0]
    ends = np.where(np.diff(np.r_[0, mask.astype(np.int8), 0]) == -1)[0] - 1
    return [dict(first=int(first), last=int(last), frames=int(last-first+1)) for first, last in zip(starts, ends)]


def point_to_triangles(points, triangles):
    """Exact Euclidean closest distance to the supplied indexed triangles."""
    a, b, c = (triangles[:, i] for i in range(3))
    ab, ac = b-a, c-a
    normal = np.cross(ab, ac)
    nn = np.einsum("ij,ij->i", normal, normal)
    d00 = np.einsum("ij,ij->i", ab, ab)
    d01 = np.einsum("ij,ij->i", ab, ac)
    d11 = np.einsum("ij,ij->i", ac, ac)
    denominator = d00*d11-d01*d01
    result = []
    for point in points:
        ap = point-a
        distance_plane = np.einsum("ij,ij->i", ap, normal)
        projection = ap-normal*(distance_plane/np.maximum(nn, 1e-20))[:, None]
        d20 = np.einsum("ij,ij->i", projection, ab)
        d21 = np.einsum("ij,ij->i", projection, ac)
        u = (d11*d20-d01*d21)/np.maximum(denominator, 1e-20)
        v = (d00*d21-d01*d20)/np.maximum(denominator, 1e-20)
        inside = (denominator > 1e-18) & (u >= 0) & (v >= 0) & (u+v <= 1)
        best = np.where(inside, distance_plane**2/np.maximum(nn, 1e-20), np.inf)
        for first, last in ((a,b), (b,c), (c,a)):
            delta = last-first
            along = np.einsum("ij,ij->i", point-first, delta)/np.maximum(np.einsum("ij,ij->i", delta, delta), 1e-20)
            near = first+delta*np.clip(along, 0, 1)[:, None]
            best = np.minimum(best, np.einsum("ij,ij->i", point-near, point-near))
        result.append(float(np.sqrt(best.min())))
    return result


def source_scale_and_translation_proof(catalog, core):
    sys.path.insert(0, str(ROOT / "research/mhw-longsword/20261012/tooling/mhw-anim-tools"))
    from core.formats.lmt.reader import read_lmt_file
    lmt = read_lmt_file(ROOT / "research/mhw-original/extracted/hm/wp/wp03/mot/w03_00/w03_00.lmt")
    slots = {catalog[slot].get("source_slot", slot) for slot in core if slot != 1000}
    slots.update((170, 171, 172, 173))
    forbidden = []
    for action in lmt.actions:
        if action.id not in slots:
            continue
        forbidden.extend(dict(slot=action.id, bone=t.header.bone_id, usage=t.header.usage)
                         for t in action.tracks if (t.header.bone_id in (34,37,51,54) and t.header.usage == 1)
                         or (t.header.bone_id in range(1,13) and t.header.usage == 2))
    if forbidden:
        raise ValueError("Rigid knuckle-root projection is not exact for " + str(forbidden))
    return dict(source_slots=sorted(slots), no_knuckle_translation_tracks=True, no_main_arm_scale_tracks=True,
                knuckle_functions=[34,37,51,54], proof="Actual original LMT track usages, not guessed finger positions")


def audit():
    report = dict(schema_version=1, coordinate="Source Unity Y-up meters", modes=[], assets_mutated=False,
                  source_contact_flag_verified=False, game_launched=False, server_started=False, hashes_computed=False)
    hierarchy = read(ROOT / "research/multiweapon/source-rig-full-hierarchy.json")["bones"]
    for mode in MODES:
        folder = ROOT / "assets/modes" / mode
        source = read(folder / "source_weapon_tracks.json")["tracks"]
        moves = read(folder / "moves.json")["commands"]
        catalog = {row["slot"]: row for row in read(folder / "catalog.json")}
        core = sorted({slot for slots in moves.values() for slot in slots})
        grip = read(folder / "grip_tracks.json")["tracks"]
        mode_row = dict(mode=mode, core_clips=len(core), clips=[])
        if mode == "longsword":
            prop = read(folder / "props/bs_swo001.json")
            vertices = np.array(prop["vertices"], dtype=float)
            triangles = np.asarray(prop["indices"], dtype=int).reshape(-1,3)
            handle_faces = triangles[np.max(vertices[triangles,1], axis=1) < 0]
            fingers = read(folder / "finger_tracks.json")["bones"]
            rig = read(folder / "source_rig.json")["bones"]
            skin = read(folder / "source_skin_tracks.json")["tracks"]
            indices, weights = np.asarray(prop["skin_bone_indices"]), np.asarray(prop["skin_weights"])
            hand_points = {}
            for side, function in (("left",8), ("right",12)):
                rest = rig[side+"_hand"]
                rest_r = Rotation.from_quat(rest["rotation"]).as_matrix()
                selected = [f for f in fingers if f["name"] in (side+"_index0", side+"_middle0")]
                if any(hierarchy[str(b["function"])]["parent"] != f"MhBone_{function:03d}" for b in selected):
                    raise ValueError("Finger-root parent proof changed")
                hand_points[side] = [(rest_r.T@(np.array(b["rest_position"])-rest["position"])) for b in selected]
            # The radius is derived from this hunter's actual index-to-pinky
            # knuckle span; it is a palm-width envelope, not a chosen wrist gap.
            all_fingers = {f["name"]: f for f in fingers}
            radius = np.linalg.norm(np.array(all_fingers["left_index0"]["rest_position"])-all_fingers["left_pinky0"]["rest_position"])/2
            mode_row["hilt_geometry"] = dict(prop="assets/modes/longsword/props/bs_swo001.json", normalized_root=prop["source_grip_point"],
                subset="Indexed triangles whose original normalized vertices all lie in Y<0, opposite measured +Y blade",
                triangles=len(handle_faces), original_bounds_min=vertices[handle_faces].min((0,1)).tolist(),
                original_bounds_max=vertices[handle_faces].max((0,1)).tolist(),
                hand_envelope_radius_m=float(radius), hand_envelope_evidence="Half actual MOD3 left index0-to-pinky0 bind distance",
                own_skin_policy="Actual LSK own-skin matrices where a source track is published; original normalized bind geometry otherwise, without inventing native carry/interpolation",
                native_own_skin_carry_verified=False)
            mode_row["knuckle_projection_proof"] = source_scale_and_translation_proof(catalog, core)
        for slot in core:
            frames = source[str(slot)]["frames"]
            count = len(frames)
            raw = (folder / grip[str(slot)]).read_bytes()
            lsg = np.frombuffer(raw, dtype="<f4", offset=16).reshape(-1,4)[:,3]
            wrist = np.array([np.linalg.norm(np.array(f["left"]["position"])-f["right"]["position"]) for f in frames])
            attachment = np.array([np.linalg.norm(np.array(f["attachments"]["30"]["position"])-f["attachments"]["47"]["position"]) for f in frames])
            weights_json = np.array([f["support_weight"] for f in frames])
            item = dict(slot=slot, frames=count, wrist_distance_m=summary(wrist), attachment_30_47_distance_m=summary(attachment),
                        json_full_weight_frames=int(np.sum(weights_json>.999)), lsg_full_weight_frames=int(np.sum(lsg>.999)),
                        json_lsg_weight_mismatch_frames=int(np.sum(abs(weights_json-lsg)>1e-5)))
            if mode == "longsword":
                lsk = None
                if str(slot) in skin:
                    row = skin[str(slot)]
                    data = (folder / row["file"]).read_bytes()
                    n,b,fps = struct.unpack_from("<III", data, 4)
                    if data[:4] != b"LSK1" or n != count:
                        raise ValueError("Hilt own skin dimensions differ")
                    lsk = np.frombuffer(data,dtype="<f4",offset=16).reshape(n,b,4,4).transpose(0,1,3,2)
                left_near, right_near, left_points, right_points = [],[],[],[]
                for at, frame in enumerate(frames):
                    mount = frame["attachments"]["47"]
                    mr = Rotation.from_quat(mount["rotation_xyzw"]).as_matrix()
                    v = vertices
                    if lsk is not None:
                        homogeneous = np.c_[vertices,np.ones(len(vertices))]
                        transformed = np.einsum("vwij,vj->vwi", lsk[at,indices], homogeneous)
                        v = np.einsum("vwi,vw->vi", transformed[:,:,:3], weights)
                    for side, distances, records in (("left",left_near,left_points),("right",right_near,right_points)):
                        hand = frame[side]
                        hr = Rotation.from_quat(hand["rotation_xyzw"]).as_matrix()
                        points = np.array([mr.T@(np.array(hand["position"])+hr@point-mount["position"]) for point in hand_points[side]])
                        records.append(points.mean(0).tolist())
                        distances.append(max(point_to_triangles(points, v[handle_faces])))
                mask = np.array(left_near) <= radius
                item.update(left_knuckle_to_hilt_surface_m=summary(left_near), right_knuckle_to_negative_hilt_surface_m=summary(right_near),
                            own_skin_track_applied=lsk is not None,
                            left_knuckle_center_hilt_local_bounds=dict(min=np.min(left_points,axis=0).tolist(),max=np.max(left_points,axis=0).tolist()),
                            left_hilt_candidate_frames=int(mask.sum()), left_hilt_candidate_mask=mask.astype(int).tolist(),
                            left_hilt_candidate_intervals=intervals(mask),
                            candidate_policy="Both exact proximal index/middle knuckle roots are within half source palm width of the exact own-skinned negative-Y hilt mesh",
                            candidate_is_authored_contact_flag=False, source_hand_support_offset_preserved=True)
            mode_row["clips"].append(item)
        mode_row["interpretation"] = ("Wrist gap does not determine contact: the original long hilt places the left grasp about0.58m below the right attachment. Authored30/47 often coincide even during sheath/dodge and are not two hand contact flags. Candidate intervals measure source knuckles against actual posed hilt; finger closure, native contact state and target mesh reach still require checks."
                                      if mode == "longsword" else "Independent left shield/bow and right weapon/arrow need separate owned attachments; zero inter-hand support does not imply missed contact."
                                      if mode in ("sword_shield","lance","bow") else "Existing distance-derived support policy is retained for independent follow-up; source contact state was not authored by this audit.")
        report["modes"].append(mode_row)
    output = ROOT / "research/multiweapon/source-contact-audit.json"
    output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    ls = report["modes"][0]
    source = read(ROOT / "assets/modes/longsword/source_weapon_tracks.json")["tracks"]
    candidates = dict(schema_version=1, mode="longsword", provenance="reconstructed_from_hilt_geometry",
                      authored_contact_flag=False, enabled=False,
                      policy="Exact indexed own-skinned negative-Y hilt plus exact proximal source index/middle roots within measured half palm width; target reach/contact gate required before activation",
                      source_audit="research/multiweapon/source-contact-audit.json", tracks={})
    for clip in ls["clips"]:
        slot = clip["slot"]
        candidates["tracks"][str(slot)] = dict(fps=60, candidate_mask=clip["left_hilt_candidate_mask"],
            intervals=clip["left_hilt_candidate_intervals"], source_offsets=[frame["support_offset"] for frame in source[str(slot)]["frames"]],
            source_rotations_xyzw=[frame["rotation_xyzw"] for frame in source[str(slot)]["frames"]],
            contact_provenance="reconstructed_from_hilt_geometry", target_validation_status="pending", enabled=False)
        candidates["tracks"][str(slot)]["own_skin_track_applied"] = clip["own_skin_track_applied"]
    (ROOT / "research/multiweapon/longsword-contact-candidates.json").write_text(json.dumps(candidates,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(dict(status="source_contact_geometry_measured", modes=len(report["modes"]),
                         ls_core_clips=ls["core_clips"], hilt=ls["hilt_geometry"],
                         clips=[dict(slot=c["slot"], frames=c["frames"], candidate_frames=c["left_hilt_candidate_frames"],
                                     intervals=c["left_hilt_candidate_intervals"], p95=c["left_knuckle_to_hilt_surface_m"]["p95"])
                                for c in ls["clips"]]),ensure_ascii=False))


if __name__ == "__main__":
    audit()
