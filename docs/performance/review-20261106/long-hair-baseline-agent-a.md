# Banco de referencia para reconstrucción de crin — Agent A

## Estado y alcance

Este cambio añade herramientas, no una optimización del motor. El banco C construye la receta en la etapa solicitada, compila el SDF y pasa a `LongHairSet_Build` el campo de reposo `min(body SDF, mandíbula en reposo)`. La transformación de mandíbula, la prueba AABB y el snapshot siguen el código de `MonsterVisual_UpdateLongHair` en `src/MonsterVisual.c`. El wrapper cuenta evaluaciones exactas del campo durante `LongHairSet_Build`; el registro de llamada no cambia fuentes de producción.

No se midió rendimiento en esta tarea: el coordinador no ha concedido una ventana. Por tanto, no se adjunta un JSON de mediciones afirmando que el baseline haya sido reproducido. Las cifras históricas (857.381 evaluaciones y ~1,47 s/adulto) se incluyen como referencia reportada, no como resultado de este banco. Al ejecutar el comando, el JSON guarda muestras nuevas y separa `null` (fase no instrumentada) de cero.

Desglose histórico reportado para etapa adulta, no reproducido en esta rama:

| Fase / costo | Llamadas reportadas | Tiempo reportado |
|---|---:|---:|
| `MonsterSDF_Build` | — | 0,06–0,09 ms |
| Raíces/lift de hijos | 51.079 (14.896 proyección inicial + 36.183 lift) | incluido en pelo |
| Colisión, pulido y certificado de guías | 101.097 | incluido en pelo |
| Auditorías de 72 puntos | 705.205 | incluido en pase de folículos |
| Pelo completo | 857.381 llamadas totales | ~1,43–1,48 s |

El desglose atribuye la mayor parte del trabajo al pase de folículos. El wrapper nuevo informa el total exacto de evaluaciones del campo, pero no intenta inferir subfases por orden de llamada; para medirlas individualmente hace falta instrumentación interna aprobada.

## Construir y ejecutar

Desde la raíz del worktree:

```sh
tools/agent_a_build_benchmark.sh
RECIPE=/home/guillermo/Escritorio/monster_engine/docs/validation/long-hair-styling/caballo-crin-foliculos.creature.json
for stage in 0 1 2 3; do
  build/benchmark_long_hair_build --recipe "$RECIPE" --stage "$stage" \
    --warmups 3 --samples 3 --commit "$(git rev-parse HEAD)" \
    --certification off --output "/tmp/long-hair-stage-${stage}-off.json"
done
```

Esas cuatro ejecuciones requieren autorización de ventana y máquina aislada. Los objetos usados son los de `CORE_SRCS`; no se compilan SDL/OpenGL ni se escribe en la referencia congelada. La receta se lee desde el repositorio original y las salidas deben quedar fuera de él.

El campo de `LongHairSet_Build` es el cuerpo más mandíbula en reposo, no sólo cuerpo. El SDF se construye una vez por ejecución y su costo se registra aparte. La medición incluye la construcción de guías, colisión/certificados y folículos. El tiempo de `MonsterVisual_UpdateLongHair` completo, pose, empaquetado GPU y captura visual no se mide por este ejecutable.

`distance_calls` cuenta **una vez cada invocación** del callback combinado `LongHairDistance`, que es la semántica que corresponde al contador histórico de 857.381. No suma subevaluaciones internas. `body_evaluations`, `jaw_snapshot_evaluations`, `jaw_aabb_earlyouts` y `jaw_disabled_scale` son contadores separados para desglosar qué hizo ese callback; por lo tanto, no deben sumarse como si fueran llamadas del callback.

## Ablación de costo base

Para comparar costo de guías/colisión sin el bucle de folículos, ejecutar además:

```sh
build/benchmark_long_hair_build --recipe "$RECIPE" --stage 3 \
  --warmups 3 --samples 3 --commit "$(git rev-parse HEAD)" \
  --skip-follicles --output /tmp/long-hair-stage-3-no-follicle-pass.json
```

`--skip-follicles` fija `childrenPerGuide=1` **sólo en la copia benchmark**. Así, el `if (childrenPerGuide < 2)` omite el reparto, pero conserva `rootFullness`, guías, raíces, colisión, densidad y la envolvente. El campo `skip_ablation` explica el cambio y el JSON queda marcado como benchmark-only. No usar esta salida como calidad o número de fibras de producción. No usar `rootFullness=0` para esta ablación: `LongHairGroup_Envelope` depende de ese valor y también cambia la colisión.

## Certificación A/B y comparación

`--certification off|on` registra y exporta el modo solicitado como `LONG_HAIR_CERTIFICATION`. Antes de la construcción, el harness limpia `LONG_HAIR_CERTIFICATION_EFFECTIVE`. El runtime debe establecer explícitamente esa variable con el modo realmente aplicado (`off` u `on`) después de consumir el control. Sin esa confirmación, el JSON marca `certification_effective: false`; una confirmación ausente o discordante hace que el comparador rechace el A/B. La etiqueta solicitada por CLI no cuenta como confirmación.

