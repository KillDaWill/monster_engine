# Ager: índice anatómico y demo A/B — 2026-11-12

**GO para la microoptimización manual verificada. No hay mejora perceptible de frame time demostrada.**
No se certifica la aplicación del parche adjunto: los materiales indicados no estaban disponibles.

## Identidad, entorno y alcance

- Baseline: `main`, commit `0eb37d709433aa08ef6c3416cd76edf1a26108f0`.
- Rama de trabajo: `ager-index-demo-20261112`; candidato sin commit, identificado por SHA-256 de
  `src/MonsterSDF.c`: `b83369748145fcad59214ef5f9a9b5eecbc62b443afd7f99959750551994e3ed`.
- Fuente SDF baseline SHA-256: `4887d7431670c867582806b3884b536ebc8ff4bda36ed653334f674a984a0f0c`.
- Árbol inicial sucio: eliminación de `docs/performance/COORDINATION_LEDGER.md` y documentos no
  rastreados, preservados. Lista exacta: [initial-status.txt](docs/validation/ager-index-demo/initial-status.txt).
- No encontrados: `REPORTE_AUDITORIA_AGER_MONSTER_ENGINE_2026-11-12.md`,
  `EVIDENCIAS_PRUEBAS_AGER_2026-11-12.zip`, `evidencias/MonsterSDF_index.patch` y
  `verify_sdf_output.c`. Por ello **no se pudo ejecutar `patch -p1 --dry-run` sobre el adjunto**.
  Se implementó exclusivamente el cambio descrito en la solicitud; no se afirma equivalencia textual
  con un parche que no se recibió.
- CPU: AMD Ryzen 5 5600H, 6 núcleos / 12 hilos; Linux `7.0.0-38-generic`, x86_64.
- GPU realmente usada: AMD Radeon Graphics, `radeonsi/renoir/ACO`, aceleración directa;
  OpenGL 4.6 Compatibility, Mesa `25.2.8-0ubuntu0.24.04.2`, pantalla `:0`.
- GCC y flags canónicos de Make: `-Wall -Wextra -Wpedantic -O2 -Iinclude -Itests`;
  SDL2, GL/GLU, `-lm -pthread`. Versiones y hashes de binarios en
  [evidencias](docs/validation/ager-index-demo/) y [manifest](docs/validation/ager-index-demo/build-manifest.json).

## Cambios

- [Parche SDF independiente](docs/validation/ager-index-demo/MonsterSDF_index.patch): ampliar el ámbito
  del `AnatomyGraphIndex` existente; sustituir las búsquedas lineales de extremos del bucle de
  conectores por dos consultas indexadas y reutilizar los punteros resultantes.
- Se conserva el orden: clasificación de soporte ornamental, descarte de conexiones dormidas/soportes
  semánticos y después comprobación de extremos. La validación previa del grafo ya rechazaba IDs
  ausentes; no se añadió un rechazo anterior. Sin cambios en asignación, orden de conectores ni CSG.
- Demo: sólo tres columnas adicionales en el CSV optativo `--gpu-trace`: tiempo de construcción SDF
  CPU wall, interpolación Ager CPU wall y actualización geométrica Ager CPU wall, usando timers existentes.
- `tools/build_ager_index_demo.py`: construcción reproducible de dos ejecutables, compartiendo todos
  los objetos salvo `MonsterSDF.o`. Extrae la fuente baseline del commit registrado sin cambiar el árbol.
- Sin cambios en resolución, voxel, shaders, pelo, colisiones, RNG, certificados de intervalo, ownership
  asíncrono o número de hilos. Culling/poda conservadora siguen activos. No se presupone la presencia
  de campañas F/G3: se inspeccionaron las implementaciones actuales de pelo, sin trasladar sus
  resultados históricos ni modificar código de pelo.

## Corrección y visual

