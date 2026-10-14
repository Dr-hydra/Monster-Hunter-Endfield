"""Build lossless release banks without replacing editable source resources.

LSW stores the exact binary32 inputs used by native attachment reconstruction.
LGB copies complete original LSG files byte for byte. Per-mode metadata retains
source provenance. No quantization, resampling, hashes or game process is used.
"""
from pathlib import Path
import argparse
import json
import math
import struct

ROOT = Path(__file__).resolve().parents[1]
MODES = ("longsword", "sword_shield", "greatsword", "lance", "light_bowgun", "bow")
LSW_FRAME = struct.Struct("<36fII")
HEADER = struct.Struct("<4sIII")


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def write(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_name(path.name+".bank-tmp")
    temporary.write_bytes(value)
    temporary.replace(path)


def metadata(path, value):
    write(path, json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8"))


def contained(root, relative):
    if not isinstance(relative, str) or not relative or Path(relative).is_absolute():
        raise ValueError("Bank source must be a relative file")
    target = (root / relative).resolve()
    if not target.is_relative_to(root.resolve()):
        raise ValueError("Bank path escapes its mode directory")
    return target


def numeric(values, count):
    if not isinstance(values, list) or len(values) != count:
        raise ValueError("Invalid original pose dimension")
    result = [float(value) for value in values]
    if any(not math.isfinite(value) or abs(value) > 100000 for value in result):
        raise ValueError("Invalid original pose value")
    return result


def pose(value):
    return numeric(value["position"], 3)+numeric(value["rotation_xyzw"], 4)


def selected_pair(frame, side, node):
    attachments = frame.get("attachments", {})
    if side not in frame or node not in attachments:
        return [0.0]*14, 0
    return pose(frame[side])+pose(attachments[node]), 1


def weapon_frame(frame, mode):
    main_side, main_node = ("left", "30") if mode == "bow" else ("right", "47")
    main, main_valid = selected_pair(frame, main_side, main_node)
    offhand, off_valid = selected_pair(frame, "left", "30")
    support = float(frame.get("support_weight", 0.0))
    if not math.isfinite(support) or not 0 <= support <= 1:
        raise ValueError("Invalid original support weight")
    values = numeric(frame["rotation_xyzw"], 4)+numeric(frame["support_offset"], 3)+[support]+main+offhand
    if len(values) != 36:
        raise ValueError("Invalid bank frame stride")
    return LSW_FRAME.pack(*values, main_valid, off_valid)


def weapon_bank(source, mode):
    original = read(source)
    tracks = original["tracks"]
    records = {}
    bank = bytearray(HEADER.pack(b"LSW1", 1, len(tracks), LSW_FRAME.size))
    frames = 0
    for slot in sorted(tracks, key=int):
        row = tracks[slot]
        count = len(row["frames"])
        fps = float(row["fps"])
        if not 0 < fps <= 240 or not 0 < count <= 36000:
            raise ValueError("Invalid source track timing")
        record = {key: value for key, value in row.items() if key != "frames"}
        record.update(frames=count, offset=len(bank))
        records[slot] = record
        for frame in row["frames"]:
            bank.extend(weapon_frame(frame, mode))
        frames += count
    result = {key: value for key, value in original.items() if key != "tracks"}
    result.update(schema_version=3, bank="source_weapon_tracks.lsw", tracks=records)
    # Decode every frame back to its exact float32 values and flags, independently
    # compare each component bit pattern (including signed zero), and bound all
    # offsets before publishing. Native JSON inputs were already float32.
    cursor = HEADER.size
    if HEADER.unpack_from(bank) != (b"LSW1", 1, len(records), LSW_FRAME.size):
        raise ValueError("Invalid generated LSW header")
    for slot, record in records.items():
        if record["offset"] != cursor or record["frames"] != len(tracks[slot]["frames"]):
            raise ValueError("LSW has gaps or reordered source samples")
        for index, frame in enumerate(tracks[slot]["frames"]):
            at = cursor+index*LSW_FRAME.size
            decoded = LSW_FRAME.unpack_from(bank, at)
            expected = LSW_FRAME.unpack(weapon_frame(frame, mode))
            if decoded[36:] != expected[36:] or any(struct.pack("<f", got) != struct.pack("<f", want)
                                                   for got, want in zip(decoded[:36], expected[:36])):
                raise ValueError("LSW float32 roundtrip differs from original JSON")
        cursor += record["frames"]*LSW_FRAME.size
    if cursor != len(bank):
        raise ValueError("LSW bank has a trailing or missing payload")
    return result, bank, frames


def grip_bank(source):
    original = read(source)
    records = {}
    payloads = {}
    bank = bytearray(HEADER.pack(b"LGB1", 1, len(original["tracks"]), 0))
    for slot in sorted(original["tracks"], key=int):
        path = contained(source.parent, original["tracks"][slot])
        payload = path.read_bytes()
        if len(payload) < HEADER.size or payload[:4] != b"LSG1":
            raise ValueError("Grip bank accepts complete original LSG1 files only")
        records[slot] = dict(offset=len(bank), size=len(payload), source_file=original["tracks"][slot])
        payloads[slot] = payload
        bank.extend(payload)
    result = {key: value for key, value in original.items() if key != "tracks"}
    result.update(schema_version=2, bank="grip_tracks.lgb", tracks=records)
    cursor = HEADER.size
    if HEADER.unpack_from(bank) != (b"LGB1", 1, len(records), 0):
        raise ValueError("Invalid generated LGB header")
    for slot, record in records.items():
        if record["offset"] != cursor or bank[cursor:cursor+record["size"]] != payloads[slot]:
            raise ValueError("LGB byte copy differs from original LSG1")
        cursor += record["size"]
    if cursor != len(bank):
        raise ValueError("LGB bank has a trailing or missing payload")
    return result, bank, original["tracks"]


def prepare(root: Path, package_root: Path):
    """Generate four release overlay files per mode; return exact audit results."""
    source_root, package_root = root.resolve(), package_root.resolve()
    if source_root == package_root or package_root.is_relative_to(source_root / "assets"):
        raise ValueError("Release banks must not overwrite editable source assets")
    rows = []
    for mode in MODES:
        source = source_root / "assets/modes" / mode
        destination = package_root / "assets/modes" / mode
        mounts, lsw, frames = weapon_bank(source / "source_weapon_tracks.json", mode)
        grips, lgb, source_files = grip_bank(source / "grip_tracks.json")
        if set(mounts["tracks"]) != set(grips["tracks"]):
            raise ValueError("Source mount/grip slot closure differs")
        # Verify all removable release copies before publishing. Never delete or
        # modify source clips, and never recursively delete a computed directory.
        remove = []
        for slot, relative in source_files.items():
            target = contained(destination, relative)
            if target.exists():
                record = grips["tracks"][slot]
                if target.read_bytes() != lgb[record["offset"]:record["offset"]+record["size"]]:
                    raise ValueError("Release grip copy differs from original; refusing cleanup")
                remove.append(target)
        write(destination / mounts["bank"], lsw)
        write(destination / grips["bank"], lgb)
        metadata(destination / "source_weapon_tracks.json", mounts)
        metadata(destination / "grip_tracks.json", grips)
        for target in remove:
            target.unlink()
        row = dict(mode=mode, mount_tracks=len(mounts["tracks"]), mount_frames=frames,
                   original_mount_json_bytes=(source / "source_weapon_tracks.json").stat().st_size,
                   mount_bank_bytes=len(lsw), mount_metadata_bytes=(destination / "source_weapon_tracks.json").stat().st_size,
                   grip_tracks=len(grips["tracks"]), grip_bank_bytes=len(lgb),
                   grip_metadata_bytes=(destination / "grip_tracks.json").stat().st_size,
                   release_grip_files_removed=len(remove), mount_float32_roundtrip_exact=True,
                   grip_complete_lsg_bytes_exact=True)
        rows.append(row)
        print(json.dumps(row), flush=True)
    report = dict(schema_version=1, status="passed", modes=rows,
                  mount_tracks=sum(row["mount_tracks"] for row in rows),
                  mount_frames=sum(row["mount_frames"] for row in rows),
                  original_mount_json_bytes=sum(row["original_mount_json_bytes"] for row in rows),
                  mount_bank_bytes=sum(row["mount_bank_bytes"] for row in rows),
                  grip_tracks=sum(row["grip_tracks"] for row in rows),
                  grip_bank_bytes=sum(row["grip_bank_bytes"] for row in rows),
                  source_assets_unchanged=True, hashes_computed=False, game_launched=False, server_started=False)
    report["overlays"] = [str(Path("assets/modes") / mode / file).replace("\\", "/")
                          for mode in MODES for file in
                          ("source_weapon_tracks.json", "source_weapon_tracks.lsw", "grip_tracks.json", "grip_tracks.lgb")]
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--package-root", type=Path, default=ROOT / "build/development/package")
    parser.add_argument("--report", type=Path, default=ROOT / "build/development/runtime-bank-report.json")
    args = parser.parse_args()
    report = prepare(args.source_root, args.package_root)
    metadata(args.report.resolve(), report)
    print(json.dumps({key: value for key, value in report.items() if key not in ("modes", "overlays")}), flush=True)


if __name__ == "__main__":
    main()
