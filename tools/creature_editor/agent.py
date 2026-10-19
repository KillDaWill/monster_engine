#!/usr/bin/env python3
"""Cliente de agentes del editor: estado, transacciones, cámara, mallas y guardado."""
import argparse
import json
from pathlib import Path
from urllib.error import HTTPError
from urllib.request import Request, urlopen


def request(base, path, body=None, binary=False):
    req = Request(base+path, data=None if body is None else json.dumps(body, allow_nan=False).encode(),
                  headers={'Content-Type': 'application/json'})
    try:
        with urlopen(req, timeout=90) as response:
            data = response.read()
            return data if binary else json.loads(data)
    except HTTPError as error:
        raise SystemExit(error.read().decode()) from error


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--url', default='http://127.0.0.1:8765')
    sub = parser.add_subparsers(dest='command', required=True)
    for name in ('capabilities', 'schema', 'list', 'state', 'undo', 'redo', 'inspect'):
        sub.add_parser(name)
    p = sub.add_parser('open'); p.add_argument('key')
    p = sub.add_parser('set'); p.add_argument('path'); p.add_argument('value', help='Valor JSON')
    p = sub.add_parser('apply'); p.add_argument('file', type=Path, help='Array de {path,value}')
    p = sub.add_parser('view'); p.add_argument('--stage', choices=('seed', 'larva', 'juvenile', 'adult'))
    p.add_argument('--part', choices=('body', 'head', 'limbs', 'tails', 'ornaments'))
    p.add_argument('--camera', choices=('perspective', 'side', 'front', 'top'))
    p.add_argument('--quality', choices=('draft', 'fine')); p.add_argument('--index', type=int)
    p = sub.add_parser('save'); p.add_argument('filename'); p.add_argument('--overwrite', action='store_true')
    p.add_argument('--catalog-override', action='store_true', help='Conservar ID de una receta incorporada')
    for name in ('preview', 'render'):
        p = sub.add_parser(name); p.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    path = {'capabilities': '/api/agent', 'schema': '/api/agent/schema',
            'list': '/api/library', 'state': '/api/agent/state'}.get(args.command)
    if path:
        result = request(args.url, path)
    elif args.command == 'inspect':
        result=request(args.url,'/api/agent/inspect',{})
    elif args.command in ('preview', 'render'):
        data = request(args.url, '/api/agent/'+args.command, {}, True)
        args.output.parent.mkdir(parents=True, exist_ok=True); args.output.write_bytes(data)
        result = {'output': str(args.output), 'bytes': len(data)}
    else:
        state = request(args.url, '/api/agent/state')
        body = {'expected_revision': state['revision'], 'action': args.command}
        if args.command == 'open': body['key'] = args.key
        elif args.command == 'set': body.update(action='patch', edits=[{'path': args.path, 'value': json.loads(args.value)}])
        elif args.command == 'apply': body.update(action='patch', edits=json.loads(args.file.read_text()))
        elif args.command == 'view': body['view'] = {key: getattr(args, key) for key in ('stage', 'part', 'camera', 'quality', 'index') if getattr(args, key) is not None}
        elif args.command == 'save': body.update(filename=args.filename, overwrite=args.overwrite, catalog_override=args.catalog_override)
        result = request(args.url, '/api/agent/save' if args.command == 'save' else '/api/agent/action', body)
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
