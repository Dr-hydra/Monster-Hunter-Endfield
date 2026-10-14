"""Record exact source prop root normalization and authored hand attachments.

Run in the project Blender. Existing mesh, materials and decoded textures are
preserved. Metadata is a coordinate round trip, never a second mounting turn.
"""
from pathlib import Path
import importlib.util, json, sys
import bpy
from mathutils import Matrix, Vector

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('multi', ROOT/'tools/mhw_multi_bake.py')
BASE = importlib.util.module_from_spec(spec); spec.loader.exec_module(BASE)
C = BASE.CONVERT.to_4x4(); CI = C.inverted()

def load(path): return json.loads(path.read_text(encoding='utf-8-sig'))
def dump(path, data):
    tmp = path.with_suffix(path.suffix+'.tmp')
    tmp.write_text(json.dumps(data, ensure_ascii=False, separators=(',', ':')), encoding='utf-8'); tmp.replace(path)
def matrix(mat): return [mat[r][c] for c in range(4) for r in range(4)]
def rig_for(path, name):
    options = dict(importArmatureOnly=True, importAllLODs=False, importBoundingBoxes=False)
    parsed = BASE.BASE.ParsedMod3File(); parsed.parse(BASE.BASE.readMod3File(str(path), options), options)
    col = bpy.data.collections.new(name+'.mod3'); col['~TYPE'] = 'MHW_MOD3_COLLECTION'; bpy.context.scene.collection.children.link(col)
    rig, _ = BASE.BASE.importSkeleton(parsed.skeleton.boneInfoList, name, col, 'STICK', 5)
    return rig

