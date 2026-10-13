"""Build an E Mod Loader format-1 Windows ZIP without hashing artifacts."""
from __future__ import annotations

import argparse
import json
from pathlib import Path, PurePosixPath
import zipfile

PROJECT = Path(__file__).resolve().parents[1]
ALLOWED_ASSET_EXTENSIONS = {".lsm", ".lsg", ".json", ".wav"}


def safe_relative(path: str) -> PurePosixPath:
    value = PurePosixPath(path)
    if not path or "\\" in path or ":" in path or value.is_absolute() or ".." in value.parts:
        raise ValueError(f"Unsafe package path: {path!r}")
    return value


def package(root: Path, package_root: Path, output: Path | None) -> Path:
    manifest_path = root / "module.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8-sig"))
    if manifest.get("format") != 1 or manifest.get("abi") != 1:
        raise ValueError("Expected E Mod Loader format 1 / ABI 1")
    library = safe_relative(manifest["libraries"]["windows-x64"])
    if len(manifest["libraries"]) != 1:
        raise ValueError("This package currently supports Windows x64 only")
    safe_relative(manifest["ui"])
    dll = package_root.joinpath(*library.parts)
    if not dll.is_file():
        raise FileNotFoundError(f"Build the native module first: {dll}")
    entries: dict[str, Path | bytes] = {"module.json": manifest_path, str(library): dll}
    for name in ("README.md", "THIRD_PARTY_NOTICES.md", "LICENSE"):
        source = root / name
        if source.is_file():
            entries[name] = source
        elif name != "LICENSE":
            raise FileNotFoundError(source)
    required_profile = root / "profiles" / "female_admin.json"
    if not required_profile.is_file():
        raise FileNotFoundError(f"Required female administrator profile is missing: {required_profile}")
    json.loads(required_profile.read_text(encoding="utf-8-sig"))
    for directory in ("ui", "assets", "profiles"):
        source_directory = root / directory
        if not source_directory.is_dir():
            raise FileNotFoundError(source_directory)
        for source in sorted(source_directory.rglob("*")):
            if not source.is_file():
                continue
            if source.is_symlink():
                raise ValueError(f"Refusing linked resource: {source}")
            if directory == "assets" and source.suffix.lower() not in ALLOWED_ASSET_EXTENSIONS:
                continue
            if directory == "profiles" and source.suffix.lower() != ".json":
                continue
            if directory == "ui" and source.suffix.lower() not in {".html", ".css", ".js", ".svg", ".png", ".webp"}:
                continue
            name = source.relative_to(root).as_posix()
            safe_relative(name)
            entries[name] = source
    if manifest["ui"] not in entries:
        raise FileNotFoundError(manifest["ui"])
    for name in ("assets/source_rig.json", "assets/grip_tracks.json", "assets/ground_tracks.json", "assets/hit_windows.json", "assets/source_weapon_tracks.json"):
        if name not in entries:
            raise FileNotFoundError("Required retarget/practice asset missing: " + name)
    grip = json.loads((root / "assets/grip_tracks.json").read_text(encoding="utf-8-sig"))
    if grip.get("schema_version") != 1 or not grip.get("tracks"):
        raise ValueError("Invalid grip manifest")
    for filename in grip["tracks"].values():
        path = safe_relative(filename)
        if len(path.parts) != 1 or path.suffix != ".lsg" or "assets/" + filename not in entries:
            raise FileNotFoundError("Required grip track missing: " + filename)
    audio_manifest = root / "assets" / "audio" / "cues.json"
    audio = json.loads(audio_manifest.read_text(encoding="utf-8-sig"))
    if audio.get("schema_version") != 1 or not audio.get("clips") or not audio.get("cues"):
        raise ValueError("Required audio cue manifest is empty or invalid")
    clip_ids = set()
    for clip in audio["clips"]:
        relative = safe_relative(clip["file"])
        if len(relative.parts) != 1 or relative.suffix.lower() != ".wav":
            raise ValueError("Audio clips must be WAV files directly inside assets/audio")
        if clip["id"] in clip_ids:
            raise ValueError("Duplicate audio clip ID")
        clip_ids.add(clip["id"])
        name = "assets/audio/" + str(relative)
        if name not in entries:
            raise FileNotFoundError("Required audio clip missing: " + name)
    for cues in audio["cues"].values():
        for cue in cues:
            if cue["clip"] not in clip_ids:
                raise ValueError("Audio cue references an absent clip")
    preview_file = root / "assets" / "ui_preview.json"
    if preview_file.is_file():
        preview = json.loads(preview_file.read_text(encoding="utf-8-sig"))
        # Embedding is intentional: a directly opened file:// page cannot fetch JSON everywhere.
        encoded = json.dumps(preview, ensure_ascii=False, separators=(",", ":")).replace("\u2028", "\\u2028").replace("\u2029", "\\u2029")
        entries["ui/preview-data.js"] = ("window.LONGSWORD_PREVIEW=" + encoded + ";\n").encode("utf-8")
    output = output or root / "dist" / f"{manifest['id']}-{manifest['version']}-win-x64.zip"
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, source in sorted(entries.items()):
            if isinstance(source, bytes):
                archive.writestr(name, source)
            else:
                archive.write(source, name)
    print(f"Packaged {len(entries)} files -> {output} ({output.stat().st_size:,} bytes)")
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=PROJECT)
    parser.add_argument("--package-root", type=Path, help="Native build package directory; default: ROOT/build/package")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = args.root.resolve()
    package(root, (args.package_root or root / "build" / "package").resolve(), args.output)


if __name__ == "__main__":
    main()
