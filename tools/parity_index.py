"""Checkpointed, read-only PE analysis. Discoveries are not approved APIs."""
import hashlib
import json
import re
import sqlite3
import struct
from pathlib import Path

import capstone
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
import pefile


def digest(path):
    with Path(path).open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def identity(game):
    return {'gameSha256': digest(game), 'analyzerSha256': digest(__file__),
            'capstone': capstone.__version__, 'pefile': pefile.__version__, 'schema': 1}


def metadata(db, key, value=None):
    if value is not None:
        db.execute('INSERT OR REPLACE INTO meta VALUES (?,?)', (key, json.dumps(value)))
    row = db.execute('SELECT value FROM meta WHERE key=?', (key,)).fetchone()
    return json.loads(row[0]) if row else None


def create_database(path, stamp):
    db = sqlite3.connect(path)
    db.executescript('''
        CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS functions(rva INTEGER PRIMARY KEY,end_rva INTEGER,
            sha256 TEXT,entry32 TEXT,complete INTEGER,reason TEXT);
        CREATE TABLE IF NOT EXISTS refs(owner INTEGER,site INTEGER,kind TEXT,target INTEGER,
            PRIMARY KEY(owner,site,kind,target));
        CREATE INDEX IF NOT EXISTS refs_target ON refs(target,kind);
        CREATE TABLE IF NOT EXISTS strings(rva INTEGER PRIMARY KEY,text TEXT);
        CREATE TABLE IF NOT EXISTS types(rva INTEGER PRIMARY KEY,name TEXT);
        CREATE TABLE IF NOT EXISTS vtables(rva INTEGER PRIMARY KEY,type_rva INTEGER,object_offset INTEGER);
        CREATE TABLE IF NOT EXISTS slots(table_rva INTEGER,slot INTEGER,target INTEGER,
            PRIMARY KEY(table_rva,slot));
        CREATE TABLE IF NOT EXISTS imports(rva INTEGER PRIMARY KEY,dll TEXT,name TEXT);
    ''')
    previous = metadata(db, 'identity')
    if previous is not None and previous != stamp:
        db.close()
        raise ValueError('Index identity differs; choose a new cache directory')
    metadata(db, 'identity', stamp)
    db.commit()
    return db


def open_index(path, game_sha=None, require_current=True):
    db = sqlite3.connect(Path(path).resolve().as_uri() + '?mode=ro', uri=True)
    try:
        stamp = metadata(db, 'identity')
        if not stamp or (game_sha and stamp['gameSha256'] != game_sha):
            raise ValueError('Index/game identity mismatch')
        if metadata(db, 'complete') is not True:
            raise ValueError('Index is incomplete')
        if require_current and (stamp['analyzerSha256'] != digest(__file__) or
                                stamp['capstone'] != capstone.__version__ or stamp['pefile'] != pefile.__version__):
            raise ValueError('Index analyzer changed; rerun the pipeline')
        return db
    except BaseException:
        db.close()
        raise


def executable(pe, rva):
    return any(s.VirtualAddress <= rva < s.VirtualAddress + s.Misc_VirtualSize
               and s.Characteristics & 0x20000000 for s in pe.sections)


def image_data(pe, rva, count):
    if rva < 0 or rva + count > pe.OPTIONAL_HEADER.SizeOfImage:
        return b''
    data = pe.get_data(rva, count)
    return data if len(data) == count else b''


def index_metadata(db, pe):
    if metadata(db, 'metadataComplete'):
        return
    base = pe.OPTIONAL_HEADER.ImageBase
    types = {}
    sections = [s for s in pe.sections if not s.Characteristics & 0x20000000]
    for section in sections:
        data = section.get_data()
        for match in re.finditer(rb'\.\?A[UV][\x21-\x7e]{1,1020}\x00', data):
            offset = match.start() - 16
            if offset >= 0 and (section.VirtualAddress + offset) % 8 == 0:
                types[section.VirtualAddress + offset] = match.group()[:-1].decode('ascii')
        db.executemany('INSERT OR IGNORE INTO strings VALUES (?,?)',
                       ((section.VirtualAddress + m.start(), m.group()[:-1].decode('ascii'))
                        for m in re.finditer(rb'[\x20-\x7e]{4,1024}\x00', data)))
    db.executemany('INSERT OR IGNORE INTO types VALUES (?,?)', types.items())
    locators = {}
    for section in sections:
        data = section.get_data()
        for pos in range((-section.VirtualAddress) % 4, len(data) - 23, 4):
            if data[pos:pos+4] != b'\x01\0\0\0':
                continue
            signature, offset, cd, type_rva, hierarchy, own = struct.unpack_from('<6I', data, pos)
            if own == section.VirtualAddress + pos and type_rva in types and image_data(pe, hierarchy, 16):
                locators[own] = (type_rva, offset)
    for section in sections:
        data = section.get_data()
        for pos in range((-section.VirtualAddress) % 8, len(data) - 7, 8):
            locator = struct.unpack_from('<Q', data, pos)[0] - base
            if locator not in locators:
                continue
            table = section.VirtualAddress + pos + 8
            entries = []
            for slot in range(512):
                raw = image_data(pe, table + slot * 8, 8)
                target = struct.unpack('<Q', raw)[0] - base if raw else -1
                if not executable(pe, target):
                    break
                entries.append((table, slot, target))
            if entries:
                db.execute('INSERT OR IGNORE INTO vtables VALUES (?,?,?)', (table, *locators[locator]))
                db.executemany('INSERT OR IGNORE INTO slots VALUES (?,?,?)', entries)
    for library in getattr(pe, 'DIRECTORY_ENTRY_IMPORT', []):
        for entry in library.imports:
            db.execute('INSERT OR IGNORE INTO imports VALUES (?,?,?)',
                       (entry.address - base, library.dll.decode('ascii', 'replace'),
                        entry.name.decode('ascii', 'replace') if entry.name else f'ordinal:{entry.ordinal}'))
    metadata(db, 'metadataComplete', True)
    db.commit()


