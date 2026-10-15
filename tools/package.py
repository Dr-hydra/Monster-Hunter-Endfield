"""Validate complete multi-mode references and build EML format-1 ZIP, without hashes."""
from __future__ import annotations
import argparse
import array
import importlib.util
import json
import math
from pathlib import Path, PurePosixPath
import re
import struct
import zipfile
_audit_spec = importlib.util.spec_from_file_location('mhe_resource_audit_contract', Path(__file__).with_name('resource_audit_contract.py'))
_audit_contract = importlib.util.module_from_spec(_audit_spec)
_audit_spec.loader.exec_module(_audit_contract)
validate_catalog_status = _audit_contract.validate_catalog_status
validate_special_catalog = _audit_contract.validate_special_catalog
_native_spec = importlib.util.spec_from_file_location('mhe_audit_native_build', Path(__file__).with_name('audit_native_build.py'))
_native_build = importlib.util.module_from_spec(_native_spec)
_native_spec.loader.exec_module(_native_build)
_oracle_spec = importlib.util.spec_from_file_location('mhe_audit_oracle_provenance', Path(__file__).with_name('audit_oracle_provenance.py'))
_oracle_provenance = importlib.util.module_from_spec(_oracle_spec)
_oracle_spec.loader.exec_module(_oracle_provenance)


def validate_current_mesh_audits(root):
    _native_build.validate_native_build(root, _audit_contract.NATIVE_INPUTS)
    _oracle_provenance.verify_oracle_provenance(root)
    return _audit_contract.validate_all(root)

PROJECT = Path(__file__).resolve().parents[1]
MODES = {'longsword', 'sword_shield', 'greatsword', 'lance', 'light_bowgun', 'bow'}
ALLOWED_ASSET_EXTENSIONS = {'.lsm', '.lsg', '.lsf', '.lsk', '.json', '.wav', '.rgba', '.png', '.webp'}
EML_ENTRY_COUNT = 4096
EML_TOTAL_BYTES = 256 * 1024 * 1024
EML_ENTRY_BYTES = 128 * 1024 * 1024
EML_MANIFEST_BYTES = 256 * 1024
LSW_FRAME = struct.Struct('<36fII')


def safe_relative(path: str) -> PurePosixPath:
    value = PurePosixPath(path)
    parts = path.split('/')
    if not path or len(path.encode('utf-16-le')) // 2 > 240 or '\\' in path or ':' in path or value.is_absolute() or any(
            not part or part in ('.', '..') or part.endswith(('.', ' ')) or
            any(ord(character) < 32 or 127 <= ord(character) <= 159 for character in part) or
            re.fullmatch(r'(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])', part.split('.')[0], re.IGNORECASE)
            for part in parts):
        raise ValueError(f'Unsafe package path: {path!r}')
    return value


def read(path: Path):
    def reject_constant(value):
        raise ValueError(f'Non-finite JSON number in {path}: {value}')
    return json.loads(path.read_text(encoding='utf-8-sig'), parse_constant=reject_constant)


def required(entries, name: str, extension=None):
    path = safe_relative(name)
    if extension and path.suffix.lower() != extension:
        raise ValueError(f'Unexpected reference format: {name}')
    if name not in entries:
        raise FileNotFoundError('Referenced resource absent from package: ' + name)
    return entries[name]


def relative_reference(root_name, value):
    return str(safe_relative(root_name) / safe_relative(value))


def validate_loader_limits(sizes):
    """Match the installed EML import boundary on final uncompressed payloads."""
    if len(sizes) > EML_ENTRY_COUNT:
        raise ValueError('EML ZIP entry count exceeds 4096')
    names, total = set(), 0
    for name, size in sizes.items():
        safe_relative(name)
        folded = name.casefold()
        if folded in names:
            raise ValueError('EML ZIP has case-insensitive duplicate paths: ' + name)
        names.add(folded)
        if type(size) is not int or size < 0 or size > EML_ENTRY_BYTES:
            raise ValueError('EML ZIP entry exceeds 128 MiB or has invalid size: ' + name)
        if name == 'module.json' and size > EML_MANIFEST_BYTES:
            raise ValueError('EML module.json exceeds 256 KiB')
        total += size
        if total > EML_TOTAL_BYTES:
            raise ValueError('EML ZIP extracted total exceeds 256 MiB')
    return total


def validate_native_identity(payload, module_id, version):
    marker = ('MHE_BUILD_V1|' + module_id + '|' + version + '|abi=1|').encode('ascii')
    if not payload.startswith(b'MZ') or marker + b'\0' not in payload:
        raise ValueError('Native DLL identity/version differs from the package; rebuild the module')


def final_payloads(entries):
    """Minify JSON without rounding; use identical bytes for validation and ZIP."""
    payloads = {}
    for name, source in entries.items():
        if isinstance(source, bytes):
            payloads[name] = source
        elif name.endswith('.json'):
            payloads[name] = json.dumps(read(source), ensure_ascii=False, separators=(',', ':'),
                                        allow_nan=False).encode('utf-8')
        else:
            payloads[name] = source.read_bytes()
    return payloads


