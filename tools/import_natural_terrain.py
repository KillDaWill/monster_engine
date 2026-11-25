#!/usr/bin/env python3
"""Construye el pack visual v2 desde mapas CC0 fijados por SHA-256."""
import argparse
import hashlib
import json
from pathlib import Path
from urllib.request import urlopen

import numpy as np
from PIL import Image

from generate_terrain_materials import SIZE, encode, normal, relief, rgba

ROOT = Path(__file__).resolve().parents[1]
OUTPUT = ROOT / "assets/terrain/natural-v2"
CDN = "https://dl.polyhaven.org/file/ph-assets/Textures/jpg/1k"
# Nombre, activo, autor, ancho medido, periodo de presentación, albedo sRGB objetivo,
# contraste RMS relativo, escala de pendiente, sesgo de rugosidad.
LAYERS = (
    ("meadow", "leafy_grass", "Charlotte Baglioni", 2, 2.7, (.429, .487, .318), .52, .54, .05),
    ("soil", "dirt", "Charlotte Baglioni", 2, 2.1, (.49, .39, .30), .60, .60, .02),
    ("rock", "dry_riverbed_rock", "Amal Kumar", 2, 3.6, (.47, .47, .44), .63, .65, .10),
    ("silt", "brown_mud", "Rob Tuytel", 1.3, 1.65, (.365, .34, .29), .53, .46, -.10),
    ("dry_grass", "withered_grass", "Charlotte Baglioni", 2, 2.55, (.55, .50, .34), .51, .51, .05),
    ("gravel", "gravelly_sand", "Dario Barresi", 2.5, 2.4, (.48, .44, .355), .59, .58, .04),
)


def srgb_decode(rgb):
    return np.where(rgb <= .04045, rgb / 12.92, ((rgb + .055) / 1.055) ** 2.4)


def srgb_encode(rgb):
    rgb = np.maximum(0, rgb)
    return np.where(rgb <= .0031308, rgb * 12.92, 1.055 * rgb ** (1 / 2.4) - .055)


def file_hash(data):
    return hashlib.sha256(data).hexdigest()


def source_map(cache, asset, channel, expected):
    base = f"{asset}_{channel}_1k.jpg"
    url = f"{CDN}/{asset}/{base}"
    path = cache / base
    if not path.exists():
        with urlopen(url, timeout=30) as response:
            path.write_bytes(response.read())
    sha = file_hash(path.read_bytes())
    if expected and expected["sha256"] != sha:
        raise ValueError(f"El mapa fuente cambió: {url}")
    with Image.open(path) as image:
        if image.size != (1024, 1024):
            raise ValueError(f"Dimensiones inesperadas: {url}: {image.size}")
        rgb = np.asarray(image.convert("RGB"), dtype=np.float64)[::-1] / 255
    return rgb, {"url": url, "sha256": sha, "size": [1024, 1024]}


def reduce2(rgb):
    return rgb.reshape(SIZE, 2, SIZE, 2, -1).mean(axis=(1, 3))


def edge_ratio(rgb):
    x = np.abs(rgb[:, -1] - rgb[:, 0]).mean() / np.abs(np.diff(rgb, axis=1)).mean()
    y = np.abs(rgb[-1] - rgb[0]).mean() / np.abs(np.diff(rgb, axis=0)).mean()
    return [round(float(x), 3), round(float(y), 3)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cache", type=Path, required=True)
    args = parser.parse_args()
    args.cache.mkdir(parents=True, exist_ok=True)
    OUTPUT.mkdir(parents=True, exist_ok=True)
    manifest_path = OUTPUT / "manifest.json"
    previous = json.loads(manifest_path.read_text()) if manifest_path.is_file() else {}
    pinned = {entry["asset"]: entry["maps"] for entry in previous.get("materials", [])}
    manifest = {
        "version": 2, "size": [512, 512], "license": "CC0-1.0",
        "license_url": "https://polyhaven.com/license",
        "layout": "512x512, layer-major with rows in increasing UV v; unsigned normalized bytes",
        "materials": [], "textures": {},
    }
    output = {"albedo": [], "normal": [], "masks": []}
    for name, asset, author, width, tile, target, contrast, slope, rough_bias in LAYERS:
        maps = {}
        source = {}
        for channel in ("diff", "nor_gl", "rough", "ao"):
            source[channel], maps[channel] = source_map(args.cache, asset, channel, pinned.get(asset, {}).get(channel))
        rgb = reduce2(srgb_decode(source["diff"]))
        # Retiene estructuras del escaneo y limita valores extremos de iluminación horneada.
        mean = np.mean(rgb, axis=(0, 1))
        luminance = np.array([.2126, .7152, .0722])
        relative_luma = (rgb @ luminance) / (mean @ luminance)
        chroma = .32 if name in ("meadow", "dry_grass") else .60
        relative = (1 - chroma) * relative_luma[..., None] + chroma * rgb / mean
        texel = srgb_decode(np.asarray(target)) * np.clip(1 + (relative - 1) * contrast, .46, 1.8)
        encoded = encode(rgba(srgb_encode(texel)))
        normal_rgb = reduce2(source["nor_gl"] * 2 - 1)
        gradients = normal_rgb[..., :2] / np.maximum(normal_rgb[..., 2:3], .28)
        gradients *= slope * width / tile
        normal_xy = gradients / np.sqrt(1 + np.sum(gradients**2, axis=-1, keepdims=True))
        rough = np.clip(reduce2(source["rough"])[..., 0] * .70 + .30 + rough_bias, .45, .98)
        ao = .76 + .24 * reduce2(source["ao"])[..., 0]
        output["albedo"].append(encoded)
        output["normal"].append(encode(normal_xy * .5 + .5))
        output["masks"].append(encode(np.stack((rough, ao), axis=-1)))
        manifest["materials"].append({
            "name": name, "asset": asset, "source": f"https://polyhaven.com/a/{asset}",
            "author": author, "source_width_m": width, "units_per_tile": tile, "maps": maps,
            "srgb_target": target, "linear_mean": np.around(srgb_decode(encoded[..., :3] / 255).mean(axis=(0, 1)), 5).tolist(),
            "diffuse_contrast": contrast, "diffuse_chroma_fraction": chroma, "normal_slope_scale": round(slope * width / tile, 6),
            "roughness_offset": rough_bias, "albedo_edge_to_internal_delta": edge_ratio(texel),
        })
    for seed, sigma in ((24103, 5.3), (72587, 2.9)):
        rng = np.random.default_rng(seed)
        output["normal"].append(encode(normal(2.3 * relief(rng, sigma) + .56 * relief(rng, 1.9))))
    manifest["water_normals"] = {"source": "Original procedural artwork", "seeds": [24103, 72587], "layers": [6, 7]}
    for name, channels in (("albedo", "sRGB RGB + opaque A"), ("normal", "linear OpenGL XY"),
                           ("masks", "linear roughness and indirect occlusion")):
        data = b"".join(image.tobytes() for image in output[name])
        (OUTPUT / f"{name}.raw").write_bytes(data)
        manifest["textures"][name] = {"channels": channels, "layers": len(output[name]), "sha256": file_hash(data)}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
