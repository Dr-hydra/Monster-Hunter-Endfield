"""Build distributable UI metadata from the same local assets the native host reads.

No game is launched. This never converts rig availability into mesh or runtime proof.
Raw models, audio and source files remain in place; output is package-relative metadata.
"""
from __future__ import annotations
import argparse
import json
import os
import math
from pathlib import Path
from collections import defaultdict
from PIL import Image

MODE_ORDER = ['longsword', 'sword_shield', 'greatsword', 'lance', 'light_bowgun', 'bow']
NAMES = {'longsword': ('太刀', 'LONG SWORD', '／', '单手剑'),
         'sword_shield': ('片手剑', 'SWORD & SHIELD', '＋', '单手剑'),
         'greatsword': ('大剑', 'GREAT SWORD', '╱', '双手剑'),
         'lance': ('长枪', 'LANCE', '↗', '长柄'),
         'light_bowgun': ('轻弩', 'LIGHT BOWGUN', '═', '手铳'),
         'bow': ('弓', 'BOW', '）', '施术单元')}
BASE_INPUT = {'slash': 'attack', 'attack': 'attack', 'thrust': 'secondary',
              'secondary': 'secondary', 'spirit': 'spirit', 'dodge': 'dodge',
              'sheathe': 'sheathe', 'aim': 'aim', 'reload': 'reload'}
BIT_INPUT = {1: 'attack', 2: 'secondary', 4: 'spirit', 8: 'dodge', 16: 'sheathe',
             32: 'aim', 64: 'reload'}
KEY_DEFAULTS = {'attack': 1, 'secondary': 2, 'spirit': 6, 'dodge': 32,
                'sheathe': 69, 'aim': 2, 'reload': 5}
LEGACY_INPUTS = {'slash': 'slash', 'thrust': 'thrust', 'spirit': 'spirit',
                 'dodge': 'dodge', 'sheathe': 'normal_sheathe'}
LEGACY_CHORDS = {'3': 'fade', '5': 'helm', '6': 'foresight', '12': 'sheathe'}
LEGACY_LABELS = {'slash': '普通斩', 'thrust': '突刺', 'spirit': '气刃连段',
                 'fade': '袈裟斩', 'foresight': '见切斩', 'helm': '气刃突刺 / 登龙',
                 'sheathe': '特殊纳刀', 'iai': '居合拔刀斩', 'iai_spirit': '居合气刃斩',
                 'dodge': '翻滚', 'normal_sheathe': '收刀'}
INPUT_ACTION_LABELS = {'attack': '普通攻击键', 'secondary': '副攻击键', 'spirit': '独有动作键',
                       'dodge': '回避键', 'sheathe': '纳刀键', 'aim': '瞄准键', 'reload': '装填／装瓶键'}


def read(path: Path, default=None):
    if not path.is_file():
        return default
    with path.open(encoding='utf-8-sig') as stream:
        return json.load(stream)


def write_atomic(path: Path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name + '.building')
    temporary.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    os.replace(temporary, path)


def within(root: Path, value: str) -> Path:
    """Reject references outside the package, including Windows absolute paths."""
    relative = Path(value)
    if relative.is_absolute() or ':' in value or '\\' in value:
        raise ValueError(f'Package-relative path required: {value}')
    result = (root / relative).resolve()
    result.relative_to(root.resolve())
    return result


def binding_metadata(raw):
    keys = dict(KEY_DEFAULTS)
    raw = raw or {}
    entries = raw.get('keys', raw)
    for source, value in entries.items():
        target = BASE_INPUT.get(source)
        if target and isinstance(value, int) and 0 < value < 255 and value != 27:
            keys[target] = value
    result = {'keys': keys, 'source': raw.get('source', 'Current module mapping; game defaults not independently verified'),
              'verified': raw.get('verified') is True}
    for field in ('source_url', 'preset_source_url', 'verification_scope', 'label', 'fixed_sheathe_auxiliary'):
        if field in raw:
            result[field] = raw[field]
    if isinstance(raw.get('presets'), dict):
        result['presets'] = {name: {**preset, 'keys': {**KEY_DEFAULTS, **preset.get('keys', {})}}
                             for name, preset in raw['presets'].items() if name in ('mouse5', 'mouse3')}
    return result


