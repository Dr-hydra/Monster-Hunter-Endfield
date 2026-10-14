"""Read-only source controller proofs from the locally derived MHW executable.

The original Steam executable is never modified or executed. Steamless 3.1.0.5
from atom0s/Steamless was used once on an ignored research copy. These checks
anchor native class factories through source reflection registrations and read
original machine instructions; they do not infer LMT slots from FSM enum math.
"""
from pathlib import Path
import json
import re
import struct

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'research/multiweapon/executable/MonsterHunterWorld.exe.unpacked.exe'


def probe():
    import capstone
    import pefile
    raw=SOURCE.read_bytes();pe=pefile.PE(data=raw,fast_load=True)
    base=pe.OPTIONAL_HEADER.ImageBase
    decoder=capstone.Cs(capstone.CS_ARCH_X86,capstone.CS_MODE_64);decoder.detail=True
    def instructions(address,count=300):
        offset=pe.get_offset_from_rva(address-base)
        return list(decoder.disasm(raw[offset:offset+count],address))
    def check(address,mnemonic,operand):
        instruction=instructions(address,16)[0]
        assert instruction.mnemonic==mnemonic and instruction.op_str==operand, f'Native source differs at {address:x}'
        return dict(address=hex(address),mnemonic=mnemonic,operand=operand)
    section=next(s for s in pe.sections if s.Name.startswith(b'.text'))
    code=section.get_data();code_base=base+section.VirtualAddress
    class_names=[
        'nHmAction::Wp06::cChargeStart','nHmAction::Wp06::cChargeStartStab','nHmAction::Wp06::cChargeEndStab',
        'nHmAction::Wp06::cChargeCounterS_Stab','nHmAction::Wp06::cChargeCounterS_StabUp',
        'nHmAction::Wp06::cChargeCounterM_Stab','nHmAction::Wp06::cChargeCounterM_StabUp',
        'nHmAction::Wp06::cChargeCounterL_Guard','nHmAction::Wp11::cShootNormal','nHmAction::Wp11::cShootGosya',
        'nHmAction::Wp01::cActRush0_ShieldBash','nHmAction::Wp01::cActRush0_ShieldBash_Just',
        'nHmAction::Wp01::cActRush3_ShieldUpper','nHmAction::Wp01::cActRush3_ShieldUpper_Just',
        'nHmAction::Wp01::cActRush4_ConstStab','nHmAction::Wp01::cActRush4_ConstStab_Just',
        'nHmAction::Wp01::cActRush5_ConstStabEmStick','nHmAction::Wp01::cActRush5_ConstStabEmStick_Just',
    ]+['nHmTransition::cWp01Rush'+str(i) for i in range(6)]+['nHmTransition::cWp01RushBase','rWp11Param','uWp11']
    addresses={}
    for name in class_names:
        at=raw.find(name.encode()+b'\0');assert at>=0,name
        addresses[base+pe.get_rva_from_offset(at)]=name
    classes={}
    for match in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]',code):
        offset=match.start();target=code_base+offset+7+struct.unpack_from('<i',code,offset+3)[0]
        if target not in addresses:continue
        registration=instructions(code_base+offset,70)
        metadata=next(i for i in registration if i.mnemonic=='lea' and i.op_str.startswith('rax, [rip'))
        table=metadata.address+metadata.size+metadata.operands[1].mem.disp
        methods=struct.unpack_from('<4Q',raw,pe.get_offset_from_rva(table-base))
        classes[addresses[target]]=dict(registration=hex(code_base+offset),metadata=hex(table),factory=hex(methods[1]),method_addresses=[hex(a) for a in methods])
    assert set(classes)==set(class_names),'Native reflection class coverage changed'
    motions={
        'cChargeStart':(120,0x14181e3f1,'dword ptr [rbx + 0x30], 0xc078'),
        'cChargeStartStab':(122,0x14181e521,'dword ptr [rbx + 0xcd0], 0xc07a'),
        'cChargeEndStab':(121,0x14181ec41,'dword ptr [rbx + 0x30], 0xc079'),
        'cChargeCounterS_Stab':(126,0x14181e651,'dword ptr [rbx + 0x30], 0xc07e'),
        'cChargeCounterS_StabUp':(123,0x14181e781,'dword ptr [rbx + 0x30], 0xc07b'),
        'cChargeCounterM_Stab':(124,0x14181e8b1,'dword ptr [rbx + 0x30], 0xc07c'),
        'cChargeCounterM_StabUp':(127,0x14181e9e1,'dword ptr [rbx + 0x30], 0xc07f'),
        'cChargeCounterL_Guard':(125,0x14181eb11,'dword ptr [rbx + 0x30], 0xc07d'),
        'cActRush0_ShieldBash':(345,0x1417dd3a1,'dword ptr [rbx + 0x30], 0xc159'),
        'cActRush0_ShieldBash_Just':(347,0x1417dd5d1,'dword ptr [rbx + 0x30], 0xc15b'),
        'cActRush3_ShieldUpper':(346,0x1417dd831,'dword ptr [rbx + 0x30], 0xc15a'),
        'cActRush3_ShieldUpper_Just':(348,0x1417dda91,'dword ptr [rbx + 0x30], 0xc15c'),
        'cActRush4_ConstStab':(319,0x1417ddd2b,'dword ptr [rbx - 0xca8], 0xc13f'),
        'cActRush4_ConstStab_Just':(341,0x1417ddef1,'dword ptr [rbx + 0x30], 0xc155'),
    }
    motion_proofs={}
    for name,(slot,address,operand) in motions.items():
        class_name=next(k for k in classes if k.endswith('::'+name))
        methods=[int(a,16) for a in classes[class_name]['method_addresses']]
        assert any(start<=address<start+0x200 for start in methods[1:]),class_name+' constructor association'
        motion_proofs[name]=dict(source_slot=slot,proof=check(address,'mov',operand),source_class=class_name)
    short=[check(0x141822115,'mov','edx, dword ptr [rbx + 0xcd0]'),
        check(0x141824363,'mov','rax, qword ptr [rcx]'),
        check(0x141824366,'call','qword ptr [rax + 0x2b0]'),
        check(0x141824381,'mov','edx, 0xc06a')]
    rush=[check(0x140f0e461,'call','0x141f62070'),check(0x140f0e4b5,'call','0x1417be130'),
        check(0x140f0e4be,'mov','eax, dword ptr [rdi + 0x10]'),check(0x140f0e4c1,'shr','eax, 0x1b'),
        check(0x140f0e4c4,'and','eax, 1'),check(0x140f0e4c7,'inc','eax'),
        check(0x140f0e4c9,'mov','dword ptr [rbx + 0x3c], eax'),check(0x140f0e4cf,'shr','ecx, 0x1c'),
        check(0x140f0e4d5,'jne','0x140f0e4dc')]
    arrows=[check(0x141383a5c,'lea','rdx, [rbp + 0x8f8]'),check(0x141383a70,'lea','rdx, [rbp + 0x908]'),
        check(0x141383a84,'lea','rdx, [rbp + 0x918]'),
        check(0x1421436a3,'mov','edx, dword ptr [r10 + rdx*4 + 0x8f8]'),
        check(0x142142ce3,'mov','edx, dword ptr [r10 + rdx*4 + 0x908]'),
        check(0x14214392b,'mov','edx, dword ptr [r10 + rdx*4 + 0x918]'),
        check(0x1418669f3,'mov','edx, 0xc07d'),check(0x14186648e,'mov','edx, 0xc088')]
    charge=[check(0x141383896,'lea','rdx, [rbp + 0x800]'),check(0x1413838ac,'lea','rdx, [rbp + 0x810]'),
        check(0x142144059,'movss','xmm1, dword ptr [rdx + rax*4 + 0x800]'),
        check(0x142144070,'movss','xmm2, dword ptr [rdx + r8*4 + 0x800]')]
    collision=[check(0x14211dfa7,'lea','r8, [rip + 0x13b545a]'),check(0x14211dfdf,'mov','r8d, 3'),
        check(0x14211dfe5,'mov','rdx, rbx'),check(0x14211dfe8,'call','0x1412deac0'),
        check(0x1412deafe,'lea','rdi, [rsi + r14*8]'),check(0x1412deb18,'mov','qword ptr [rdi + 0xd0], rbx'),
        check(0x1412deb9a,'cmp','dword ptr [rax + 0x20], -1'),check(0x1412deba0,'mov','r9d, 0xffffffff')]
    instruction=instructions(0x14211dfa7,7)[0]
    target=instruction.address+instruction.size+instruction.operands[1].mem.disp
    offset=pe.get_offset_from_rva(target-base);end=raw.index(0,offset)
    assert raw[offset:end]==b'hm\\wp\\wp01\\collision\\wp01_02'
    return dict(source=SOURCE.relative_to(ROOT).as_posix(),source_bytes=len(raw),pe_timestamp=pe.FILE_HEADER.TimeDateStamp,
        classes=classes,source_motion_classes=motion_proofs,lance_short_release=dict(source_slots=[122,106],proof=short),
        perfect_rush=dict(just_bit=27,input_gate_bit=28,latch_scope='button_predicate_at_input_then_wait_for_gate',proof=rush),
        sword_shield_constraint_attack=dict(source='hm/wp/wp01/collision/wp01_02.col',bank=3,
            native_bone_binding_known=False,names_attachment_override=-1,proof=collision),
        bow=dict(normal_slot=125,power_slot=136,normal_arrow_count_row=0,power_arrow_count_row=1,third_arrow_row=2,
                 arrow_row_proof=arrows,charge_threshold_row=0,charge_proof=charge),game_launched=False,hashes_computed=False)


if __name__=='__main__':
    result=probe();destination=ROOT/'research/mhw-original/combat-native-controller-proof.json'
    destination.write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
    print(json.dumps(dict(native_reflection_classes=len(result['classes']),source_motion_classes=len(result['source_motion_classes']),
        short_release=result['lance_short_release']['source_slots'],perfect_rush_bits=[27,28],bow_arrow_count_rows=[0,1,2],passed=True)))
