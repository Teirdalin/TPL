"""Generate native wrappers only for explicitly reviewed, uniquely matched APIs."""
import argparse
import hashlib
import json
import re
from pathlib import Path

import parity_index as indexer


def atomic_text(path, text):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(text, encoding='utf-8')
    temporary.replace(path)


def index_path(game, root):
    identity = indexer.identity(game)
    key = hashlib.sha256(json.dumps(identity, sort_keys=True).encode()).hexdigest()[:16]
    return Path(root) / 'index' / (identity['gameSha256'] + '-' + key + '.sqlite')


def candidates(db, selector):
    allowed = {'functionSha256', 'stringsAll', 'vtable'}
    if not isinstance(selector, dict) or not selector or set(selector) - allowed:
        raise ValueError('Unsupported binding selector')
    fingerprint = selector.get('functionSha256', '')
    if not re.fullmatch('[a-f0-9]{64}', fingerprint):
        raise ValueError('Review must pin the independently observed function hash')
    found = {r[0] for r in db.execute('SELECT rva FROM functions WHERE complete=1 AND length(entry32)=64 AND sha256=?', (fingerprint,))}
    if 'stringsAll' in selector:
        strings = selector['stringsAll']
        if not isinstance(strings, list) or not strings or any(not isinstance(s, str) or not s for s in strings):
            raise ValueError('Invalid string anchors')
        for text in strings:
            found &= {r[0] for r in db.execute('SELECT refs.owner FROM refs JOIN strings ON refs.target=strings.rva WHERE strings.text=?', (text,))}
    if 'vtable' in selector:
        table = selector['vtable']
        if not isinstance(table, dict) or set(table) != {'type', 'objectOffset', 'slot'} or \
                type(table['slot']) is not int or table['slot'] < 0 or type(table['objectOffset']) is not int or table['objectOffset'] < 0:
            raise ValueError('Invalid vtable selector')
        found &= {r[0] for r in db.execute('''SELECT slots.target FROM slots
            JOIN vtables ON slots.table_rva=vtables.rva JOIN types ON vtables.type_rva=types.rva
            WHERE types.name=? AND vtables.object_offset=? AND slots.slot=?''',
            (table['type'], table['objectOffset'], table['slot']))}
    return sorted(found)


def signature(abi):
    if not isinstance(abi, dict) or set(abi) != {'convention', 'return', 'parameters', 'opaqueTypes'} or abi['convention'] != 'msvc-x64':
        raise ValueError('Only explicit MSVC x64 native signatures are supported')
    opaque = abi['opaqueTypes']
    if not isinstance(opaque, list) or len(set(opaque)) != len(opaque) or any(not isinstance(t, str) or not re.fullmatch('[A-Z][A-Za-z0-9_]*', t) for t in opaque):
        raise ValueError('Invalid opaque type names')
    primitives = {'void', 'bool', 'char', 'wchar_t', 'float', 'double', 'int8_t', 'uint8_t',
                  'int16_t', 'uint16_t', 'int32_t', 'uint32_t', 'int64_t', 'uint64_t', 'intptr_t', 'uintptr_t', 'size_t'}
    def valid(value, result=False):
        if not isinstance(value, str):
            return False
        if value in primitives:
            return value != 'void' or result
        match = re.fullmatch(r'(?:const )?([A-Za-z_][A-Za-z0-9_]*)\*', value)
        return match is not None and match[1] in primitives | set(opaque)
    parameters = abi['parameters']
    if not isinstance(parameters, list) or len(parameters) > 32 or not valid(abi['return'], True) or not all(valid(p) for p in parameters):
        raise ValueError('Unsupported ABI type; containers, by-value objects and references need a reviewed adapter')
    return opaque, abi['return'], parameters


