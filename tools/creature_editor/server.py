#!/usr/bin/env python3
"""Editor local: archivos JSON y mallas calculadas por Monster Engine, sin dependencias."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import tempfile
import threading
import io
from collections import OrderedDict
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, unquote, parse_qs
from agent_api import Workspace, Conflict

ROOT = Path(__file__).resolve().parents[2]
STATIC = Path(__file__).resolve().parent
BRIDGE = ROOT / 'build' / 'creature_editor_bridge'
RECIPES = ROOT / 'recipes'
LIMIT = 2 * 1024 * 1024
PREVIEW_LOCK = threading.Lock()
CACHE = OrderedDict()
NAMES = ('Lagarto', 'Perro', 'Brumacerno', 'Velkar')


def bridge(*args):
    result = subprocess.run([str(BRIDGE), *map(str, args)], capture_output=True,
                            timeout=60, cwd=ROOT)
    if result.returncode:
        raise ValueError(result.stderr.decode('utf-8', errors='replace')[:1000] or 'No se pudo construir la criatura.')
    return result.stdout


def document_bytes(document):
    data = json.dumps(document, ensure_ascii=False, allow_nan=False).encode()
    if len(data) > LIMIT:
        raise ValueError('La receta supera 2 MiB.')
    return data


def process(document, command='validate', stage=3, quality='draft'):
    data = document_bytes(document)
    with tempfile.TemporaryDirectory(prefix='monster-recipe-') as folder:
        path = Path(folder) / 'recipe.json'
        path.write_bytes(data)
        if command == 'preview':
            return bridge(command, path, stage, quality)
        return json.loads(bridge(command, path))


def recipe_path(filename):
    if not isinstance(filename, str) or not re.fullmatch(r'[a-z0-9][a-z0-9_-]{0,79}\.creature\.json', filename):
        raise ValueError('Nombre de archivo inválido: usa letras minúsculas, números y guiones.')
    path = RECIPES / filename
    if path.is_symlink():
        raise ValueError('El archivo no puede ser un enlace simbólico.')
    return path


def library():
    entries = [{'key': f'builtin:{i}', 'name': name, 'id': i, 'builtin': True}
               for i, name in enumerate(NAMES, 1)]
    for path in sorted(RECIPES.glob('*.creature.json')):
        if path.is_symlink():
            continue
        try:
            doc = json.loads(path.read_bytes())
            entries.append({'key': path.name, 'name': doc['recipe']['name'],
                            'id': doc['recipe']['id'], 'builtin': False})
        except (ValueError, KeyError, OSError, TypeError):
            continue
    return entries


def load_document(key):
    if isinstance(key, str) and re.fullmatch(r'builtin:[1-4]', key):
        return json.loads(bridge('export', key[-1]))
    return process(json.loads(recipe_path(key).read_bytes()))


def save_document(document, body):
    doc = process(document)
    path = recipe_path(body['filename'])
    override = body.get('catalog_override', False) is True
    if doc['recipe']['id'] < 1000 and not (override and 1 <= doc['recipe']['id'] <= 4):
        raise ValueError('Las copias necesitan ID desde 1000; conservar ID incorporado requiere catalog_override=true.')
    if path.exists() and not body.get('overwrite', False):
        raise Conflict('Ya existe ese archivo. Usa otro nombre o overwrite=true.')
    if any(e['id'] == doc['recipe']['id'] and e['key'] != path.name and not
           (override and e['builtin']) for e in library()):
        raise Conflict('Otra receta ya utiliza ese ID.')
    RECIPES.mkdir(exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode='w', dir=RECIPES, suffix='.tmp', delete=False) as file:
            json.dump(doc, file, ensure_ascii=False, indent=2, allow_nan=False)
            file.write('\n'); temporary = file.name
        os.replace(temporary, path)
        names=sorted(p.name for p in RECIPES.glob('*.creature.json') if not p.is_symlink())
        with tempfile.NamedTemporaryFile(mode='w',dir=RECIPES,suffix='.tmp',delete=False) as manifest:
            manifest.write('# Recetas externas del motor; generado por el editor.\n'+'\n'.join(names)+'\n')
            manifest_path=manifest.name
        os.replace(manifest_path,RECIPES/'catalog.txt')
    finally:
        if temporary and os.path.exists(temporary): os.unlink(temporary)
    return {'key': path.name, 'document': doc}


def workspace(http):
    with http.save_lock:
        if not hasattr(http, 'workspace'):
            http.workspace = Workspace(load_document, process)
        return http.workspace


class Handler(BaseHTTPRequestHandler):
    def reply(self, data, status=200, mime='application/json; charset=utf-8'):
        if not isinstance(data, bytes):
            data = json.dumps(data, ensure_ascii=False).encode()
        self.send_response(status)
        self.send_header('Content-Type', mime)
        self.send_header('Content-Length', str(len(data)))
        self.send_header('Cache-Control', 'no-store')
        self.send_header('X-Content-Type-Options', 'nosniff')
        self.end_headers()
        try:
            self.wfile.write(data)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def allowed(self):
        host = self.headers.get('Host', '')
        if host not in (f'127.0.0.1:{self.server.server_port}', f'localhost:{self.server.server_port}'):
            return False
        origin = self.headers.get('Origin')
        return not origin or origin in (f'http://{host}',)

    def do_GET(self):
        if not self.allowed():
            return self.reply({'error': 'Origen local requerido.'}, 403)
        parsed = urlparse(self.path)
        try:
            if parsed.path == '/api/agent':
                return self.reply({'version': 1, 'state': '/api/agent/state', 'schema': '/api/agent/schema',
                    'action': '/api/agent/action', 'save': '/api/agent/save',
                    'preview': '/api/agent/preview', 'render': '/api/agent/render',
                    'inspect': '/api/agent/inspect',
                    'actions': ['open', 'patch', 'replace', 'view', 'undo', 'redo'],
                    'patch': {'edits': [{'path': '/recipe/adult/head/muzzleLength', 'value': 0.6}]},
                    'concurrency': 'Cada escritura requiere expected_revision; conflicto HTTP 409.',
                    'render_note': 'PNG diagnóstico de la geometría real; no es captura del shader OpenGL.'})
            if parsed.path == '/api/agent/schema':
                return self.reply(bridge('schema'))
            if parsed.path == '/api/agent/state':
                state = workspace(self.server).state()
                since = parse_qs(parsed.query).get('since', [None])[0]
                return self.reply({'revision': state['revision']} if since == str(state['revision']) else state)
            if parsed.path == '/api/library':
                return self.reply(library())
            if parsed.path.startswith('/api/recipe/'):
                key = unquote(parsed.path[len('/api/recipe/'):])
                return self.reply(load_document(key))
            files = {'/': ('index.html', 'text/html; charset=utf-8'),
                     '/style.css': ('style.css', 'text/css; charset=utf-8'),
                     '/app.js': ('app.js', 'text/javascript; charset=utf-8'),
                     '/agent-sync.js': ('agent-sync.js', 'text/javascript; charset=utf-8'),
                     '/viewport.js': ('viewport.js', 'text/javascript; charset=utf-8')}
            if parsed.path not in files:
                return self.reply({'error': 'No encontrado.'}, 404)
            name, mime = files[parsed.path]
            return self.reply((STATIC / name).read_bytes(), mime=mime)
        except (OSError, ValueError) as error:
            self.reply({'error': str(error)}, 404)

    def do_POST(self):
        if not self.allowed():
            return self.reply({'error': 'Origen local requerido.'}, 403)
        try:
            size = int(self.headers.get('Content-Length', '0'))
            if size <= 0 or size > LIMIT:
                return self.reply({'error': 'Tamaño de solicitud inválido.'}, 413)
            if not self.headers.get('Content-Type', '').startswith('application/json'):
                return self.reply({'error': 'Se requiere application/json.'}, 415)
            body = json.loads(self.rfile.read(size))
            if not isinstance(body,dict):raise ValueError('La solicitud debe ser un objeto JSON.')
            if self.path == '/api/agent/action':
                return self.reply(workspace(self.server).action(body))
            if self.path == '/api/agent/save':
                def save(document, request):
                    with self.server.save_lock:
                        return save_document(document, request)
                return self.reply(workspace(self.server).mark_saved(body, save))
            if self.path in ('/api/agent/preview', '/api/agent/render', '/api/agent/inspect'):
                state = workspace(self.server).state()
                data = process(state['document'], 'preview',
                               ('seed', 'larva', 'juvenile', 'adult').index(state['view']['stage']), state['view']['quality'])
                if self.path.endswith('/inspect'):
                    length=int.from_bytes(data[:4],'little')
                    return self.reply({'revision':state['revision'],**json.loads(data[4:4+length])})
                if self.path.endswith('/render'):
                    from render_preview import read_mesh, render
                    yaw = {'side': 1.5708, 'front': 0, 'perspective': .95, 'top': .95}[state['view']['camera']]
                    output = io.BytesIO(); render(read_mesh(data), (1100, 800), yaw=yaw,
                        pitch=1.45 if state['view']['camera']=='top' else .22).save(output, format='PNG')
                    return self.reply(output.getvalue(), mime='image/png')
                return self.reply(data, mime='application/octet-stream')
            if self.path == '/api/validate':
                return self.reply(process(body['document']))
            if self.path == '/api/save':
                with self.server.save_lock:
                    return self.reply(save_document(body['document'], body))
            if self.path == '/api/preview':
                stage = body.get('stage', 3)
                quality = body.get('quality', 'draft')
                if type(stage) is not int or not 0 <= stage <= 3 or quality not in ('draft', 'fine'):
                    raise ValueError('Etapa o calidad inválida.')
                digest = hashlib.sha256(document_bytes(body)+str(BRIDGE.stat().st_mtime_ns).encode()).hexdigest()
                if not PREVIEW_LOCK.acquire(timeout=1):
                    return self.reply({'error': 'La vista previa está ocupada. Vuelve a intentarlo.'}, 429)
                try:
                    if digest not in CACHE:
                        CACHE[digest] = process(body['document'], 'preview', stage, quality)
                        while len(CACHE) > 6:
                            CACHE.popitem(last=False)
                    data = CACHE[digest]
                    CACHE.move_to_end(digest)
                finally:
                    PREVIEW_LOCK.release()
                return self.reply(data, mime='application/octet-stream')
            self.reply({'error': 'No encontrado.'}, 404)
        except subprocess.TimeoutExpired:
            self.reply({'error': 'La reconstrucción superó 60 segundos.'}, 422)
        except Conflict as error:
            self.reply({'error': str(error)}, 409)
        except (ValueError, KeyError, TypeError, OSError, IndexError, ImportError) as error:
            self.reply({'error': str(error)}, 422)

    def log_message(self, fmt, *args):
        if args and '200' not in str(args):
            super().log_message(fmt, *args)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--port', type=int, default=8765)
    args = parser.parse_args()
    if not BRIDGE.exists():
        subprocess.run(['make', 'build/creature_editor_bridge'], cwd=ROOT, check=True)
    RECIPES.mkdir(exist_ok=True)
    server = ThreadingHTTPServer(('127.0.0.1', args.port), Handler)
    server.save_lock = threading.Lock()
    print(f'Editor de criaturas: http://127.0.0.1:{args.port}', flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
