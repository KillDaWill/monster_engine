# Integración SDF y selección GPU — 2026-11-12

**GO:** parche SDF integrado y publicado; arranque GPU verificado de demo/editor.
**NO-GO:** usar/comparar la RTX 3050 o prometer más FPS: falta su driver de kernel.

## Git y parche

- Estado real inicial: `main` y `ager-index-demo-20261112` en
  `0eb37d709433aa08ef6c3416cd76edf1a26108f0`; candidato sólo en el árbol sucio.
  [Inventario Git y diff originales](docs/validation/main-gpu/initial-git.txt).
- Worktree nuevo desde ese `main`: `/tmp/opencode/monster-main-gpu`, rama
  `integrate/sdf-gpu-20261112`. No se reutilizó ningún commit ajeno de otras ramas.
- `git apply --check` PASS; parche aplicado **una vez** en el worktree limpio.
  En el original, comprobación inversa PASS: ya estaba aplicado. El parche disponible en
  `docs/validation/ager-index-demo/MonsterSDF_index.patch` produce exactamente el mismo archivo
  que la implementación anterior: SHA-256 `b83369748145fcad59214ef5f9a9b5eecbc62b443afd7f99959750551994e3ed`.
  **Ninguna diferencia textual ni semántica** respecto a ese parche; no se presupone otro adjunto.
- Commit SDF: **`a38c891c1231179fbaefef6e62dab8631fd35442`**.
  Sólo amplía el ámbito del índice existente, resuelve extremos con `AnatomyGraphIndex_Find`
  y reutiliza punteros. Conserva filtrado dormant/support antes del rechazo de extremos ausentes,
  orden de conectores, geometría, CSG y allocations.
- Commit GPU/arranque/instrumentación: **`8ae6a1c94b8e20809f0ceb4ea52f7e59f3f2a8b7`**.
- Integrados mediante **fast-forward normal a `main`**, con checks aprobados.
  `git push origin main` PASS; lectura remota confirmó `8ae6a1c94b8e20809f0ceb4ea52f7e59f3f2a8b7`.
  Este informe/evidencias se añade posteriormente como commit documental de `main`.
- Árbol original preservado: misma salida porcelain, misma rama original; incluyendo eliminación de
  `docs/performance/COORDINATION_LEDGER.md`, modificaciones y documentos no rastreados.
  [Comprobación](docs/validation/main-gpu/original-tree-preserved.txt).

## Inventario y renderer real

Ryzen 5 5600H, 6 núcleos/12 hilos; Linux `7.0.0-38-generic`; GCC 13.3.0 `-O2`.
Sesión **Wayland**, con Xwayland `DISPLAY=:0`; SDL/GLX comprobado.

| PCI | Dispositivo real | Driver / acceso | Capacidad verificada |
| --- | --- | --- | --- |
| `0000:01:00.0` | NVIDIA GA107M **GeForce RTX 3050 Mobile**, `10de:25a5` | **Sin driver asociado**, sin nodo NVIDIA/render; módulo `nvidia` no encontrado | No disponible; `nvidia-smi` falla y PRIME GLX produce BadValue |
| `0000:07:00.0` | AMD Cezanne Radeon Vega, `1002:1638` | `amdgpu`; `/dev/dri/renderD128` accesible | Direct rendering, accelerated=yes; OpenGL 4.6 |

`switcherooctl` enumera sólo AMD. No están cargados `nvidia` ni `nouveau`; Secure Boot está
**desactivado**. Un descriptor GLVND NVIDIA presente no prueba que funcione el driver.
[Diagnóstico completo, sólo lectura](docs/validation/main-gpu/gpu-inventory.txt).

Contexto **real de la demo**, no sólo el sondeo:

```text
policy=auto; PCI=0000:07:00.0; driver=amdgpu; hardware confirmado
GL_VENDOR=AMD
GL_RENDERER=AMD Radeon Graphics (radeonsi, renoir, ACO, DRM 3.64, 7.0.0-38-generic)
GL_VERSION=4.6 (Compatibility Profile) Mesa 25.2.8-0ubuntu0.24.04.2; MSAA=4
sdf-raymarch | RAYMARCH
WARNING: fallback a integrada; preferencia dedicada NO satisfecha
```

## Arranque entregado

```sh
cd /tmp/opencode/monster-main-gpu
make run-morph ARGS='--view=3 --realtime'  # ciclo continuo, calidad intacta
make run-morph GPU=integrated
make run-morph GPU=0000:01:00.0           # hoy devuelve error útil, no falsa selección
make creature-editor                    # navegador propio + servidor propio
make creature-editor GPU=integrated ARGS='--port 8766'
```

`tools/run_monster_engine_gpu.py`: inventario PCI/driver/permisos; preferencia secundaria/dedicada
**heurística, no benchmark entre modelos**; sondeo SDL aislado antes del contexto de aplicación.
Mesa selecciona PCI mediante DRI_PRIME; NVIDIA propietario solicita PRIME **OpenGL/GLX**, no Vulkan.
Auto permite fallback avisado; `high-performance`, `integrated` y PCI explícito no silencian fallos.
Comprueba aceleración e identidad Mesa vendor/device, mapeada a un único PCI; verifica de nuevo
el renderer real en la demo. Un renderer inesperado se rechaza (smoke de mismatch: exit 2).
No cambia variables del padre, drivers, BIOS, escritorio, shaders ni calidad. Preserva argumentos.

