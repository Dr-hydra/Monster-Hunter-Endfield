import importlib.metadata
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import check_resource_toolchain as toolchain


class ToolchainTests(unittest.TestCase):
    def test_material_dictionary_must_decode_as_nonempty_json_object(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            path=root/'research/mhw-longsword/20261012/tooling/MHW_Model_Editor/addons/MHW_Model_Editor/modules/mrl3/dict/various_hash_dict.json'
            path.parent.mkdir(parents=True)
            for payload,okay in ((b'{"material":"name"}',True),(b'{}',False),(b'[]',False),(b'{"x":\xff}',False),(b'{',False)):
                path.write_bytes(payload)
                report=toolchain.inspect(root,root,('mhw',))
                row=next(row for row in report['checks'] if row['name']=='Model Editor material-name dictionary')
                self.assertEqual(row['status']=='ready',okay)
                self.assertNotIn('"material"',row['detail'])

    def test_empty_machine_lists_exact_missing_inputs_and_does_not_create_them(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            report=toolchain.inspect(root,root/'be',('endfield','mhw','audio'),which=lambda _:None)
            self.assertFalse(report['ready'])
            self.assertEqual(list(root.iterdir()),[])
            self.assertTrue(any(row['detail'].endswith('NativeAssetReader.dll') for row in report['checks']))
            self.assertTrue(any(row['detail'].endswith('mhe_ooz.dll') for row in report['checks']))

    def test_missing_and_incompatible_python_versions_fail(self):
        def missing(_): raise importlib.metadata.PackageNotFoundError('missing')
        with tempfile.TemporaryDirectory() as folder, mock.patch.object(toolchain.sys,'version_info',(3,12)):
            ready=toolchain.inspect(folder,folder,('python',),version=toolchain.PYTHON_PACKAGES.__getitem__)
            self.assertTrue(ready['ready'])
            self.assertFalse(toolchain.inspect(folder,folder,('python',),version=lambda _:'0.0')['ready'])
            self.assertFalse(toolchain.inspect(folder,folder,('python',),version=missing)['ready'])

    def test_complete_files_still_require_both_dotnet_runtimes(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            empty=toolchain.inspect(root,root/'be',('endfield',),which=lambda _:None)
            for row in empty['checks']:
                if row['name'] in ('dotnet command','pycryptodome == 3.23.0'): continue
                path=Path(row['detail']);path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'synthetic')
            def run(args,**_):
                return mock.Mock(stdout='10.0.300 [SDK]' if args[-1]=='--list-sdks' else 'Microsoft.NETCore.App 8.0.0 [runtime]\nMicrosoft.NETCore.App 10.0.0 [runtime]')
            self.assertTrue(toolchain.inspect(root,root/'be',('endfield',),version=lambda _:'3.23.0',which=lambda _:'dotnet',run=run)['ready'])
            self.assertFalse(toolchain.inspect(root,root/'be',('endfield',),version=lambda _:'3.23.0',which=lambda _:'dotnet',run=lambda *_a,**_k:mock.Mock(stdout='10.0.300 [SDK]'))['ready'])


if __name__=='__main__':unittest.main()
