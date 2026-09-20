"""Generate reviewed bindings from game instructions, without upstream RVA data.

Recipes describe evidence relationships, not function addresses. Discovery is
hash-gated and only the explicitly reviewed four-function recipe is supported.
This is disassembly automation, not recovery of original C++ source or types.
"""
import argparse
import hashlib
import importlib.util
import json
import re
import struct
from pathlib import Path

import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG, X86_REG_RIP
import pefile

spec = importlib.util.spec_from_file_location('native_inspector', Path(__file__).with_name('inspect-native-ui.py'))
inspector = importlib.util.module_from_spec(spec)
spec.loader.exec_module(inspector)


def unique(values, description):
    values = set(values)
    if len(values) != 1:
        raise ValueError(f'{description}: expected one result, found {len(values)}')
    return values.pop()


def canonical(name):
    aliases = {'eax': 'rax', 'ebx': 'rbx', 'ecx': 'rcx', 'edx': 'rdx',
               'esi': 'rsi', 'edi': 'rdi', 'ebp': 'rbp', 'esp': 'rsp'}
    for full, partials in [('rax', ('ax', 'al', 'ah')), ('rbx', ('bx', 'bl', 'bh')),
                           ('rcx', ('cx', 'cl', 'ch')), ('rdx', ('dx', 'dl', 'dh')),
                           ('rsi', ('si', 'sil')), ('rdi', ('di', 'dil')),
                           ('rbp', ('bp', 'bpl')), ('rsp', ('sp', 'spl'))]:
        aliases.update({part: full for part in partials})
    return aliases.get(name, re.sub(r'^(r\d+)[dwb]$', r'\1', name))


def constant(instructions, before, register, depth=0):
    """Bounded backwards slice for reviewed LEA/MOV argument setup patterns."""
    if depth > 8:
        return None
    register = canonical(register)
    for pos in range(before - 1, max(-1, before - 40), -1):
        ins = instructions[pos]
        if ins.mnemonic == 'call' and register in ('rax', 'rcx', 'rdx', 'r8', 'r9', 'r10', 'r11'):
            return None
        _, writes = ins.regs_access()
        if register not in [canonical(ins.reg_name(r)) for r in writes]:
            continue
        operands = ins.operands
        if len(operands) != 2 or operands[0].type != X86_OP_REG:
            return None
        if operands[0].size < 4:
            return None
        source = operands[1]
        if ins.mnemonic == 'lea' and source.type == X86_OP_MEM and source.mem.base == X86_REG_RIP:
            return ins.address + ins.size + source.mem.disp
        if ins.mnemonic == 'mov':
            if source.type == X86_OP_IMM:
                return source.imm
            if source.type == X86_OP_REG:
                return constant(instructions, pos, ins.reg_name(source.reg), depth + 1)
        return None
    return None


def delegate_methods(instructions, table):
    targets = set()
    for index, ins in enumerate(instructions):
        op = ins.operands
        if ins.mnemonic != 'mov' or len(op) != 2 or op[0].type != X86_OP_MEM or op[1].type != X86_OP_REG:
            continue
        slot = op[0].mem
        if slot.disp != 0 or slot.index or not slot.base:
            continue
        if constant(instructions, index, ins.reg_name(op[1].reg)) != table:
            continue
        base = canonical(ins.reg_name(slot.base))
        # Pinned MIT MyGUI CMethodDelegate: vptr, unlink, owner, method.
        for offset, later in enumerate(instructions[index+1:index+13], index+1):
            if later.mnemonic in ('call', 'jmp', 'ret'):
                break
            operands = later.operands
            if later.mnemonic == 'mov' and len(operands) == 2 and operands[0].type == X86_OP_MEM:
                field = operands[0].mem
                if field.disp == 24 and not field.index and canonical(later.reg_name(field.base)) == base:
                    if operands[1].type == X86_OP_REG:
                        value = constant(instructions, offset, later.reg_name(operands[1].reg))
                        if value is not None:
                            targets.add(value)
                    break
            if base in [canonical(later.reg_name(r)) for r in later.regs_access()[1]]:
                break
    return targets


def validate_recipe(recipe, digest):
    if recipe.get('schema') != 1 or recipe.get('class') != 'LoadSaveWindow':
        raise ValueError('Unsupported discovery recipe')
    if digest.lower() != recipe.get('gameSha256', '').lower():
        raise ValueError('Unsupported game SHA-256; no bindings generated')
    rows = recipe.get('bindings', [])
    expected = {1: 'constructor', 2: 'closeDelegate', 3: 'keyDelegate', 4: 'confirmationCall'}
    if len(rows) != 4 or {b['id']: b['discovery'] for b in rows} != expected:
        raise ValueError('Unreviewed or duplicate binding IDs')
    if any(not re.fullmatch(r'[A-Za-z][A-Za-z0-9_:]*', b['name']) for b in rows):
        raise ValueError('Invalid binding name')
    if recipe.get('runtimeVerified') is not False:
        raise ValueError('Static analysis cannot claim live validation')


