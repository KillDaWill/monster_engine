# Comparación del banco aislado

Radeon Renoir / Mesa 25.2.8 / 900 × 900 / sin MSAA. No extrapolar FPS al ecosistema.

| Adulto | GPU pelo ms antes → después | CPU pelo ms antes → después | Runtime bytes antes → después | FPS antes → después |
|---|---:|---:|---:|---:|
| caballo-curve | 0.485 → 2.846 | 0.159 → 9.229 | 32240 → 476292 | 176.6 → 90.4 |

Las filas caninas no comparan el mismo trabajo CPU: antes el paso estaba desactivado.
Los tiempos de cuadro no incluyen el bucle separado de actualización CPU de pelo/animador.
Los buffers GPU mantienen 80 vec4 por guía (16 de curva y 64 de folículos). Construcción, morph, colisiones y cuatro etapas: `before-after-performance.json`.
