"""Reject stale offline native binaries before producing or trusting mesh proof.

Conservative local build freshness by modification time, not cryptographic
provenance. A clean rebuild is required after any declared native input changes.
"""
from pathlib import Path


def validate_native_build(root, source_inputs):
    root=Path(root).resolve()
    directory=root/'build/offline-retarget-dual/Release'
    if not (directory/'endfield_pose_solver.dll').is_file():
        directory=root/'build/offline-retarget/Release'
    sources=[root/name for name in source_inputs]
    if not sources:raise ValueError('Native build input list cannot be empty')
    stamps={path:path.stat().st_mtime_ns for path in sources}
    latest=max(stamps.values())
    binaries=[directory/'endfield_pose_solver.dll',directory/'endfield_retarget_oracle.exe']
    for binary in binaries:
        if not binary.is_file() or binary.stat().st_size==0:
            raise FileNotFoundError('Build current offline native binaries first: '+str(binary))
        if binary.stat().st_mtime_ns < latest:
            newer=[path.relative_to(root).as_posix() for path,stamp in stamps.items() if stamp>binary.stat().st_mtime_ns]
            raise ValueError('Offline native binary is older than current sources; rebuild with --clean-first: '
                             +str(binary)+'; newer inputs: '+', '.join(newer))
    return {'directory':str(directory),'source_inputs':len(sources),'binaries':len(binaries),'fresh':True}