def discover(game, recipe):
    data = game.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    validate_recipe(recipe, digest)
    pe = pefile.PE(data=data)
    if pe.FILE_HEADER.Machine != 0x8664:
        raise ValueError('Only the reviewed x64 ABI is supported')
    base = pe.OPTIONAL_HEADER.ImageBase
    extents = {e.struct.BeginAddress: e.struct.EndAddress for e in pe.DIRECTORY_ENTRY_EXCEPTION}
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    decoder.detail = True

    def body(rva):
        seen = set()
        while pe.get_data(rva, 1) == b'\xe9':
            if rva in seen or len(seen) >= 16:
                raise ValueError('Invalid jump-thunk chain')
            seen.add(rva)
            rva += 5 + struct.unpack('<i', pe.get_data(rva + 1, 4))[0]
        if rva not in extents:
            raise ValueError(f'No function extent for {rva:x}')
        return rva

    def decode(rva):
        blob = pe.get_data(rva, extents[rva] - rva)
        instructions = list(decoder.disasm(blob, base + rva))
        if not instructions or sum(i.size for i in instructions) != len(blob):
            raise ValueError('Incomplete function disassembly')
        return instructions

    evidence = inspector.inspect(game, recipe['class'])
    types = {t['name']: t for t in evidence['types']}

    def table(name):
        return unique((int(t['rva'], 16) for t in types[name]['vtables'] if t['objectOffset'] == 0),
                      'delegate vtable')

    def creators(name):
        return {int(s['functionRva'], 16) for t in types[name]['vtables'] for s in t['creationSites']}

    constructor = unique(creators(recipe['ownerType']) & creators(recipe['keyDelegateType']), 'constructor')
    instructions = decode(constructor)
    key = body(unique(delegate_methods(instructions, base + table(recipe['keyDelegateType'])), 'key callback') - base)
    close = body(unique(delegate_methods(instructions, base + table(recipe['closeDelegateType'])), 'close callback') - base)
    keys = decode(key)
    references = set()
    for ins in keys:
        for operand in ins.operands:
            if operand.type == X86_OP_MEM and operand.mem.base == X86_REG_RIP:
                references.add(ins.address + ins.size + operand.mem.disp - base)
    strings = {pe.get_data(rva, 160).split(b'\0', 1)[0].decode('ascii', errors='replace') for rva in references}
    if not set(recipe['keyStrings']).issubset(strings):
        raise ValueError('Native active-save refusal/confirmation evidence missing')
    if not any(i.mnemonic == 'cmp' and len(i.operands) == 2 and i.operands[0].type == X86_OP_REG
               and i.reg_name(i.operands[0].reg) == 'r8d' and i.operands[1].type == X86_OP_IMM
               and i.operands[1].imm == 0xd3 for i in keys):
        raise ValueError('Delete-key ABI evidence missing')
    calls = [body(i.operands[0].imm - base) for pos, i in enumerate(keys)
             if i.mnemonic == 'call' and i.operands[0].type == X86_OP_IMM
             and constant(keys, pos, 'r9') == 6]
    message = unique(calls, 'Yes/No message call')
    found = {'constructor': constructor, 'keyDelegate': key, 'closeDelegate': close, 'confirmationCall': message}
    rows = []
    for binding in recipe['bindings']:
        rva = found[binding['discovery']]
        decode(rva)
        blob = pe.get_data(rva, extents[rva] - rva)
        if len(blob) < 32:
            raise ValueError('Function is too short for the reviewed entry check')
        rows.append(dict(binding, rva=rva, expected32=blob[:32].hex(), functionSha256=hashlib.sha256(blob).hexdigest(),
                         functionSize=len(blob), binaryVerified=True, runtimeVerified=False))
    return {'schema': 1, 'gameSha256': digest, 'recipe': recipe, 'bindings': rows, 'rttiEvidence': evidence}


def generate(report):
    rows = report['bindings']
    header = '// Generated from independently reviewed recipes; do not edit.\n'
    header += f'#define TPL_ENGINE_GAME_SHA "{report["gameSha256"]}"\n'
    header += '\n'.join(f'TPL_ENGINE_BINDING({b["id"]}, "{b["name"]}", 0x{b["rva"]:x}, "{b["expected32"]}")' for b in rows)
    return header + '\n'


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--recipe', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    recipe = json.loads(args.recipe.read_text(encoding='utf-8'))
    report = discover(args.game, recipe)
    report['generatorSha256'] = hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
    report['inspectorSha256'] = hashlib.sha256(Path(inspector.__file__).read_bytes()).hexdigest()
    report['analysisTools'] = {'capstone': capstone.__version__, 'pefile': pefile.__version__}
    header = generate(report)
    args.output.mkdir(parents=True, exist_ok=True)
    for name, contents in [('bindings.json', json.dumps(report, indent=2) + '\n'), ('tpllib_engine_profile.inc', header)]:
        temporary = args.output / (name + '.tmp')
        temporary.write_text(contents, encoding='utf-8')
        temporary.replace(args.output / name)
    print(f'Generated {len(report["bindings"])} binary-reviewed bindings; runtime verification pending: {args.output}')
