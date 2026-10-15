import os
from pathlib import Path
import sys
import tempfile
import unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from audit_native_build import validate_native_build


class NativeAuditBuildTests(unittest.TestCase):
    def test_stale_solver_or_oracle_is_rejected_even_when_both_files_exist(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);source=root/'native/core.h';source.parent.mkdir();source.write_bytes(b'synthetic source')
            directory=root/'build/offline-retarget-dual/Release';directory.mkdir(parents=True)
            binaries=[directory/'endfield_pose_solver.dll',directory/'endfield_retarget_oracle.exe']
            for binary in binaries:binary.write_bytes(b'synthetic binary')
            stamp=2_000_000_000_000_000_000;os.utime(source,ns=(stamp,stamp))
            for binary in binaries:os.utime(binary,ns=(stamp+10_000_000,stamp+10_000_000))
            self.assertTrue(validate_native_build(root,['native/core.h'])['fresh'])
            for binary in binaries:
                os.utime(binary,ns=(stamp-10_000_000,stamp-10_000_000))
                with self.assertRaisesRegex(ValueError,'clean-first'):validate_native_build(root,['native/core.h'])
                os.utime(binary,ns=(stamp+10_000_000,stamp+10_000_000))
            binaries[1].unlink()
            with self.assertRaises(FileNotFoundError):validate_native_build(root,['native/core.h'])


if __name__=='__main__':unittest.main()