def contextual_control_details(identifier, command, slots, moves):
    """Describe shipped policies without presenting module timing as source timing."""
    notes = []
    for hold_command, policy in moves.get('hold_commands', {}).items():
        policies = [policy, *moves.get('hold_variants', {}).get(hold_command, [])]
        for current in policies:
            releases = current.get('releases', [])
            if any(level.get('slot') in slots for level in releases):
                key = next((BASE_INPUT.get(name) for name, mapped in moves.get('inputs', {}).items()
                            if mapped == hold_command and BASE_INPUT.get(name)), None)
                if key:
                    notes.append(f'松开{INPUT_ACTION_LABELS[key]}进入释放动作')
                else:
                    notes.append('自动演示释放对应保持段时进入')
                if len(releases) > 1:
                    seconds = '／'.join(f'{level.get("seconds", 0):g}' for level in releases)
                    notes.append(f'持键 {seconds} 秒选择对应等级；这是模组展示阈值')
                break
        if command == hold_command and moves.get('hold_variants', {}).get(command):
            names = [moves.get('labels', {}).get(str(variant['slot']), str(variant['slot']))
                     for variant in moves['hold_variants'][command]]
            notes.append('根据上一次释放招式，在下一连段窗口切入' + '／'.join(names))
    routes = moves.get('hardware_routes', {})
    for mask in sorted({int(mask) for mapping in routes.values() for mask, destination in mapping.items()
                        if destination == command}):
        actions = '＋'.join(INPUT_ACTION_LABELS[key] for bit, key in BIT_INPUT.items() if mask & bit)
        starters = [int(slot) for slot, mapping in routes.items()
                    if mapping.get(str(mask)) == command and int(slot) not in slots]
        continuations = any(mapping.get(str(mask)) == command and int(slot) in slots
                            for slot, mapping in routes.items())
        if starters:
            names = list(dict.fromkeys(moves.get('labels', {}).get(str(slot), str(slot)) for slot in starters))
            notes.append('在「' + '／'.join(names) + f'」的派生窗口按{actions}进入')
        if continuations:
            notes.append(f'连段中继续按{actions}推进，末段结束后不循环续接')
    if identifier == 'longsword':
        if command in ('foresight', 'sheathe'):
            notes.append('先攻击，再在派生窗口使用组合键；静止起手不触发')
        elif command in ('iai', 'iai_spirit'):
            key = '普通攻击键' if command == 'iai' else '独有动作键'
            notes.append(f'特殊纳刀保持时按{key}进入')
    elif identifier == 'bow' and command == 'special':
        notes.append('组合键是本模组展示快捷键；原作键盘组合尚未认证')
    return notes


