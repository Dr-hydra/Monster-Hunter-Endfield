"""Bind mesh-audit results to current runtime inputs without artifact hashes.

Profile values are compared directly. Local geometry/graph/source track files
are bound by their full names, sizes and nanosecond modification times. These
file identities detect a regenerated input and require re-audit; they are not
cryptographic proof and must not be fabricated from an unverified old result.
"""
from pathlib import Path
import json
import importlib.metadata
import platform
import sys

SCHEMA = 2
MODES = {'longsword', 'sword_shield', 'greatsword', 'lance', 'light_bowgun', 'bow'}
PYTHON_INPUTS = ('tools/endfield_mesh_audit.py', 'tools/endfield_pose_projection.py',
                'tools/endfield_profiles.py', 'tools/laevat_profile.py',
                'tools/resource_audit_contract.py', 'tools/resource_transaction.py')
NATIVE_INPUTS = ('native/retarget.h', 'native/core.h', 'tools/offline_retarget/pose_solver.cpp',
                'tools/offline_retarget/main.cpp', 'tools/offline_retarget/CMakeLists.txt',
                'third_party/include/nlohmann/json.hpp')
PROFILE_KEYS = ('character_id', 'skeleton_root_path', 'bones', 'bone_paths', 'finger_bones',
                'rest_local_paths', 'rest_local_transforms', 'rest_model_matrices',
                'body_proportions', 'mount_sockets', 'weapon', 'visual_weapon', 'visual_weapons',
                'mesh_to_model_bind_registration', 'bind_registration_by_renderer',
                'ancestor_palette_main_bones', 'ground_foot_probes', 'ground_foot_mesh_rest_min_y',
                'ground_hand_probes', 'ground_extra_foot_probe_vertices', 'secondary_skin_roots',
                'blade_direction', 'practice_blade_axis')
QUALIFIED = {'passed', 'pass', 'offline-qualified', 'offline-mesh-qualified',
             'offline-lbs-qualified', 'offline-lbs-passed', 'offline-mesh-passed', 'offline_mesh_checked'}
SPECIAL = {'laevatain_blade': ('longsword', 'wpn_misc_0016', 'laevat-weapon-raw.json', 'research/weapon-coverage/laevat-geometry'),
           'typhoea_bow': ('bow', 'wpn_misc_0051', 'special-weapon-model-raw.json', 'research/typhoea/geometry')}


def read(path): return json.loads(Path(path).read_text(encoding='utf-8-sig'))


def profile_inputs(profile):
    return {key: profile[key] for key in PROFILE_KEYS if key in profile}


def numerical_environment():
    import numpy as np
    dependencies = np.__config__.CONFIG.get('Build Dependencies', {})
    return {'python': platform.python_version(), 'implementation': sys.implementation.name,
            'platform': sys.platform, 'machine': platform.machine(), 'bits': 64 if sys.maxsize > 2**32 else 32,
            'packages': {name: importlib.metadata.version(name) for name in ('numpy', 'scipy', 'Pillow')},
            'linear_algebra': {name: {key: value for key, value in dependencies.get(name, {}).items()
                if key in ('name', 'version', 'openblas configuration')} for name in ('blas', 'lapack')}}


def qualified(row):
    if row.get('status') not in QUALIFIED or row.get('errors') != []:
        raise ValueError('Mesh audit must be checked with an explicit empty error list')


def input_contract(root, character_id, mode, profile):
    root = Path(root).resolve()
    registry = read(root/'assets/modes/index.json')
    entry = next((item for item in registry['modes'] if item['id'] == mode), None)
    if not entry: raise ValueError('Unknown audit mode: ' + mode)
    mode_root = (root/entry['data_root']).resolve(); mode_root.relative_to(root)
    graph_name = profile.get('evidence', {}).get('source_graph')
    if not graph_name: raise ValueError('Current profile lacks its source graph identity')
    graph = Path(graph_name)
    if not graph.is_absolute(): graph = root/graph
    geometry = root/'research/character-coverage/geometry'/character_id
    if not graph.is_file() or not (geometry/'inspection.json').is_file():
        raise FileNotFoundError('Current target graph/geometry is unavailable: ' + character_id)
    paths = {graph.resolve(), (geometry/'inspection.json').resolve(),
             (root/'research/character-coverage'/(character_id+'-rig.json')).resolve(),
             (root/'profiles/index.json').resolve(), (root/'assets/modes/index.json').resolve()}
    paths.update((root/'research/character-coverage/oracle'/character_id/(mode+suffix)).resolve() for suffix in ('.json', '.ltp'))
    for relative in (*PYTHON_INPUTS, *NATIVE_INPUTS):
        paths.add((root/relative).resolve())
    solver=root/'build/offline-retarget-dual/Release/endfield_pose_solver.dll'
    if not solver.is_file():solver=root/'build/offline-retarget/Release/endfield_pose_solver.dll'
    paths.add(solver.resolve())
    paths.add((solver.parent/'endfield_retarget_oracle.exe').resolve())
    for row in read(geometry/'inspection.json'):
        if row.get('kind', 'Mesh') != 'Mesh': continue
        mesh = (geometry/row['file']).resolve(); mesh.relative_to(geometry.resolve()); paths.add(mesh)
    paths.update(path.resolve() for path in mode_root.rglob('*') if path.is_file() and path.suffix in {'.json', '.lsm', '.lsg', '.lsf', '.lsk'})
    if not (mode_root/'catalog.json').is_file(): raise FileNotFoundError(mode_root/'catalog.json')
    files = []
    for path in sorted(paths):
        stat = path.stat()
        name = path.relative_to(root).as_posix() if path.is_relative_to(root) else str(path)
        files.append({'path': name, 'bytes': stat.st_size, 'modified_ns': stat.st_mtime_ns})
    return {'schema': SCHEMA, 'character_id': character_id, 'mode': mode,
            'profile': profile_inputs(profile), 'files': files, 'environment': numerical_environment()}