- `make -j4 demos/demo_morph_dog_lizard`: PASS, core afectado y demo compilados.
- `make -j4 test`: PASS, todas las suites; sin selección ni repetición de estrés.
- Un único diferencial acotado: perro adulto ↔ lagarto adulto, mismo cierre bucal que la demo,
  edades `0, 0.5, 1, 0.5, 0`; rejilla 9³, superficies de todos los nodos (incluida cabeza/cuello),
  centros y bordes de cada conector. **17.175 distancias y SDFSample completos finitos por variante,
  coincidencia exacta** de distancia escalar, distancia del sample, RGBA y material.
  Salida hexadecimal comparada byte a byte, SHA-256
  `f71a73a00a4261960af386f2f216a31c09fa3a15effb6199f8042d969ff39720`.
  [Fuente del comprobador](docs/validation/ager-index-demo/verify_sdf_output.c).
  No se ejecutó matriz extensa, hash de mallas ni inyección de fallos de allocation.
- Tres pares de capturas a edades `0`, `0.5`, `1`: **píxeles y metadatos de cámara/escena idénticos**.
  Vista 3, 1100×820, timestep `0.016666667`, captura al tercer frame.
  [Comparación](docs/validation/ager-index-demo/comparison.png):

  | Edad | Baseline | Patched |
  | --- | --- | --- |
  | 0 | [PNG](docs/validation/ager-index-demo/baseline-age0.png) | [PNG](docs/validation/ager-index-demo/patched-age0.png) |
  | 0.5 | [PNG](docs/validation/ager-index-demo/baseline-age0.5.png) | [PNG](docs/validation/ager-index-demo/patched-age0.5.png) |
  | 1 | [PNG](docs/validation/ager-index-demo/baseline-age1.png) | [PNG](docs/validation/ager-index-demo/patched-age1.png) |

- Smoke visual: 45 frames renderizados, `--validate-cycle`, timestep `0.5`, recorrido real `0→1→0`.
  [GIF muestreado](docs/validation/ager-index-demo/patched-loop.gif), 13 capturas a 350 ms por imagen,
  **no vídeo en tiempo real**, y [hoja de contacto](docs/validation/ager-index-demo/loop-contact-sheet.png).
  La silueta, extremidades, orejas, cola y material cambian efectivamente y regresan al estado inicial.
  Inspección de capturas: sin grietas, partes ausentes o diferencias atribuibles al parche observadas.
  Esto no certifica ausencia de flicker/pops entre capturas ni todos los ángulos/poses.
- El modo SDF conserva su material de pelo procedural habilitado; no demuestra siluetas de hebras largas
  ni su attachment en animación. No se cambió a otro renderer para aparentar mayor calidad o rendimiento.

## Una medición A–B–B–A

Después de terminar los tests/capturas, sin otros trabajos de esta sesión en paralelo: un warmup de
12 frames por variante; luego A1–B1–B2–A2 de 100 frames cada uno. Se omiten los primeros 8 frames
de cada pasada para excluir arranque de contexto/shaders: 92 por pasada, 184 por variante.
Mismos endpoints/semillas incorporados en recetas, cámara orbital vista 3, timestep `0.25`,
1100×820, `sdf-raymarch`, pelo activado, calidad por defecto, MSAA 4 solicitado sin aviso de fallback,
VSync solicitado (`SDL_GL_SetSwapInterval(1)`; no se afirma sincronización efectiva).
La secuencia de edades coincide exactamente entre variantes. No es un ensayo térmico controlado.

Todos los valores son **ms**. Resumen agrupado (mediana / p95 nearest-rank / máximo):