def mode_product(root, entry):
    identifier = entry['id']
    if identifier not in NAMES:
        raise ValueError(f'Unknown mode {identifier}')
    label, english, icon, category = NAMES[identifier]
    data = within(root, entry.get('data_root', 'assets'))
    moves = read(data / 'moves.json', {})
    clips = read(data / 'catalog.json', [])
    if isinstance(clips, dict):
        clips = clips.get('clips', [])
    available = {clip['slot']: clip for clip in clips
                 if isinstance(clip, dict) and isinstance(clip.get('slot'), int)}
    inputs = moves.get('inputs', LEGACY_INPUTS if identifier == 'longsword' else {})
    chords = moves.get('chords', LEGACY_CHORDS if identifier == 'longsword' else {})
    command_keys = defaultdict(list)
    key_actions = {}
    for key, command in inputs.items():
        base = BASE_INPUT.get(key)
        if identifier in ('bow', 'light_bowgun') and base == 'spirit':
            continue  # The ranged native input path does not sample SpiritBit.
        if base:
            command_keys[command].append(base)
            key_actions[base] = command
    # Native ReadMode extends the original five input bits with independent Aim
    # and Reload. Explicit keys take priority over secondary / sheathe aliases.
    if identifier in ('bow', 'light_bowgun'):
        if 'aim' in moves.get('commands', {}):
            command_keys['aim'].insert(0, 'aim')
            key_actions['aim'] = 'aim'
        if 'reload' in moves.get('commands', {}):
            command_keys['reload'].insert(0, 'reload')
            key_actions['reload'] = 'reload'
    for mask, command in chords.items():
        bits = int(mask)
        if identifier in ('bow', 'light_bowgun') and bits & 4:
            continue
        binding = '+'.join(key for bit, key in BIT_INPUT.items() if bits & bit)
        if binding:
            command_keys[command].append(binding)
    for input_name, behavior in moves.get('input_behavior', {}).items():
        for kind in ('tap', 'hold'):
            command = behavior.get(kind)
            if command and input_name in KEY_DEFAULTS and input_name not in command_keys[command]:
                command_keys[command].insert(0, input_name)
    output_moves = []
    missing_slots = []
    for command, slots in moves.get('commands', {}).items():
        if not isinstance(slots, list) or not slots:
            continue
        first = slots[0]
        missing = [slot for slot in slots if slot not in available or
                   not (data / str(available[slot].get('file', ''))).is_file()]
        missing_slots.extend(missing)
        labels = [moves.get('labels', {}).get(str(slot)) for slot in slots]
        title = next((name for name in labels if name), LEGACY_LABELS.get(command, command))
        details = []
        if len(slots) > 1:
            policies = [policy for name, policy in moves.get('hold_commands', {}).items()]
            policies += [variant for variants in moves.get('hold_variants', {}).values() for variant in variants]
            is_release_levels = any(len(policy.get('releases', [])) > 1 and
                                    all(slot in slots for slot in (level.get('slot') for level in policy['releases']))
                                    for policy in policies)
            if is_release_levels:
                details.append(f'{len(slots)} 个原版蓄力等级动作；每次择一播放')
            elif command in moves.get('hold_variants', {}):
                details.append(f'{len(slots)} 个原版保持姿势；按前一招选择阶段')
            else:
                details.append(f'{len(slots)} 段原版动作')
        if command in moves.get('hold_commands', {}):
            details.append('保持 / 释放，时间由本地状态机控制')
        if command in moves.get('experimental_commands', []):
            details.append('实验组合；源片段与展示控制分别记录')
        for behavior in moves.get('input_behavior', {}).values():
            if behavior.get('hold') == command:
                details.append(f'长按 ≥{behavior.get("hold_seconds", 0):g} 秒；模组输入阈值')
                details.append('达到阈值后等待当前动作的派生窗口，松键结束保持')
            elif behavior.get('tap') == command:
                details.append('短按触发；长按使用对应连续动作')
        contextual = contextual_control_details(identifier, command, slots, moves)
        details.extend(contextual)
        if not command_keys[command] and not contextual:
            details.append('通过派生或网页试演进入；未配置直接键')
        elif not command_keys[command]:
            details.append('上述条件下进入，也可网页试演；未配置独立直接键')
        output_moves.append({'name': command, 'label': title,
                             'key': command_keys[command][0] if command_keys[command] else '',
                             'bindings': command_keys[command], 'slots': slots, 'slot': first,
                             'status': 'missing' if missing else 'available',
                             'detail': ' · '.join(details),
                             'source': moves.get('source', entry.get('source', ''))})
    by_command = {move['name']: move['label'] for move in output_moves}
    return {'id': identifier, 'label': entry.get('label', label), 'english': english, 'icon': icon,
            'category': category, 'description': ' / '.join(by_command.get(c, c) for c in entry.get('demo', [])[:3]),
            'status': 'missing' if missing_slots or not clips else 'available',
            'detail': entry.get('detail', moves.get('limitation', '')),
            'moves': output_moves, 'clip_count': len(clips), 'missing_slots': sorted(set(missing_slots)),
            'source': entry.get('source', moves.get('source', '')),
            'key_actions': {key: by_command.get(command, command) for key, command in key_actions.items()},
            'input_keys': list(key_actions), 'default_bindings': binding_metadata(entry.get('default_bindings')),
            'input_behavior': moves.get('input_behavior', {}),
            'conditional_control_evidence': moves.get('conditional_control_evidence', {}),
            'control_verification': moves.get('control_verification', {}),
            'unresolved_control_actions': moves.get('unresolved_control_actions', []),
            'demo': entry.get('demo', moves.get('demo', [])), 'runtime_status': 'pending'}


