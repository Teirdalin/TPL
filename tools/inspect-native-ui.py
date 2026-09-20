"""Read-only MSVC RTTI/code evidence from the user's game, not an RVA database."""
import argparse
import bisect
import hashlib
import json
import re
import struct
from pathlib import Path

import capstone
import pefile


def occurrences(data, needle):
    offset = data.find(needle)
    while offset >= 0:
        yield offset
        offset = data.find(needle, offset + 1)


def inspect(path, class_name="LoadSaveWindow"):
    data = path.read_bytes()
    pe = pefile.PE(data=data, fast_load=True)
    if pe.FILE_HEADER.Machine != 0x8664:
        raise ValueError("Expected x64 game")
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY['IMAGE_DIRECTORY_ENTRY_EXCEPTION']])
    base = pe.OPTIONAL_HEADER.ImageBase
    functions = [(e.struct.BeginAddress, e.struct.EndAddress) for e in pe.DIRECTORY_ENTRY_EXCEPTION]
    starts = [f[0] for f in functions]
    text = next(s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text')
    code = text.get_data()
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    result = {"gameSha256": hashlib.sha256(data).hexdigest(), "imageBase": hex(base),
              "source": "Game PE RTTI and instructions only; candidates are not approved bindings", "types": []}
    pattern = rb'\.\?AV[^\x00]{0,300}' + re.escape(class_name.encode('ascii')) + rb'[^\x00]{0,300}\x00'
    for match in re.finditer(pattern, data):
        name = match.group()[:-1].decode('ascii')
        descriptor = pe.get_rva_from_offset(match.start() - 16)
        item = {"name": name, "typeRva": hex(descriptor), "vtables": []}
        for ref in occurrences(data, struct.pack('<I', descriptor)):
            col = ref - 12
            if col < 0 or col + 24 > len(data):
                continue
            signature, offset, _, _, _, self_rva = struct.unpack_from('<6I', data, col)
            if signature != 1 or self_rva != pe.get_rva_from_offset(col):
                continue
            for vref in occurrences(data, struct.pack('<Q', base + self_rva)):
                table = pe.get_rva_from_offset(vref + 8)
                evidence = {"rva": hex(table), "objectOffset": offset, "creationSites": []}
                # RIP-relative LEA, REX.W variants; verify candidates by decoding
                # the containing unwind function rather than trusting byte matches.
                for lea in re.finditer(rb'[\x48\x4c]\x8d[\x05\x0d\x15\x1d\x25\x2d\x35\x3d]', code):
                    start = lea.start()
                    if start + 7 > len(code):
                        continue
                    rva = text.VirtualAddress + start
                    target = rva + 7 + struct.unpack_from('<i', code, start + 3)[0]
                    if target != table:
                        continue
                    idx = bisect.bisect_right(starts, rva) - 1
                    if idx < 0 or rva >= functions[idx][1]:
                        continue
                    begin, end = functions[idx]
                    instructions = list(decoder.disasm(pe.get_data(begin, end - begin), base + begin))
                    positions = [i for i, ins in enumerate(instructions) if ins.address == base + rva]
                    if not positions:
                        continue
                    pos = positions[0]
                    evidence['creationSites'].append({"functionRva": hex(begin), "referenceRva": hex(rva),
                        "instructions": [f'{ins.address - base:08x}: {ins.mnemonic} {ins.op_str}'
                                         for ins in instructions[max(0, pos-14):pos+20]]})
                item['vtables'].append(evidence)
        result['types'].append(item)
    return result


def inspect_functions(path, addresses):
    data = path.read_bytes()
    pe = pefile.PE(data=data)
    base = pe.OPTIONAL_HEADER.ImageBase
    imports = {entry.address: entry.name.decode('ascii') if entry.name else str(entry.ordinal)
               for dll in pe.DIRECTORY_ENTRY_IMPORT for entry in dll.imports}
    functions = {e.struct.BeginAddress: e.struct.EndAddress for e in pe.DIRECTORY_ENTRY_EXCEPTION}
    decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
    decoder.detail = True
    result = {"gameSha256": hashlib.sha256(data).hexdigest(), "functions": []}
    for address in addresses:
        thunk = address
        seen = set()
        while pe.get_data(address, 1) == b'\xe9':
            if address in seen or len(seen) > 16:
                raise ValueError('Invalid thunk chain')
            seen.add(address)
            address += 5 + struct.unpack('<i', pe.get_data(address + 1, 4))[0]
        if address not in functions:
            raise ValueError(f'No unwind function at {address:x}')
        blob = pe.get_data(address, functions[address] - address)
        lines = []
        for ins in decoder.disasm(blob, base + address):
            line = f'{ins.address-base:08x}: {ins.mnemonic} {ins.op_str}'
            for operand in ins.operands:
                if operand.type == capstone.x86.X86_OP_MEM and operand.mem.base == capstone.x86.X86_REG_RIP:
                    target = ins.address + ins.size + operand.mem.disp
                    if target in imports:
                        line += ' ; ' + imports[target]
                    else:
                        raw = pe.get_data(target-base, 160).split(b'\0', 1)[0]
                        if len(raw) >= 4 and all(32 <= c < 127 for c in raw):
                            line += ' ; ' + repr(raw.decode('ascii'))
            lines.append(line)
        result['functions'].append({"requestedRva": hex(thunk), "bodyRva": hex(address),
                                    "entry32": blob[:32].hex(), "instructions": lines})
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('game', type=Path)
    parser.add_argument('output', type=Path)
    parser.add_argument('--function', type=lambda s: int(s, 0), action='append')
    args = parser.parse_args()
    report = inspect_functions(args.game, args.function) if args.function else inspect(args.game)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(f"Recorded native binary evidence: {args.output}")