def main():
    bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    body = ROOT/'research/mhw-original/extracted/pl/m_equip/pl043_0000/body/mod/m_body043_0000.mod3'
    rig = rig_for(body, 'Original hunter attachment metadata')
    functions = {int(b.name.split('_')[-1]): b for b in rig.data.bones if b.name.startswith('MhBone_')}
    attachments = {}
    for function, hand in ((30, 8), (47, 12)):
        bone, wrist = functions[function], functions[hand]
        if bone.parent != wrist: raise ValueError('Authored hand attachment parent changed')
        local = C@wrist.matrix_local.inverted()@bone.matrix_local@CI
        attachments[str(function)] = dict(function=function, hand_function=hand, parent=wrist.name,
            source_model=body.relative_to(ROOT).as_posix(), hand_local_matrix=matrix(local),
            source_model_rest_matrix=matrix(C@bone.matrix_local@CI),
            evidence='Original MOD3 function 30/47 is an immediate child of hand function 8/12; exact imported rest matrices',
            native_cpp_mount_selection_verified=False)
    evidence = []
    for mode in BASE.MODES:
        folder = ROOT/f'assets/modes/{mode}'
        index = load(folder/'props/index.json')
        for row in index['props']:
            path = folder/row['file']; doc = load(path)
            prop = rig_for(ROOT/doc['source'], doc['id'])
            roots = [b for b in prop.data.bones if b.parent is None]
            if not roots: raise ValueError('Source prop has no skeleton root')
            root = roots[0]; original = C@root.matrix_local@CI
            attachment = 30 if ('sld' in doc['id'] or doc['id'].endswith('_bow001')) else 47
            role = 'sheath' if 'saya' in doc['id'] else 'shield' if 'sld' in doc['id'] else 'arrow' if doc['id'].endswith('_ya001') else 'main'
            doc['role'] = role; row['role'] = role
            doc.update(source_attachment_function=attachment, attachment_source_bone=attachment,
                source_hand_function=8 if attachment==30 else 12,
                source_attachment=attachments[str(attachment)],
                source_root_bind_matrix=matrix(original), source_root_bind_inverse_matrix=matrix(original.inverted()),
                source_root_bone=root.name,
                source_geometry_normalized=True,
                source_mount_formula='normalizedVertex = C * rootBindInverse * importedVertex; worldVertex = attachmentWorldUnity * normalizedVertex; DO NOT reapply rootBindInverse',
                attachment_verified=False,
                limitation='Authored hand-child attachment is exact resource evidence. Native C++ selection/dynamic sheathing is not replayed; no extra guessed hand or blade turn.')
            if role=='main' and mode in ('longsword','sword_shield','greatsword','lance'):
                # This measured axis is ONLY a local practice trace. It never
                # rotates a hand or decides a weapon installation transform.
                spans=[doc['bounds_max'][i]-doc['bounds_min'][i] for i in range(3)]
                major=max(range(3),key=lambda i:spans[i]);sign=1 if abs(doc['bounds_max'][major])>abs(doc['bounds_min'][major]) else -1
                axis=[0.,0.,0.];axis[major]=float(sign)
                projected=[sign*p[major] for p in doc['vertices']];length=max(projected)
                tips=[p for p,value in zip(doc['vertices'],projected) if value>=length-max(.015,length*.008)]
                tip=[sum(p[i] for p in tips)/len(tips) for i in range(3)]
                doc.update(source_attack_axis=axis,source_attack_length=length,source_grip_point=[0.,0.,0.],source_attack_tip=tip,
                    source_trace_evidence=dict(source='Exact indexed original normalized MOD3 vertices and original root bind origin',
                        indexed_vertices=len(set(doc['indices'])),bone_palette=[b['name'] for b in doc['rig_bones']],
                        policy='Long-axis tip span relative original own root; original vertices only. Used solely for local practice trace; never mounting/wrist adjustment or source damage',
                        native_hitbox_replayed=False))
            if role == 'sheath':
                doc.update(source_attachment_function=None, attachment_source_bone=None, source_attachment=None,
                    source_hand_function=None, mounting_available=False,
                    limitation='Actual original sheath geometry/root/materials; native stowed mounting relation not decoded. Retain data and hide visual until exact mounting is available.')
            if doc['id'].endswith('_ya001'):
                # Preserve skeleton tip evidence if present. No sign is assigned
                # from a bounding box or an arbitrary PCA eigenvector.
                bones = []
                for b in prop.data.bones:
                    normalized = C@root.matrix_local.inverted()@b.matrix_local@CI
                    bones.append(dict(name=b.name, parent=b.parent.name if b.parent else None,
                        position=list(normalized.translation)))
                doc['source_arrow_bone_positions'] = bones
                by_name = {b['name']: Vector(b['position']) for b in bones}
                directions = [(by_name['MhBone_103']-by_name['MhBone_102']).normalized(),
                    (by_name['MhBone_105']-by_name['MhBone_104']).normalized()]
                if directions[0].dot(directions[1]) < .9999: raise ValueError('Original arrow chain directions disagree')
                doc['source_forward_axis'] = list(directions[0])
                doc['source_forward_axis_evidence'] = dict(
                    source='Own original ya001 MOD3 root-normalized skeleton and skinned geometry',
                    chains=[['MhBone_102','MhBone_103'],['MhBone_104','MhBone_105']],
                    chain_length_meters=[(by_name['MhBone_103']-by_name['MhBone_102']).length,
                        (by_name['MhBone_105']-by_name['MhBone_104']).length],
                    geometric_bounds=[doc['bounds_min'], doc['bounds_max']],
                    correspondence='Both original arrow chains extend 0.650m along +X towards actual positive-tip geometry; no target camera or wrist-axis fitting',
                    native_projectile_handler_replayed=False)
            dump(path, doc)
            evidence.append(dict(mode=mode, id=doc['id'], source=doc['source'], function=attachment,
                root_bone=root.name, root_bind=doc['source_root_bind_matrix'],
                root_inverse=doc['source_root_bind_inverse_matrix'], normalized=True,
                arrow_bones=doc.get('source_arrow_bone_positions')))
        dump(folder/'props/index.json', index)
    target = ROOT/'research/multiweapon/source-attachment-evidence.json'
    dump(target, dict(schema_version=1, coordinate='Unity Y-up meters; column-major 4x4',
        attachments=attachments, props=evidence, hashes_computed=False, game_launched=False))
    print(json.dumps(dict(props=len(evidence), attachments=list(attachments), arrows=[e for e in evidence if e['arrow_bones']]), ensure_ascii=False), flush=True)

if __name__ == '__main__': main()