def audio_product(root, entry, product_mode):
    identifier = entry['id']
    directory = within(root, entry.get('audio_root', 'assets/audio'))
    source = read(directory / 'cues.json', {})
    by_clip = defaultdict(list)
    cues = source.get('cues', {})
    for slot, items in (cues.items() if isinstance(cues, dict) else []):
        for cue in items:
            for clip in set([cue.get('clip', '')] + cue.get('variants', [])):
                if clip:
                    by_clip[clip].append({**cue, 'slot': int(slot)})
    output = []
    absent = []
    move_labels = defaultdict(list)
    for move in product_mode.get('moves', []):
        for slot in move.get('slots', []):
            if move['label'] not in move_labels[slot]:
                move_labels[slot].append(move['label'])
    for clip in source.get('clips', []):
        path = within(root, (directory / clip['file']).relative_to(root).as_posix())
        relative = path.relative_to(root / 'assets').as_posix()
        uses = by_clip[clip['id']]
        cue = uses[0] if uses else {}
        available = path.is_file() and path.stat().st_size > 44
        if not available:
            absent.append(clip['id'])
        condition = cue.get('condition', '')
        group = cue.get('group', 'weapon')
        category = 'charge' if cue.get('loop') else 'weapon' if condition in ('', 'always') else 'impact'
        pcm_source = Path(clip.get('source', source.get('source', ''))).name
        confidence = cue.get('confidence', 'unmapped_source_clip')
        used_by = list(dict.fromkeys(label for use in uses for label in move_labels[use['slot']]))
        named = clip.get('label') or (' / '.join(used_by[:2]) + ' · 使用音源' if used_by else clip['id'])
        output.append({'id': clip['id'], 'file': relative, 'label': named,
                       'source': f'MHW · {pcm_source}' if 'endfield' not in clip['id'].lower() else f'Endfield · {pcm_source}',
                       'confidence': '原事件与源时序对应；用途以 Req 条件为准' if confidence == 'source_timing_and_event_descendant' else '源片段 / 用途待核对',
                       'confidence_id': confidence, 'gain': float(cue.get('gain', 1)),
                       'group': group, 'category': category, 'condition': condition,
                       'loop': bool(cue.get('loop')), 'balanced': bool(clip.get('balanced', False)),
                       'seconds': clip.get('seconds'), 'status': 'available' if available else 'missing',
                       'requests': clip.get('requests', []), 'use_count': len(uses),
                       'used_by': used_by,
                       'slots': sorted({use['slot'] for use in uses}),
                       'timing_evidence': source.get('timing_evidence', ''),
                       'variant_limitation': cue.get('variant_limitation', '')})
    theme_clips = theme_audio_product(root, identifier)
    output.extend(theme_clips)
    absent.extend(clip['id'] for clip in theme_clips if clip['status'] == 'missing')
    return {'status': 'missing' if absent or not output else 'available', 'clips': output,
            'detail': source.get('limitation', ''), 'missing_clips': absent,
            'source_event_evidence': source.get('mapping_evidence', ''),
            'missing_source_requests': source.get('missing_source_requests', [])}


def theme_audio_product(root, mode):
    theme = {'longsword': 'laevatain_blade', 'bow': 'typhoea_bow'}.get(mode)
    if not theme:
        return []
    manifest = read(root / 'assets/audio/theme_cues.json', {})
    stages = manifest.get('themes', {}).get(theme, {})
    uses = defaultdict(list)
    labels = {'start': '长刀火焰持续', 'stop': '长刀火焰结束',
              'charge': '原弓拉弦', 'release': '原弓放箭', 'cancel': '原弓取消'}
    for stage, cues in stages.items():
        for cue in cues:
            if cue.get('kind') == 'stop':
                continue
            for clip in set([cue.get('clip', '')] + cue.get('variants', [])):
                if clip:
                    uses[clip].append({**cue, 'stage': stage})
    output = []
    for clip in manifest.get('clips', []):
        if clip['id'] not in uses:
            continue
        cue = uses[clip['id']][0]
        path = within(root, 'assets/audio/' + clip['file'])
        output.append({'id': clip['id'], 'file': path.relative_to(root / 'assets').as_posix(),
                       'label': labels[cue['stage']] + ' · ' + str(clip.get('media_id', clip['id'])),
                       'source': 'Endfield · ' + clip.get('source_event', clip.get('source', '')),
                       'confidence': '原角色事件与有效音源对应；由展示生命周期触发',
                       'confidence_id': cue.get('confidence', ''),
                       'gain': float(cue.get('gain', 1)), 'group': cue.get('group', 'weapon'),
                       'category': 'fire' if theme == 'laevatain_blade' else 'charge' if cue['stage'] == 'charge' else 'weapon',
                       'condition': 'always', 'loop': bool(cue.get('loop', False)),
                       'balanced': bool(clip.get('balanced', False)), 'seconds': clip.get('seconds'),
                       'status': 'available' if path.is_file() and path.stat().st_size > 44 else 'missing',
                       'use_count': len(uses[clip['id']]), 'used_by': [labels[use['stage']] for use in uses[clip['id']]],
                       'slots': [], 'theme': theme,
                       'timing_evidence': manifest.get('timing_evidence', ''),
                       'variant_limitation': cue.get('variant_limitation', ''),
                       'lifecycle_policy': cue.get('lifecycle_policy', '')})
    return output


