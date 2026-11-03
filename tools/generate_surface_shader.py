#!/usr/bin/env python3
"""Empaqueta módulos GLSL sin rutas de ejecución ni dependencias de texturas."""
from pathlib import Path
import json
import re
root = Path(__file__).resolve().parents[1]
def tuft_curves():
    # Un solo origen C para las curvas largas; no mantener dos fórmulas a mano.
    text = (root / 'src/Fur.c').read_text()
    functions = []
    for name in ('Fur_TuftOffset', 'Fur_TuftDerivative'):
        start = text.index('Vector3 ' + name + '(')
        end = text.index('\n}', start) + 2
        code = text[start:end].replace('Vector3', 'vec3')
        functions.append(re.sub(r'(?<=\d)f\b', '', code))
    return ('#define Vec3_Scale(a,b) ((a)*(b))\n#define Vec3_Add(a,b) ((a)+(b))\n'
            + '\n'.join(functions) + '\n#undef Vec3_Scale\n#undef Vec3_Add\n')
def expand(path):
    if path.name == 'fur_tuft.glsl':
        return tuft_curves()
    text = path.read_text()
    return re.sub(r'^#include "([^"]+)"$', lambda m: expand(path.parent / m[1]), text, flags=re.M)
output = ['/* Generado por tools/generate_surface_shader.py; editar shaders/. */']
for name, source in [('longHairVertexSource','long_hair.vert'), ('longHairFragmentSource','long_hair.frag'), ('pigmentDomainComputeSource','pigment_domain.comp'), ('meshSkinComputeSource','mesh_skin.comp'), ('surfaceVertexSource', 'monster_mesh.vert'), ('surfaceFragmentSource', 'monster_surface.frag'), ('furGuardVertexSource','fur_guard.vert'), ('furGuardFragmentSource','fur_guard.frag')]:
    output.append('static const char* const ' + name + '[] = {')
    output += [json.dumps(line + '\n') + ',' for line in expand(root / 'shaders' / source).splitlines()]
    output.append('};')
(root / 'src/MonsterSurfaceShader.generated.h').write_text('\n'.join(output) + '\n')
