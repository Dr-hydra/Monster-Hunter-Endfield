"""Read named original w03p/SHLP/COL fields with authors' restricted templates.

Only fixed declarations, named structures/arrays and ==/!= conditions are
accepted; this is not a 010 code executor. Decryption mirrors MHW-Editor's
Blowfish byte-swap implementation. Keys are read from the upstream source in
local/tools, never printed or written to reports. Zero encrypted padding is
checked independently from the logical decode cursor. No hashes are computed.
"""
from pathlib import Path
import json
import math
import re
import struct

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'local/tools/mhw-combat-source'
FORMATS={'uint':'I','int':'i','int64':'q','float':'f','ubyte':'B','byte':'b','ushort':'H','short':'h','char':'B'}


class TemplateReader:
    def __init__(self, template, data, allow_unknown_tail=False):
        text=re.sub(r'//[^\n]*','',template)
        text=re.sub(r'<[^>]*>','',text)
        token_pattern=r'!=|==|\w+(?:\.\w+)*|[{}\[\];()]'
        if re.sub(token_pattern,'',text).strip():
            raise ValueError('Unsupported template syntax')
        self.tokens=re.findall(token_pattern,text);self.index=0;self.types={}
        self.ast=self.parse();self.data=data;self.cursor=0;self.fields={}
        self.evaluate(self.ast)
        trailer=data[self.cursor:]
        if (len(trailer)>15 or any(trailer)) and not (allow_unknown_tail and len(trailer)<=1024):
            raise ValueError(f'Unexpected bytes after decode: {self.cursor}/{len(data)}')

    def pop(self, expected=None):
        if self.index>=len(self.tokens):raise ValueError('Incomplete template')
        token=self.tokens[self.index];self.index+=1
        if expected is not None and token!=expected:raise ValueError(f'Template wanted {expected}, got {token}')
        return token

    def parse(self, end=None):
        rows=[]
        while self.index<len(self.tokens) and self.tokens[self.index]!=end:
            token=self.pop()
            if token=='typedef':
                self.pop('struct');self.pop('{');body=self.parse('}');self.pop('}');name=self.pop();self.pop(';');self.types[name]=body
            elif token=='if':
                self.pop('(');left=self.pop();op=self.pop();right=self.pop();self.pop(')');self.pop('{');body=self.parse('}');self.pop('}')
                if op not in ('==','!='):raise ValueError('Unsupported template condition')
                rows.append(('if',left,op,right,body))
            else:
                name=self.pop();count=None
                if self.tokens[self.index]=='[':self.pop('[');count=self.pop();self.pop(']')
                self.pop(';');rows.append(('value',token,name,count))
        return rows

    def value(self, name, prefix):
        if name.isdigit():return int(name)
        current=prefix
        while True:
            at=self.fields.get(current+name)
            if at:return at['value']
            if not current:raise ValueError('Unresolved template reference '+name)
            current=current.rstrip('.').rsplit('.',1)[0]+'.' if '.' in current.rstrip('.') else ''

    def evaluate(self, ast, prefix=''):
        for row in ast:
            if row[0]=='if':
                _,left,op,right,body=row
                comparison=self.value(left,prefix)==self.value(right,prefix)
                if comparison == (op=='=='):self.evaluate(body,prefix)
                continue
            _,typ,name,count=row
            amount=int(self.value(count,prefix)) if count else 1
            if not 0<=amount<=100000:raise ValueError('Unsafe template array length')
            for index in range(amount):
                key=prefix+name+(f'[{index}]' if count else '')
                if typ in self.types:self.evaluate(self.types[typ],key+'.');continue
                offset=self.cursor
                if typ=='string':
                    end=self.data.index(0,self.cursor);value=self.data[self.cursor:end].decode('utf-8','strict');self.cursor=end+1
                elif typ in FORMATS:
                    fmt=FORMATS[typ];value=struct.unpack_from('<'+fmt,self.data,self.cursor)[0];self.cursor+=struct.calcsize(fmt)
                    if typ=='float' and not math.isfinite(value):raise ValueError('Non-finite source parameter')
                else:raise ValueError('Unsupported template type '+typ)
                self.fields[key]=dict(offset=offset,value=value,type=typ)


def decrypt(data, key_name):
    from Crypto.Cipher import Blowfish
    source=(SOURCE/'EncryptionKeys.cs').read_text(encoding='utf-8-sig')
    key=re.search(r'\b'+key_name+r'\s*=\s*"([^\"]+)"',source).group(1).encode('utf-8')
    if len(data)%8:raise ValueError('Invalid encrypted source block length')
    swap=lambda raw:b''.join(raw[i:i+4][::-1] for i in range(0,len(raw),4))
    return swap(Blowfish.new(key,Blowfish.MODE_ECB).decrypt(swap(data)))


def read(path, extension):
    path=path.resolve()
    weapon_parameter=bool(re.fullmatch(r'w\d{2}p',extension))
    encrypted=weapon_parameter or extension=='shlp'
    data=path.read_bytes()
    if encrypted:data=decrypt(data,'PL_PARAM_KEY' if weapon_parameter else 'OWP_DAT_KEY')
    template=SOURCE/(extension+'.bt')
    reader=TemplateReader(template.read_text(encoding='utf-8-sig'),data,allow_unknown_tail=extension=='col')
    return dict(source=path.relative_to(ROOT).as_posix(),
                template_url='https://github.com/Synthlight/MHW-Editor/blob/master/010%20Templates/'+extension+'.bt',
                decrypt_source='https://github.com/Synthlight/MHW-Editor/blob/master/Util/EncryptionHelper.cs' if encrypted else None,
                bytes=len(data),decoded_bytes=reader.cursor,
                zero_padding_bytes=len(data)-reader.cursor if not any(data[reader.cursor:]) else 0,
                unknown_tail_hex=data[reader.cursor:].hex() if any(data[reader.cursor:]) else None,fields=reader.fields)


def main():
    out=ROOT/'research/mhw-original';base=out/'extracted/hm/wp/wp03'
    params=read(base/'wp03_param.w03p','w03p')
    (out/'combat-weapon-params.json').write_text(json.dumps(params,ensure_ascii=False,indent=2),encoding='utf-8')
    shells={p.name:read(p,'shlp') for p in (base/'shell/data').glob('*delay*.shlp')}
    (out/'combat-delayed-shells.json').write_text(json.dumps(shells,ensure_ascii=False,indent=2),encoding='utf-8')
    collision=ROOT/'research/multiweapon/extracted/hm/wp/wp03/collision/wp03.col'
    colliders=read(collision,'col')
    (out/'combat-colliders.json').write_text(json.dumps(colliders,ensure_ascii=False,indent=2),encoding='utf-8')
    shell_colliders=read(base/'shell/collision/object.col','col')
    (out/'combat-shell-colliders.json').write_text(json.dumps(shell_colliders,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(dict(weapon_bytes=params['bytes'],weapon_cursor=params['decoded_bytes'],shells=len(shells),
                         shell_cursors=sorted({x['decoded_bytes'] for x in shells.values()}),
                         collision_bytes=colliders['bytes'],collision_cursor=colliders['decoded_bytes'])))


if __name__=='__main__':main()