def character_product(root, index, audit):
    combinations = {(row['character_id'], row['mode']): row for row in audit.get('combinations', [])}
    result = []
    for source in index.get('characters', []):
        identifier = source.get('character_id', source.get('id'))
        profile = read(within(root, 'profiles/' + source['profile']), {})
        source_evidence = profile.get('evidence', {})
        default = source.get('default_mode', 'longsword')
        name = source.get('label', source.get('display_name', identifier))
        if identifier == 'chr_0002_endminm':
            name = '管理员（男）'
        if identifier == 'chr_0003_endminf':
            name = '管理员（女）'
        if identifier == 'chr_0034_typhoea':
            name = '提夫罗斯'
        modes = {}
        for mode in MODE_ORDER:
            row = combinations.get((identifier, mode), {})
            errors = row.get('errors', [])
            audit_status = row.get('status', 'pending')
            passed = audit_status in {'passed', 'pass', 'offline-qualified', 'offline-mesh-qualified',
                                      'offline-lbs-qualified', 'offline-lbs-passed', 'offline-mesh-passed'} and not errors
            checked = audit_status == 'offline_mesh_checked' and not errors
            images = publish_reference_images(root, row, identifier, mode)
            image = images[0]['file'] if images else None
            modes[mode] = {'offline_status': 'passed' if passed else 'mesh_checked' if checked else 'failed' if errors or audit_status == 'failed' else 'pending',
                           'host_status': row.get('host_status', 'pending'),
                           'runtime_status': 'pending', 'audit_status': audit_status,
                           'evidence': row.get('evidence', ''), 'frames': row.get('frames', 0),
                           'errors': errors, 'warnings': row.get('warnings', []),
                           'detail': row.get('detail', '离线网格采样检查；视觉质量未认证，材质、衣物、场景与运行另验。'),
                           'visual_quality_certified': row.get('visual_quality_certified') is True,
                           'reference_image': image,
                           'reference_images': images,
                           'reference_caption': '实际离线蒙皮网格关键帧；灰材质；武器/原生 shader/衣物动态另验。固定参考图，不随源动作时间轴同步。' if image else ''}
        result.append({'id': identifier, 'name': name, 'category': NAMES.get(default, NAMES['longsword'])[3],
                       'default_mode': default, 'supported_modes': source.get('supported_modes', MODE_ORDER),
                       'profile_status': source.get('status', 'pending'),
                       'source_game_version': source_evidence.get('game_version', ''),
                       'current_vfs_verified': source_evidence.get('current_vfs_verified') is True,
                       'source_mesh_available': bool(source.get('source_mesh_available')),
                       'modes': modes, 'runtime_status': 'pending'})
    return result


def publish_reference_images(root, row, identifier, mode):
    evidence = row.get('evidence', [])
    if isinstance(evidence, str):
        evidence = [evidence]
    path = next((within(root, item) for item in evidence if isinstance(item, str)
                 and item.lower().endswith('.png') and within(root, item).is_file()), None)
    if not path:
        return []
    images = []
    for suffix, label in [('', '默认视角'), ('-front', '正面'), ('-side', '侧面')]:
        candidate = path.with_name(path.stem + suffix + path.suffix)
        if not candidate.is_file():
            continue
        output = root / 'assets/ui_reference' / f'{identifier}-{mode}{suffix}.webp'
        output.parent.mkdir(parents=True, exist_ok=True)
        if not output.is_file() or output.stat().st_mtime < candidate.stat().st_mtime:
            with Image.open(candidate) as image:
                image.convert('RGB').save(output, format='WEBP', quality=90, method=4)
        images.append({'label': label, 'file': output.relative_to(root / 'assets').as_posix(),
                       'evidence': candidate.relative_to(root).as_posix()})
    return images


