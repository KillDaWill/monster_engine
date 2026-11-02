#!/usr/bin/env python3
"""Valida recetas v1 reordenadas y captura mallas reales de C en varias vistas.

El puente de referencia es opcional; debe compilarse desde la revisión de base.
Las imágenes son diagnósticas, sin el shader de pelaje ni animación OpenGL.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools' / 'creature_editor'))
from render_preview import read_mesh, render


def preview(bridge, document, stage, cwd=ROOT):
    with tempfile.NamedTemporaryFile(suffix='.creature.json') as file:
        file.write(json.dumps(document, allow_nan=False).encode())
        file.flush()
        start = time.perf_counter()
        data = subprocess.check_output([str(bridge), 'preview', file.name, str(stage), 'draft'], cwd=cwd)
        return data, time.perf_counter() - start


def metadata(data):
    length, = struct.unpack_from('<I', data)
    return json.loads(data[4:4 + length])


def compare(a, b):
    """Comparar geometría y materiales; el orden de nodos no es semántico."""
    ma, mb = metadata(a), metadata(b)
    assert ma['meshes'] == mb['meshes'], 'Cambian conteos o materiales'
    assert sorted(ma['nodes'], key=lambda n: n['id']) == sorted(mb['nodes'], key=lambda n: n['id']), 'Cambian estaciones'
    maximum = 0.0
    for (va, ia), (vb, ib) in zip(read_mesh(a), read_mesh(b)):
        assert np.isfinite(vb).all(), 'Malla no finita'
        assert np.array_equal(ia, ib), 'Cambian índices de malla'
        maximum = max(maximum, float(np.max(np.abs(va - vb))))
    assert maximum <= 1e-6, f'Diferencia de vértices: {maximum}'
    return maximum


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--bridge', type=Path, default=ROOT / 'build/creature_editor_bridge')
    parser.add_argument('--baseline-bridge', type=Path)
    parser.add_argument('--baseline-cwd', type=Path, default=ROOT,
        help='Directorio de ejecución de la base; evita que lea catálogos con capacidades futuras')
    parser.add_argument('--output', type=Path, default=ROOT / 'build/anatomy-validation')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    report = []
    for recipe_id in range(1, 5):
        document = json.loads(subprocess.check_output([str(args.bridge), 'export', str(recipe_id)], cwd=ROOT))
        assert document['version'] == 1
        reversed_doc = json.loads(json.dumps(document))
        reversed_doc['recipe']['bodyPlan']['modules'].reverse()
        (args.output / f'reordered-{recipe_id}.creature.json').write_text(
            json.dumps(reversed_doc, ensure_ascii=False, indent=2) + '\n')
        montage = Image.new('RGB', (1800, 1360), (245, 246, 241))
        for stage in range(4):
            ordered, seconds = preview(args.bridge, document, stage)
            reordered, reverse_seconds = preview(args.bridge, reversed_doc, stage)
            delta = compare(ordered, reordered)
            baseline_seconds = baseline_delta = None
            if args.baseline_bridge:
                baseline, baseline_seconds = preview(args.baseline_bridge.resolve(), document, stage, args.baseline_cwd)
                baseline_delta = compare(baseline, ordered)
            meshes = read_mesh(reordered)
            for view, yaw, pitch in (('side', np.pi/2, 0), ('front', 0, 0), ('three-quarter', .95, .22)):
                column = ('side', 'front', 'three-quarter').index(view)
                image = render(meshes, (600, 310), yaw=yaw, pitch=pitch)
                montage.paste(image, (600*column, 340*stage))
                ImageDraw.Draw(montage).text((600*column+12, 340*stage+317),
                    f"{document['recipe']['name']} | etapa {stage} | {view}", fill=(36, 61, 62))
            report.append({'recipe': recipe_id, 'stage': stage,
                'nodes': len(metadata(reordered)['nodes']),
                'triangles': sum(len(indices) for _, indices in meshes),
                'ordered_seconds': seconds, 'reordered_seconds': reverse_seconds,
                'baseline_seconds': baseline_seconds, 'baseline_max_vertex_delta': baseline_delta,
                'reordered_max_vertex_delta': delta,
                'ordered_sha256': hashlib.sha256(ordered).hexdigest(),
                'materials': [entry['materialCounts'] for entry in metadata(ordered)['meshes']]})
        montage.save(args.output / f'recipe-{recipe_id}.png')
    (args.output / 'results.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
    print(f'PASS: {len(report)} casos, cuatro etapas, tres vistas; {args.output}')


if __name__ == '__main__':
    main()
