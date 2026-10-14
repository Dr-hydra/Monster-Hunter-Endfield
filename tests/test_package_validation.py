"""Package boundary regressions, using small synthetic tracks rather than game data."""
import importlib.util
import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest
import wave

SPEC = importlib.util.spec_from_file_location('mhe_package', Path(__file__).parents[1] / 'tools/package.py')
PACKAGE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(PACKAGE)


class PackageBoundaryTests(unittest.TestCase):
    def test_runtime_paths_cannot_escape_archive(self):
        for value in ('../file.lsf', 'assets/../../secret', 'G:/source', '/absolute', r'assets\file'):
            with self.subTest(value=value), self.assertRaises(ValueError):
                PACKAGE.safe_relative(value)
        self.assertEqual(str(PACKAGE.safe_relative('assets/modes/bow/finger_1.lsf')),
                         'assets/modes/bow/finger_1.lsf')

    def test_finger_binary_dimensions_and_truncation(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'finger.lsf'
            path.write_bytes(struct.pack('<4sIII', b'LSF1', 2, 30, 60) + struct.pack('<4f', 0, 0, 0, 1) * 60)
            self.assertEqual(PACKAGE.validate_binary(path, b'LSF1', 30), (2, 60))
            with self.assertRaises(ValueError):
                PACKAGE.validate_binary(path, b'LSF1', 32)
            path.write_bytes(path.read_bytes()[:-4])
            with self.assertRaises(ValueError):
                PACKAGE.validate_binary(path, b'LSF1', 30)

    def test_source_skin_matrices_finite_and_complete(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'source.lsk'
            values = [1 if column == row else 0 for column in range(4) for row in range(4)]
            path.write_bytes(struct.pack('<4sIII', b'LSK1', 2, 1, 60) + struct.pack('<32f', *(values * 2)))
            self.assertEqual(PACKAGE.validate_binary(path, b'LSK1', 1), (2, 60))
            values[0] = float('nan')
            path.write_bytes(struct.pack('<4sIII', b'LSK1', 2, 1, 60) + struct.pack('<32f', *(values * 2)))
            with self.assertRaises(ValueError):
                PACKAGE.validate_binary(path, b'LSK1', 1)

    def test_timeline_requires_published_frame_and_rate_alignment(self):
        motions = {1: {'frames': 2, 'fps': 60}}
        PACKAGE.validate_timeline({'tracks': {'1': {'fps': 60, 'clearance': [0, .2]}}}, motions, 'bow', 'clearance')
        for manifest in ({'tracks': {}}, {'tracks': {'1': {'fps': 30, 'clearance': [0, .2]}}},
                         {'tracks': {'1': {'fps': 60, 'clearance': [0]}}},
                         {'tracks': {'1': {'fps': 60, 'clearance': [0, float('nan')]}}}):
            with self.subTest(manifest=manifest), self.assertRaises(ValueError):
                PACKAGE.validate_timeline(manifest, motions, 'bow', 'clearance')

    def test_sound_closure_and_source_time_bounds(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            wav = root / 'clip.wav'
            with wave.open(str(wav), 'wb') as stream:
                stream.setnchannels(1)
                stream.setsampwidth(2)
                stream.setframerate(48000)
                stream.writeframes(b'\0\0' * 4)
            cues = root / 'cues.json'
            manifest = {'schema_version': 2, 'clips': [{'id': 'tone', 'file': 'clip.wav'}],
                        'cues': {'1': [{'time': .2, 'clip': 'tone'}]}}
            entries = {'assets/audio/cues.json': cues, 'assets/audio/clip.wav': wav}
            motions = {1: {'duration': 1}}
            def check():
                cues.write_text(json.dumps(manifest), encoding='utf-8')
                return PACKAGE.validate_audio(root, entries, 'bow', 'assets/audio', motions)
            self.assertEqual(check(), 1)
            for changes in ({'1': [{'time': 1.1, 'clip': 'tone'}]},
                            {'2': [{'time': 0, 'clip': 'tone'}]},
                            {'1': [{'time': .4, 'clip': 'tone'}, {'time': .2, 'clip': 'tone'}]},
                            {'1': [{'time': 0, 'clip': 'absent'}]}):
                manifest['cues'] = changes
                with self.subTest(cues=changes), self.assertRaises(ValueError):
                    check()

    def test_special_audio_lifecycle_requires_all_stages_and_pcm(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            wav = root / 'theme.wav'
            with wave.open(str(wav), 'wb') as stream:
                stream.setnchannels(1)
                stream.setsampwidth(2)
                stream.setframerate(48000)
                stream.writeframes(b'\0\0' * 4)
            path = root / 'theme_cues.json'
            cue = {'time': 0, 'clip': 'theme'}
            manifest = {'schema_version': 1, 'mapping_evidence': 'source event',
                        'timing_evidence': 'visual lifecycle',
                        'clips': [{'id': 'theme', 'file': 'theme.wav'}],
                        'themes': {'laevatain_blade': {'start': [cue], 'stop': [cue]},
                                   'typhoea_bow': {'charge': [cue], 'release': [cue], 'cancel': [cue]}}}
            entries = {'assets/audio/theme_cues.json': path, 'assets/audio/theme.wav': wav}
            def check():
                path.write_text(json.dumps(manifest), encoding='utf-8')
                return PACKAGE.validate_theme_audio(entries)
            self.assertEqual(check(), 1)
            del manifest['themes']['typhoea_bow']['cancel']
            with self.assertRaises(ValueError):
                check()
            manifest['themes']['typhoea_bow']['cancel'] = [cue]
            del entries['assets/audio/theme.wav']
            with self.assertRaises(FileNotFoundError):
                check()

    def test_contact_weights_cannot_cross_geometric_pass_or_frame_bounds(self):
        slots = (3, 9, 11, 101, 102, 103, 104, 105, 106, 107, 108, 109, 112, 127, 128, 140, 306, 307, 311, 1000)
        track = {'fps': 60, 'frames': 5, 'pass_mask': [0, 1, 1, 1, 0],
                 'intervals': [{'first': 1, 'last': 3, 'frames': 3}], 'weights': [0, 0, .5, 0, 0]}
        models = {model: {'characters': {'character': {'tracks': {str(slot): copy.deepcopy(track) for slot in slots}}}}
                  for model in ('wpn_misc_0016', 'source:longsword_bs_swo001')}
        good = {'schema_version': 2, 'mode': 'longsword', 'models': models,
                'authored_contact_flag': False, 'source_offsets_modified': False,
                'source_rotations_modified': False, 'runtime_verified': False, 'visual_quality_certified': False,
                'provenance': 'reconstructed_from_actual_source_hilt_and_exact_target_geometry',
                'fade_seconds': .1, 'summary': {'model_character_pairs': 2, 'positive_weight_frames': 40}}
        motions = {slot: {'fps': 60, 'frames': 5} for slot in slots}
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'contact_masks.json'
            entries = {'assets/modes/longsword/contact_masks.json': path}
            def check(bank):
                path.write_text(json.dumps(bank), encoding='utf-8')
                return PACKAGE.validate_contact_masks(entries, motions, {'character'})
            self.assertEqual(check(good), 2)
            for weights in ([.5, 0, 0, 0, 0], [0, 0, -.5, 0, 0], [0, 0, 1.1, 0, 0], [0, .5, 0, 0]):
                invalid = copy.deepcopy(good)
                invalid['models']['wpn_misc_0016']['characters']['character']['tracks']['3']['weights'] = weights
                with self.subTest(weights=weights), self.assertRaises(ValueError):
                    check(invalid)
            for field in ('authored_contact_flag', 'source_offsets_modified', 'runtime_verified'):
                invalid = copy.deepcopy(good)
                invalid[field] = True
                with self.subTest(field=field), self.assertRaises(ValueError):
                    check(invalid)
            invalid = copy.deepcopy(good)
            invalid['models']['wpn_misc_0016']['characters']['character']['tracks']['3']['fps'] = 30
            with self.assertRaises(ValueError):
                check(invalid)

    def test_support_ground_rejects_wrong_motion_and_nonbinary_contact(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            path = root / 'support.json'
            clip = root / 'clip.lsm'
            clip.write_bytes(struct.pack('<4sIII', b'LSM1', 2, 21, 60))
            probe = {'side': 'left', 'influences': [{'bone_path': 'root/hand', 'bone_name': 'hand',
                       'weight': 1, 'local_point': [0, 0, 0]}]}
            profile = root / 'profile.json'
            profile.write_text(json.dumps({'ground_hand_probes': {'left': [probe] * 16,
                   'right': [{**probe, 'side': 'right'}] * 13}}), encoding='utf-8')
            entries = {'assets/modes/sword_shield/support_ground_tracks.json': path,
                       'assets/modes/sword_shield/clip_10011.lsm': clip,
                       'profiles/chr_0018_dapan.json': profile}
            manifest = {'schema_version': 1, 'authored_ground_contact_flag': False,
                        'source_root_and_rotations_modified': False,
                        'tracks': {'10011': {'fps': 60, 'frames': 2,
                                   'left': {'active': [0, 1], 'clearance': [1, 0]},
                                   'right': {'active': [0, 0], 'clearance': [1, 1]}}}}
            def check(value):
                path.write_text(json.dumps(value), encoding='utf-8')
                return PACKAGE.validate_hand_support(entries)
            self.assertEqual(check(manifest), 29)
            invalid = copy.deepcopy(manifest)
            invalid['tracks']['10011']['left']['active'][1] = 2
            with self.assertRaises(ValueError):
                check(invalid)
            invalid = copy.deepcopy(manifest)
            invalid['tracks']['10011']['fps'] = 30
            with self.assertRaises(ValueError):
                check(invalid)
            invalid = copy.deepcopy(manifest)
            invalid['tracks']['10011']['right']['clearance'] = [1]
            with self.assertRaises(ValueError):
                check(invalid)
            invalid = copy.deepcopy(manifest)
            invalid['tracks']['10011']['left']['kind'] = [3, 3]
            with self.assertRaises(ValueError):
                check(invalid)

    def test_hardware_and_hold_routes_close_over_real_source_clips(self):
        motions = {1: {'frames': 10}, 2: {'frames': 20}, 3: {'frames': 30}}
        moves = {'commands': {'reload': [1], 'spirit': [2], 'charge': [3]},
                 'hold_commands': {'spirit': {'frame': -1, 'loop': True, 'releases': []}},
                 'input_behavior': {'reload': {'tap': 'reload', 'hold': 'spirit', 'hold_seconds': .35}},
                 'hardware_routes': {'1': {'1': 'spirit'}},
                 'hold_variants': {'charge': [{'from_slots': [1], 'slot': 2, 'frame': -1, 'loop': True,
                                              'releases': [{'seconds': 0, 'slot': 3}]}]}}
        PACKAGE.validate_controls(moves, motions, 'test')
        for field, value in (('hardware_routes', {'99': {'1': 'spirit'}}),
                             ('hardware_routes', {'1': {'128': 'spirit'}}),
                             ('input_behavior', {'reload': {'tap': 'reload', 'hold': 'missing'}}),
                             ('hold_variants', {'charge': [{'from_slots': [1], 'slot': 2, 'frame': 20}]})):
            invalid = copy.deepcopy(moves)
            invalid[field] = value
            with self.subTest(field=field), self.assertRaises(ValueError):
                PACKAGE.validate_controls(invalid, motions, 'test')

    def test_physical_presets_do_not_certify_a_swapped_mouse_button(self):
        keys5 = {'attack': 1, 'secondary': 6, 'spirit': 6, 'dodge': 32, 'sheathe': 69, 'aim': 2, 'reload': 5}
        keys3 = {**keys5, 'secondary': 4, 'spirit': 4, 'reload': 82}
        good = {'id': 'bow', 'default_bindings': {'keys': keys5, 'verified': True,
                'source_url': 'https://game.capcom.com/manual/MHW_PC/en/steam/page/2/2',
                'verification_scope': 'Physical default keys only',
                'presets': {'mouse5': {'keys': keys5, 'verified': True},
                            'mouse3': {'keys': keys3, 'verified': True}}}}
        PACKAGE.validate_keyboard_presets([good])
        invalid = copy.deepcopy(good)
        invalid['default_bindings']['presets']['mouse5']['keys']['secondary'] = 5
        with self.assertRaises(ValueError):
            PACKAGE.validate_keyboard_presets([invalid])

    def test_packed_mount_bank_rejects_corrupt_samples_clocks_and_ranges(self):
        motions = {1: {'frames': 2, 'fps': 60}, 4: {'frames': 1, 'fps': 30}}
        values = [0.0] * 36
        values[3], values[7], values[14], values[21] = 1.0, .5, 1.0, 1.0
        good_frame = struct.pack('<36fII', *values, 1, 0)
        good_data = struct.pack('<4sIII', b'LSW1', 1, 2, 152) + good_frame * 3
        good = {'schema_version': 3, 'bank': 'source.lsw', 'tracks': {
                '1': {'fps': 60, 'frames': 2, 'offset': 16},
                '4': {'fps': 30, 'frames': 1, 'offset': 320}}}
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'source.lsw'
            entries = {'assets/modes/test/source.lsw': path}
            def check(manifest=good, data=good_data):
                path.write_bytes(data)
                return PACKAGE.validate_packed_weapon(entries, 'assets/modes/test', manifest, motions)
            self.assertEqual(check(), 2)
            invalid_manifests = []
            for field, value in (('fps', 59), ('frames', 3), ('offset', 17), ('offset', True)):
                invalid = copy.deepcopy(good)
                invalid['tracks']['1'][field] = value
                invalid_manifests.append(invalid)
            invalid = copy.deepcopy(good)
            invalid['tracks']['4']['offset'] = 319
            invalid_manifests.append(invalid)
            invalid = copy.deepcopy(good)
            invalid['bank'] = '../source.lsw'
            invalid_manifests.append(invalid)
            invalid = copy.deepcopy(good)
            invalid['tracks']['01'] = invalid['tracks'].pop('1')
            invalid_manifests.append(invalid)
            for invalid in invalid_manifests:
                with self.subTest(manifest=invalid), self.assertRaises(ValueError):
                    check(invalid)
            corrupt = [good_data[:-1], good_data + b'\0',
                       struct.pack('<4sIII', b'LSW1', 1, 3, 152) + good_data[16:]]
            for index, value in ((0, float('nan')), (3, 0.0), (4, 3.1), (7, 1.1), (14, 0.0), (15, 3.1)):
                damaged = bytearray(good_data)
                struct.pack_into('<f', damaged, 16 + index * 4, value)
                corrupt.append(bytes(damaged))
            damaged = bytearray(good_data)
            struct.pack_into('<I', damaged, 16 + 36 * 4, 2)
            corrupt.append(bytes(damaged))
            for index, damaged in enumerate(corrupt):
                with self.subTest(corruption=index), self.assertRaises(ValueError):
                    check(data=damaged)

    def test_packed_grip_bank_checks_complete_original_lsg_payloads(self):
        motions = {1: {'frames': 2, 'fps': 60}, 4: {'frames': 1, 'fps': 30}}
        sample = struct.pack('<4f', .1, -.2, .3, .5)
        first = struct.pack('<4sIII', b'LSG1', 2, 60, 4) + sample * 2
        second = struct.pack('<4sIII', b'LSG1', 1, 30, 4) + sample
        good_data = struct.pack('<4sIII', b'LGB1', 1, 2, 0) + first + second
        good = {'schema_version': 2, 'bank': 'grips.lgb', 'tracks': {
                '1': {'offset': 16, 'size': 48, 'source_file': 'original_1.lsg'},
                '4': {'offset': 64, 'size': 32, 'source_file': 'original_4.lsg'}}}
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'grips.lgb'
            entries = {'assets/modes/test/grips.lgb': path}
            def check(manifest=good, data=good_data):
                path.write_bytes(data)
                return PACKAGE.validate_packed_grip(entries, 'assets/modes/test', manifest, motions)
            self.assertEqual(check(), 2)
            for field, value in (('offset', 15), ('size', 49), ('size', 4 * 1024 * 1024 + 1)):
                invalid = copy.deepcopy(good)
                invalid['tracks']['1'][field] = value
                with self.subTest(field=field, value=value), self.assertRaises(ValueError):
                    check(invalid)
            invalid = copy.deepcopy(good)
            invalid['tracks']['4']['offset'] = 65
            with self.assertRaises(ValueError):
                check(invalid)
            corrupt = [good_data[:-1], good_data + b'\0',
                       struct.pack('<4sIII', b'LGB1', 1, 2, 1) + good_data[16:]]
            for at, format_string, value in ((16 + 8, '<I', 59), (16 + 12, '<I', 5),
                                             (32, '<f', float('nan')), (32, '<f', 3.1),
                                             (32 + 12, '<f', -.1)):
                damaged = bytearray(good_data)
                struct.pack_into(format_string, damaged, at, value)
                corrupt.append(bytes(damaged))
            for index, damaged in enumerate(corrupt):
                with self.subTest(corruption=index), self.assertRaises(ValueError):
                    check(data=damaged)

    def test_actual_eml_import_limits_apply_to_final_payload_sizes(self):
        self.assertEqual(PACKAGE.validate_loader_limits({'a': 128 * 1024 * 1024,
                                                        'b': 128 * 1024 * 1024}), 256 * 1024 * 1024)
        self.assertEqual(PACKAGE.validate_loader_limits({str(index): 0 for index in range(4096)}), 0)
        for sizes in ({str(index): 0 for index in range(4097)},
                      {'a': 128 * 1024 * 1024 + 1},
                      {'a': 128 * 1024 * 1024, 'b': 128 * 1024 * 1024, 'c': 1},
                      {'module.json': 256 * 1024 + 1}, {'a': -1}, {'a': True},
                      {'assets/A.json': 1, 'assets/a.json': 1}, {'a' * 241: 1},
                      {'folder//file': 1}, {'folder/CON.txt': 1}, {'folder/file.': 1}):
            with self.subTest(keys=list(sizes)[:3]), self.assertRaises(ValueError):
                PACKAGE.validate_loader_limits(sizes)

    def test_minified_json_keeps_numeric_values_and_actual_size(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'values.json'
            original = {'中文': [1.2345678901234567, -0.0, 1e-20], 'count': 3}
            path.write_text(json.dumps(original, ensure_ascii=False, indent=2), encoding='utf-8')
            payloads = PACKAGE.final_payloads({'values.json': path, 'ui/embedded.js': b'globals; '})
            self.assertEqual(json.loads(payloads['values.json']), original)
            self.assertLess(len(payloads['values.json']), path.stat().st_size)
            self.assertEqual(payloads['ui/embedded.js'], b'globals; ')
            self.assertEqual(PACKAGE.validate_loader_limits({name: len(value) for name, value in payloads.items()}),
                             sum(len(value) for value in payloads.values()))


if __name__ == '__main__':
    unittest.main()