def weapons_product(root, index):
    result = []
    for item in index.get('weapons', []):
        path = within(root, 'profiles/weapons/' + item['profile'])
        profile = read(path, {})
        identifier = item['id']
        label = profile.get('display_name', identifier)
        if identifier == 'laevatain_blade':
            label = '莱万汀长刀'
        elif identifier == 'typhoea_bow':
            label = '提夫罗斯弓'
        components = profile.get('parts', [])
        if not components and profile.get('controls', {}).get('type') == 'bow':
            components = [{'renderer': '弓身'}, {'renderer': '弦'}, {'renderer': '箭 / 本地显示控制'}]
        result.append({'id': identifier, 'resource_id': profile.get('resource_id', identifier), 'label': label,
                       'modes': item.get('modes', []), 'mount_side': item.get('mount_side', 'right'),
                       'components': components, 'status': 'available' if profile else 'missing',
                       'offline_status': 'available' if profile else 'missing', 'host_status': 'pending', 'runtime_status': 'pending',
                       'detail': profile.get('vfx_status', '') or profile.get('mount_evidence', ''),
                       'mount_evidence': profile.get('mount_evidence', ''), 'model_profile': path.relative_to(root).as_posix()})
    return result


def build_preview(root, entries):
    """Use the actual Blender evaluated source positions, never guessed wrist FK."""
    parents = [-1, 0, 1, 2, 3, 2, 5, 6, 7, 2, 9, 10, 11, 0, 13, 14, 15, 0, 17, 18, 19]
    clips = []
    names = None
    missing = []
    for identifier in MODE_ORDER:
        entry = entries.get(identifier)
        if not entry:
            continue
        directory = within(root, entry.get('data_root', 'assets'))
        moves = read(directory / 'moves.json', {})
        core = read(directory / 'core_moves.json', {})
        slots = core.get('runtime_core_slots') or list(dict.fromkeys(
            slot for command_slots in moves.get('commands', {}).values() for slot in command_slots))
        # Include composite source stages and intentional source holds when present.
        for slots_for_command in moves.get('commands', {}).values():
            for slot in slots_for_command:
                if slot not in slots:
                    slots.append(slot)
        for slot in slots:
            path = root / f'preview/modes/{identifier}/slot_{slot:05d}.json'
            if identifier == 'longsword' and not path.is_file():
                # Legacy cached LS previews remain a scoped fallback; current
                # full-source bake uses the same per-mode layout as other arms.
                path = root / f'preview/slot_{slot:03d}.json'
            source = read(path)
            if not source:
                missing.append({'mode': identifier, 'slot': slot})
                continue
            order = source.get('bones', [])
            positions = source.get('positions', [])
            if len(order) != 21 or not positions or any(len(frame) != 21 or any(
                    len(point) != 3 or any(not isinstance(v, (int, float)) or not math.isfinite(v) for v in point)
                    for point in frame) for frame in positions):
                raise ValueError('Malformed actual source preview ' + path.as_posix())
            if names is not None and order != names:
                raise ValueError('Preview main-bone order differs between modes')
            names = order
            stride = 3
            sampled = positions[::stride]
            if (len(positions) - 1) % stride:
                sampled.append(positions[-1])
            label = moves.get('labels', {}).get(str(slot))
            if not label:
                command = next((command for command, command_slots in moves.get('commands', {}).items()
                                if slot in command_slots), '')
                label = LEGACY_LABELS.get(command, f'原版源动作 #{slot}')
            clips.append({'mode': identifier, 'slot': slot, 'label': label,
                          'frames': [[[round(v, 5) for v in point] for point in frame] for frame in sampled],
                          'fps': float(source.get('fps', 60)) / stride,
                          'duration': (len(positions) - 1) / float(source.get('fps', 60)),
                          'source_frames': len(positions), 'source_frame_stride': stride,
                          'source': source.get('source', 'Actual evaluated source positions'),
                          'experimental': bool(source.get('experimental') or source.get('phase_map')),
                          'evidence': path.relative_to(root).as_posix()})
    preview = {'schema': 1, 'up_axis': 'y', 'fps': 20,
               'source': 'Actual Blender evaluated MHW source positions; every third frame. Source skeleton only, not Endfield mesh or gameplay.',
               'bones': [{'name': name, 'parent': parents[i]} for i, name in enumerate(names or [])],
               'clips': clips, 'missing_source_previews': missing}
    write_atomic(root / 'assets/ui_modes_preview.json', preview)
    return {'file': 'ui_modes_preview.json', 'clips': len(clips), 'missing': missing,
            'scope': 'actual evaluated MHW source main bones; no target mesh / no gameplay'}


