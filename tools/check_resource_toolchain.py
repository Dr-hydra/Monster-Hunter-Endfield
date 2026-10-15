"""Read-only local resource prerequisite checks; no downloads, hashes or game launch.

This checks availability and declared runtime versions. It does not certify an
AnimeStudio parser's Endfield compatibility or replace the graph/mesh audits.
"""
from __future__ import annotations
import argparse
import importlib.metadata
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys

STAGES = ('python', 'endfield', 'mhw', 'audio')
PYTHON_PACKAGES = {'numpy': '2.2.5', 'scipy': '1.15.2', 'Pillow': '12.3.0'}


def inspect(root, be_root, stages=STAGES, version=importlib.metadata.version,
            which=shutil.which, run=subprocess.run):
    root, be_root = Path(root).resolve(), Path(be_root).resolve()
    checks = []

    def record(name, okay, detail):
        checks.append({'name': name, 'status': 'ready' if okay else 'missing_or_incompatible', 'detail': str(detail)})

    def file(name, path):
        okay = path.is_file() and path.stat().st_size > 0
        record(name, okay, path)

    if 'python' in stages:
        record('Python >= 3.12', sys.version_info >= (3, 12), sys.version.split()[0])
        for package, expected in PYTHON_PACKAGES.items():
            try: actual = version(package)
            except importlib.metadata.PackageNotFoundError: actual = 'not installed'
            record(package + ' == ' + expected, actual == expected, actual)
    if 'endfield' in stages:
        try: crypto_version = version('pycryptodome')
        except importlib.metadata.PackageNotFoundError: crypto_version = 'not installed'
        record('pycryptodome == 3.23.0', crypto_version == '3.23.0', crypto_version)
        for name, relative in (
            ('BE input refresh', 'scripts/RefreshEndfieldResourceInputs.py'),
            ('BE workspace dependency', 'scripts/workspace_config.py'),
            ('BE bundle extraction', 'tools/CustomModel/extract_native_bundles.py'),
            ('VFS decryptor', 'toolchains/EndfieldUnpacker/decrypt_vfs.py'),
            ('VFS configuration dependency', 'toolchains/EndfieldUnpacker/config.py'),
            ('SparkBuffer decoder', 'toolchains/EndfieldUnpacker/decode_sparkbuffer.py'),
            ('Bounded backend preparation', 'tools/CustomModel/prepare_native_backend.py'),
            ('NativeAssetReader source', 'tools/CustomModel/NativeAssetReader/NativeAssetReader.csproj'),
            ('NativeAssetReader', 'toolchains/native-asset-reader/identities/NativeAssetReader.dll'),
            ('NativeAssetReader runtime manifest', 'toolchains/native-asset-reader/identities/NativeAssetReader.runtimeconfig.json'),
            ('Compatible AnimeStudio backend', 'toolchains/native-asset-reader/identities/AnimeStudio.dll'),
            ('ResConv source', 'toolchains/FkArkEnd/ResConv/ResConv.csproj'),
            ('ResConv', 'toolchains/FkArkEnd/ResConv/bin/Release/net10.0/ResConv.exe'),
        ): file(name, be_root/relative)
        inspector = root/'tools/endfield_weapon_inspector/bin/Release/net8.0-windows'
        file('EndfieldWeaponInspector', inspector/'EndfieldWeaponInspector.dll')
        file('Inspector AnimeStudio backend', inspector/'AnimeStudio.dll')
        dotnet = which('dotnet')
        record('dotnet command', bool(dotnet), dotnet or 'Install .NET 10 SDK and .NET 8 runtime')
        if dotnet:
            for argument, versions in (('--list-sdks', ('10.',)), ('--list-runtimes', ('Microsoft.NETCore.App 8.', 'Microsoft.NETCore.App 10.'))):
                try:
                    result = run([dotnet, argument], capture_output=True, text=True, check=True, timeout=30)
                    lines = result.stdout.splitlines()
                    for expected in versions:
                        record('dotnet ' + argument + ' ' + expected, any(line.startswith(expected) for line in lines), '\n'.join(lines))
                except (OSError, subprocess.SubprocessError) as error:
                    record('dotnet ' + argument, False, error)
    if 'mhw' in stages:
        for name, relative in (
            ('Blender 4.5.14 executable', 'local/tools/blender/blender-4.5.14-windows-x64/blender.exe'),
            ('MHWNoChunk source', 'local/tools/mhw-nochunk/MHWNoChunk/Chunk.cs'),
            ('MHWNoChunk format key', 'local/tools/mhw-nochunk/MHWNoChunk/Resources/chunk.key'),
            ('Local ooz wrapper', 'local/tools/mhw-ooz-source/mhe_ooz.dll'),
            ('Model Editor importer', 'research/mhw-longsword/20261012/tooling/MHW_Model_Editor/addons/MHW_Model_Editor/modules/mod3/mod3_functions.py'),
            ('Animation reader', 'research/mhw-longsword/20261012/tooling/mhw-anim-tools/core/formats/lmt/reader.py'),
            ('TIML reader', 'research/mhw-longsword/20261012/tooling/mhw-anim-tools/core/formats/timl/reader.py'),
        ): file(name, root/relative)
        dictionary = root/'research/mhw-longsword/20261012/tooling/MHW_Model_Editor/addons/MHW_Model_Editor/modules/mrl3/dict/various_hash_dict.json'
        try:
            value = json.loads(dictionary.read_text(encoding='utf-8'))
            okay = isinstance(value, dict) and bool(value)
            record('Model Editor material-name dictionary', okay,
                   f'{len(value)} entries' if okay else 'Expected a nonempty JSON object')
        except (OSError, UnicodeError, json.JSONDecodeError) as error:
            record('Model Editor material-name dictionary', False,
                   str(dictionary) + ': ' + type(error).__name__)
    if 'audio' in stages:
        file('vgmstream', root/'local/tools/vgmstream/vgmstream-cli.exe')
        file('wwiser', root/'local/tools/wwiser-source/wwiser-master/wwiser.py')
    return {'schema': 1, 'root': str(root), 'be_root': str(be_root), 'stages': list(stages),
            'ready': all(row['status'] == 'ready' for row in checks), 'checks': checks,
            'scope': 'Availability and declared runtimes only; parser provenance/compatibility and owned game inputs require separate validation.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument('--be-root', type=Path, help='BE tool workspace; defaults to MHE_BE_ROOT or the sibling Better Endfield directory')
    parser.add_argument('--stage', action='append', choices=STAGES)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    report = inspect(args.root, args.be_root or Path(os.environ.get('MHE_BE_ROOT', str(args.root.resolve().parent/'Better Endfield'))), args.stage or STAGES)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    failures = [row for row in report['checks'] if row['status'] != 'ready']
    print(f"Resource prerequisites: {len(report['checks']) - len(failures)}/{len(report['checks'])} ready")
    for row in failures: print(row['name'] + ': ' + row['detail'])
    if failures: print('See docs/RESOURCE_TOOLCHAIN.md for exact locations and build steps.')
    return 1 if failures else 0


if __name__ == '__main__': raise SystemExit(main())