Cuando exista esa confirmación, compilar **una sola vez** y correr ambas variantes con el mismo ejecutable, stage, receta y equipo:

```sh
build/benchmark_long_hair_build --recipe "$RECIPE" --stage 3 --warmups 3 --samples 5 \
  --commit "$(git rev-parse HEAD)" --certification off --output /tmp/hair-off.json
build/benchmark_long_hair_build --recipe "$RECIPE" --stage 3 --warmups 3 --samples 5 \
  --commit "$(git rev-parse HEAD)" --certification on --output /tmp/hair-on.json
python3 tools/compare_long_hair_baseline.py /tmp/hair-off.json /tmp/hair-on.json
```

El comparador falla cerrado si faltan metadatos/forma de datos, hay valores no finitos, los digests de repeticiones divergen o el runtime no confirma el toggle. También exige igualdad de SHA fuente, **identidad FNV-1a-64 del ejecutable real** leído de `/proc/self/exe`, receta, configuración del SDF, compiler/flags, máquina y etapa. El FNV es una huella de identidad no criptográfica; para este A/B se exige ejecutar off/on con el mismo binario sin recompilar. Informa p50/p95/max y compara cada raíz, ID y descriptor `{neighbor, weight, lift, validity}` con tolerancia absoluta `1e-6`. El JSON conserva el detalle canónico además del digest cuantizado. Para Agent E/F, repetir en etapas 0–3 y agregar validación de colisión/GPU y comparación visual oficial; el comparador no acredita esos aspectos.

La regresión sintética acotada del comparador se ejecuta con `python3 tools/compare_long_hair_baseline.py --self-test`; verifica registros válidos, mismatch, children ausentes, NaN/Inf, divergencia de digest, datos canónicos vacíos, tolerancia inválida e interpolación p95.

## Esquema de datos y límites actuales

El JSON incluye revisión y huella del ejecutable, etapa, receta, unión/configuración del campo, banderas de compilación, arquitectura/sistema, warmups, muestras de SDF/build, contadores diferenciados, digests y descriptores canónicos. Las métricas reservadas son `range_calls`, `range_unknown`, `range_certified`, `range_ms`, `certified_samples`, `fallback_ratios` y `threads`. El banco inicial usa un hilo y no hace consultas de rango. No tiene puntos de instrumentación dentro de `LongHair.c`; por ello sus fases internas (`guide_root_projection`, `collision_pbd_polish`, `guide_certificate`, `follicle_initial_root_projection`, `lift_retries`, auditoría de 72 puntos, packing y pose) y el histograma de reintentos son `null`, no cero. Añadir cronometraje/counters por fase requiere una aprobación de integración para editar fuentes compartidas.

La tolerancia del digest/cotejo numérico es `1e-6` unidades de mundo. Compilar y medir con el mismo compilador, flags y configuración; para aceptar un A/B el comparador también exige igualdad de revisión, identidad del ejecutable, etapa y máquina.

## Medición CPU controlada — 2026-11-06

Ejecutada durante el slot exclusivo autorizado, con 3 warmups + 5 muestras medidas por etapa, y la misma cantidad en la ablación adulta. **No se ejecutó `make test`, `make editor-test`, GPU ni capturas oficiales.** El binario construye el campo combinado cuerpo + mandíbula de reposo y llama directamente a `LongHairSet_Build`; sus milisegundos no incluyen pose, GPU ni tiempo completo de `MonsterVisual_UpdateLongHair`.

Entorno de ejecución: AMD Ryzen 5 5600H with Radeon Graphics, 12 CPU lógicas, Linux x86_64, GCC 13.3.0, `-Wall -Wextra -Wpedantic -O2 -Iinclude`. Revisión fuente de la medición: `ea78dd059d40dc378b0705bf7972c548dc0d4859`; SHA-256 del ejecutable: `27fe50ea3ef2ed7d7a85d4d20680ab2837e66dd81fa66adc6857465dd90e8d90`; FNV-1a-64 reportado por el ejecutable: `7d3581df39287dfa`. Receta congelada `caballo-crin-foliculos.creature.json`, SHA-256 `241682fc8b63eebc601feb687ae52812cbcbd2ace6ece3e3d7f6291fd5ba2fb3`.

| Etapa | Build p50 (ms) | p95 (ms) | max (ms) | `distance_calls` combinados | Body evals | Jaw snapshot evals | AABB jaw early-outs | Raíces visibles | Folículos válidos |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 1442.405 | 1445.968 | 1446.433 | 865.208 | 865.208 | 32.592 | 832.616 | 288/288 | 9.216/9.216 |
| 1 | 1433.593 | 1434.946 | 1434.970 | 861.136 | 861.136 | 29.435 | 831.701 | 288/288 | 9.216/9.216 |
| 2 | 1425.038 | 1427.216 | 1427.755 | 857.918 | 857.918 | 24.855 | 833.063 | 288/288 | 9.216/9.216 |
| 3 | 1420.968 | 1430.420 | 1431.522 | 857.381 | 857.381 | 21.046 | 836.335 | 288/288 | 9.216/9.216 |