def build_provenance(root, registry, character_index, audit, manifest):
    """Small distributable evidence summary; never copy raw game research into Git."""
    modes = []
    for entry in registry.get('modes', []):
        directory = within(root, entry['data_root'])
        audio_directory = within(root, entry['audio_root'])
        catalog = read(directory / 'catalog.json', [])
        rig = read(directory / 'source_rig.json', {})
        ground = read(directory / 'ground_tracks.json', {})
        audio = read(audio_directory / 'cues.json', {})
        props = read(directory / 'props/index.json', {})
        skin = read(directory / 'source_skin_tracks.json', {})
        fingers = read(within(root, entry.get('finger_root', entry['data_root'])) / 'finger_tracks.json', {})
        originals = defaultdict(int)
        evaluations = defaultdict(int)
        for clip in catalog:
            originals[clip.get('source', 'unrecorded')] += 1
            evaluations[clip.get('source_evaluation', 'manifest-declared')] += 1
        modes.append({'id': entry['id'], 'data_root': entry['data_root'],
                      'audio_root': entry['audio_root'], 'source_clips': dict(originals),
                      'evaluation_versions': dict(evaluations), 'clip_count': len(catalog),
                      'source_skeleton': rig.get('source', ''),
                      'source_sole_evidence': ground.get('ground_evidence', ''),
                      'sound_mapping_evidence': audio.get('mapping_evidence', ''),
                      'sound_timing_evidence': audio.get('timing_evidence', ''),
                      'original_props': [{key: prop[key] for key in ('id', 'file') if key in prop}
                                         for prop in props.get('props', [])],
                      'original_weapon_skin_tracks': len(skin.get('tracks', {})),
                      'original_finger_tracks': len(fingers.get('tracks', {})),
                      'keyboard_defaults_verified': (entry.get('default_bindings') or {}).get('verified') is True,
                      'runtime_verified': False})
    character_evidence = [read(within(root, 'profiles/' + item['profile']), {}).get('evidence', {})
                          for item in character_index.get('characters', [])]
    source_manifests = sorted({item.get('source_manifest') or item.get('manifest', '')
                              for item in character_evidence} - {''})
    theme_audio = read(root / 'assets/audio/theme_cues.json', {})
    result = {'schema': 1, 'version': manifest.get('version', 'unknown'),
              'verification_scope': 'offline source and target data, browser, isolated EML; game validation deferred',
              'endfield_source_manifest': audit.get('source_manifest', ''),
              'character_source_manifests': source_manifests,
              'current_endfield_source': character_index.get('current_source', {}),
              'character_source_game_versions': sorted({item.get('game_version', '') for item in character_evidence} - {''}),
              'characters_pending_current_vfs_validation': [item['character_id']
                  for item, evidence in zip(character_index.get('characters', []), character_evidence)
                  if not evidence.get('current_vfs_verified')],
              'source_reference_policy': 'Source paths identify local extraction evidence, not executable package dependencies.',
              'coordinate_policy': 'Unity Y-up/Z-forward meters; LSM world-rest quaternion deltas; per-renderer target bind registration.',
              'runtime_verified': False, 'modes': modes,
              'special_model_audio': {'manifest': 'assets/audio/theme_cues.json',
                  'clips': len(theme_audio.get('clips', [])),
                  'mapping_evidence': theme_audio.get('mapping_evidence', ''),
                  'timing_evidence': theme_audio.get('timing_evidence', ''),
                  'limitation': theme_audio.get('limitation', ''),
                  'stages': {theme: list(stages) for theme, stages in theme_audio.get('themes', {}).items()}},
              'tools': ['tools/mhw_multi_extract.py', 'tools/mhw_multi_bake.py', 'tools/mhw_multi_ground.py',
                        'tools/mhw_multi_audio.py', 'tools/mhw_finalize_modes.py', 'tools/mhw_source_mount_metadata.py',
                        'tools/mhw_complete_core_fingers.py', 'tools/mhw_prop_skin_tracks.py',
                        'tools/mhw_source_tour_render.py', 'tools/mhw_source_tour_package.py',
                        'tools/endfield_profiles.py', 'tools/endfield_weapon_profiles.py', 'tools/endfield_extract_weapon_audio.py',
                        'tools/publish_endfield_weapon_audio.py', 'tools/verify_endfield_weapon_audio.py',
                        'tools/weapon_reference_decode.py', 'tools/build_product_catalog.py', 'tools/package.py'],
              'unresolved': ['Original shader/VFX, clothing dynamics, scene constraints and actual input/audio timing require game checks.',
                             'Source command mappings and mounting evidence remain explicitly qualified in each mode/profile.',
                             'Offline mesh sampling does not certify aesthetic quality.']}
    write_atomic(root / 'assets/build_provenance.json', result)


