"""Read current MHW GMD control/tutorial text without guessing key bindings.

The open-source beeequeue/gmd decoder documents the common v0x10302 header and
text-index/key-offset design. Current World PC files use 32-byte metadata rows
and 256 64-bit buckets, validated here from exact file/table/string dimensions.
Only logical ICON tags found in source text are exported. Physical keyboard
bindings must be verified independently, not inferred from PAD tag names.
"""
from pathlib import Path
import argparse
import collections
import json
import re
import struct

ROOT = Path(__file__).resolve().parents[1]
PREFIXES = dict(greatsword="taiken",longsword="tachi",sword_shield="katate",lance="ransu",light_bowgun="lgun",bow="bow")


def decode(path):
    data = path.read_bytes()
    if len(data)<40 or data[:4] != b"GMD\0":
        raise ValueError("Not a GMD file")
    magic,version,language,reserved1,reserved2,labels_count,text_count,key_size,text_size,name_size = struct.unpack_from("<10I",data)
    if version != 0x10302 or reserved1 or reserved2:
        raise ValueError("Unsupported source GMD header")
    table = 40+name_size+1
    label_pool = len(data)-text_size-key_size
    text_pool = len(data)-text_size
    if data[40+name_size] != 0 or label_pool-table-labels_count*32 != 2048:
        raise ValueError("Current MHW64 GMD dimensions do not match")
    raw = data[text_pool:].split(b"\0")
    if raw[-1] or len(raw)-1 != text_count:
        raise ValueError("GMD text count/terminator does not match")
    texts = [value.decode("utf-8") for value in raw[:-1]]
    entries,keys = [],set()
    for row in range(labels_count):
        index,hash1,hash2,padding,offset,next_bucket = struct.unpack_from("<IIIIQQ",data,table+row*32)
        if index>=text_count or offset>=key_size or padding != 0xcdcdcdcd:
            raise ValueError("GMD metadata index/key range/padding does not match")
        end = data.find(b"\0",label_pool+offset,text_pool)
        if end<0:
            raise ValueError("GMD label lacks terminator")
        key = data[label_pool+offset:end].decode("ascii")
        if key in keys:
            raise ValueError("GMD label is not unique")
        keys.add(key)
        entries.append(dict(label=key,text_index=index,text=texts[index],tags=re.findall(r"<[^>]+>",texts[index])))
    return dict(schema_version=1,format_version="0x10302",layout="MHW PC64: 32-byte metadata rows + 256 8-byte buckets",language=language,
                file_name=data[40:40+name_size].decode("ascii"),labels=entries,texts=texts,hashes_computed=False,
                reference_parser="https://github.com/beeequeue/gmd/blob/main/src/decode.ts",
                limitation="The primary parser covers20B/32-bit MT layout; this current World64 layout is verified against actual source sizes/indices/UTF8 pools, not fed into the incompatible20B parser")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input",type=Path,default=ROOT/"research/keyboard-defaults/game-text")
    parser.add_argument("--output",type=Path,default=ROOT/"research/keyboard-defaults/decoded-text")
    args = parser.parse_args()
    args.output.mkdir(parents=True,exist_ok=True)
    source = {}
    for path in sorted(args.input.rglob("*.gmd")):
        result = decode(path)
        result["file"] = path.relative_to(args.input).as_posix()
        (args.output/(path.stem+".json")).write_text(json.dumps(result,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        source[path.stem] = result
    en,ch = source["weaponTutorial_eng"],source["weaponTutorial_chS"]
    chinese = {row["label"]:row["text"] for row in ch["labels"]}
    controls = dict(schema_version=1,modes={},physical_default_keys_verified=False,game_launched=False,hashes_computed=False,
                    source="Current merged MHW chunks:weaponTutorial English+SimplifiedChinese/buttonGuide English",
                    logical_tag_limitation="Source tutorial uses PAD/PL logical controller icons; no keyboard/mouse default is inferred from them",
                    ot_weapon_classification="ot_weapon is Palico/Felyne equipment text, not player weapon keybinding settings")
    for mode,prefix in PREFIXES.items():
        rows = [dict(**row,text_chS=chinese[row["label"]]) for row in en["labels"]
                if row["label"].lower().startswith(prefix) or prefix in row["label"].lower().split("_")]
        controls["modes"][mode] = dict(labels=rows,logical_icon_tags=sorted({tag for row in rows for tag in row["tags"] if tag.startswith("<ICON ")}),
                                     hold_release_conditions=[row["label"] for row in rows if re.search(r"\b(?:hold|held|release|charging|during|after|then)\b",row["text"],re.I)])
    (args.output/"six-weapon-control-facts.json").write_text(json.dumps(controls,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps(dict(status="decoded",files=len(source),modes={key:len(value["labels"]) for key,value in controls["modes"].items()},
                         physical_keys_inferred=False,hashes_computed=False)))


if __name__ == "__main__":
    main()