def validate_current(root, character_id, mode, profile, row):
    qualified(row)
    proof = row.get('input_contract')
    if not isinstance(proof, dict) or proof.get('schema') != SCHEMA:
        raise ValueError('Mesh audit has no current-input contract; rerun actual mesh audit: ' + character_id + '/' + mode)
    if proof != input_contract(root, character_id, mode, profile):
        raise ValueError('Mesh audit inputs changed; rerun actual mesh audit: ' + character_id + '/' + mode)
    return profile_inputs(profile)


def validate_catalog_status(status, profile, expected_profile):
    if status.get('offline_status') not in ('passed', 'mesh_checked') or status.get('audit_status') not in QUALIFIED or status.get('errors') != []:
        raise ValueError('Complete package refuses failed/pending/errored mesh audit')
    if status.get('current_inputs_verified') is not True or expected_profile != profile_inputs(profile):
        raise ValueError('Mesh audit is not bound to current character profile inputs')


def special_input_contract(root, character_id, weapon_id, profile):
    root = Path(root).resolve()
    mode, resource, graph, geometry_name = SPECIAL[weapon_id]
    contract = input_contract(root, character_id, mode, profile)
    registry = read(root/'profiles/weapons/index.json')['weapons']
    item = next(row for row in registry if row['id'] == weapon_id)
    profile_path = (root/'profiles/weapons'/item['profile']).resolve()
    profile_path.relative_to(root/'profiles/weapons')
    geometry = root/geometry_name
    paths = {root/'research/expansion-20261013'/graph, geometry/'inspection.json',
             root/'tools/endfield_special_mesh_audit.py', root/'tools/endfield_special_previews.py'}
    for row in read(geometry/'inspection.json'):
        mesh = (geometry/row['file']).resolve(); mesh.relative_to(geometry.resolve()); paths.add(mesh)
    contract['special'] = {'weapon_id': weapon_id, 'resource_id': resource,
                           'profile': read(profile_path), 'files': []}
    for path in sorted(paths):
        stat = path.stat()
        contract['special']['files'].append({'path': path.relative_to(root).as_posix(),
                                             'bytes': stat.st_size, 'modified_ns': stat.st_mtime_ns})
    return contract


def validate_special_current(root, character_id, weapon_id, profile, row):
    mode, resource, _, _ = SPECIAL[weapon_id]
    if (row.get('status') != 'offline_special_mesh_checked' or row.get('errors') != [] or
            row.get('character_id') != character_id or row.get('mode') != mode or row.get('weapon') != resource):
        raise ValueError('Special mesh audit must match target, mode and weapon with no errors')
    if row.get('input_contract') != special_input_contract(root, character_id, weapon_id, profile):
        raise ValueError('Special mesh audit inputs changed or lack proof; rerun actual audit: ' + character_id + '/' + weapon_id)


def validate_special_catalog(weapon, profile, character_profiles):
    weapon_id = weapon['id']
    if weapon.get('offline_status') != 'mesh_checked' or weapon.get('audit_weapon_profile') != profile:
        raise ValueError('Special weapon is not mesh checked against its current profile')
    rows = weapon.get('character_audits')
    if not isinstance(rows, dict) or set(rows) != set(character_profiles):
        raise ValueError('Special weapon audit must cover every current character')
    for character_id, status in rows.items():
        if (status.get('status') != 'offline_special_mesh_checked' or status.get('errors') != [] or
                status.get('current_inputs_verified') is not True or
                status.get('character_profile_ref') != character_id):
            raise ValueError('Special weapon character audit is failed, pending or stale: ' + character_id)


def validate_special_all(root, index=None):
    root = Path(root)
    index = index or read(root/'profiles/index.json')
    audit = read(root/'research/character-coverage/special-cross-coverage.json')
    rows = {(row['character_id'], row['mode']): row for row in audit.get('combinations', [])}
    expected = {(item['character_id'], spec[0]) for item in index['characters'] for spec in SPECIAL.values()}
    if len(rows) != len(audit.get('combinations', [])) or set(rows) != expected:
        raise ValueError('Special mesh audit roster differs from every current character and both weapons')
    for item in index['characters']:
        profile = read(root/'profiles'/item['profile'])
        for weapon_id, spec in SPECIAL.items():
            validate_special_current(root, item['character_id'], weapon_id, profile, rows[item['character_id'], spec[0]])
    return len(rows)


def validate_all(root):
    root=Path(root);index=read(root/'profiles/index.json');audit=read(root/'research/character-coverage/coverage.json')
    if not isinstance(index.get('modes'), list) or len(index['modes']) != len(MODES) or set(index['modes']) != MODES:
        raise ValueError('Mesh audit registry must require all six current modes')
    rows={(row['character_id'],row['mode']):row for row in audit.get('combinations',[])}
    if len(rows)!=len(audit.get('combinations',[])):raise ValueError('Duplicate mesh audit identity')
    expected={(item['character_id'],mode) for item in index['characters'] for mode in index['modes']}
    if set(rows)!=expected:raise ValueError('Mesh audit roster does not match all current character/mode pairs')
    for item in index['characters']:
        profile=read(root/'profiles'/item['profile'])
        for mode in index['modes']:validate_current(root,item['character_id'],mode,profile,rows[item['character_id'],mode])
    validate_special_all(root, index)
    return len(rows)
