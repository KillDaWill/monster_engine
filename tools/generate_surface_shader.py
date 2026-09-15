#!/usr/bin/env python3
"""Empaqueta módulos GLSL sin rutas de ejecución ni dependencias de texturas."""
from pathlib import Path
import json
import re
root = Path(__file__).resolve().parents[1]
def expand(path):
    text = path.read_text()
    return re.sub(r'^#include "([^"]+)"$', lambda m: expand(path.parent / m[1]), text, flags=re.M)
output = ['/* Generado por tools/generate_surface_shader.py; editar shaders/. */']
for name, source in [('surfaceVertexSource', 'monster_mesh.vert'), ('surfaceFragmentSource', 'monster_surface.frag')]:
    output.append('static const char* const ' + name + '[] = {')
    output += [json.dumps(line + '\n') + ',' for line in expand(root / 'shaders' / source).splitlines()]
    output.append('};')
(root / 'src/MonsterSurfaceShader.generated.h').write_text('\n'.join(output) + '\n')
