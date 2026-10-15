"""Check source-declared runtime contracts against a fresh targeted IL2CPP export.

The input uses the methods/classes/source format emitted by a local metadata
extractor. --game verifies input sizes and mtimes against installed files; it
does not run the client, hash artifacts, or infer hook dispatch from a matching
signature. Hook reachability requires a separate binary/runtime audit.
"""
from __future__ import annotations
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
ALIASES = {'Game': 'Gameplay.Beyond.dll', 'Core': 'UnityEngine.CoreModule.dll', 'nullptr': None}


def arguments(raw):
    parts = re.findall(r'"[^"\n]*"|[^,]+', raw)
    result = []
    for part in parts:
        value = part.strip()
        if value.startswith('"') and value.endswith('"'):
            result.append(value[1:-1])
        elif value in ALIASES:
            result.append(ALIASES[value])
        elif value.isdecimal():
            result.append(int(value))
        else:
            return None
    return result


def normalized(value):
    return re.sub(r'`\d+', '', value).replace('+', '.').replace('/', '.')


def contracts(root, extra_sources=()):
    source = (root / 'native/module.cpp').read_text(encoding='utf-8')
    methods = [arguments(row) for row in re.findall(r'^ M\((.*)\),?$', source, re.M)]
    if not methods or any(row is None or len(row) != 7 for row in methods):
        raise ValueError('Unable to parse the complete main Method table')
    shields = source.split('Shield shields[]{', 1)[1].split('\n};', 1)[0]
    for row in re.findall(r'\{\{(.*?)\}\}', shields):
        value = arguments(row)
        if value is None or len(value) != 7:
            raise ValueError('Unable to parse an input shield descriptor')
        methods.append(value)
    sources = [source]
    for path in extra_sources:
        paths = sorted(path.glob('*.h')) if path.is_dir() else [path]
        if not paths:
            raise ValueError('No extension headers found in ' + str(path))
        sources += [p.read_text(encoding='utf-8') for p in paths]
    fields = []
    for text in sources:
        for kind, body in re.findall(r'BE_(Method|Field)DescriptorV1\s+\w+\s*\{(.*?)\};', text, re.S):
            values = arguments(body)
            if values is not None and len(values) == (7 if kind == 'Method' else 5):
                (methods if kind == 'Method' else fields).append(values)
    # These field names are passed through the runtime layout helper.
    fields += [['UnityEngine.PhysicsModule.dll', 'UnityEngine', 'RaycastHit', name, typ]
               for name, typ in [('m_Point', 'UnityEngine.Vector3'), ('m_Normal', 'UnityEngine.Vector3'), ('m_Distance', 'System.Single')]]
    fields += [['UnityEngine.CoreModule.dll', 'UnityEngine', 'Vector3', name, 'System.Single'] for name in 'xyz']
    return list(dict.fromkeys(map(tuple, methods))), list(dict.fromkeys(map(tuple, fields)))


def audit(metadata, game, methods, fields):
    errors = []
    source = metadata.get('source', {})
    files = {key.replace('\\', '/'): value for key, value in source.get('files', {}).items()}
    for name in ('GameAssembly.dll', 'Endfield_Data/il2cpp_data/Metadata/global-metadata.dat'):
        path = game / name
        record = files.get(name)
        if not path.is_file() or record != {'size': path.stat().st_size, 'mtime_ns': path.stat().st_mtime_ns}:
            errors.append(f'Stale or missing metadata source evidence: {name}')
    if source.get('metadata_version') != 29:
        errors.append('Unsupported metadata version')
    matched_methods = []
    for asm, ns, cls, name, params, returns, count in methods:
        full = f'{ns}.{cls}' if ns else cls
        candidates = [m for m in metadata['methods'] if m['asm'] == asm and normalized(m['cls']) == full
                      and m['name'] == name and m['count'] == count
                      and (params is None or normalized('|'.join(m['types'])) == params)
                      and (returns is None or normalized(m['ret']) == returns)]
        if len(candidates) != 1 or not candidates[0].get('rva'):
            errors.append(f'Method mismatch: {asm}:{full}.{name}({params or ""}), matches={len(candidates)}')
        else:
            matched_methods.append(candidates[0])
    for asm, ns, cls, name, typ in fields:
        full = f'{ns}.{cls}' if ns else cls
        candidates = [f for c in metadata['classes'] if c['asm'] == asm and normalized(c['cls']) == full
                      for f in c['fields'] if f['name'] == name and normalized(f['type']) == typ]
        if len(candidates) != 1:
            errors.append(f'Field mismatch: {asm}:{full}.{name}:{typ}, matches={len(candidates)}')
    return {'scope': 'literal main-module/shield/supplied-extension descriptors and terrain layout fields; dynamic visual-resource descriptors excluded',
            'method_contracts': len(methods), 'field_contracts': len(fields), 'matched_methods': len(matched_methods),
            'source': source, 'errors': errors, 'runtime_verified': False, 'hook_dispatch_verified': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--metadata', required=True, type=Path)
    parser.add_argument('--game', required=True, type=Path)
    parser.add_argument('--repo', type=Path, default=ROOT)
    parser.add_argument('--extra-source', action='append', type=Path, default=[], help='Additional source file or header directory to audit')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    result = audit(json.loads(args.metadata.read_text(encoding='utf-8')), args.game, *contracts(args.repo, args.extra_source))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    print(json.dumps({key: result[key] for key in ('method_contracts', 'field_contracts', 'matched_methods', 'errors')}))
    raise SystemExit(bool(result['errors']))


if __name__ == '__main__':
    main()