def decode_function(pe, decoder, start, end):
    if end <= start or end - start > 1024 * 1024 or not executable(pe, start) or not executable(pe, end - 1):
        return (start, end, '', '', 0, 'invalid-or-oversized-extent'), []
    blob = image_data(pe, start, end - start)
    if not blob:
        return (start, end, '', '', 0, 'missing-image-bytes'), []
    base, consumed, refs = pe.OPTIONAL_HEADER.ImageBase, 0, []
    for ins in decoder.disasm(blob, base + start):
        consumed += ins.size
        for operand in ins.operands:
            target, kind = None, None
            if operand.type == X86_OP_MEM and operand.mem.base == X86_REG_RIP:
                target, kind = ins.address + ins.size + operand.mem.disp - base, 'rip'
            elif ins.mnemonic in ('call', 'jmp') and operand.type == X86_OP_IMM:
                target, kind = operand.imm - base, ins.mnemonic
            if target is not None and 0 <= target < pe.OPTIONAL_HEADER.SizeOfImage:
                refs.append((start, ins.address - base, kind, target))
    complete = consumed == len(blob)
    return (start, end, hashlib.sha256(blob).hexdigest(), blob[:32].hex(), int(complete),
            '' if complete else 'partial-disassembly'), refs


def analyze(game, database, progress=print):
    game, database = Path(game), Path(database)
    stamp = identity(game)
    database.parent.mkdir(parents=True, exist_ok=True)
    db = create_database(database, stamp)
    pe = None
    try:
        if metadata(db, 'complete') is True:
            return summary(db)
        pe = pefile.PE(str(game))
        if pe.FILE_HEADER.Machine != 0x8664 or pe.OPTIONAL_HEADER.Magic != 0x20b:
            raise ValueError('Expected a PE32+ x64 executable')
        extents = sorted({(f.struct.BeginAddress, f.struct.EndAddress)
                          for f in getattr(pe, 'DIRECTORY_ENTRY_EXCEPTION', [])})
        if not extents or len({r for r, _ in extents}) != len(extents):
            raise ValueError('Missing or ambiguous unwind function extents')
        index_metadata(db, pe)
        done = {r[0] for r in db.execute('SELECT rva FROM functions')}
        decoder = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)
        decoder.detail = True
        for number, (start, end) in enumerate(extents, 1):
            if start in done:
                continue
            row, refs = decode_function(pe, decoder, start, end)
            db.execute('INSERT INTO functions VALUES (?,?,?,?,?,?)', row)
            db.executemany('INSERT OR IGNORE INTO refs VALUES (?,?,?,?)', refs)
            if number % 1000 == 0:
                db.commit()
                progress(f'Indexed {number}/{len(extents)} unwind functions')
        # Detect changes during analysis; a failed run is never a publishable index.
        if digest(game) != stamp['gameSha256']:
            raise ValueError('Game executable changed during analysis')
        metadata(db, 'complete', True)
        metadata(db, 'limits', ['Unwind entries are not the entire function universe.',
                              'RTTI/vtable entries are candidates, not verified class layouts.',
                              'Indirect calls, leaf functions and semantics require further analysis.'])
        db.commit()
        return summary(db)
    finally:
        if pe is not None:
            pe.close()
        db.close()


def summary(db):
    return {'functions': db.execute('SELECT count(*) FROM functions').fetchone()[0],
            'fullyDecoded': db.execute('SELECT count(*) FROM functions WHERE complete=1').fetchone()[0],
            'rttiTypes': db.execute('SELECT count(*) FROM types').fetchone()[0],
            'vtables': db.execute('SELECT count(*) FROM vtables').fetchone()[0],
            'references': db.execute('SELECT count(*) FROM refs').fetchone()[0],
            'strings': db.execute('SELECT count(*) FROM strings').fetchone()[0]}
