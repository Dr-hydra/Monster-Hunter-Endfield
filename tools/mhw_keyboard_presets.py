"""Publish verified physical defaults without changing user configuration or poses."""
from pathlib import Path
import json
from mhw_control_mappings import correct_control_metadata

ROOT = Path(__file__).resolve().parents[1]


def binding_metadata(mode):
    source = json.loads((ROOT / 'tools/data/mhw_keyboard_presets.json').read_text(encoding='utf-8'))
    group = 'ranged' if mode in ('bow', 'light_bowgun') else 'close_range'
    presets = {name: {'label': preset['label'], 'keys': preset[group], 'verified': True}
               for name, preset in source['presets'].items()}
    return {'keys': presets['mouse5']['keys'], 'presets': presets, 'verified': True,
            'source': source['source'], 'source_url': source['source_url'],
            'preset_source_url': source['preset_source_url'],
            'verification_scope': source['verification_scope'],
            'fixed_sheathe_auxiliary': source['fixed_sheathe_auxiliary'],
            'label': '怪猎官方物理键位已核对 · 各招派生另列证据'}


def publish(root=ROOT):
    registry_path = root / 'assets/modes/index.json'
    registry = json.loads(registry_path.read_text(encoding='utf-8'))
    for entry in registry['modes']:
        metadata = binding_metadata(entry['id'])
        path = root / 'assets/modes' / entry['id'] / 'moves.json'
        moves = json.loads(path.read_text(encoding='utf-8'))
        moves['default_keys'] = metadata['keys']
        moves['keyboard_defaults'] = {name: value for name, value in metadata.items() if name != 'keys'}
        if entry['id'] != 'longsword':
            correct_control_metadata(entry['id'], moves, entry['offset'])
        path.write_text(json.dumps(moves, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
        entry['default_bindings'] = metadata
        entry['commands'] = list(moves['commands'])
    registry_path.write_text(json.dumps(registry, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print('Published six-mode official mouse5/mouse3 physical defaults; user settings untouched')


if __name__ == '__main__':
    publish()