| Métrica | Baseline | Patched |
| --- | --- | --- |
| Frame wall, incluye swap/esperas | 53.621 / 82.735 / 120.037 | 53.859 / 84.849 / 115.868 |
| MonsterSDF_Build CPU wall | 0.151919 / 0.207740 / 0.314719 | 0.141971 / 0.202760 / 0.454010 |
| Render GPU timer | 39.824 / 62.459 / 73.225 | 39.563 / 63.121 / 72.435 |
| Hilo principal CPU, no wall | 3.500 / 4.428 / 4.824 | 3.598 / 4.566 / 5.317 |
| Ager interpolación CPU wall | 0.487049 / 0.790799 / 0.880253 | 0.500229 / 0.814783 / 1.194407 |
| Ager geometría CPU wall | 0 / 0.478077 / 0.687416 | 0 / 0.521648 / 0.792902 |

Medianas por pasada: frame A1/B1/B2/A2 = `54.131 / 54.992 / 53.119 / 53.359`;
SDF build = `0.152471 / 0.147552 / 0.138079 / 0.151058`.
La reducción agrupada de build es aproximadamente **0.00995 ms (6.5%)**, pequeña y con ruido;
el máximo de build patched es peor. **No hay evidencia de mejora perceptible de frame time/FPS**:
la mediana de frame agrupada es ligeramente peor. No se reutilizan las cifras históricas como resultado actual.

GPU timers corresponden a generaciones completadas anteriores, no necesariamente al frame CPU de la fila;
no se suman con CPU wall. `sampling_gpu_ms` y extracción no aplican en raymarch: ceros no significan
muestreo gratuito. `rig_ms`, `boundary_bind_ms` y presentación separada no están instrumentados en esta ruta:
**no disponibles**. Geometría Ager vale cero cuando no se actualiza el snapshot cuantizado.
[CSV y metadatos](docs/validation/ager-index-demo/),
[resumen por pasada](docs/validation/ager-index-demo/abba-summary.json),
[script de medición](docs/validation/ager-index-demo/measure_abba.py).

## Construir y ejecutar

Desde la raíz, con SDL2/OpenGL y sesión gráfica:

```sh
python3 tools/build_ager_index_demo.py
build/ager-index-demo/demo-baseline --surface-mode=sdf-raymarch --view=3 --realtime
build/ager-index-demo/demo-patched --surface-mode=sdf-raymarch --view=3 --realtime
```

Ejecutar una variante cada vez. No suministrar `--age` para el bucle automático continuo.
Espacio pausa; flechas avanzan/regresan; 0–4 fijan presets; F1/F2/F3/F4/F5 muestran lateral,
frente, tres cuartos, cabeza y cola; R alterna órbita, rueda zoom, Escape sale.
Inspeccionar pops, grietas, orejas/cola ausentes, unión cuello/miembros, pies, pelo separado,
flicker, materiales inconsistentes y geometría atrasada. La órbita permite revisar uniones ocultas.

Reproducir una captura (sustituir variante y edad por las seis combinaciones de la tabla):

```sh
build/ager-index-demo/demo-patched --surface-mode=sdf-raymarch --age=0.5 --view=3 --fixed-timestep=0.016666667 --frames=3 --capture-prefix=build/ager-index-demo/recheck
build/ager-index-demo/demo-patched --surface-mode=sdf-raymarch --fixed-timestep=0.5 --view=3 --frames=100 --validate-cycle=build/ager-index-demo/recheck-loop
python3 docs/validation/ager-index-demo/measure_abba.py
```

Capturas originales PPM/depth, logs completos y comprobadores compilados permanecen en
`build/ager-index-demo/`; evidencia compacta persistente: ~1.15 MB.

## Límite y oportunidades futuras

**GO**: candidato manual pequeño, build/tests/diferencial exacto y comparación renderizada aprobados
para este escenario. **No GO para prometer aceleración FPS ni equivalencia con el adjunto ausente.**
Sin validación exhaustiva de especies, poses, LOD, otras GPU o stress asíncrono; fuera del alcance solicitado.
Futuro, sin implementación aquí: refit incremental de topología/contactos de boundary y planificación
CPU/GPU basada en tiempos de generaciones realmente completadas, cada una con su propia aceptación.
