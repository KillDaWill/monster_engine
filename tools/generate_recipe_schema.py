#!/usr/bin/env python3
"""Genera descriptores JSON a partir de los tipos de datos públicos del motor."""
import re
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
HEADERS = ('Vector', 'Color', 'PigmentPattern', 'Surface', 'Head', 'Eye',
           'RigBuilder', 'CreaturePhenotype', 'Attachment', 'CreatureRecipe')
structs, enums = {}, {}
for header in HEADERS:
    text = (ROOT / 'include' / (header + '.h')).read_text()
    text = re.sub(r'/\*.*?\*/|//[^\n]*', '', text, flags=re.S)
    for match in re.finditer(r'typedef\s+(struct|enum)\s*\w*\s*\{', text):
        start, depth, end = match.end(), 1, match.end()
        while depth:
            depth += (text[end] == '{') - (text[end] == '}')
            end += 1
        name = re.match(r'\s*(\w+)\s*;', text[end:])
        if not name:
            continue
        body = text[start:end-1]
        if match[1] == 'enum':
            enums[name[1]] = [part.strip().split('=')[0].strip() for part in body.split(',') if part.strip()]
        else:
            structs[name[1]] = body

COUNTS = {'modules': 'moduleCount', 'limbs': 'limbCount', 'tails': 'tailCount',
          'ornaments': 'ornamentCount', 'layers': 'layerCount'}
SCALARS = {'float': ('F_FLOAT', '0'), 'unsigned': ('F_UNSIGNED', 'UINT_MAX'),
           'size_t': ('F_SIZE', 'SIZE_MAX'), 'uint32_t': ('F_U32', 'UINT32_MAX'),
           'uint16_t': ('F_U16', 'UINT16_MAX'), 'uint8_t': ('F_U8', 'UINT8_MAX'),
           'bool': ('F_BOOL', '1'), 'CreatureRecipeId': ('F_U32', 'UINT32_MAX'),
           'AnatomyId': ('F_U32', 'UINT32_MAX'), 'AttachmentSlotId': ('F_U16', 'UINT16_MAX'),
           'const char*': ('F_STRING', '191')}
OPTIONAL = {'AxialPhenotype': {'dewlapDepth', 'brisketDepth'}, 'TailPhenotype': {'droop'}, 'PigmentLayer': {'regionMask'}}
output, emitted = ['/* Generado por tools/generate_recipe_schema.py; no editar. */'], set()


def declarations(body):
    parts, start, depth = [], 0, 0
    for i, ch in enumerate(body):
        depth += (ch == '{') - (ch == '}')
        if ch == ';' and depth == 0:
            parts.append(body[start:i].strip())
            start = i + 1
    return parts


def emit(name, body=None, owner=None, prefix=''):
    if name in emitted:
        return
    emitted.add(name)
    owner = owner or name
    rows = []
    for declaration in declarations(body if body is not None else structs[name]):
        if declaration.startswith('struct {'):
            end = declaration.rindex('}')
            field = declaration[end+1:].strip()
            child = name + '_' + field
            emit(child, declaration[declaration.index('{')+1:end], owner, prefix + field + '.')
            rows.append(f'{{"{field}",offsetof({owner},{prefix}{field}),sizeof((({owner}*)0)->{prefix}{field}),1,F_OBJECT,0,{child}_fields,NO_COUNT,NULL,false}}')
            continue
        match = re.fullmatch(r'(const char\s*\*|\w+)\s+(\w+(?:\s*\[[\w]+\])?(?:\s*,\s*\w+(?:\s*\[[\w]+\])?)*)', declaration)
        if not match:
            raise ValueError((name, declaration))
        typ = match[1].strip().replace(' *', '*')
        for item in match[2].split(','):
            field, count = re.fullmatch(r'\s*(\w+)(?:\s*\[(\w+)\])?\s*', item).groups()
            child = 'NULL'
            if typ in structs:
                emit(typ)
                kind, maximum, child = 'F_OBJECT', '0', typ + '_fields'
            elif typ in enums:
                kind, maximum = 'F_ENUM', enums[typ][-1]
            else:
                kind, maximum = SCALARS[typ]
            path = prefix + field
            count_offset = f'offsetof({owner},{prefix}{COUNTS[field]})' if count and field in COUNTS else 'NO_COUNT'
            # Los offsets de subobjetos anónimos son relativos a su propio comienzo.
            offset = f'offsetof({owner},{path})' + (f'-offsetof({owner},{prefix[:-1]})' if prefix else '')
            choices=json.dumps('|'.join(enums[typ])) if typ in enums else 'NULL'
            rows.append(f'{{"{field}",{offset},sizeof({typ}),{count or 1},{kind},{maximum},{child},{count_offset}' + (f'-offsetof({owner},{prefix[:-1]})' if prefix and count_offset != 'NO_COUNT' else '') + f',{choices},' + ('true' if field in OPTIONAL.get(name, set()) else 'false') + '}')
    output.append(f'static const Field {name}_fields[] = {{\n    ' + ',\n    '.join(rows) + ',\n    {0}\n};')


emit('CreatureRecipe')
(ROOT / 'src' / 'CreatureRecipeSchema.inc').write_text('\n'.join(output) + '\n')
