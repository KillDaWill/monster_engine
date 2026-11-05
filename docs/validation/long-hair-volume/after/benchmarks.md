# Comparación del banco aislado

Radeon Renoir / Mesa 25.2.8 / 900 × 900 / sin MSAA. No extrapolar FPS al ecosistema.

| Adulto | GPU pelo ms antes → después | CPU pelo ms antes → después | Runtime bytes antes → después | FPS antes → después |
|---|---:|---:|---:|---:|
| asno-curve | 0.484 → 0.747 | 0.162 → 2.350 | 32240 → 69956 | 156.8 → 152.1 |
| asno-tail | 0.544 → 0.932 | 0.169 → 4.470 | 32240 → 69956 | 237.8 → 223.0 |
| caballo-curve | 0.485 → 1.030 | 0.159 → 2.862 | 32240 → 69956 | 176.6 → 149.6 |
| caballo-patch | 0.316 → 0.522 | 0.118 → 2.780 | 21744 → 51780 | 210.1 → 201.9 |
| caballo-tail | 0.530 → 1.558 | 0.166 → 6.799 | 32240 → 69956 | 199.2 → 151.4 |
| perro-patch | 0.324 → 0.511 | sin animación → 3.039 | 21744 → 55428 | 103.5 → 103.5 |

Las filas caninas no comparan el mismo trabajo CPU: antes el paso estaba desactivado.
Los tiempos de cuadro no incluyen el bucle separado de actualización CPU de pelo/animador.
Los buffers GPU mantienen 16 vec4 por guía. Construcción, morph, colisiones y cuatro etapas: `before-after-performance.json`.
