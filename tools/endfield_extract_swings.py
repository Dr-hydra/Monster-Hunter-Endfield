"""Verify three current Endfield attack SFX routes and stage offline PCM WAVs.

Only reads effective VFS metadata/current audio indexes and the three selected
BNK/WEM ranges. Does not run the game, rebuild manifests, modify audio cues or
calculate artifact/file hashes. Wwise FNV-1 IDs are required routing keys.
"""
from __future__ import annotations
import argparse
import importlib.util
import json
from pathlib import Path
import re
import struct
import subprocess
import sys
import types
import wave
import zlib


EVENTS = {
    "au_chr_0003_endmin_attack1": 929949674,
    "au_chr_0003_endmin_attack2": 198925091,
    "au_chr_0003_endmin_attack3": 484159784,
}


def load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module
    spec.loader.exec_module(module)
    return module


def read_record(record, chunks: dict[str, Path], unpacker) -> bytes:
    path = chunks.get(record.chunk)
    if path is None or not path.is_file():
        raise FileNotFoundError("Current CHK " + record.chunk)
    if record.offset < 0 or record.size <= 0 or record.offset + record.size > path.stat().st_size:
        raise ValueError("VFS range exceeds its current chunk")
    with path.open("rb") as stream:
        stream.seek(record.offset)
        data = stream.read(record.size)
    if len(data) != record.size:
        raise ValueError("truncated current VFS entry")
    return unpacker.per_file_decrypt(data, record.iv_seed) if record.encrypted else data


def manifest_identity(data: bytes) -> dict:
    # Mirrors Beyond.ManifestBinary.ManifestDataBinary.InitBinary/Utils:
    # Brotli, HEAD1, UTF16 Version, HEAD2, UTF16 Hash, UTF16 perforceCL.
    # Only decompress the beginning, not the full asset/bundle catalog.
    import brotli
    decoder = brotli.Decompressor()
    plain = b""
    for at in range(0, min(len(data), 1024 * 1024), 1024):
        plain += decoder.process(data[at:at + 1024])
        if len(plain) < 256:
            continue
        position = 4
        if struct.unpack_from("<I", plain)[0] != 0xff11ff11:
            raise ValueError("current manifest HEAD1 invalid")
        def string():
            nonlocal position
            size = struct.unpack_from("<i", plain, position)[0]
            if size < 0 or size > 512:
                raise ValueError("manifest identity string is malformed")
            position += 4
            value = plain[position:position + size * 2].decode("utf-16-le")
            if len(value) != size:
                raise ValueError("truncated manifest identity")
            position += size * 2
            return value
        version = string()
        if struct.unpack_from("<I", plain, position)[0] != 0xf1f2f3f4:
            raise ValueError("current manifest HEAD2 invalid")
        position += 4
        return {"manifest_version": version, "stored_content_tag": string(), "perforce_cl": string()}
    raise ValueError("could not decode current manifest identity prefix")


