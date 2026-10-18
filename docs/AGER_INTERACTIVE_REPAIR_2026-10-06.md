# Recuperación de ejecución interactiva perro ↔ lagarto

## Causa y corrección

La caída a menos de 1 FPS tiene una causa de selección de backend: la demo arrancaba en `gpu-sdf-dense`, un extractor volumétrico experimental. Con voxel 0,0175, la retícula tiene aproximadamente 209 millones de esquinas; el plan por franjas evalúa 211.208.130 muestras en cada actualización global. Durante un morph la edad cambia cada frame, así que la caché estática no evita ese trabajo. La espera de fences muestra el trabajo acumulado, no es su causa.

El SDF que anteriormente era utilizable es otra ruta: `OpenGLRenderer_RenderSDF`, que traza rayos por píxel sobre el campo de edad actual. No genera una malla volumétrica. Además, `--sdf` no estaba reconocido por el parser de esta demo: se ignoraba y dejaba activo el extractor denso. Esto explica por qué pedir SDF podía seguir produciendo el cuello de botella volumétrico.

Se restauró **`sdf-raymarch` como predeterminado** y se añadió `--sdf` como selección efectiva de esa misma ruta. `--exact-sdf` conserva su comportamiento histórico de trazado interactivo. `--surface-mode=exact-sdf` permanece como referencia diagnóstica, con evaluación completa y mayor presupuesto de iteraciones. Los extractores `gpu-sdf-dense` y `gpu-sdf-sparse` y la ruta `legacy-handoff` requieren selección explícita.

Se mantienen resolución 1100×820, materiales, pelaje habilitado en el renderer SDF existente, cámaras, controles, velocidad 0,12/s y edad actual del campo. No se activa resolución adaptativa en esta demo. La ruta normal no depende de malla diferida ni worker.

Los argumentos desconocidos ahora producen error antes de crear la ventana; no se pueden ignorar silenciosamente errores de selección. `--gpu-signature` exige un extractor explícito. El CSV registra tiempos GPU también para el trazador y asocia cada consulta completada con su frame, en lugar de dejar esas columnas a cero.

## Evidencia ejecutada

AMD Radeon Graphics/Renoir, Mesa 25.2.8, OpenGL 4.6. La ejecución normal, con cámara giratoria y morph continuo, completó **1600 frames y cinco retornos completos lagarto→perro**, con edades entre 0 y 1. Salida 0 y ningún error GL reportado. No hubo muestreo ni clasificación de retícula volumétrica en ninguno de los frames.

| Métrica | Ejecución normal |
|---|---:|
| FPS medios tras 10 frames de arranque | 15,66 |
| Wall p50 | 61,69 ms |
| Wall p95 | 92,61 ms |
| Wall p99 | 98,82 ms |
| Wall máximo, incluido arranque | 107,37 ms |
| GPU p50, consultas únicas | 57,31 ms |
| GPU p95, consultas únicas | 88,24 ms |

Los dos últimos frames no tienen consulta GPU recogida; el resumen los marca pendientes. Los tiempos wall incluyen presentación. El perfil completo también conserva las diez muestras de arranque. Esto recupera una tasa comparable al rango anterior de 10–25 FPS; **no acredita 30 FPS estables**. El mínimo instantáneo del tramo medido, derivado del máximo wall, ronda 9,3 FPS.

Por comparación, el ensayo disperso de actualización por edades de la fase anterior tenía wall p50 2297,86 ms; el denso también reconstruye globalmente durante un morph. Son protocolos distintos: sirven para identificar órdenes de coste y no para atribuir un cociente exacto de velocidad entre backends. El extractor sigue necesitando evaluación localizada y cobertura fina conforme antes de servir de ruta interactiva normal.

Comprobaciones adicionales:

- `make -B -j6 test demos build/validate_morph_gpu_regressions`: todas las suites pasan, sin warnings de compilación.
- `make visual-morph-gpu-regressions`: raíces propietarias, invalidación de caché y paridad del campo pasan.
- Capturas de cabeza a edad 0,35: el arranque predeterminado y `--sdf` producen imagen y profundidad idénticas; captura inspeccionada visualmente.
- `--sdf-typo`: salida 2 con mensaje de argumento desconocido, antes de abrir ventana.
- `git diff --check`: pasa.

Los perfiles, manifests y resúmenes están en [validation/interactive-repair-20261006](validation/interactive-repair-20261006). Las capturas están en `build/interactive-repair/`.

## Uso

```bash
./demos/demo_morph_dog_lizard
./demos/demo_morph_dog_lizard --sdf
./demos/demo_morph_dog_lizard --frames=1600 --gpu-trace=/tmp/morph-interactivo.csv
python3 tools/analyze_morph_profile.py --startup-frames=10 /tmp/morph-interactivo.csv
```

La demo vuelve a ser ejecutable con la ruta SDF interactiva. La aceptación visual exhaustiva y los 30 FPS de la misión original siguen pendientes; el [informe anterior](AGER_DIAGNOSTIC_REPAIR_2026-10-06.md) conserva esos defectos y el estado de los extractores.
