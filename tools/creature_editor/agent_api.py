"""Estado transaccional compartido por agentes y la interfaz humana del editor."""
import copy
import threading
import re

STAGES = ('seed', 'larva', 'juvenile', 'adult')
PARTS = ('body', 'head', 'limbs', 'tails', 'ornaments')


class Conflict(ValueError):
    """La revisión de origen ya no es la vigente."""


def pointer_set(document, pointer, value):
    """Reemplaza un campo existente por JSON Pointer, sin crear rutas implícitas."""
    if not isinstance(pointer, str) or not pointer.startswith('/'):
        raise ValueError('La ruta debe ser un JSON Pointer absoluto.')
    if re.search(r'~(?![01])',pointer):raise ValueError('Escape JSON Pointer inválido.')
    parts = [part.replace('~1', '/').replace('~0', '~') for part in pointer[1:].split('/')]
    def index(part,target):
        if not re.fullmatch(r'0|[1-9][0-9]*',part) or int(part)>=len(target):raise ValueError('Índice fuera del array.')
        return int(part)
    target = document
    for part in parts[:-1]:
        target = target[index(part,target)] if isinstance(target, list) else target[part]
    key = parts[-1]
    if isinstance(target, list):
        target[index(key,target)] = copy.deepcopy(value)
    else:
        if key not in target:
            raise ValueError('Campo inexistente: '+pointer)
        target[key] = copy.deepcopy(value)


class Workspace:
    def __init__(self, load, validate):
        self.lock = threading.RLock()
        self.load, self.validate = load, validate
        self.document = load('builtin:2')
        self.source = 'builtin:2'
        self.revision = 1
        self.dirty = False
        self.view = {'stage': 'adult', 'part': 'body', 'index': 0, 'tab': 'shape',
                     'camera': 'perspective', 'quality': 'draft'}
        self.undo, self.redo = [], []

    def state(self):
        with self.lock:
            return copy.deepcopy({'revision': self.revision, 'document': self.document,
                                  'source': self.source, 'dirty': self.dirty, 'view': self.view})

    def require_revision(self, request):
        if type(request.get('expected_revision')) is not int or request['expected_revision'] != self.revision:
            raise Conflict(f'Revisión desactualizada; actual={self.revision}. Lee el estado y reintenta.')

    def remember(self):
        self.undo.append((copy.deepcopy(self.document), self.source, self.dirty))
        self.undo = self.undo[-50:]
        self.redo.clear()

    def action(self, request):
        with self.lock:
            self.require_revision(request)
            action = request.get('action')
            if action == 'open':
                key = request['key']
                next_document = self.load(key)
                self.remember()
                self.document, self.source, self.dirty = next_document, key, False
            elif action in ('patch', 'replace'):
                next_document = copy.deepcopy(request.get('document', self.document))
                if action == 'patch':
                    edits = request.get('edits')
                    if not isinstance(edits, list) or not 1 <= len(edits) <= 512:
                        raise ValueError('Se requieren entre 1 y 512 ediciones.')
                    for edit in edits:
                        pointer_set(next_document, edit['path'], edit['value'])
                next_document = self.validate(next_document)
                self.remember()
                self.document, self.dirty = next_document, bool(request.get('dirty', True))
                if action == 'replace' and 'source' in request:
                    self.source = request['source']
            elif action == 'view':
                view = copy.deepcopy(self.view)
                view.update(request['view'])
                if (set(view) != set(self.view) or view['stage'] not in STAGES or view['part'] not in PARTS
                    or view['tab'] not in ('shape', 'paint', 'advanced') or view['camera'] not in
                    ('perspective', 'side', 'front', 'top') or view['quality'] not in ('draft', 'fine')
                    or type(view['index']) is not int or not 0 <= view['index'] < 64):
                    raise ValueError('Selección o cámara inválida.')
                self.view = view
            elif action in ('undo', 'redo'):
                origin, destination = (self.undo, self.redo) if action == 'undo' else (self.redo, self.undo)
                if not origin:
                    raise ValueError('No hay cambios para '+action+'.')
                destination.append((copy.deepcopy(self.document), self.source, self.dirty))
                self.document, self.source, self.dirty = origin.pop()
            else:
                raise ValueError('Acción no admitida.')
            self.revision += 1
            return self.state()

    def mark_saved(self, request, save):
        with self.lock:
            self.require_revision(request)
            result = save(self.document, request)
            self.document, self.source, self.dirty = result['document'], result['key'], False
            self.revision += 1
            return self.state()
