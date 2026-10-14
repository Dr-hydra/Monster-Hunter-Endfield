"""Keep source motion identities separate from keyboard and conditional routing."""


def correct_control_metadata(mode, moves, offset=0):
    commands, inputs, chords = moves['commands'], moves['inputs'], moves['chords']
    if mode == 'greatsword':
        # Nine sequential charge/release pairs traverse the three source hold
        # phases at each of the three local display levels. Runtime grouping
        # selects basic -> strong -> true, then advances the display level.
        moves['demo'] = ['slash', 'slash', 'slash', 'thrust', 'guard', 'counter'] + \
            ['charge', 'release'] * 9 + \
            ['spirit', 'spirit', 'spirit', 'special', 'special', 'special', 'dodge', 'dodge', 'normal_sheathe']
        commands['charge'] = [offset + 144, offset + 145, offset + 245]
        moves['labels'][str(offset + 145)] = '强蓄力保持'
        moves['labels'][str(offset + 245)] = '真蓄力稳持'
        commands['thrust'] = [offset + 106]
        commands['advance'] = [offset + 107]
        chords['3'] = 'advance'
        chords.pop('6', None)
        moves['hold_variants'] = {'charge': [
            {'from_slots': [offset + slot for slot in (152, 153, 154)], 'slot': offset + 145,
             'frame': 52, 'loop': True,
             'releases': [{'seconds': seconds, 'slot': offset + slot} for seconds, slot in ((0, 155), (.65, 156), (1.25, 157))]},
            {'from_slots': [offset + slot for slot in (155, 156, 157)], 'slot': offset + 245,
             'frame': 113, 'loop': False,
             'releases': [{'seconds': seconds, 'slot': offset + slot} for seconds, slot in ((0, 146), (.65, 189), (1.25, 190))]}
        ]}
        moves['charge_stage_evidence'] = {
            'source_at_rows': [[115, 144, 114], [123, 145, 122], [246, 245, 247]],
            'source_at_file': 'hm/wp/wp00/wp00_tree.at',
            'holds': {'basic': offset + 144, 'strong': offset + 145, 'true': offset + 245},
            'strong_release_pose_rms_m': .00277, 'true_release_pose_rms_m': .00030,
            'release_threshold_scope': '0/.65/1.25 seconds are module display levels, not original Focus, stamina or native timing certification.',
            'source_rotations_modified': False}
    elif mode == 'lance':
        chords['3'] = 'spirit'
        chords.pop('12', None)
        moves['unresolved_control_actions'] = ['Power Guard source pose and startup mapping are not yet certified; slot 174 is a finisher, not Power Guard.']
    elif mode == 'sword_shield':
        chords.pop('3', None)
        chords.pop('6', None)
        moves['conditional_control_evidence'] = {'perfect_rush': {
            'after': ['backstep', 'slinger_burst'], 'input': 'attack',
            'source_gmd_label': 'S_katate_TEXT2', 'slots': [offset + slot for slot in (344, 345, 346, 341, 342)],
            'scope': 'Source entry/continuation conditions; runtime routing handled separately.'}}
        moves['hardware_routes'] = {str(offset + slot): {'1': 'special'} for slot in (110, 344, 345, 346, 341)}
    elif mode == 'light_bowgun':
        inputs['thrust'] = 'special'
        inputs.pop('spirit', None)
        chords.clear()
        moves['conditional_control_evidence'] = {'reload_all': {
            'input': 'reload', 'behavior': 'hold', 'tap_slot': offset + 102, 'hold_slot': offset + 107,
            'source_gmd_label': 'lgun1', 'scope': 'PAD_RU maps to reload, not attack, for ranged weapons.'}}
        moves['input_behavior'] = {'reload': {'tap': 'reload', 'hold': 'spirit', 'hold_seconds': .35,
                                            'threshold_scope': 'Module long-press threshold; native frame threshold not certified.'}}
        moves['hold_commands']['spirit'] = {'frame': -1, 'loop': True, 'releases': []}
    elif mode == 'bow':
        inputs.pop('spirit', None)
        chords.pop('6', None)
        chords.pop('12', None)
        moves['experimental_commands'] = sorted(set(moves.get('experimental_commands', [])) | {'reload'})
        moves['unresolved_control_actions'] = ['Coating button is verified; slot 164 currently shows arrow handling, not certified native coating animation.',
            'Dragon Piercer keyboard combo remains a presentation shortcut until the source GUI combo icons are decoded.']
    moves['control_verification'] = {
        'physical_default_mapping_verified': True,
        'weapon_specific_combinations_fully_verified': False,
        'source_keyboard_manual': 'https://game.capcom.com/manual/MHW_PC/en/steam/page/2/2',
        'source_controller_manual': 'https://game.capcom.com/manual/MHW_PC/en/steam/page/2/1',
        'source_hunter_notes': 'common/text/weaponTutorial_eng.gmd',
        'source_hunter_notes_evidence': 'research/keyboard-defaults/decoded-text/six-weapon-control-facts.json',
        'limitation': 'Basic label/button associations use source action identities; hard combo icons outside GMD and native timing thresholds are not certified by physical preset verification.'}
    return moves
