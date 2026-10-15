"""Stage generated resource files and publish a complete batch with rollback.

Validation/export failures never touch old outputs. Publish failures restore the
previous files; this is not a claim of crash-atomic multi-directory filesystem IO.
"""
from pathlib import Path
import json
import os
import shutil
import tempfile


class ResourceTransaction:
    def __init__(self, root):
        self.root = Path(root).resolve()
        scratch = self.root / 'output/resource-staging'
        scratch.mkdir(parents=True, exist_ok=True)
        self._temporary = tempfile.TemporaryDirectory(prefix='batch-', dir=scratch)
        self.stage = Path(self._temporary.name) / 'new'
        self.backup = Path(self._temporary.name) / 'old'
        self.files = {}

    def __enter__(self): return self
    def __exit__(self, *_): self._temporary.cleanup()

    def destination(self, target):
        target = Path(target)
        if not target.is_absolute(): target = self.root / target
        target = target.resolve()
        relative = target.relative_to(self.root)
        if not relative.parts: raise ValueError('Resource output cannot replace the workspace root')
        return target, relative

    def directory(self, target):
        _, relative = self.destination(target)
        staging = self.stage / relative
        staging.mkdir(parents=True, exist_ok=True)
        return staging

    def add_file(self, target, source):
        target, _ = self.destination(target)
        source = Path(source).resolve()
        source.relative_to(self.stage.resolve())
        if not source.is_file(): raise FileNotFoundError(source)
        self.files[target] = source

    def add_directory(self, target):
        target, relative = self.destination(target)
        for source in (self.stage / relative).rglob('*'):
            if source.is_file(): self.add_file(target / source.relative_to(self.stage / relative), source)

    def write_bytes(self, target, data):
        target, relative = self.destination(target)
        path = self.stage / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        self.files[target] = path

    def write_json(self, target, value):
        self.write_bytes(target, (json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False) + '\n').encode('utf-8'))

    def commit(self):
        changes = []
        for target, source in self.files.items():
            self.destination(target)  # Recheck any junction/symlink introduced during preparation.
            if target.is_file() and target.read_bytes() == source.read_bytes(): continue
            if target.exists() and not target.is_file(): raise ValueError('Output is not a regular file: ' + str(target))
            relative = target.relative_to(self.root)
            saved = self.backup / relative if target.exists() else None
            if saved:
                saved.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(target, saved)
            changes.append((target, source, saved))
        applied = []
        try:
            for target, source, saved in changes:
                target.parent.mkdir(parents=True, exist_ok=True)
                applied.append((target, saved))
                os.replace(source, target)
        except BaseException:
            for target, saved in reversed(applied):
                if saved: shutil.copy2(saved, target)
                else: target.unlink(missing_ok=True)
            raise