def stage(args) -> dict:
    game, be, out = args.game.resolve(), args.be.resolve(), args.output.resolve()
    if out == game or out.is_relative_to(game):
        raise ValueError("staging must be outside the game")
    out.mkdir(parents=True, exist_ok=True)
    deps = Path(__file__).resolve().parents[1] / "local/python-deps/endfield-swings"
    if deps.is_dir():
        sys.path.insert(0, str(deps))
    sys.path.insert(0, str(be / "scripts"))
    helper = load("mhe_swing_vfs_helper", be / "scripts/RefreshEndfieldResourceInputs.py")
    generator = load("mhe_swing_wwise", be / "scripts/GenerateResourceManifests.py")
    # These analysis functions also attach optional fingerprints. This task
    # explicitly disables those computations, retaining byte/structure checks.
    generator.sha256_bytes = lambda _data: "not-computed"
    generator.sha256_file = lambda _path: "not-computed"
    stub = types.ModuleType("config")
    stub.get_game_dir = lambda: str(game)
    sys.modules["config"] = stub
    unpacker = load("mhe_swing_vfs", be / "toolchains/EndfieldUnpacker/decrypt_vfs.py")
    unpacker.crc32 = lambda data: (zlib.crc32(data) + 2**31) % 2**32 - 2**31
    roots = {layer: game / "Endfield_Data" / layer / "VFS" for layer in ("StreamingAssets", "Persistent")}
    pattern = re.compile(r"^(?:Bundles/Windows/manifest\.hgmmap|TableCfg/AudioDialog\.bytes|.*\.pck)$", re.I)
    records = helper.select_overlay(unpacker, list(roots.items()), pattern)
    chunks = helper.build_chunk_index(list(roots.items()))
    effective = {record.relative_path.casefold(): record for record in records}
    cache = be / "inputs/1.5.3/Windows/baseline"
    freshness = []
    manifest = None
    dialog_matches = []
    for relative in ("Bundles/Windows/manifest.hgmmap", "TableCfg/AudioDialog.bytes"):
        record = effective.get(relative.casefold())
        if not record:
            raise ValueError("missing effective current " + relative)
        current = read_record(record, chunks, unpacker)
        cached_path = cache / relative
        matches = cached_path.is_file() and current == cached_path.read_bytes()
        if relative.endswith("hgmmap"):
            manifest = manifest_identity(current)
        else:
            spark = load("mhe_swing_spark", be / "toolchains/EndfieldUnpacker/decode_sparkbuffer.py")
            table_name, table = spark.parse_sparkbuffer(current)
            if table_name != "AudioDialog" or not isinstance(table, dict):
                raise ValueError("current AudioDialog is not valid SparkBuffer")
            dialog_matches = [key for key in table if key in EVENTS or "au_" + key in EVENTS]
        freshness.append({"path": relative, "layer": record.layer, "block": record.block,
                          "chunk": record.chunk, "size": len(current), "direct_byte_match_to_be_cache": matches,
                          "used_current_resource_not_cache": True})
    packages = []
    for record in records:
        if not record.relative_path.casefold().endswith(".pck"):
            continue
        path = chunks[record.chunk]
        if record.offset != 0 or record.encrypted:
            raise ValueError("non-direct audio PCK requires an explicit offset/decryption reader")
        index = generator.parse_pck(path, game)
        packages.append((record, index))
    if not packages:
        raise ValueError("effective VFS has no audio PCK records")
    inventory = [{"package": record.relative_path, "layer": record.layer,
                  "chunk": index.source, "languages": index.languages,
                  "banks": len(index.banks), "media": len(index.media),
                  "target_banks": [{"id": b.file_id, "language": b.language} for b in index.banks
                                   if b.file_id in {generator.wwise_id(name) for name in EVENTS}],
                  "target_media": [{"id": m.file_id, "language": m.language, "kind": m.kind, "bytes": m.size}
                                   for m in index.media if m.file_id in EVENTS.values()]}
                 for record, index in packages]
    (out / "package-routing-evidence.json").write_text(json.dumps(inventory, ensure_ascii=False, indent=2))
    report = {"schema": 1, "game": str(game), "effective_order": ["StreamingAssets", "Persistent"],
              "manifest_version": manifest["manifest_version"], "manifest_identity": manifest,
              "metadata_freshness": freshness, "event_names_are_known_sfx_aliases": True,
              "audio_dialog_note": "Attack SFX aliases are not *_sv AudioDialog voice rows.",
              "audio_dialog_exact_attack_sfx_rows": dialog_matches,
              "file_hashes_computed": False, "events": []}
    (out / "current-metadata.json").write_text(json.dumps(report, ensure_ascii=False, indent=2))
    for number, (event, expected_media) in enumerate(EVENTS.items(), 1):
        event_id = generator.wwise_id(event)
        candidates = [(record, index, bank) for record, index in packages for bank in index.banks if bank.file_id == event_id]
        if not candidates:
            raise ValueError(f"missing current bank for {event} ({event_id})")
        # Global SFX use the current bank package; prefer the largest full
        # current package, with Persistent as tie-breaker. Do not mix languages.
        candidates.sort(key=lambda item: (len(item[1].banks), item[0].layer == "Persistent"), reverse=True)
        record, index, bank = candidates[0]
        payload = generator.read_pck_payload(index, bank)
        cached_bank_matches = []
        for cached_bank in (be / f"inputs/1.5.3/Windows/bank-pck/bnk/{event_id}.bnk",
                            be / f"inputs/1.5.3/Windows/bank-pck/bnk/{bank.language}/{event_id}.bnk"):
            if cached_bank.is_file():
                if cached_bank.read_bytes() != payload:
                    raise ValueError("current bank conflicts with same-language research cache: " + str(cached_bank))
                cached_bank_matches.append(str(cached_bank))
        chunks = dict(generator.iter_chunks(payload))
        if b"BKHD" not in chunks or len(chunks[b"BKHD"]) < 8:
            raise ValueError("missing BNK header")
        bank_version, bank_id = struct.unpack_from("<II", chunks[b"BKHD"])
        if bank_version != 150 or bank_id != event_id:
            raise ValueError("BNK version/ID disagrees with Event routing")
        graph = generator.parse_hirc_bank(payload, event_id)
        graph.pop("routingStructureSha256", None)
        if expected_media not in graph["mediaIds"]:
            raise ValueError(f"swing sourceID {expected_media} is not reachable from current {event}: {graph['mediaIds']}")
        bank_parent = Path(record.relative_path).parent.as_posix().casefold()
        media_candidates = [(media_record, media_index, entry)
            for media_record, media_index in packages
            if Path(media_record.relative_path).parent.as_posix().casefold() == bank_parent
            for entry in media_index.media
            if entry.file_id == expected_media and entry.language == bank.language]
        if len(media_candidates) != 1 or bank.language != "s":
            raise ValueError("swing Media must exist exactly once in the matching current SFX package namespace")
        media_record, media_index, media_entry = media_candidates[0]
        wem = generator.read_pck_payload(media_index, media_entry)
        if not wem.startswith((b"RIFF", b"RIFX")):
            raise ValueError("selected media is not WEM")
        local = out / "inputs"
        local.mkdir(exist_ok=True)
        (local / f"{event_id}.bnk").write_bytes(payload)
        wem_path = local / f"{expected_media}.wem"
        wem_path.write_bytes(wem)
        wav_path = out / f"endfield_swing_{number}.wav"
        result = subprocess.run([str(args.vgmstream), "-o", str(wav_path), str(wem_path)],
                                text=True, capture_output=True, check=False)
        (out / f"decode-{number}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        if result.returncode != 0:
            raise ValueError("vgmstream failed; inspect the staging decode log")
        with wave.open(str(wav_path), "rb") as wav:
            if wav.getcomptype() != "NONE" or wav.getsampwidth() != 2 or wav.getnchannels() not in (1, 2):
                raise ValueError("decoded swing is not mono/stereo PCM16")
            pcm = {"channels": wav.getnchannels(), "sample_rate": wav.getframerate(),
                   "frames": wav.getnframes(), "seconds": wav.getnframes() / wav.getframerate()}
        report["events"].append({"event": event, "event_id_fnv1": event_id,
            "bank_id": bank_id, "bank_version": bank_version, "sourceID": expected_media,
            "sourceID_offset_in_sound_body": 9, "graph": graph,
            "matching_existing_bank_caches": cached_bank_matches,
            "selected_variation_of_event": True,
            "package": record.relative_path, "layer": record.layer,
            "chunk": index.source, "language": bank.language,
            "media_package": media_record.relative_path, "media_chunk": media_index.source,
            "same_current_package_namespace_and_language": True,
            "same_physical_pck": media_index.path == index.path,
            "bank_range": {"offset": bank.offset, "bytes": bank.size},
            "media_range": {"offset": media_entry.offset, "bytes": media_entry.size},
            "wav": wav_path.name, "pcm": pcm})
    (out / "mapping.json").write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game", type=Path, default=Path("G:/Arknights Endfield"))
    parser.add_argument("--be", type=Path, default=Path("G:/Better Endfield"))
    parser.add_argument("--output", type=Path, default=root / "research/endfield-swings")
    parser.add_argument("--vgmstream", type=Path, default=root / "local/tools/vgmstream/vgmstream-cli.exe")
    args = parser.parse_args()
    report = stage(args)
    print(json.dumps({"manifest_version": report["manifest_version"], "events": [
        {"event": row["event"], "sourceID": row["sourceID"], "seconds": row["pcm"]["seconds"]}
        for row in report["events"]]}, ensure_ascii=False))


if __name__ == "__main__":
    main()