`distance_calls` cuenta una llamada del callback `min(body,jaw)`, no cada subevaluación: el body se evalúa una vez por callback, y la columna jaw cuenta sólo las evaluaciones reales del snapshot tras el early-out AABB. Para la etapa 3, las **878.427 evaluaciones de componentes** son 857.381 body + 21.046 jaw. Los 836.335 early-outs son consultas de cota AABB baratas, no evaluaciones exactas del snapshot.

Esto explica la comparación histórica: los **857.381** comunicados para etapa 3 eran el contador body-only de la instrumentación anterior. El nuevo contador comparable de llamadas del callback/body también es 857.381, pero el campo de producción combinado ejecutó además 21.046 evaluaciones jaw (2,45% sobre body-only). No se debe presentar 857.381 como total de subevaluaciones cuerpo+mandíbula. Los digests fueron constantes en las cinco repeticiones de cada etapa (`e303a27dede72738`, `1fb03b903717472f`, `df1fd423a357f163`, `caffa9480bf7f4a8`, respectivamente).

| Ablación etapa 3 | p50 (ms) | p95 (ms) | max (ms) | callbacks/body | jaw snapshot | early-outs | guías | slots / válidos |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `childrenPerGuide=1`, `rootFullness` preservado | 176.907 | 178.306 | 178.498 | 105.963 | 2.050 | 103.913 | 288/288 | 288 / 0 |

Esta es la ablación limpia del pase de folículos: el reparto se omite, `rootFullness` se conserva y por ello la envolvente/colisión no cambia a la configuración artificial `rootFullness=0`. Su conteo no es comparable directamente con las 101.097 llamadas del viejo ensayo con `rootFullness=0`; la diferencia incluye el cambio de envolvente que ese ensayo introducía. La salida de ablación no es evidencia de calidad del pelo.

Los p95 son cuantiles interpolados de sólo cinco muestras, muy cercanos al máximo y con incertidumbre alta; no son estimaciones robustas de cola de producción. Durante la medición, `loadavg` de 1 minuto observado fue 1,39–1,86 (media 1,66); cada banco usó ~100% de un CPU lógico. No hubo otros tests/capturas del coordinador, pero la sesión gráfica/browser y procesos de fondo siguieron activos; hubo un `curl` usando ~4,7% CPU en la lectura previa. Por ello, son cifras controladas respecto a workloads del programa, no aislamiento físico total.

Artefactos JSON y SHA-256:

| Archivo | SHA-256 |
|---|---|
| `stage-0.json` | `36ae2240a65ac7d94c94ea3abce3992329719f888ebe0e22aa8d74d755cf799b` |
| `stage-1.json` | `6a4ebe6a098ec15e6420c5a462a2baeb2eba84c80e955a2a2cd7c11ff374c787` |
| `stage-2.json` | `6799e5df80cbffed6913fdd94ba553942b7d43c34ece5a6e04ab04768a0c86d3` |
| `stage-3.json` | `b17aaf1fd6669c8d692aab79a0ee5bc9734b39c40574ed48062df6090d1e05dd` |
| `stage-3-skip-follicles.json` | `f86a32054a77a52293b88ba1713e798aed6199dd0f7967ba171bb3e22432a0e7` |

Todos están en `/mnt/storage/monster-engine-agents/scratch/perf-20261106-a/controlled-baseline/`. `SHA256SUMS` allí contiene hashes también de logs, comandos, carga, lista de procesos y salidas stderr. Las fases internas de `LongHair.c` continúan desconocidas/null. El JSON mantiene `certification_effective=false` y `unsupported-no-runtime-acknowledgement`; sólo se midió el modo actual solicitado `off`, sin A/B ni aceptación de certificación.

## Evidencia disponible

- `docs/validation/long-hair-styling/RESULTADOS_CRIN.md` fue leído desde el repositorio original de sólo lectura; describe cuatro etapas, 288 guías, 9.216 hijos y mediciones históricas.
- La receta `caballo-crin-foliculos.creature.json` está en ese repositorio original; el sparse checkout de este worktree omite `docs/validation`.
- El informe `INFORME_OPTIMIZACION_MONSTER_ENGINE_SDF_PELO_2026-11-05.md` y el README del paquete solicitado por el prompt del coordinador no se encontraron en `/home/guillermo/Escritorio/monster_engine`.
- No se ejecutaron `make test`, `make editor-test`, validaciones GPU ni capturas oficiales. La medición CPU controlada está descrita arriba.