def expand(base, game, database, recipes, root):
    result = json.loads(json.dumps(base))
    if result['gameSha256'] != indexer.digest(game):
        raise ValueError('Base profile/game mismatch')
    paths = sorted(Path(recipes).glob('*.json'))
    names = {b['name'] for b in result['bindings']}
    ids = {b['id'] for b in result['bindings']}
    generated, opaque_types, reviews = [], set(), []
    db = indexer.open_index(database, result['gameSha256']) if paths else None
    try:
        for path in paths:
            recipe = json.loads(path.read_text(encoding='utf-8'))
            if recipe.get('schema') != 1 or recipe.get('gameSha256') != result['gameSha256'] or not isinstance(recipe.get('bindings'), list) or not recipe['bindings']:
                raise ValueError(f'Invalid reviewed recipe: {path.name}')
            for row in recipe['bindings']:
                ident, name = row.get('id'), row.get('name', '')
                if type(ident) is not int or not 1000 <= ident < 0xffffffff or ident in ids or name in names or not re.fullmatch(r'[A-Za-z][A-Za-z0-9_:]*', name):
                    raise ValueError('Duplicate or invalid reviewed binding ID/name')
                if row.get('runtimeVerified', False) is not False:
                    raise ValueError('Generation cannot grant runtime verification')
                review = row.get('review', {})
                if review.get('approved') is not True or not review.get('reviewer') or not review.get('evidenceFile'):
                    raise ValueError('Explicit ABI and semantic review required')
                evidence = (Path(root) / review['evidenceFile']).resolve()
                if not evidence.is_relative_to(Path(root).resolve()) or not evidence.is_file() or indexer.digest(evidence) != review.get('evidenceSha256'):
                    raise ValueError('Review evidence missing, changed or outside project')
                matched = candidates(db, row['select'])
                if len(matched) != 1:
                    raise ValueError(f'{name}: expected one reviewed match, found {len(matched)}')
                rva = matched[0]
                end, digest, entry = db.execute('SELECT end_rva,sha256,entry32 FROM functions WHERE rva=?', (rva,)).fetchone()
                opaque, returns, parameters = signature(row['abi'])
                opaque_types.update(opaque)
                symbol = re.sub('[^A-Za-z0-9]+', '_', name) + '_' + str(ident)
                generated.extend([f'enum {{ {symbol}_ID = {ident} }};',
                                  f'typedef {returns} (*{symbol}_Fn)({", ".join(parameters) or "void"});'])
                result['bindings'].append({'id': ident, 'name': name, 'rva': rva, 'expected32': entry,
                    'functionSha256': digest, 'functionSize': end - rva, 'binaryVerified': True,
                    'runtimeVerified': False, 'abi': row['abi'], 'review': review})
                ids.add(ident); names.add(name)
            reviews.append({'file': str(path.resolve()), 'sha256': indexer.digest(path)})
    finally:
        if db is not None:
            db.close()
    result['reviewedRecipes'] = reviews
    result['expandedBySha256'] = indexer.digest(__file__)
    header = ('// Generated from independently reviewed native contracts. Do not edit.\n'
              '// SPDX-License-Identifier: LicenseRef-JDL-1\n'
              '// Copyright (c) 2026 Teirdalin.\n'
              '// See PLUGIN_API_PERMISSION.md for API reuse rights.\n'
              '#ifndef TPLLIB_ENGINE_GENERATED_HPP\n#define TPLLIB_ENGINE_GENERATED_HPP\n'
              '#include <stdint.h>\n#include <stddef.h>\nnamespace TPLLib { namespace Generated {\n')
    header += '\n'.join('struct ' + t + ';' for t in sorted(opaque_types)) + '\n'
    header += '\n'.join(generated) + '\n} }\n#endif\n'
    return result, header


def publish(report, header, output):
    profile = '// Generated from independently reviewed recipes; do not edit.\n'
    profile += f'#define TPL_ENGINE_GAME_SHA "{report["gameSha256"]}"\n'
    profile += '\n'.join(f'TPL_ENGINE_BINDING({b["id"]}, "{b["name"]}", 0x{b["rva"]:x}, "{b["expected32"]}")' for b in report['bindings']) + '\n'
    atomic_text(Path(output) / 'tpllib_engine_profile.inc', profile)
    atomic_text(Path(output) / 'tpllib_engine_generated.hpp', header)
    atomic_text(Path(output) / 'bindings.json', json.dumps(report, indent=2) + '\n')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game', type=Path, required=True)
    parser.add_argument('--base', type=Path, required=True)
    parser.add_argument('--recipes', type=Path, required=True)
    parser.add_argument('--index', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    database = args.index or index_path(args.game, root / 'build/parity-pipeline')
    report, header = expand(json.loads(args.base.read_text(encoding='utf-8')), args.game, database, args.recipes, root)
    publish(report, header, args.output)
    print(f'Published {len(report["bindings"])} reviewed native bindings; no claim of full parity')
