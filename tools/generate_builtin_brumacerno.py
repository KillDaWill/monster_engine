#!/usr/bin/env python3
"""Compila el JSON canónico a un inicializador C sin dependencia de archivos en ejecución."""
import argparse
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def initializer(value):
    if isinstance(value, dict):
        return '{' + ','.join('.' + key + '=' + initializer(item) for key, item in value.items() if item != []) + '}'
    if isinstance(value, list):
        return '{' + (','.join(map(initializer, value)) if value else '0') + '}'
    if isinstance(value, bool):
        return 'true' if value else 'false'
    if isinstance(value, float):
        return repr(value) + 'f'
    return json.dumps(value, ensure_ascii=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    recipe = json.loads((ROOT / 'recipes/brumacerno.creature.json').read_text())['recipe']
    # Una línea por etapa conserva legibilidad de diffs sin miles de líneas de duplicación.
    text = '/* Generado desde recipes/brumacerno.creature.json; no editar. */\n{\n'
    text += ',\n'.join('.' + key + '=' + initializer(value) for key, value in recipe.items()) + '\n}\n'
    target = ROOT / 'src/CreatureRecipeBrumacerno.generated.inc'
    if args.check:
        return int(not target.exists() or target.read_text() != text)
    target.write_text(text)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
