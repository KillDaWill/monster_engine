#!/usr/bin/env python3
"""Regenera las texturas periódicas originales del entorno (NumPy y SciPy)."""
import hashlib
import json
from pathlib import Path

import numpy as np
from scipy.ndimage import gaussian_filter, map_coordinates

SIZE = 512
OUT = Path(__file__).resolve().parents[1] / "assets" / "terrain"
Y, X = np.indices((SIZE, SIZE), dtype=np.float64)
LAYERS = ("grass", "dry_soil", "rock", "wet_mud")


def relief(rng, sigma_x, sigma_y=None):
    noise = rng.standard_normal((SIZE, SIZE))
    field = gaussian_filter(noise, (sigma_y or sigma_x, sigma_x), mode="wrap")
    return field / field.std()


def fragments(rng, count, axes):
    """Distribuye fragmentos elípticos con envoltura toroidal."""
    field = np.zeros((SIZE, SIZE))
    for _ in range(count):
        x, y = rng.integers(0, SIZE, 2)
        a, b = (rng.uniform(*axis) for axis in axes)
        angle = rng.uniform(0, np.pi)
        radius = int(np.ceil(max(a, b)))
        t = np.arange(-radius, radius + 1)
        dx, dy = np.meshgrid(t, t)
        u = (dx * np.cos(angle) + dy * np.sin(angle)) / a
        v = (dy * np.cos(angle) - dx * np.sin(angle)) / b
        slab = np.maximum(0, 1 - u * u - v * v) ** 0.65
        coords = np.ix_((y + t) % SIZE, (x + t) % SIZE)
        field[coords] = np.maximum(field[coords], slab * rng.uniform(0.45, 1.0))
    return field


def rgba(rgb):
    return np.concatenate((rgb, np.ones((*rgb.shape[:2], 1))), axis=2)


def encode(values):
    return np.uint8(np.rint(np.clip(values, 0, 1) * 255))


def normal(height):
    dx = (np.roll(height, -1, axis=1) - np.roll(height, 1, axis=1)) * 0.5
    dy = (np.roll(height, -1, axis=0) - np.roll(height, 1, axis=0)) * 0.5
    length = np.sqrt(1 + dx * dx + dy * dy)
    return np.stack((-dx / length, -dy / length), axis=-1) * 0.5 + 0.5


def material(i):
    rng = np.random.default_rng(89237 + i)
    macro = relief(rng, 26)
    patches = relief(rng, 8)
    fine = relief(rng, 1.3)
    grit = relief(rng, 0.58)
    if i == 0:
        blades = fragments(rng, 12500, ((2.8, 12), (0.65, 1.6)))
        cover = np.clip(0.36 + 0.13 * patches + 0.68 * blades, 0, 1)
        rgb = np.array([0.36, 0.305, 0.185]) + cover[..., None] * [0.06, 0.20, -0.04]
        height = 1.30 * blades + 0.24 * fine + 0.13 * grit
        variation = 0.035 * macro + 0.028 * fine + 0.036 * blades
    elif i == 1:
        stones = fragments(rng, 4100, ((1.2, 5.5), (0.8, 3.8)))
        rgb = np.array([0.49, 0.389, 0.265]) + stones[..., None] * [0.135, 0.139, 0.135]
        height = 1.7 * stones + 0.34 * fine + 0.15 * grit
        variation = 0.045 * macro + 0.038 * patches + 0.029 * grit
    elif i == 2:
        warp = relief(rng, 17)
        strata = relief(rng, 20, 3)
        strata = map_coordinates(strata, (Y + 11 * warp, X + 6 * macro), order=1, mode="grid-wrap")
        fracture = np.exp(-np.abs(relief(rng, 16) + patches * 0.21) * 13)
        rgb = np.array([0.44, 0.415, 0.375]) + strata[..., None] * [0.044, 0.038, 0.030]
        height = 1.3 * strata - 2.6 * fracture + 0.45 * fine + 0.18 * grit
        variation = 0.047 * macro - 0.115 * fracture + 0.037 * fine
    else:
        clasts = fragments(rng, 2250, ((1.0, 4.8), (0.85, 3.6)))
        sediment = relief(rng, 13, 2.9)
        rgb = np.array([0.375, 0.327, 0.254]) + clasts[..., None] * [0.16, 0.14, 0.115]
        height = 1.25 * clasts + 0.62 * sediment + 0.20 * fine
        variation = 0.042 * macro + 0.032 * sediment + 0.023 * grit
    base_roughness = (0.83, 0.86, 0.69, 0.54)[i]
    roughness = base_roughness + 0.042 * patches + 0.025 * fine
    ao = np.clip(0.92 + 0.069 * (height - gaussian_filter(height, 3, mode="wrap")), 0.61, 1)
    return encode(rgba(rgb + variation[..., None])), encode(normal(height)), encode(np.stack((roughness, ao), axis=-1))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    albedo, normals, masks = zip(*(material(i) for i in range(4)))
    rng = np.random.default_rng(44539)
    ripple = normal(2.65 * relief(rng, 6.5, 4.0) + 1.3 * relief(rng, 12, 7) + 0.48 * relief(rng, 2.1, 1.5))
    manifest = {
        "source": "Original procedural artwork, tools/generate_terrain_materials.py",
        "license": "CC0-1.0",
        "license_url": "https://creativecommons.org/publicdomain/zero/1.0/",
        "size": SIZE,
        "tile_world_units": dict(zip(LAYERS, (2.1, 3.0, 4.4, 2.8))),
        "layout": "Layer-major, rows in increasing UV v, unsigned normalized bytes",
        "textures": {},
    }
    for name, layers, semantics in (
        ("albedo", albedo, "sRGB RGB and opaque alpha"),
        ("normal", (*normals, encode(ripple)), "Linear XY OpenGL normals; reconstructed positive Z"),
        ("masks", masks, "Linear roughness and ambient occlusion"),
    ):
        payload = b"".join(layer.tobytes() for layer in layers)
        (OUT / f"{name}.raw").write_bytes(payload)
        manifest["textures"][name] = {
            "layers": [*LAYERS, "water_ripple"] if name == "normal" else LAYERS,
            "channels": semantics, "sha256": hashlib.sha256(payload).hexdigest(),
        }
    (OUT / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")


if __name__ == "__main__":
    main()
