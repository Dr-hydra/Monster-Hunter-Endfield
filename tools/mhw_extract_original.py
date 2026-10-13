"""Selective Iceborne CMP extraction, following JodoZT/MHWNoChunk (MIT).

No PKG or full archive expansion. All archive directories are merged in numeric
patch order; only selected entries are extracted. Oodle is decoded by locally
compiled powzix/ooz (GPL-3.0+) using trusted user-owned game inputs.
"""
from pathlib import Path, PurePosixPath
from functools import lru_cache
import argparse, ctypes, json, re, struct

ROOT = Path(__file__).resolve().parents[1]
BLOCK = 0x40000


class Archive:
    def __init__(self, path, dll, keys, pattern):
        self.path, self.dll, self.keys, self.pattern = path, dll, keys, pattern
        self.file = path.open('rb')
        if self.file.read(4) != b'CMP\0':
            raise ValueError(f'Not an Iceborne CMP: {path}')
        count, = struct.unpack('<I', self.file.read(4))
        self.blocks = []
        for _ in range(count):
            row = self.file.read(8)
            # MHWNoChunk Chunk.cs: split nibble encoding, not uint24 >> 4.
            size = ((row[0] >> 4) | row[1] << 8 | row[2] << 16)
            size = (size >> 4) + (size & 15)
            self.blocks.append((int.from_bytes(row[3:], 'little'), size))

    @lru_cache(maxsize=64)
    def block(self, i):
        offset, size = self.blocks[i]
        self.file.seek(offset)
        encoded = self.file.read(size or BLOCK)
        if size:
            source = ctypes.create_string_buffer(encoded + b'\0' * 64)
            destination = ctypes.create_string_buffer(BLOCK + 64)
            got = self.dll.MHE_Decompress(source, len(encoded), destination, BLOCK)
            if got != BLOCK:
                raise ValueError(f'ooz decompression returned {got}: {self.path.name}:{i}, codec={encoded[:8].hex()}')
            decoded = destination.raw[:BLOCK]
        else:
            decoded = encoded
        key = self.keys[self.pattern[i] if self.pattern[i] < 16 else 0]
        # XOR is per-block with a 16-byte periodic key. Big integers avoid a Python byte loop.
        repeated = (key * ((len(decoded) + 15) // 16))[:len(decoded)]
        return (int.from_bytes(decoded, 'little') ^ int.from_bytes(repeated, 'little')).to_bytes(len(decoded), 'little')

    def read(self, offset, count):
        rows = []
        while count:
            i, at = divmod(offset, BLOCK)
            take = min(count, BLOCK-at)
            rows.append(self.block(i)[at:at+take])
            offset += take
            count -= take
        return b''.join(rows)

    def entries(self):
        header = self.read(0, 256)
        parents, children = struct.unpack_from('<II', header, 12)
        if parents > 100000 or children > 10000000:
            raise ValueError(f'Invalid directory: {self.path.name}, {header[:24].hex()}')
        cursor, result = 256, []
        for _ in range(parents):
            row = self.read(cursor, 84)
            cursor += 84
            count, = struct.unpack_from('<I', row, 80)
            for _ in range(count):
                row = self.read(cursor, 184)
                cursor += 184
                size, offset, entry_type, unknown = struct.unpack_from('<QQII', row, 160)
                if entry_type in (0, 2):
                    name = row[:80 if entry_type == 2 else 160].split(b'\0')[0].decode('ascii')
                    result.append(dict(path=name.replace('\\','/').lstrip('/'), size=size,
                                       offset=offset, type=entry_type, archive=self.path.name))
        return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--game', type=Path, default=Path('G:/SteamLibrary/steamapps/common/Monster Hunter World'))
    p.add_argument('--index-only', action='store_true')
    p.add_argument('--select', action='append', help='Case insensitive regex of merged path; repeatable.')
    args = p.parse_args()
    tooling = ROOT/'local/tools'
    source = (tooling/'mhw-nochunk/MHWNoChunk/Chunk.cs').read_text(encoding='utf-8-sig')
    keys = [bytes(int(x,16) for x in re.findall(r'0x([0-9a-fA-F]{2})', row))
            for row in re.findall(r'new byte\[\] \{([^}]+)\}', source)]
    if len(keys) != 16 or any(len(k)!=16 for k in keys): raise ValueError('Unrecognized upstream key definition')
    key_file = (tooling/'mhw-nochunk/MHWNoChunk/Resources/chunk.key').read_bytes()
    start, end = struct.unpack_from('<II', key_file)
    pattern = key_file[8:]
    if start != 1 or len(pattern) != end: raise ValueError('Unrecognized upstream key table')
    dll = ctypes.CDLL(str(tooling/'mhw-ooz-source/mhe_ooz.dll'))
    dll.MHE_Decompress.argtypes = [ctypes.c_void_p,ctypes.c_size_t,ctypes.c_void_p,ctypes.c_size_t]
    dll.MHE_Decompress.restype = ctypes.c_int
    chunks = sorted(args.game.glob('chunk/chunkG*.bin'), key=lambda x:int(re.search(r'chunkG(\d+)',x.name)[1]))
    result, archives, summary = {}, {}, []
    for path in chunks:
        archive = Archive(path,dll,keys,pattern)
        entries = archive.entries()
        archive.block.cache_clear()
        overridden = sum(row['path'] in result for row in entries)
        for row in entries:
            previous = result.get(row['path'])
            row['overrides'] = ([previous['archive']] + previous.get('overrides',[])) if previous else []
            result[row['path']] = row
        archives[path.name] = archive
        summary.append(dict(archive=path.name,entries=len(entries),overridden=overridden,blocks=len(archive.blocks)))
        print(path.name, 'entries',len(entries),'overrides',overridden,flush=True)
    out = ROOT/'research/mhw-original'
    out.mkdir(parents=True,exist_ok=True)
    (out/'merged-index.json').write_text(json.dumps(dict(archives=summary,entries=list(result.values())),ensure_ascii=False),encoding='utf-8')
    selectors = args.select or [r'(^|/)hm/wp/wp03/', r'(^|/)sound/.*wp03', r'(^|/)wp/w03/.*(?:mod3|mrl3|lmt|evwp)$', r'(^|/)pl/m_equip/pl000_0000/body/mod/.*\.mod3$']
    selected = [row for name,row in result.items() if any(re.search(s,name,re.I) for s in selectors)]
    if not args.index_only:
        for row in selected:
            relative = PurePosixPath(row['path'])
            if relative.is_absolute() or '..' in relative.parts: raise ValueError('Unsafe extracted path')
            target = out/'extracted'/relative
            target.parent.mkdir(parents=True,exist_ok=True)
            target.write_bytes(archives[row['archive']].read(row['offset'],row['size']))
    (out/'selection.json').write_text(json.dumps(dict(extracted=not args.index_only,selectors=selectors,entries=selected,total_bytes=sum(r['size'] for r in selected)),ensure_ascii=False,indent=2),encoding='utf-8')
    print('merged',len(result),'selected',len(selected),'bytes',sum(r['size'] for r in selected),flush=True)


if __name__ == '__main__': main()