def validate_binary(path: Path, expected_magic, expected_bones=None):
    with path.open('rb') as stream:
        header = stream.read(16)
    if len(header) != 16:
        raise ValueError(f'Truncated track: {path}')
    magic, count, third, fourth = struct.unpack('<4sIII', header)
    if magic != expected_magic or not 1 <= count <= 36000:
        raise ValueError(f'Invalid track header: {path}')
    if magic == b'LSG1':
        fps, components = third, fourth
        stride = components * 4
        if components != 4:
            raise ValueError(f'Grip components must be 4: {path}')
    else:
        bones, fps = third, fourth
        if not 1 <= bones <= 1000 or expected_bones is not None and bones != expected_bones:
            raise ValueError(f'Track bone count differs from manifest: {path}')
        stride = bones * (64 if magic == b'LSK1' else 16) + (12 if magic == b'LSM1' else 0)
    if not 1 <= fps <= 240 or path.stat().st_size != 16 + count * stride:
        raise ValueError(f'Track size/fps invalid: {path}')
    with path.open('rb') as stream:
        stream.seek(16)
        values = array.array('f')
        values.fromfile(stream, count * stride // 4)
    if any(not math.isfinite(value) for value in values):
        raise ValueError(f'Non-finite source track data: {path}')
    if magic == b'LSK1':
        for begin in range(0, len(values), 16):
            matrix = values[begin:begin + 16]
            if any(abs(value) > 100000 for value in matrix) or any(abs(matrix[i]) >= .001 for i in (3, 7, 11)) or abs(matrix[15] - 1) >= .001:
                raise ValueError(f'Invalid source skin affine matrix: {path}')
    if magic == b'LSG1':
        for frame in range(count):
            x, y, z, weight = values[frame * 4:frame * 4 + 4]
            if x*x+y*y+z*z > 9 or not 0 <= weight <= 1:
                raise ValueError(f'Invalid source grip offset/weight: {path} frame {frame}')
    if magic in (b'LSM1', b'LSF1'):
        quaternion_components = bones * 4
        for frame in range(count):
            begin = frame * stride // 4
            for component in range(0, quaternion_components, 4):
                norm = sum(value * value for value in values[begin + component:begin + component + 4])
                if abs(norm - 1) > .02:
                    raise ValueError(f'Non-unit source quaternion: {path} frame {frame}')
    return count, fps


def validate_audio(root: Path, entries, mode, audio_root, motions):
    source = required(entries, audio_root + '/cues.json', '.json')
    audio = read(source)
    return validate_audio_manifest(audio, entries, mode, audio_root, motions)


def validate_audio_manifest(audio, entries, mode, audio_root, motions):
    if audio.get('schema_version') not in (1, 2) or not audio.get('clips') or not audio.get('cues') or len(audio['clips']) > 1024:
        raise ValueError(f'Empty/invalid audio manifest: {mode}')
    clip_ids = set()
    for clip in audio['clips']:
        if clip['id'] in clip_ids:
            raise ValueError(f'Duplicate audio ID in {mode}: {clip["id"]}')
        clip_ids.add(clip['id'])
        name = relative_reference(audio_root, clip['file'])
        path = required(entries, name, '.wav')
        data = path.read_bytes()
        if not 44 <= len(data) <= 64*1024*1024 or data[:4] != b'RIFF' or data[8:12] != b'WAVE' or struct.unpack('<I', data[4:8])[0] + 8 != len(data):
            raise ValueError(f'Invalid/truncated source WAV: {name}')
        fmt, payload, at = None, None, 12
        while at + 8 <= len(data):
            kind, size = struct.unpack_from('<4sI', data, at)
            start = at + 8
            if size > len(data) - start:
                raise ValueError(f'Truncated WAV chunk: {name}')
            if kind == b'fmt ':
                if size < 16:
                    raise ValueError(f'Truncated WAV format: {name}')
                fmt = struct.unpack_from('<HHIIHH', data, start)
                if fmt[0] == 0xfffe:
                    if size < 40 or data[start+28:start+40] != bytes.fromhex('00001000800000aa00389b71'):
                        raise ValueError(f'Invalid extensible WAV: {name}')
                    fmt = (struct.unpack_from('<I', data, start+24)[0],) + fmt[1:]
            elif kind == b'data' and payload is None:
                payload = data[start:start+size]
            at = start + size + (size & 1)
        if fmt is None or not payload:
            raise ValueError(f'WAV lacks format/samples: {name}')
        format_id, channels, rate, byte_rate, align, bits = fmt
        if channels not in (1, 2) or not 8000 <= rate <= 192000 or format_id not in (1, 3) or (
                bits not in (8, 16, 24, 32) if format_id == 1 else bits != 32) or align != channels*(bits//8) or len(payload) % align or len(payload)/align/rate > 30:
            raise ValueError(f'Unsupported WAV PCM dimensions: {name}')
        if format_id == 3 and any(not math.isfinite(value[0]) for value in struct.iter_unpack('<f', payload)):
            raise ValueError(f'Non-finite float WAV samples: {name}')
    for slot, cues in audio['cues'].items():
        if int(slot) not in motions:
            raise ValueError(f'Cue refers to unpublished source in {mode}/{slot}')
        if not isinstance(cues, list) or len(cues) > 256:
            raise ValueError(f'Too many or invalid audio cues in {mode}/{slot}')
        previous = -1
        for cue in cues:
            kind = cue.get('kind', 'oneshot')
            if kind not in ('oneshot', 'start', 'loop', 'stop') or not isinstance(cue.get('loop', False), bool):
                raise ValueError(f'Invalid audio lifecycle type in {mode}/{slot}')
            if not isinstance(cue.get('group', 'weapon'), str) or len(cue.get('group', 'weapon')) > 128:
                raise ValueError(f'Invalid audio group in {mode}/{slot}')
            if not isinstance(cue.get('condition', ''), str) or len(cue.get('condition', '')) > 64:
                raise ValueError(f'Invalid audio condition in {mode}/{slot}')
            for name in ('fade', 'fade_in', 'fade_out', 'gain', 'priority'):
                value = cue.get(name, 1 if name == 'gain' else 0)
                low, high = (-1, 2) if name in ('fade_in', 'fade_out') else (0, 100) if name == 'priority' else (0, 2)
                if not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(value) or not low <= value <= high:
                    raise ValueError(f'Invalid audio {name} in {mode}/{slot}')
            if cue.get('kind', 'oneshot') != 'stop' and cue.get('clip') not in clip_ids:
                raise ValueError(f'Cue references absent clip in {mode}: {cue.get("clip")}')
            if len(cue.get('variants', [])) > 64 or any(variant not in clip_ids for variant in cue.get('variants', [])):
                raise ValueError(f'Cue references absent variation in {mode}')
            time = cue.get('time', -1)
            if not isinstance(time, (int, float)) or not math.isfinite(time) or time < 0 or time < previous:
                raise ValueError(f'Invalid source cue order/time in {mode}/{slot}')
            previous = time
            if time > motions[int(slot)]['duration'] + 1e-5:
                raise ValueError(f'Cue extends beyond playable clip in {mode}/{slot}')
    return len(clip_ids)


def validate_theme_audio(entries):
    """Display lifecycle PCM uses the same WAV and cue closure as motion audio."""
    audio = read(required(entries, 'assets/audio/theme_cues.json', '.json'))
    stages = {'laevatain_blade': {'start', 'stop'},
              'typhoea_bow': {'charge', 'release', 'cancel'}}
    themes = audio.get('themes', {})
    if audio.get('schema_version') != 1 or set(themes) != set(stages):
        raise ValueError('Missing/unknown special model audio lifecycle')
    if not audio.get('mapping_evidence') or not audio.get('timing_evidence'):
        raise ValueError('Special model audio lacks source/lifecycle evidence')
    cues = {}
    for theme, required_stages in stages.items():
        if set(themes[theme]) != required_stages:
            raise ValueError('Incomplete special model audio stages: ' + theme)
        for items in themes[theme].values():
            if not isinstance(items, list) or not items:
                raise ValueError('Empty special model audio stage: ' + theme)
            cues[str(len(cues))] = items
    converted = {**audio, 'cues': cues}
    return validate_audio_manifest(converted, entries, 'special_themes', 'assets/audio',
                                   {int(slot): {'duration': 30} for slot in cues})


def validate_contact_masks(entries, motions, character_ids):
    bank = read(required(entries, 'assets/modes/longsword/contact_masks.json', '.json'))
    models = {'wpn_misc_0016', 'source:longsword_bs_swo001'}
    slots = {'3', '9', '11', '101', '102', '103', '104', '105', '106', '107',
             '108', '109', '112', '127', '128', '140', '306', '307', '311', '1000'}
    if bank.get('schema_version') != 2 or bank.get('mode') != 'longsword' or set(bank.get('models', {})) != models:
        raise ValueError('Invalid geometric contact mask schema/model coverage')
    for field in ('authored_contact_flag', 'source_offsets_modified', 'source_rotations_modified',
                  'runtime_verified', 'visual_quality_certified'):
        if bank.get(field) is not False:
            raise ValueError('Geometric contact masks must preserve non-authored/unverified scope: ' + field)
    if bank.get('provenance') != 'reconstructed_from_actual_source_hilt_and_exact_target_geometry':
        raise ValueError('Missing source hilt/target geometry contact provenance')
    fade = bank.get('fade_seconds')
    if not isinstance(fade, (int, float)) or isinstance(fade, bool) or not math.isfinite(fade) or not 0 <= fade <= 2:
        raise ValueError('Invalid geometric contact fade')
    pairs = positive = 0
    for model_id, model in bank['models'].items():
        if set(model.get('characters', {})) != set(character_ids):
            raise ValueError('Contact mask character coverage differs: ' + model_id)
        for character_id, character in model['characters'].items():
            pairs += 1
            if set(character.get('tracks', {})) != slots:
                raise ValueError('Contact mask core slot coverage differs: ' + character_id)
            for slot, track in character['tracks'].items():
                motion = motions.get(int(slot))
                if motion is None or track.get('fps') != motion['fps'] or track.get('frames') != motion['frames']:
                    raise ValueError('Contact mask differs from published body frames/fps: ' + slot)
                frames = motion['frames']
                passed, weights = track.get('pass_mask'), track.get('weights')
                if not isinstance(passed, list) or not isinstance(weights, list) or len(passed) != frames or len(weights) != frames:
                    raise ValueError('Contact mask dimensions differ: ' + slot)
                if any(type(value) is not int or value not in (0, 1) for value in passed):
                    raise ValueError('Contact pass mask must be exact binary evidence: ' + slot)
                inside = set()
                previous = -1
                for interval in track.get('intervals', []):
                    first, last = interval.get('first'), interval.get('last')
                    if type(first) is not int or type(last) is not int or not previous < first <= last < frames or interval.get('frames') != last - first + 1:
                        raise ValueError('Contact interval bounds/order invalid: ' + slot)
                    inside.update(range(first, last + 1))
                    previous = last
                if {frame for frame, value in enumerate(passed) if value} != inside:
                    raise ValueError('Contact intervals do not match exact passing frames: ' + slot)
                for frame, weight in enumerate(weights):
                    if not isinstance(weight, (int, float)) or isinstance(weight, bool) or not math.isfinite(weight) or not 0 <= weight <= 1:
                        raise ValueError('Contact weight outside finite 0..1: ' + slot)
                    if weight > 0:
                        positive += 1
                        if passed[frame] != 1 or frame not in inside:
                            raise ValueError('Contact weight activates outside exact geometric pass: ' + slot)
    if bank.get('summary', {}).get('model_character_pairs') != pairs or bank.get('summary', {}).get('positive_weight_frames') != positive:
        raise ValueError('Contact mask summary differs from actual evidence')
    return pairs


def validate_hand_support(entries):
    bank = read(required(entries, 'assets/modes/sword_shield/support_ground_tracks.json', '.json'))
    if bank.get('schema_version') != 1 or not bank.get('tracks') or not set(bank['tracks']) <= {'10011', '10112'}:
        raise ValueError('Hand support must reference the measured SnS source motions only')
    if bank.get('authored_ground_contact_flag') is not False or bank.get('source_root_and_rotations_modified') is not False:
        raise ValueError('Hand support must preserve geometric/non-authored contact scope')
    for slot, track in bank['tracks'].items():
        motion = required(entries, 'assets/modes/sword_shield/clip_' + slot + '.lsm', '.lsm')
        with motion.open('rb') as stream:
            _, frames, _, fps = struct.unpack('<4sIII', stream.read(16))
        if track.get('fps') != fps or track.get('frames') != frames:
            raise ValueError('Hand support frames/fps differ from actual LSM')
        for side in ('left', 'right'):
            data = track.get(side, {})
            active, clearance = data.get('active'), data.get('clearance')
            if not isinstance(active, list) or not isinstance(clearance, list) or len(active) != frames or len(clearance) != frames:
                raise ValueError('Hand support dimension mismatch: ' + side)
            if any(type(value) is not int or value not in (0, 1) for value in active):
                raise ValueError('Hand support active flags must be binary: ' + side)
            if any(not isinstance(value, (int, float)) or isinstance(value, bool) or not math.isfinite(value) for value in clearance):
                raise ValueError('Hand support clearance must be finite: ' + side)
            if 'kind' in data:
                kinds = data['kind']
                if not isinstance(kinds, list) or len(kinds) != frames or any(type(kind) is not int or kind not in (0, 1, 2, 3) for kind in kinds):
                    raise ValueError('Invalid hand support/collision kind: ' + side)
                if any((kind in (1, 2)) != bool(flag) for kind, flag in zip(kinds, active)):
                    raise ValueError('Target hand collision guard cannot falsify source active: ' + side)
    profile = read(required(entries, 'profiles/chr_0018_dapan.json', '.json'))
    hands = profile.get('ground_hand_probes', {})
    if set(hands) != {'left', 'right'} or any(not isinstance(probes, list) or not 8 <= len(probes) <= 32 for probes in hands.values()):
        raise ValueError('Missing measured Dapan hand mesh probes')
    for side, probes in hands.items():
        for probe in probes:
            influences = probe.get('influences', [])
            if probe.get('side') != side or not influences:
                raise ValueError('Invalid hand mesh support probe')
            total = 0
            for influence in influences:
                weight, point = influence.get('weight'), influence.get('local_point')
                if not influence.get('bone_path') or not influence.get('bone_name') or not isinstance(weight, (int, float)) or isinstance(weight, bool) or not math.isfinite(weight) or not 0 < weight <= 1:
                    raise ValueError('Invalid hand probe skin influence')
                if not isinstance(point, list) or len(point) != 3 or any(not isinstance(value, (int, float)) or not math.isfinite(value) for value in point):
                    raise ValueError('Invalid hand probe local point')
                total += weight
            if abs(total - 1) > .02:
                raise ValueError('Hand mesh probe skin weights are not normalized')
    return len(hands['left']) + len(hands['right'])


def validate_timeline(manifest, motions, mode, field):
    """Every declared frame track must follow the actual published LSM dimensions."""
    tracks = manifest.get('tracks', {})
    if set(tracks) != {str(slot) for slot in motions}:
        raise ValueError(f'{field} track coverage differs from source catalog: {mode}')
    for slot, track in tracks.items():
        motion = motions[int(slot)]
        frames = track.get(field, [])
        if track.get('fps') != motion['fps'] or len(frames) != motion['frames']:
            raise ValueError(f'{field} frame count/fps differs in {mode}/{slot}')
        if field == 'clearance' and any(not isinstance(v, (int, float)) or not math.isfinite(v) or v < 0 for v in frames):
            raise ValueError(f'Invalid shoe clearance in {mode}/{slot}')
        if field == 'frames':
            for frame in frames:
                for data in (frame, frame.get('left', {}), frame.get('right', {}), frame.get('parent', {})):
                    if not data:
                        continue
                    quaternion = data.get('rotation_xyzw', [])
                    position = data.get('position', [])
                    if len(quaternion) != 4 or len(position) != 3 or any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in quaternion + position):
                        raise ValueError(f'Invalid source attachment frame in {mode}/{slot}')
                    if abs(sum(v*v for v in quaternion) - 1) > .02:
                        raise ValueError(f'Non-unit source attachment rotation in {mode}/{slot}')


def validate_controls(moves, motions, mode):
    commands = moves.get('commands', {})
    def hold_policy(policy, slots, command):
        frame = policy.get('frame', -1)
        if type(frame) is not int or frame < -1 or any(frame >= motions[slot]['frames'] for slot in slots) or not isinstance(policy.get('loop', False), bool):
            raise ValueError(f'Hold points beyond playable source in {mode}/{command}')
        if 'loop_frame' in policy or policy.get('loop',False):
            loop_frame=policy.get('loop_frame',0)
            if type(loop_frame) is not int or loop_frame<0 or not policy.get('loop',False) or any(loop_frame>=motions[slot]['frames']-1 for slot in slots):
                raise ValueError(f'Hold loop must have a nonzero source interval in {mode}/{command}')
        previous = -1
        for release in policy.get('releases', []):
            seconds = release.get('seconds', -1)
            if release.get('slot') not in motions or not isinstance(seconds, (int, float)) or isinstance(seconds, bool) or not math.isfinite(seconds) or not 0 <= seconds <= 30 or seconds < previous:
                raise ValueError(f'Hold release invalid in {mode}/{command}')
            previous = seconds
    for command, hold in moves.get('hold_commands', {}).items():
        if command not in commands:
            raise ValueError(f'Hold names absent command in {mode}/{command}')
        hold_policy(hold, commands[command], command)
    for slot, routes in moves.get('hardware_routes', {}).items():
        if int(slot) not in motions or not isinstance(routes, dict):
            raise ValueError('Hardware route references unpublished source: ' + mode)
        for mask, command in routes.items():
            if not 1 <= int(mask) <= 127 or command not in commands:
                raise ValueError('Hardware route input/command invalid: ' + mode)
    for key, behavior in moves.get('input_behavior', {}).items():
        seconds = behavior.get('hold_seconds', .35)
        if key not in {'attack', 'secondary', 'spirit', 'dodge', 'sheathe', 'aim', 'reload'} or behavior.get('tap') not in commands or behavior.get('hold') not in commands or not isinstance(seconds, (int, float)) or isinstance(seconds, bool) or not math.isfinite(seconds) or not .05 <= seconds <= 5:
            raise ValueError('Invalid timed tap/hold mapping: ' + mode)
    for command, variants in moves.get('hold_variants', {}).items():
        if command not in commands or not isinstance(variants, list) or not 1 <= len(variants) <= 32:
            raise ValueError('Invalid hold variant command/count: ' + mode)
        for variant in variants:
            if variant.get('slot') not in motions or not variant.get('from_slots') or any(slot not in motions for slot in variant['from_slots']):
                raise ValueError('Hold variant references unpublished source: ' + mode)
            hold_policy(variant, [variant['slot']], command)


def validate_keyboard_presets(modes):
    for mode in modes:
        metadata = mode.get('default_bindings', {})
        presets = metadata.get('presets', {})
        if metadata.get('verified') is not True or set(presets) != {'mouse5', 'mouse3'} or metadata.get('source_url') != 'https://game.capcom.com/manual/MHW_PC/en/steam/page/2/2' or 'Physical' not in metadata.get('verification_scope', ''):
            raise ValueError('Missing official physical keyboard preset/source scope: ' + mode['id'])
        ranged = mode['id'] in ('bow', 'light_bowgun')
        for name, special, reload in (('mouse5', 6, 5), ('mouse3', 4, 82)):
            expected = {'attack': 1, 'secondary': special if ranged else 2, 'spirit': special,
                        'dodge': 32, 'sheathe': 69, 'aim': 2, 'reload': reload}
            if presets[name].get('keys') != expected or presets[name].get('verified') is not True:
                raise ValueError('Published physical preset differs from primary source: ' + mode['id'] + '/' + name)
        if metadata.get('keys') != presets['mouse5']['keys']:
            raise ValueError('Compatibility default must match mouse5: ' + mode['id'])


def packed_bank(entries, data_root, manifest, motions, magic, version, stride, extension):
    tracks = manifest.get('tracks')
    bank = str(safe_relative(manifest.get('bank', '')))
    if len(PurePosixPath(bank).parts) != 1 or not bank.endswith(extension) or not isinstance(tracks, dict) or not 1 <= len(tracks) <= 4096:
        raise ValueError('Invalid packed bank path or tracks: ' + data_root)
    if set(tracks) != {str(slot) for slot in motions}:
        raise ValueError('Packed bank coverage differs from body catalog: ' + data_root)
    for slot in tracks:
        if not slot.isascii() or not slot.isdigit() or str(int(slot)) != slot:
            raise ValueError('Invalid packed bank numeric slot: ' + slot)
    path = required(entries, relative_reference(data_root, bank), extension)
    if not 16 <= path.stat().st_size <= EML_ENTRY_BYTES:
        raise ValueError('Invalid packed bank size: ' + bank)
    payload = path.read_bytes()
    if struct.unpack_from('<4sIII', payload) != (magic, version, len(tracks), stride):
        raise ValueError('Packed bank header differs from manifest: ' + bank)
    return tracks, payload


def validate_packed_weapon(entries, data_root, manifest, motions):
    if manifest.get('schema_version') != 3:
        raise ValueError('Invalid packed weapon schema')
    tracks, payload = packed_bank(entries, data_root, manifest, motions, b'LSW1', 1, LSW_FRAME.size, '.lsw')
    cursor = 16
    for slot in sorted(tracks, key=int):
        track, motion = tracks[slot], motions[int(slot)]
        frames, fps, offset = track.get('frames'), track.get('fps'), track.get('offset')
        if type(frames) is not int or not 1 <= frames <= 36000 or frames != motion['frames'] or not isinstance(fps, (int, float)) or isinstance(fps, bool) or not math.isfinite(fps) or not 1 <= fps <= 240 or fps != motion['fps'] or type(offset) is not int or offset != cursor:
            raise ValueError('Packed weapon clock, frame count or offset mismatch: ' + slot)
        end = cursor + frames * LSW_FRAME.size
        if end > len(payload):
            raise ValueError('Truncated packed weapon samples: ' + slot)
        for at in range(cursor, end, LSW_FRAME.size):
            row = LSW_FRAME.unpack_from(payload, at)
            if any(not math.isfinite(value) for value in row[:36]) or any(flag not in (0, 1) for flag in row[36:]):
                raise ValueError('Non-finite packed weapon sample or invalid attachment flag: ' + slot)
            if abs(sum(value * value for value in row[:4]) - 1) > .001 or sum(value * value for value in row[4:7]) > 9 or not 0 <= row[7] <= 1:
                raise ValueError('Invalid packed weapon rotation or support sample: ' + slot)
            for start, valid in ((8, row[36]), (22, row[37])):
                if not valid:
                    continue  # Inactive source attachments legitimately contain zero quaternion placeholders.
                for begin in (start + 3, start + 10):
                    norm = sum(value * value for value in row[begin:begin + 4])
                    if not math.isfinite(norm) or norm < 1e-20:
                        raise ValueError('Packed source attachment cannot normalize: ' + slot)
                # Normalizing and rotating by the hand quaternion preserves this distance.
                if sum((row[start + 7 + axis] - row[start + axis]) ** 2 for axis in range(3)) > 9:
                    raise ValueError('Packed source attachment exceeds 3 m: ' + slot)
        cursor = end
    if cursor != len(payload):
        raise ValueError('Packed weapon bank has trailing samples or gaps')
    return len(tracks)


def validate_packed_grip(entries, data_root, manifest, motions):
    if manifest.get('schema_version') != 2:
        raise ValueError('Invalid packed grip schema')
    tracks, payload = packed_bank(entries, data_root, manifest, motions, b'LGB1', 1, 0, '.lgb')
    cursor = 16
    for slot in sorted(tracks, key=int):
        track, motion = tracks[slot], motions[int(slot)]
        offset, size = track.get('offset'), track.get('size')
        if type(offset) is not int or offset != cursor or type(size) is not int or not 16 <= size <= 4 * 1024 * 1024 or cursor + size > len(payload):
            raise ValueError('Packed grip range has gaps, overlap or truncation: ' + slot)
        magic, frames, fps, components = struct.unpack_from('<4sIII', payload, cursor)
        if magic != b'LSG1' or not 1 <= frames <= 36000 or frames != motion['frames'] or not 1 <= fps <= 240 or fps != motion['fps'] or components != 4 or size != 16 + frames * 16:
            raise ValueError('Packed original LSG dimensions disagree with body: ' + slot)
        for at in range(cursor + 16, cursor + size, 16):
            x, y, z, weight = struct.unpack_from('<4f', payload, at)
            if any(not math.isfinite(value) for value in (x, y, z, weight)) or x*x + y*y + z*z > 9 or not 0 <= weight <= 1:
                raise ValueError('Invalid packed original grip sample: ' + slot)
        cursor += size
    if cursor != len(payload):
        raise ValueError('Packed grip bank has trailing data')
    return len(tracks)


def validate_combat(document, mode, motions):
    """Reject broken combat references before distributing an otherwise valid bank."""
    if document.get('schema_version') != 1 or document.get('mode') != mode:
        raise ValueError('Invalid combat manifest: ' + mode)
    rows, authored = document.get('motions'), document.get('authored_windows')
    if not isinstance(rows, dict) or not rows or len(rows) > 4096 or not isinstance(authored, dict):
        raise ValueError('Invalid combat motion table: ' + mode)

    def number(value, low=0):
        return type(value) in (int, float) and math.isfinite(value) and value >= low

    parameters = document.get('parameters', {})
    for name in ('stamina_max', 'stamina_regen', 'charge_drain', 'guard_hit_cost',
                 'bow_shot_cost', 'bow_dodge_cost', 'gauge_auto_seconds', 'bow_chain_seconds'):
        if not number(parameters.get(name)) or parameters[name] == 0:
            raise ValueError('Invalid combat parameter: ' + name)
    for name in ('gauge_decay', 'gauge_auto_gain'):
        if not number(parameters.get(name)):
            raise ValueError('Invalid combat parameter: ' + name)
    if type(parameters.get('ammo_capacity')) is not int or not 1 <= parameters['ammo_capacity'] <= 1000:
        raise ValueError('Invalid combat ammo capacity')
    if 'bow_charge_seconds' in document:
        charge=document['bow_charge_seconds']
        if not isinstance(charge,list) or len(charge)!=4 or any(not number(v) for v in charge) or any(a>=b for a,b in zip(charge,charge[1:])):
            raise ValueError('Invalid source bow charge thresholds')
    for key, row in rows.items():
        if not key.isdecimal() or int(key) not in motions or not isinstance(row, dict):
            raise ValueError('Combat slot absent from source clips: ' + key)
        duration = motions[int(key)]['duration']
        for field in ('outgoing', 'defensive'):
            windows = row.get(field)
            if not isinstance(windows, list) or len(windows) > 128:
                raise ValueError('Invalid combat windows: ' + key)
            for window in windows:
                if not isinstance(window, list) or len(window) != 2 or not all(number(v) for v in window) or not window[0] < window[1] <= duration + 1e-4:
                    raise ValueError('Combat window exceeds source clip: ' + key)
        gains = row.get('gauge_gain_by_window')
        if not isinstance(gains, list) or len(gains) != len(row['outgoing']) or not all(number(v) for v in gains):
            raise ValueError('Combat gauge window mismatch: ' + key)
        if not number(row.get('gauge_gain')) or not number(row.get('gauge_cost')) or not number(row.get('reload_at'), -1) or row['reload_at'] > duration + 1e-4:
            raise ValueError('Invalid combat resource marker: ' + key)
        values=row.get('motion_value_by_window')
        if not isinstance(values,list) or len(values)!=len(row['outgoing']) or any(not number(v) for v in values):
            raise ValueError('Combat motion value window mismatch: '+key)
        if 'motion_value_by_level_by_window' in row:
            levels=row['motion_value_by_level_by_window']
            if not isinstance(levels,list) or len(levels)!=len(row['outgoing']) or any(not isinstance(v,list) or len(v)!=4 or any(not number(x) for x in v) for v in levels):
                raise ValueError('Combat source level motion values mismatch: '+key)
        if 'motion_value_scope_by_window' in row:
            scopes=row['motion_value_scope_by_window']
            if not isinstance(scopes,list) or len(scopes)!=len(values) or any(not isinstance(v,str) or not v for v in scopes):
                raise ValueError('Combat motion value scope mismatch: '+key)
        for name in ('counter_short_slot','counter_full_slot','completion_followup_slot','hit_followup_slot'):
            value=row.get(name,0)
            if type(value) is not int or value<0 or value and value not in motions:
                raise ValueError('Combat source followup absent: '+key+'/'+name)
        short,full=row.get('counter_short_slot',0),row.get('counter_full_slot',0)
        if bool(short)!=bool(full) or (short and row.get('lance_counter') is not True) or not number(row.get('counter_full_seconds',0)):
            raise ValueError('Invalid source counter release policy: '+key)
        if type(row.get('true_charge',False)) is not bool:
            raise ValueError('Invalid true charge role: '+key)
        if row.get('true_charge',False):
            first,last=row.get('true_charge_first_window'),row.get('true_charge_final_window')
            if type(first) is not int or type(last) is not int or first==last or not 0<=first<len(values) or not 0<=last<len(values):
                raise ValueError('Invalid true charge source windows: '+key)
        if not number(row.get('weak_finish_multiplier',1)) or row.get('weak_finish_multiplier',1)==0 or row.get('weak_finish_scope','unknown') not in ('unknown','motion_has_weakpoint_first_hit'):
            raise ValueError('Invalid true charge weak finish: '+key)
        branches=row.get('timed_inputs',[])
        if not isinstance(branches,list) or len(branches)>128:
            raise ValueError('Invalid source timed input list: '+key)
        for branch in branches:
            if not isinstance(branch,dict) or not isinstance(branch.get('command'),str) or not branch['command']:
                raise ValueError('Invalid source timed input command: '+key)
            begin,end,gate_begin,gate_end=[branch.get(name) for name in ('begin','end','input_gate_begin','input_gate_end')]
            if not all(number(v) for v in (begin,end,gate_begin,gate_end)) or not gate_begin<=begin<end<=gate_end<=duration+1e-4:
                raise ValueError('Invalid source timed input window or gate: '+key)
            for field in ('normal_slot','timed_slot'):
                target=branch.get(field)
                if type(target) is not int or target<=0 or target not in motions:
                    raise ValueError('Unknown source timed input branch: '+key+'/'+field)
        tiers=row.get('projectile_tiers',[])
        markers=row.get('projectile_markers',[])
        if not isinstance(tiers,list) or len(tiers)>64 or not isinstance(markers,list) or len(markers)>128:
            raise ValueError('Invalid source projectile table: '+key)
        if (tiers or markers) and row.get('projectile') is not True:
            raise ValueError('Source projectile table lacks projectile role: '+key)
        for tier in tiers:
            if not isinstance(tier,dict) or type(tier.get('arrows')) is not int or not 1<=tier['arrows']<=64 or type(tier.get('shell_id')) is not int or tier['shell_id']<0 or any(not number(tier.get(field)) for field in ('motion_value','speed','range','radius')):
                raise ValueError('Invalid source projectile tier: '+key)
        if len(markers)>len(values) or any(not number(v) or v>duration+1e-4 for v in markers) or any(a>=b for a,b in zip(markers,markers[1:])):
            raise ValueError('Invalid source projectile marker: '+key)
    for key, contacts in authored.items():
        if key not in rows or not isinstance(contacts, list) or len(contacts) > 128:
            raise ValueError('Unknown authored combat slot: ' + key)
        seen = set()
        for contact in contacts:
            identity = contact.get('window_id')
            if type(identity) is not int or identity in seen or not 0 <= identity < len(rows[key]['outgoing']):
                raise ValueError('Invalid authored combat identity: ' + key)
            seen.add(identity)
            if contact.get('contact_role') not in ('weapon', 'shield', 'body', 'ally_only', 'delayed_effect','constraint_attack') or any(
                    not number(contact.get(field)) or abs(contact[field] - rows[key]['outgoing'][identity][i]) > 1e-4
                    for i, field in enumerate(('begin', 'end'))):
                raise ValueError('Authored combat window mismatch: ' + key)
            if contact.get('contact_role')=='constraint_attack':
                source=contact.get('source_move',{})
                if mode!='sword_shield' or type(contact.get('bank')) is not int or contact['bank']!=3 or contact.get('native_bone_binding_known') is not False or not isinstance(source,dict) or type(source.get('Attack_Type')) is not int or source['Attack_Type']!=1 or not number(source.get('Motion_Value')) or abs(rows[key]['motion_value_by_window'][identity]-source['Motion_Value'])>1e-4:
                    raise ValueError('Invalid source SnS constraint attack: '+key)
    if mode == 'longsword':
        followup = document.get('helm_followup', {})
        if followup.get('slot') not in motions or not number(followup.get('start_seconds')) or followup['start_seconds'] >= motions[followup['slot']]['duration']:
            raise ValueError('Invalid combat Helm followup')
        delay = document.get('delayed_hit', {})
        count = delay.get('hit_limit')
        if type(count) is not int or not 1 <= count <= 32 or not number(delay.get('first_delay')) or any(
                not number(delay.get(name)) or delay[name] <= 0 for name in ('between_delay', 'last_delay')):
            raise ValueError('Invalid delayed combat timing')
        for key, triggers in delay.get('triggers', {}).items():
            if key not in rows or not rows[key].get('helm_finish') or not isinstance(triggers, list):
                raise ValueError('Invalid delayed combat source')
            for trigger in triggers:
                identity, pulses, offsets = trigger.get('window_id'), trigger.get('pulse_window_ids'), trigger.get('pulse_offsets')
                if type(identity) is not int or not 0 <= identity < len(rows[key]['outgoing']) or not isinstance(pulses, list) or len(pulses) != count or len(set(pulses)) != count or not isinstance(offsets, list) or len(offsets) != count:
                    raise ValueError('Invalid delayed combat identities')
                if any(not number(trigger.get(field)) or abs(trigger[field] - rows[key]['outgoing'][identity][i]) > 1e-4 for i, field in enumerate(('begin', 'end'))):
                    raise ValueError('Delayed trigger mismatch')
                for i, pulse in enumerate(pulses):
                    expected = delay['first_delay'] + i * delay['between_delay'] + (delay['last_delay'] - delay['between_delay'] if i + 1 == count else 0)
                    if type(pulse) is not int or pulse == identity or not 0 <= pulse < len(rows[key]['outgoing']) or not number(offsets[i]) or abs(offsets[i] - expected) > 1e-4 or abs(rows[key]['outgoing'][pulse][0] - (trigger['begin'] + expected)) > 1e-4:
                        raise ValueError('Delayed pulse mismatch')
                    if 'motion_value_by_level_by_window' not in rows[key]:
                        raise ValueError('Delayed source pulse lacks gauge-level motion values')


def validate_mode(root, entries, entry):
    mode, data_root, audio_root = entry['id'], str(safe_relative(entry['data_root'])), str(safe_relative(entry['audio_root']))
    motions = read(required(entries, data_root + '/catalog.json', '.json'))
    if not isinstance(motions, list) or not motions:
        raise ValueError('No source clips for ' + mode)
    by_slot = {}
    for motion in motions:
        slot = motion['slot']
        if slot in by_slot or not isinstance(slot, int):
            raise ValueError(f'Duplicate/invalid motion slot in {mode}')
        path = required(entries, relative_reference(data_root, motion['file']), '.lsm')
        frames, fps = validate_binary(path, b'LSM1', 21)
        duration = (frames - 1) / fps
        if motion.get('frames') != frames or abs(motion.get('duration', duration) - duration) > 1e-5:
            raise ValueError(f'Catalog dimensions disagree with binary in {mode}/{slot}')
        if not motion.get('source') or not isinstance(motion.get('source_slot', slot), int):
            raise ValueError(f'Missing source provenance in {mode}/{slot}')
        by_slot[slot] = {**motion, 'duration': duration, 'fps': fps}
    validate_combat(read(required(entries, data_root + '/combat_rules.json', '.json')), mode, by_slot)
    for filename in ('source_rig.json', 'ground_tracks.json', 'grip_tracks.json', 'source_weapon_tracks.json'):
        required(entries, data_root + '/' + filename, '.json')
    moves = read(required(entries, data_root + '/moves.json', '.json'))
    if moves.get('idle') not in by_slot:
        raise ValueError('Idle absent from ' + mode)
    for command, slots in moves.get('commands', {}).items():
        if not slots or any(slot not in by_slot for slot in slots):
            raise ValueError(f'Command has missing playable source in {mode}/{command}')
    commands = moves.get('commands', {})
    for binding, command in {**moves.get('inputs', {}), **moves.get('chords', {})}.items():
        if command not in commands:
            raise ValueError(f'Input {binding} names absent command in {mode}: {command}')
    validate_controls(moves, by_slot, mode)
    for command in entry.get('demo', []):
        if command not in moves.get('commands', {}):
            raise ValueError(f'Demo command absent from {mode}: {command}')
    validate_timeline(read(entries[data_root + '/ground_tracks.json']), by_slot, mode, 'clearance')
    weapons = read(entries[data_root + '/source_weapon_tracks.json'])
    if weapons.get('schema_version') == 3:
        validate_packed_weapon(entries, data_root, weapons, by_slot)
    else:
        validate_timeline(weapons, by_slot, mode, 'frames')
    rig = read(entries[data_root + '/source_rig.json'])
    if len(rig.get('order', [])) != 21 or set(rig.get('order', [])) != set(rig.get('bones', {})):
        raise ValueError('Invalid source main-bone manifest: ' + mode)
    grip = read(entries[data_root + '/grip_tracks.json'])
    if grip.get('schema_version') == 2:
        validate_packed_grip(entries, data_root, grip, by_slot)
    else:
        for slot, filename in grip.get('tracks', {}).items():
            path = required(entries, relative_reference(data_root, filename), '.lsg')
            frames, fps = validate_binary(path, b'LSG1')
            if int(slot) not in by_slot or frames != by_slot[int(slot)]['frames'] or fps != by_slot[int(slot)]['fps']:
                raise ValueError(f'Grip length/source mismatch in {mode}/{slot}')
    finger_root = str(safe_relative(entry.get('finger_root', data_root)))
    finger_name = finger_root + '/finger_tracks.json'
    if finger_name in entries:
        fingers = read(entries[finger_name])
        for slot, track in fingers.get('tracks', {}).items():
            filename = track['file'] if isinstance(track, dict) else track
            path = required(entries, relative_reference(finger_root, filename), '.lsf')
            frames, fps = validate_binary(path, b'LSF1', len(fingers.get('bones', [])))
            if int(slot) not in by_slot or frames != by_slot[int(slot)]['frames'] or fps != by_slot[int(slot)]['fps']:
                raise ValueError(f'Finger length/source mismatch in {mode}/{slot}')
    skin_name = data_root + '/source_skin_tracks.json'
    if skin_name in entries:
        skin = read(entries[skin_name])
        if skin.get('schema_version') != 1 or not isinstance(skin.get('props'), dict) or not isinstance(skin.get('tracks'), dict):
            raise ValueError('Invalid source own-skin manifest: ' + mode)
        for identifier, prop in skin['props'].items():
            required(entries, relative_reference(data_root, prop['file']), '.json')
            bones = len(prop.get('bone_names', []))
            if not 1 <= bones <= 1000 or len(prop.get('bindposes', [])) != bones:
                raise ValueError('Invalid original weapon own-skin palette: ' + identifier)
            if any(len(matrix) != 16 or any(not math.isfinite(value) for value in matrix) for matrix in prop['bindposes']):
                raise ValueError('Invalid original source weapon bind matrices: ' + identifier)
            if len(prop.get('skin_bone_indices', [])) != len(prop.get('skin_weights', [])):
                raise ValueError('Source weapon skin index/weight counts disagree: ' + identifier)
        for slot, track in skin['tracks'].items():
            if int(slot) not in by_slot or track.get('prop_id') not in skin['props']:
                raise ValueError(f'Source skin references missing published clip or prop: {mode}/{slot}')
            bones = len(skin['props'][track['prop_id']]['bone_names'])
            path = required(entries, relative_reference(data_root, track['file']), '.lsk')
            frames, fps = validate_binary(path, b'LSK1', bones)
            motion = by_slot[int(slot)]
            if frames != motion['frames'] or fps != motion['fps'] or track.get('bone_count') != bones:
                raise ValueError(f'Source weapon skin dimensions differ: {mode}/{slot}')
    props_name = data_root + '/props/index.json'
    if props_name not in entries:
        raise FileNotFoundError(f'No original source prop index in {mode}')
    prop_index = read(entries[props_name])
    for prop in prop_index.get('props', []):
        path = required(entries, relative_reference(data_root, prop['file']), '.json')
        description = read(path)
        if len(description.get('vertices', [])) < 3 or not description.get('indices'):
            raise ValueError('Empty original source prop: ' + prop['id'])
        for material in description.get('materials', []):
            for texture in material.get('runtime_textures', {}).values():
                raw_name = relative_reference(data_root, texture['file'])
                raw = required(entries, raw_name, '.rgba')
                width, height = texture.get('width', 0), texture.get('height', 0)
                if not 1 <= width <= 2048 or not 1 <= height <= 2048 or raw.stat().st_size != width * height * 4:
                    raise ValueError('Texture dimensions/bytes mismatch: ' + raw_name)
                if texture.get('png'):
                    required(entries, relative_reference(data_root, texture['png']), '.png')
    return len(by_slot), validate_audio(root, entries, mode, audio_root, by_slot)


def unique_records(rows, key, label):
    """Version updates may grow a roster, but may not hide omissions or duplicates."""
    if not isinstance(rows, list) or not rows:
        raise ValueError(label + ' must be a nonempty list')
    result = {}
    for row in rows:
        identifier = row.get(key) if isinstance(row, dict) else None
        if not isinstance(identifier, str) or not identifier.strip() or identifier in result:
            raise ValueError(label + ' has an invalid or repeated identity')
        result[identifier] = row
    return result


def validate_rosters(character_index, weapon_index, equipment_index, product=None):
    characters = unique_records(character_index.get('characters'), 'character_id', 'Character registry')
    weapons = unique_records(weapon_index.get('weapons'), 'id', 'Weapon registry')
    equipment = unique_records(equipment_index.get('equipment'), 'weapon_id', 'Equipment registry')
    special = {'laevatain_blade', 'typhoea_bow'}
    if special.intersection(equipment) or set(weapons) != set(equipment) | special:
        raise ValueError('Weapon registry must equal current equipment plus both priority visual weapons')
    source = character_index.get('current_source')
    if not isinstance(source, dict) or not isinstance(source.get('game_version'), str) or not source['game_version'].strip() or source.get('overlay_order') != ['StreamingAssets', 'Persistent']:
        raise ValueError('Current source must identify the game version and effective VFS overlay')
    for field, records in (('character_ids', characters), ('equipment_ids', equipment)):
        identifiers = source.get(field)
        if (not isinstance(identifiers, list) or
                any(not isinstance(value, str) or not value for value in identifiers) or
                len(set(identifiers)) != len(identifiers) or set(identifiers) != set(records)):
            raise ValueError('Profile registry differs from current game source roster: ' + field)
    if product is not None:
        catalog_characters = unique_records(product.get('characters'), 'id', 'Product character catalog')
        catalog_weapons = unique_records(product.get('weapons'), 'id', 'Product weapon catalog')
        if set(catalog_characters) != set(characters) or set(catalog_weapons) != set(weapons):
            raise ValueError('Product catalog differs from current character/weapon profile registries')
        for identifier, row in catalog_weapons.items():
            if row.get('model_profile') != relative_reference('profiles/weapons', weapons[identifier]['profile']):
                raise ValueError('Product weapon profile differs from registry: ' + identifier)
    return characters, weapons, equipment


def validate_profile_source(profile, game_version, character=False):
    evidence = profile.get('evidence', {}) if character else profile
    version_field = 'game_version' if character else 'source_game_version'
    if (not isinstance(evidence, dict) or evidence.get('current_vfs_verified') is not True or
            evidence.get(version_field) != game_version):
        raise ValueError('Profile lacks current-version effective VFS evidence')


def validate_product(entries, manifest):
    product = read(required(entries, 'assets/product_catalog.json', '.json'))
    audio = read(required(entries, 'assets/audio_catalog.json', '.json'))
    if set(unique_records(product.get('modes'), 'id', 'Product modes')) != MODES:
        raise ValueError('Product catalog must cover the current six modes')
    character_registry, _, _ = validate_rosters(read(required(entries, 'profiles/index.json', '.json')),
                     read(required(entries, 'profiles/weapons/index.json', '.json')),
                     read(required(entries, 'profiles/weapons/equipment-index.json', '.json')), product)
    if product.get('version') != manifest['version']:
        raise ValueError('Regenerate product metadata after changing module version')
    required(entries, relative_reference('assets', product['preview']['file']), '.json')
    provenance = read(required(entries, 'assets/build_provenance.json', '.json'))
    if provenance.get('version') != manifest['version'] or {mode['id'] for mode in provenance.get('modes', [])} != MODES:
        raise ValueError('Build provenance must match this version and all six modes')
    current_source = read(entries['profiles/index.json'])['current_source']
    if provenance.get('current_endfield_source') != current_source or provenance.get('characters_pending_current_vfs_validation') != []:
        raise ValueError('Build provenance has stale or unverified current game sources')
    source_tours = product.get('source_tours', {})
    tour_index = read(required(entries, relative_reference('assets', source_tours.get('file', '')), '.json'))
    tours = source_tours.get('modes', [])
    if tour_index.get('schema_version') != 1 or tour_index.get('modes') != tours or len(tours) != 6 or {tour.get('id') for tour in tours} != MODES:
        raise ValueError('Six original hunter/weapon source A/V references are required')
    for tour in tours:
        video = required(entries, relative_reference('assets', tour['file']), '.mp4')
        if not tour['file'].startswith('previews/source-tours/') or video.stat().st_size < 1024:
            raise ValueError('Invalid original source reference path/size')
        with video.open('rb') as stream:
            header = stream.read(32)
        if header[4:8] != b'ftyp' or tour.get('game_launched') is not False or tour.get('visual_quality_certified') is not False:
            raise ValueError('Source reference is invalid or falsely labelled game-verified')
        if not isinstance(tour.get('seconds'), (int, float)) or not math.isfinite(tour['seconds']) or tour['seconds'] <= 0:
            raise ValueError('Invalid original source reference duration')
        if tour.get('fps') != 10 or tour.get('source_mesh_frames', 0) <= 0 or tour.get('distinct_pcm', 0) <= 0 or not tour.get('evidence') or not tour.get('limitations'):
            raise ValueError('Incomplete original source reference provenance')
    if {name for name in entries if name.endswith('.mp4')} != {relative_reference('assets', tour['file']) for tour in tours}:
        raise ValueError('Video package contents must equal the six declared source references')
    character_profiles = {}
    for character in product['characters']:
        profile = read(required(entries, relative_reference('profiles', character_registry[character['id']]['profile']), '.json'))
        character_profiles[character['id']] = profile
        if set(character.get('modes', {})) != MODES:
            raise ValueError('Incomplete character coverage metadata: ' + character['id'])
        for status in character['modes'].values():
            validate_catalog_status(status, profile, character.get('audit_profile_inputs'))
            if not status.get('reference_images'):
                raise ValueError('Missing actual target mesh reference for ' + character['id'])
            for image in status.get('reference_images', []):
                required(entries, relative_reference('assets', image['file']), '.webp')
    for weapon in product.get('weapons', []):
        profile = read(required(entries, weapon['model_profile'], '.json'))
        if weapon['id'] in _audit_contract.SPECIAL:
            validate_special_catalog(weapon, profile, character_profiles)
        if weapon['id'] in ('laevatain_blade', 'typhoea_bow') and weapon['modes'] != (['longsword'] if weapon['id'] == 'laevatain_blade' else ['bow']):
            raise ValueError('Special model mode metadata differs from native adapters')
    for mode in MODES:
        if mode not in audio.get('modes', {}) or not audio['modes'][mode].get('clips'):
            raise ValueError('Missing mode sound catalog: ' + mode)
        for clip in audio['modes'][mode]['clips']:
            required(entries, relative_reference('assets', clip['file']), '.wav')
    return product, audio


def package(root: Path, package_root: Path, output: Path | None, validate_only=False) -> Path | None:
    root = root.resolve()
    validate_current_mesh_audits(root)
    manifest_path = root / 'module.json'
    manifest = read(manifest_path)
    if manifest.get('format') != 1 or manifest.get('abi') != 1 or len(manifest.get('libraries', {})) != 1:
        raise ValueError('Expected EML format 1 / ABI 1 and one Windows x64 library')
    library = safe_relative(manifest['libraries']['windows-x64'])
    safe_relative(manifest['ui'])
    dll = package_root.joinpath(*library.parts)
    if not dll.is_file():
        raise FileNotFoundError(f'Build the native module first: {dll}')
    validate_native_identity(dll.read_bytes(), manifest['id'], manifest['version'])
    entries: dict[str, Path | bytes] = {'module.json': manifest_path, str(library): dll}
    # The root assets/audio tree contains legacy 0.x research copies. Current
    # readers use the six mode roots plus the independent special-model PCM.
    current_product = read(root / 'assets/product_catalog.json')
    root_asset_names = {'assets/product_catalog.json', 'assets/audio_catalog.json',
                        'assets/build_provenance.json',
                        relative_reference('assets', current_product['preview']['file'])}
    themes = read(root / 'assets/audio/theme_cues.json')
    theme_asset_names = {'assets/audio/theme_cues.json',
                         *(relative_reference('assets/audio', clip['file']) for clip in themes['clips'])}
    for name in ('README.md', 'THIRD_PARTY_NOTICES.md', 'LICENSE'):
        source = root / name
        if source.is_file():
            entries[name] = source
        elif name != 'LICENSE':
            raise FileNotFoundError(source)
    for directory in ('ui', 'assets', 'profiles', 'docs'):
        source_directory = root / directory
        if not source_directory.is_dir():
            raise FileNotFoundError(source_directory)
        for source in sorted(source_directory.rglob('*')):
            if source.is_symlink():
                raise ValueError(f'Refusing linked resource: {source}')
            if not source.is_file():
                continue
            if directory == 'assets' and source.suffix.lower() not in ALLOWED_ASSET_EXTENSIONS and not (
                    source.suffix.lower() == '.mp4' and source.parent == root / 'assets/previews/source-tours'):
                continue
            if directory == 'profiles' and source.suffix.lower() != '.json':
                continue
            if directory == 'docs' and source.suffix.lower() != '.md':
                continue
            if directory == 'ui' and source.suffix.lower() not in {'.html', '.css', '.js', '.svg', '.png', '.webp'}:
                continue
            name = source.relative_to(root).as_posix()
            safe_relative(name)
            if directory == 'assets' and not (
                    name in root_asset_names or name in theme_asset_names or
                    name.startswith(('assets/modes/', 'assets/ui_reference/', 'assets/previews/'))):
                continue
            entries[name] = source
    spec = importlib.util.spec_from_file_location('mhe_release_banks', Path(__file__).with_name('mhw_package_banks.py'))
    converter = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(converter)
    bank_report = converter.prepare(root, package_root)
    for mode in read(root / 'assets/modes/index.json')['modes']:
        data_root = str(safe_relative(mode['data_root']))
        source_grips = read(root / data_root / 'grip_tracks.json')
        for filename in source_grips['tracks'].values():
            entries.pop(relative_reference(data_root, filename), None)
    for name in bank_report['overlays']:
        relative = safe_relative(name)
        entries[str(relative)] = package_root.joinpath(*relative.parts)
    # Describe the actual release representation while preserving editable
    # source provenance and all bank coverage counts.
    release_provenance = read(root / 'assets/build_provenance.json')
    if 'tools/mhw_package_banks.py' not in release_provenance.get('tools', []):
        release_provenance.setdefault('tools', []).append('tools/mhw_package_banks.py')
    release_provenance['release_storage'] = {
        'mount_schema': 3, 'mount_bank_format': 'LSW1', 'mount_tracks': bank_report['mount_tracks'],
        'mount_frames': bank_report['mount_frames'], 'mount_float32_native_inputs_exact': True,
        'grip_schema': 2, 'grip_bank_format': 'LGB1', 'grip_tracks': bank_report['grip_tracks'],
        'complete_original_lsg_bytes_exact': True, 'source_assets_unchanged': True,
        'body_finger_own_skin_tracks_omitted': False, 'json_numbers_rounded': False,
        'hashes_computed': False, 'runtime_verified': False}
    provenance_overlay = package_root / 'assets/build_provenance.json'
    provenance_overlay.write_text(json.dumps(release_provenance, ensure_ascii=False, separators=(',', ':')),
                                  encoding='utf-8')
    entries['assets/build_provenance.json'] = provenance_overlay
    (package_root.parent / 'runtime-bank-report.json').write_text(
        json.dumps(bank_report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    required(entries, manifest['ui'], '.html')
    character_index = read(required(entries, 'profiles/index.json', '.json'))
    characters = character_index.get('characters', [])
    weapon_index = read(required(entries, 'profiles/weapons/index.json', '.json'))
    equipment_index = read(required(entries, 'profiles/weapons/equipment-index.json', '.json'))
    validate_rosters(character_index, weapon_index, equipment_index)
    game_version = character_index['current_source']['game_version']
    for character in characters:
        path = required(entries, relative_reference('profiles', character['profile']), '.json')
        profile = read(path)
        validate_profile_source(profile, game_version, character=True)
        if profile.get('character_id') != character['character_id'] or set(profile.get('supported_modes', [])) != MODES:
            raise ValueError('Character identity/mode coverage invalid: ' + character['character_id'])
        if len(profile.get('finger_bones', {})) != 30 or not profile.get('ground_foot_probes'):
            raise ValueError('Missing actual finger/sole profile: ' + character['character_id'])
    weapon_ids = set()
    for weapon in weapon_index.get('weapons', []):
        if weapon['id'] in weapon_ids:
            raise ValueError('Duplicate weapon profile ' + weapon['id'])
        weapon_ids.add(weapon['id'])
        validate_profile_source(read(required(entries, relative_reference('profiles/weapons', weapon['profile']), '.json')), game_version)
    registry = read(required(entries, 'assets/modes/index.json', '.json'))
    modes = registry.get('modes', [])
    if len(modes) != 6 or {mode['id'] for mode in modes} != MODES:
        raise ValueError('Six unique playable modes are required')
    validate_keyboard_presets(modes)
    counts = {entry['id']: validate_mode(root, entries, entry) for entry in modes}
    longsword_root = next(entry['data_root'] for entry in modes if entry['id'] == 'longsword')
    longsword_motions = read(required(entries, longsword_root + '/catalog.json', '.json'))
    if isinstance(longsword_motions, dict):
        longsword_motions = longsword_motions.get('motions', longsword_motions.get('clips', []))
    contact_motions = {}
    for motion in longsword_motions:
        path = required(entries, relative_reference(longsword_root, motion['file']), '.lsm')
        with path.open('rb') as stream:
            _, frames, _, fps = struct.unpack('<4sIII', stream.read(16))
        contact_motions[motion['slot']] = {'frames': frames, 'fps': fps}
    validate_contact_masks(entries, contact_motions, {character['character_id'] for character in characters})
    validate_hand_support(entries)
    theme_sounds = validate_theme_audio(entries)
    provenance = read(required(entries, 'assets/build_provenance.json', '.json'))
    if provenance.get('special_model_audio', {}).get('clips') != theme_sounds:
        raise ValueError('Regenerate special model audio build provenance')
    product, audio = validate_product(entries, manifest)
    preview = read(root / 'assets' / product['preview']['file'])
    if preview.get('missing_source_previews') or not preview.get('clips'):
        raise ValueError('Incomplete evaluated source preview catalog')
    # The tracked source file contains globals only; generated data is local to
    # this ZIP. Embedding also keeps model metadata/curves usable from file://.
    def encoded(value):
        return json.dumps(value, ensure_ascii=False, separators=(',', ':')).replace('\u2028', '\\u2028').replace('\u2029', '\\u2029')
    entries['ui/preview-data.js'] = ('window.LONGSWORD_PREVIEW=' + encoded(preview) + ';\n' +
        'window.MHE_PRODUCT_CATALOG=' + encoded(product) + ';\n' +
        'window.MHE_AUDIO_CATALOG=' + encoded(audio) + ';\n').encode('utf-8')
    payloads = final_payloads(entries)
    total = validate_loader_limits({name: len(payload) for name, payload in payloads.items()})
    if validate_only:
        print(f'Validated {len(payloads)} final files / {total:,} uncompressed bytes within current EML limits; '
              f'{sum(item[0] for item in counts.values())} motion clips; '
              f'{sum(item[1] for item in counts.values())} mode sound clips + {theme_sounds} special model sounds; {len(preview["clips"])} source previews')
        return None
    output = output or root / 'dist' / f'{manifest["id"]}-{manifest["version"]}-win-x64.zip'
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name + '.building')
    with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, payload in sorted(payloads.items()):
            archive.writestr(name, payload)
    if temporary.stat().st_size > EML_TOTAL_BYTES:
        temporary.unlink()
        raise ValueError('EML ZIP archive exceeds 256 MiB')
    temporary.replace(output)
    print(f'Packaged {len(payloads)} files / {total:,} uncompressed bytes; {sum(item[0] for item in counts.values())} motion clips; '
          f'{sum(item[1] for item in counts.values())} mode sound clips + {theme_sounds} special model sounds -> {output} ({output.stat().st_size:,} bytes)')
    return output


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=PROJECT)
    parser.add_argument('--package-root', type=Path)
    parser.add_argument('--output', type=Path)
    parser.add_argument('--validate-only', action='store_true', help='Check the full package dependency closure without writing a ZIP')
    args = parser.parse_args()
    root = args.root.resolve()
    package(root, (args.package_root or root / 'build/package').resolve(), args.output, args.validate_only)


if __name__ == '__main__':
    main()