El editor usa Chrome/Chromium aislado con ANGLE/OpenGL sobre X11/Xwayland, evitando que un proceso
ya abierto ignore PRIME. El pie verifica el **WebGL real**, independientemente de SDL:
`ANGLE (AMD, AMD Radeon Graphics (radeonsi renoir ACO), OpenGL 4.6)`;
`WebGL 2.0 ...; selección confirmada`. Smoke headless con los mismos flags/env y smoke de
`make creature-editor` gestionado PASS. No se modificó/guardó ninguna receta del usuario.
Fontconfig y servicios GCM de Chrome emitieron avisos no gráficos; no impidieron WebGL.
[Estado WebGL](docs/validation/main-gpu/editor-gpu-status.txt).

Limitaciones explícitas: boot-VGA es una heurística de portátil, no prueba universal de UMA;
varias dedicadas requieren elección PCI/medición propia. IDs idénticos ambiguos y Wayland sin
consulta GLX no se certifican. NVIDIA real queda sin validar hasta reparar el driver.
`make creature-editor-server` conserva uso headless/API/navegador manual, **sin selección GPU**.

## Comprobaciones y visual

- `make -j4 test demos/demo_morph_dog_lizard editor-test`: PASS; todas las suites C y 10 tests editor.
  7 tests headless del helper y sintaxis JS PASS; argumentos con espacios conservados.
- Diferencial nuevo: **17.175 samples completos y distancias finitos, idénticos** para edades
  `0,.5,1,.5,0`. SHA-256 salida `f71a73a00a4261960af386f2f216a31c09fa3a15effb6199f8042d969ff39720`.
- Tres pares de imágenes a edad `0,.5,1`: **RGB y metadatos de escena idénticos**.
  [Edad 0](docs/validation/main-gpu/age0.png), [0.5](docs/validation/main-gpu/age0.5.png),
  [1](docs/validation/main-gpu/age1.png), [resumen](docs/validation/main-gpu/stages.png).
- Ciclo real `0→1→0`: 45 frames, timestep `.5`, 13 capturas;
  [GIF muestreado](docs/validation/main-gpu/cycle.gif), no vídeo realtime.
  Siluetas, cabeza/cuello, miembros, orejas, cola y materiales se mantienen unidos en las capturas
  inspeccionadas y regresan al estado inicial. No certifica todos los ángulos/poses ni flicker entre capturas.
- Mismos endpoints/semillas incorporados, cámara vista 3, **1100×820**, MSAA 4 efectivo,
  shader/material/pelo habilitados y calidad por defecto. Stills: timestep `.016666667`, frame 3.
  No validación nueva de hebras largas: raymarch representa el material superficial.
- `git diff --check`: PASS. [Tests, hashes, comandos y CSV](docs/validation/main-gpu/).
  Los binarios medidos se compilaron antes del commit GPU: metadata `a38c891-dirty`;
  el manifest registra sus hashes y las fuentes comunes, que coinciden con el código publicado.
  Logs publicados con espacios finales normalizados; originales en `build/main-gpu-raw-logs/`.

## Una comparación corta nueva — misma AMD

Sin builds/tests/capturas en paralelo: warmup de 12 frames por variante; una pasada baseline y
una patched de 100 frames, omitiendo los primeros 8 (92 por variante). Timestep `.25`, edades
exactamente coincidentes, órbita/viewport/calidad iguales. No comparación térmica ni campaña ABBA nueva.

| ms, mediana / p95 nearest-rank | Baseline | Patched |
| --- | --- | --- |
| Frame wall (incluye swap/esperas) | 52.176 / 85.816 | 53.063 / 77.630 |
| Render GPU query asíncrona | 39.786 / 65.919 | 39.250 / 62.065 |
| Hilo principal CPU (no wall) | 3.451 / 5.362 | 3.520 / 5.015 |
| MonsterSDF_Build CPU wall | .147657 / .218450 | .130695 / .203521 |
| MonsterAger CPU wall | .499443 / 1.007091 | .514246 / .886820 |
| Geometría Ager CPU wall | 0 / .526169 | 0 / .551407 |

Build SDF: ~.017 ms menos en esta pasada; **no demuestra mejora de frame time/FPS**.
No sumar CPU y queries de generaciones GPU anteriores. Muestreo/extracción no aplican en raymarch;
rig/boundary/presentación separados no instrumentados. Ager y Build siguen en **CPU**.
Resultados históricos ABBA se conservaron sólo como procedencia, no como medición nueva.
Reproducir: `python3 tools/build_ager_index_demo.py` y
`python3 docs/validation/main-gpu/validate_short.py` (Pillow para imágenes).

## Bloqueo NVIDIA y siguientes pasos sin cambios destructivos

No hay dos GPUs accesibles para comparar; AMD es **la única configuración de render verificada**,
no una ganadora medida frente a RTX. No se instaló nada ni se usó sudo.

1. Consultar `ubuntu-drivers devices`, `dkms status`, `modinfo -k "$(uname -r)" nvidia`
   y `nvidia-smi`; actualmente falta el módulo para este kernel.
2. Si existe un kernel previo con driver funcional, arrancarlo manualmente desde GRUB y repetir
   `nvidia-smi` (no se cambió configuración). Si no, revisar con el usuario un driver compatible
   mediante «Controladores adicionales»; **requiere su aprobación**, no instalar automáticamente.
3. Con driver operativo: `python3 tools/run_monster_engine_gpu.py --gpu 0000:01:00.0 -- glxinfo -B`;
   después `make run-morph GPU=0000:01:00.0`. Exigir renderer NVIDIA real antes de medir.
   Una comparación breve integrada/RTX debe conservar los mismos argumentos y calidad; no se promete ganancia.

**Aceptación:** GO integración SDF y launch/visibilidad/fallback AMD; NO-GO activación NVIDIA,
comparación entre GPUs y afirmación de mayor FPS, bloqueadas explícitamente por el driver ausente.
