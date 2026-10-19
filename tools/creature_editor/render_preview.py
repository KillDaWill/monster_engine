#!/usr/bin/env python3
"""Captura diagnóstica de las mallas del editor sin pantalla (Pillow y NumPy opcionales)."""
import argparse
import json
from pathlib import Path
import struct
import time

import numpy as np
from PIL import Image, ImageDraw, ImageFont
import server


def read_mesh(data):
    length, = struct.unpack_from('<I', data)
    metadata = json.loads(data[4:4+length])
    offset, meshes = length+4, []
    for entry in metadata['meshes']:
        vertices = np.frombuffer(data, dtype='<f4', count=entry['vertices']*10, offset=offset).reshape(-1, 10)
        offset += entry['vertices']*40
        indices = np.frombuffer(data, dtype='<u4', count=entry['indices'], offset=offset).reshape(-1, 3)
        offset += entry['indices']*4
        meshes.append((vertices, indices))
    return meshes


def render(meshes, size=(600, 400), yaw=.95, pitch=.22):
    width, height = size
    positions = np.concatenate([v[:, :3] for v, _ in meshes])
    center = (positions.min(axis=0)+positions.max(axis=0))/2
    right = np.array([np.cos(yaw), 0, -np.sin(yaw)])
    front = np.array([np.sin(yaw)*np.cos(pitch), np.sin(pitch), np.cos(yaw)*np.cos(pitch)])
    up = np.cross(front, right)
    rotation = np.array([right, up, front]).T
    view = (positions-center) @ rotation
    scale = min(width*.82/max(np.ptp(view[:, 0]), .01), height*.76/max(np.ptp(view[:, 1]), .01))
    triangles, colors, depths = [], [], []
    light = np.array([-.6, 1, .8]); light /= np.linalg.norm(light)
    rim = np.array([.5, .3, -.7]); rim /= np.linalg.norm(rim)
    for vertices, indices in meshes:
        points = (vertices[:, :3]-center) @ rotation
        xy = points[:, :2]*[scale, -scale]+[width/2, height/2]
        normals = vertices[:, 3:6]
        normals = normals/np.maximum(np.linalg.norm(normals, axis=1, keepdims=True), 1e-8)
        color = vertices[:, 6:9]*(.4+np.maximum(normals@light, 0)[:, None]*.67)
        color += np.maximum(normals@rim, 0)[:, None]**3*np.array([.12, .18, .15])
        color = np.clip(color, 0, 1)**.88
        triangles.extend(xy[indices])
        colors.extend((color[indices].mean(axis=1)*255).astype('uint8'))
        depths.extend(points[:, 2][indices].mean(axis=1))
    image = Image.new('RGB', size, (27, 48, 50))
    draw = ImageDraw.Draw(image)
    for index in np.argsort(depths):
        draw.polygon([tuple(point) for point in triangles[index]], fill=tuple(colors[index]))
    return image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=server.ROOT/'docs/validation/creature-editor')
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    font_path = '/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf'
    font = ImageFont.truetype(font_path, 15) if Path(font_path).exists() else ImageFont.load_default()
    examples = [(json.loads(server.bridge('export', i)), 3) for i in range(1, 5)]
    sample = json.loads((server.RECIPES/'explorador.creature.json').read_text())
    examples += [(sample, 3), (sample, 2)]
    montage = Image.new('RGB', (1800, 900), (245, 246, 241))
    report = []
    for i, (document, stage) in enumerate(examples):
        start = time.perf_counter()
        data = server.process(document, 'preview', stage)
        seconds = time.perf_counter()-start
        meshes = read_mesh(data)
        image = render(meshes)
        name = document['recipe']['name']
        x, y = (i % 3)*600, (i//3)*450
        montage.paste(image, (x, y))
        ImageDraw.Draw(montage).text((x+20, y+415), name+(' · Juvenil' if stage == 2 else ' · Adulto'), font=font, fill=(36, 61, 62))
        report.append({'name': name, 'stage': stage, 'preview_seconds': round(seconds, 3),
                       'triangles': sum(len(indices) for _, indices in meshes), 'bytes': len(data)})
    montage.save(args.output/'geometria.png')
    (args.output/'geometria.json').write_text(json.dumps(report, ensure_ascii=False, indent=2)+'\n')
    print(json.dumps(report, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