def build(root: Path, allow_partial=False):
    root = root.resolve()
    registry = read(root / 'assets/modes/index.json', {})
    character_index = read(root / 'profiles/index.json', {})
    weapon_index = read(root / 'profiles/weapons/index.json', {})
    audit = read(root / 'research/character-coverage/coverage.json', {})
    missing = [name for name, data in [('mode registry', registry), ('character index', character_index),
                                      ('weapon index', weapon_index)] if not data]
    if missing and not allow_partial:
        raise ValueError('Missing required local metadata: ' + ', '.join(missing))
    entries = {entry['id']: entry for entry in registry.get('modes', [])}
    products, audio = [], {}
    for mode in MODE_ORDER:
        if mode not in entries:
            if not allow_partial:
                raise ValueError('Missing mode ' + mode)
            continue
        product = mode_product(root, entries[mode])
        products.append(product)
        audio[mode] = audio_product(root, entries[mode], product)
    package_manifest = read(root / 'module.json', {})
    product = {'schema': 1, 'version': package_manifest.get('version', 'unknown'),
               'verification_scope': 'Local source / offline mesh audit only; no game launched',
               'modes': products, 'characters': character_product(root, character_index, audit),
               'weapons': weapons_product(root, weapon_index),
               'default_by_mode': weapon_index.get('default_by_mode', {}),
               'coverage_audit_present': bool(audit), 'coverage_summary': audit.get('summary', {}),
               'source_metadata': ['assets/modes/index.json', 'profiles/index.json', 'profiles/weapons/index.json'],
               'partial_inputs': missing}
    product['preview'] = build_preview(root, entries)
    tours = read(root / 'assets/previews/source-tours/index.json', {})
    if tours:
        product['source_tours'] = {'file': 'previews/source-tours/index.json',
                                  'scope': 'Original MHW hunter and weapon meshes with unified PCM; offline source reference, not Endfield gameplay.',
                                  'modes': tours.get('modes', [])}
    sound = {'schema': 1, 'processing': {'sample_rate': 48000, 'channels': 2, 'resample': 'linear',
              'balance_rms': .18, 'balance_peak': .92, 'maximum_balance_gain': 3,
              'source_pcm_already_balanced': False}, 'modes': audio}
    build_provenance(root, registry, character_index, audit, package_manifest)
    write_atomic(root / 'assets/product_catalog.json', product)
    write_atomic(root / 'assets/audio_catalog.json', sound)
    return {'modes': len(products), 'characters': len(product['characters']), 'weapons': len(product['weapons']),
            'audio_clips': sum(len(entry['clips']) for entry in audio.values()),
            'coverage_audit_present': bool(audit),
            'mesh_checked_combinations': sum(mode['offline_status'] in ('passed', 'mesh_checked')
                for character in product['characters'] for mode in character['modes'].values()),
            'reference_images': sum(bool(mode['reference_image'])
                for character in product['characters'] for mode in character['modes'].values()),
            'reference_views': sum(len(mode['reference_images'])
                for character in product['characters'] for mode in character['modes'].values()),
            'missing_preview_clips': len(product['preview']['missing']),
            'missing_motion_slots': sum(len(mode['missing_slots']) for mode in products),
            'missing_audio_clips': sum(len(entry['missing_clips']) for entry in audio.values())}


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--allow-partial', action='store_true')
    args = parser.parse_args()
    print(json.dumps(build(args.root, args.allow_partial), ensure_ascii=True))
