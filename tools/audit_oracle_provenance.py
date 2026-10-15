"""Prove that cached oracle poses were generated from the current native inputs.

The compare command really regenerates every registered character/mode into an
isolated temporary directory. It publishes a separate proof only if every JSON
and LTP is byte-identical to the existing audited output. It never rewrites the
audited oracle files. File identities use size/mtime, not artifact hashes.
"""
from __future__ import annotations
import argparse
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def _neighbor(name):
    path = Path(__file__).with_name(name + '.py')
    spec = importlib.util.spec_from_file_location('mhe_oracle_' + name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_native = _neighbor('audit_native_build')
_contract = _neighbor('resource_audit_contract')
SCHEMA = 1
PROFILE_FIELDS = ('bone_paths', 'finger_bones', 'ground_foot_mesh_rest_min_y')
PROOF_NAME = 'research/character-coverage/oracle-generation-proof.json'
ORACLE_NAME = 'research/character-coverage/oracle'


def _read(path):
    def reject(value):
        raise ValueError('Non-finite JSON constant: ' + value)
    return json.loads(Path(path).read_text(encoding='utf-8-sig'), parse_constant=reject)


def _inside(root, path):
    path = Path(path).resolve()
    path.relative_to(root)
    return path


def _identity(root, path):
    path = Path(path).resolve()
    if not path.is_file():
        raise FileNotFoundError('Oracle generation input/output missing: ' + str(path))
    stat = path.stat()
    return {'path': path.relative_to(root).as_posix() if path.is_relative_to(root) else str(path),
            'bytes': stat.st_size, 'modified_ns': stat.st_mtime_ns}


def _unique(rows, key, label):
    if not isinstance(rows, list) or not rows:
        raise ValueError(label + ' must be a nonempty list')
    result = {}
    for row in rows:
        value = row.get(key) if isinstance(row, dict) else None
        if not isinstance(value, str) or not value or value in result:
            raise ValueError('Invalid/duplicate ' + label + ' identity')
        result[value] = row
    return result


def generation_inputs(root):
    """Mirror every input read by offline_retarget/main.cpp, without its math.

    Profile JSON is bound by the three actual fields read by the generator.
    Mesh setup may rewrite other profile fields or JSON formatting after oracle
    generation; those writes do not change native oracle poses.
    """
    root = Path(root).resolve()
    native = _native.validate_native_build(root, _contract.NATIVE_INPUTS)
    files = set()

    def data(path):
        path = _inside(root, path)
        before = _identity(root, path)
        value = _read(path)
        if before != _identity(root, path):
            raise ValueError('Oracle input changed while reading: ' + str(path))
        files.add(path)
        return value

    index = data(root/'profiles/index.json')
    characters = _unique(index.get('characters'), 'character_id', 'character registry')
    modes = index.get('modes')
    if not isinstance(modes, list) or len(modes) != len(_contract.MODES) or set(modes) != _contract.MODES:
        raise ValueError('Oracle provenance requires all six registered modes')
    registry = _unique(data(root/'assets/modes/index.json').get('modes'), 'id', 'mode registry')
    if set(registry) != set(modes):
        raise ValueError('Oracle mode registries disagree')

    profiles = {}
    for cid, entry in characters.items():
        path = _inside(root, root/'profiles'/entry['profile'])
        before = _identity(root, path)
        profile = _read(path)
        if before != _identity(root, path):
            raise ValueError('Oracle profile changed while reading: ' + cid)
        if not isinstance(profile.get('bone_paths'), dict) or not isinstance(profile.get('finger_bones'), dict):
            raise ValueError('Oracle profile lacks bone/finger bindings: ' + cid)
        profiles[cid] = {'path': path.relative_to(root).as_posix(),
                         'values': {key: profile[key] for key in PROFILE_FIELDS if key in profile}}
        data(root/'research/character-coverage'/(cid+'-rig.json'))

    mode_inputs = {}
    for mode in modes:
        assets = _inside(root, root/registry[mode]['data_root'])
        data(assets/'source_rig.json')
        moves = data(assets/'moves.json')
        commands = moves['commands'] if 'commands' in moves else moves['moves']
        slots = set()
        for sequence in commands.values():
            for slot in sequence:
                if type(slot) is not int:
                    raise ValueError('Oracle command slot must be an integer')
                slots.add(slot)
        if 'idle' in moves:
            if type(moves['idle']) is not int:
                raise ValueError('Oracle idle slot must be an integer')
            slots.add(moves['idle'])
        if not slots:
            raise ValueError('Oracle mode has no source slots: ' + mode)
        catalog = data(assets/'catalog.json')
        if not isinstance(catalog, list):
            raise ValueError('Native oracle expects a catalog array')
        clips = {}
        for item in catalog:
            slot = item['slot']
            if type(slot) is not int or slot in clips:
                raise ValueError('Invalid/duplicate native oracle catalog slot')
            clips[slot] = item['file']
        for slot in slots:
            files.add(_inside(root, assets/clips[slot]))

        # Match main.cpp exactly: longsword's finger root is hard-coded even
        # when the mode registry sends its body assets to a different directory.
        finger_root = root/'assets/modes/longsword' if mode == 'longsword' else assets
        finger_manifest = finger_root/'finger_tracks.json'
        finger_state = {'path': finger_manifest.relative_to(root).as_posix(), 'present': finger_manifest.is_file()}
        if finger_manifest.is_file():
            fingers = data(finger_manifest)
            tracks = fingers.get('tracks', {})
            for slot in slots:
                if str(slot) in tracks:
                    files.add(_inside(root, finger_root/tracks[str(slot)]['file']))
        mode_inputs[mode] = {'data_root': assets.relative_to(root).as_posix(),
                            'slots': sorted(slots), 'finger_manifest': finger_state}

    files.update((root/name).resolve() for name in _contract.NATIVE_INPUTS)
    directory = Path(native['directory'])
    files.update((directory/name).resolve() for name in ('endfield_retarget_oracle.exe', 'endfield_pose_solver.dll'))
    expected = [f'{cid}/{mode}{suffix}' for cid in sorted(characters)
                for mode in sorted(modes) for suffix in ('.json', '.ltp')]
    return {'schema': SCHEMA, 'profiles': profiles, 'modes': mode_inputs,
            'files': [_identity(root, path) for path in sorted(files)],
            'implementation': [_identity(root, Path(module.__file__)) for module in (_native, _contract)] +
                              [_identity(root, Path(__file__))],
            'executable': _identity(root, directory/'endfield_retarget_oracle.exe'),
            'expected_outputs': expected}


def _outputs(root, folder, names):
    result = []
    for name in names:
        path = _inside(root, folder/name)
        entry = _identity(root, path)
        if not entry['bytes']:
            raise ValueError('Empty oracle output: ' + name)
        result.append(entry)
    return result


def _proof_path(root, value):
    return _inside(root, root/PROOF_NAME if value is None else Path(value) if Path(value).is_absolute() else root/value)


def verify_oracle_provenance(root, proof_path=None):
    """Fail closed unless the current poses match a real prior regeneration."""
    root = Path(root).resolve()
    path = _proof_path(root, proof_path)
    if not path.is_file():
        raise ValueError('Oracle generation proof missing; run audit_oracle_provenance.py --rebuild-compare')
    proof = _read(path)
    if (proof.get('schema') != SCHEMA or proof.get('verification') != 'regenerated_and_byte_compared' or
            proof.get('runtime_verified') is not False):
        raise ValueError('Unsupported oracle generation proof')
    current = generation_inputs(root)
    if proof.get('inputs') != current:
        raise ValueError('Oracle generator inputs changed; regenerate oracle and run --rebuild-compare')
    outputs = _outputs(root, root/ORACLE_NAME, current['expected_outputs'])
    if proof.get('outputs') != outputs:
        raise ValueError('Audited oracle outputs changed after regeneration proof; run --rebuild-compare')
    return {'verified': True, 'pairs': len(outputs)//2, 'files': len(outputs),
            'proof': path.relative_to(root).as_posix(), 'runtime_verified': False}


def rebuild_compare_provenance(root, proof_path=None):
    """Regenerate independently, compare bytes, and atomically publish a sidecar.

    Existing oracle files and an existing sidecar remain untouched on any native
    failure, input drift, missing/extra output, or comparison failure.
    """
    root = Path(root).resolve()
    proof_path = _proof_path(root, proof_path)
    before = generation_inputs(root)
    oracle = root/ORACLE_NAME
    original_outputs = _outputs(root, oracle, before['expected_outputs'])
    scratch = _inside(root, root/'output/oracle-provenance')
    scratch.mkdir(parents=True, exist_ok=True)
    log_path = scratch/f'rebuild-{time.time_ns()}.log'
    executable = Path(before['executable']['path'])
    if not executable.is_absolute():
        executable = root/executable
    with tempfile.TemporaryDirectory(prefix='compare-', dir=scratch) as temporary:
        staging = _inside(root, Path(temporary))
        with log_path.open('w', encoding='utf-8') as stream:
            subprocess.run([str(executable), str(root), str(staging)], cwd=root,
                           stdout=stream, stderr=subprocess.STDOUT, check=True)
        generated = {path.relative_to(staging).as_posix() for path in staging.rglob('*') if path.is_file()}
        if generated != set(before['expected_outputs']):
            raise ValueError('Regenerated oracle batch has missing or extra outputs')
        if generation_inputs(root) != before:
            raise ValueError('Oracle generation inputs changed during rebuild')
        if _outputs(root, oracle, before['expected_outputs']) != original_outputs:
            raise ValueError('Existing oracle changed during comparison')
        for name in before['expected_outputs']:
            path = _inside(root, staging/name)
            if path.read_bytes() != (oracle/name).read_bytes():
                raise ValueError('Regenerated oracle differs from audited output: ' + name)
            if path.suffix == '.json':
                metadata = _read(path)
                if metadata.get('character_id') != path.parent.name or metadata.get('mode') != path.stem:
                    raise ValueError('Regenerated oracle metadata identity mismatch: ' + name)
        # Repeat after comparison so an ordinary change during file reads never
        # gets a signature for a different set of inputs or cached outputs.
        if generation_inputs(root) != before or _outputs(root, oracle, before['expected_outputs']) != original_outputs:
            raise ValueError('Oracle inputs/outputs changed before proof publication')
        proof = {'schema': SCHEMA, 'verification': 'regenerated_and_byte_compared',
                 'created_utc': datetime.now(timezone.utc).isoformat(),
                 'inputs': before, 'outputs': original_outputs, 'runtime_verified': False,
                 'log': log_path.relative_to(root).as_posix(), 'hashes_computed': False}
        proof_path.parent.mkdir(parents=True, exist_ok=True)
        fd, name = tempfile.mkstemp(prefix=proof_path.name+'.', suffix='.building', dir=proof_path.parent)
        pending = _inside(root, Path(name))
        try:
            with os.fdopen(fd, 'w', encoding='utf-8', newline='\n') as stream:
                json.dump(proof, stream, ensure_ascii=False, indent=2, allow_nan=False)
                stream.write('\n')
            os.replace(pending, proof_path)
        finally:
            pending.unlink(missing_ok=True)
    return verify_oracle_provenance(root, proof_path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--proof', type=Path)
    parser.add_argument('--rebuild-compare', action='store_true')
    args = parser.parse_args()
    action = rebuild_compare_provenance if args.rebuild_compare else verify_oracle_provenance
    print(json.dumps(action(args.root, args.proof)))


if __name__ == '__main__':
    main()
