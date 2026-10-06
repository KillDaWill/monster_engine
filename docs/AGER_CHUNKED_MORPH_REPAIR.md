# Reparación de morphing asíncrono por bloques

## Fallo reproducido

En el ciclo perro → lagarto → perro con `--worker-delay=3000`, la captura
`build/morph-repair-before-05.ppm` mostraba simultáneamente una malla antigua
erguida y el campo SDF actual, con dos cuerpos, dos colas y extremidades
superpuestas. La traza `build/morph-repair-before.csv` registró edad visual
`1.00` y edad geométrica `0.50`. El código dibujaba la malla global y luego
`OpenGLRenderer_RenderSDFRegion`; la prueba de profundidad no sustituía la
representación antigua.

## Diagnóstico estructural

| Área | Estado auditado | Causa |
| --- | --- | --- |
| Propiedad visible | PARCIALMENTE IMPLEMENTADO | La ruta inicial superponía malla y SDF; el recorte espacial y el filtrado semántico aún requieren certificación visual completa. |
| Regiones de fallback | PARCIALMENTE IMPLEMENTADO | La API anterior unía los cinco módulos en un AABB grande; ahora hay regiones separadas y el módulo desaparecido retira su malla. Falta resolver desbordamiento de capacidad. |
| Campo SDF por módulo | PARCIALMENTE IMPLEMENTADO | La vista filtrada conserva conectores del módulo, pero el collar proximal y las costuras necesitan validación. |
| Anclaje congelado | PARCIALMENTE IMPLEMENTADO | La copia posterior desde `displayCertifiedMesh` anulaba el anclaje; ahora se conserva una referencia transportada a la pose actual, todavía sin certificación visual continua. |
| Caché espacial | PARCIALMENTE IMPLEMENTADO | El worker sincronizaba flags sucios pero no invocaba `MorphSpatialCache_UpdateDirty`; ahora los consume tras construir un snapshot válido. No almacena aún muestras de esquina persistentes. |
| Chunks de render | NO IMPLEMENTADO | El worker todavía llama `MorphChunkManager_AssembleMesh` y publica una malla global. |
| Dirty de deformación y topología | PARCIALMENTE IMPLEMENTADO | El remallador aún procesa ambos tipos; un residual grande se promueve a dirty topológico. |
| Publicación por chunk | NO IMPLEMENTADO | La publicación sigue dependiendo del mesh global. |
| Pelaje por chunk | NO IMPLEMENTADO | La vista de índices filtrada evita raíces de triángulos omitidos, pero sigue existiendo una colección global. |
| Caché de muestras SDF | NO IMPLEMENTADO | `MorphSpatialCache` guarda candidatos y una muestra central, no esquinas reutilizables por `SDFMesher`. |

## Cambios efectuados

1. `MonsterVisualAsync_GetFallbackRegions` devuelve cajas independientes con
   identidad de módulo. El demostrador registra edades, cajas y propiedad
   visible en CSV y consola.
2. El shader de superficie y el de pelos de guarda aceptan las mismas cajas
   de exclusión. La vista de malla filtrada por módulo omite triángulos y, por
   usar el mismo índice para generar raíces, también elimina su pelaje.
3. `MonsterSDF_BuildModuleView` crea una vista prestada que contiene solo
   conectores del módulo. Durante el arranque sin malla certificada se muestra
   el campo completo, ya que no existe ninguna representación explícita.
4. El resultado de `AnchorFrozenVertices` se guarda antes de la predicción y
   se restaura después. La verificación de la interfaz usa esa referencia en
   pose actual.
5. El worker consume el dirty espacial después de validar el snapshot.
   `MorphChunkManager_MarkResidualDirty` detecta chunks alejados del SDF y
   solicita remallado local.
6. Una malla deformada o un ensamble que no coinciden con el SDF actual no se
   consideran certificados. El worker recurre a su mallador exacto existente
   para ese snapshot si el ensamble falla la comprobación. Esta ruta de
   seguridad no sustituye la arquitectura por chunks pendiente.
7. La certificación del ensamble también comprueba que la malla sea cerrada.
   La prueba retardada encontró 25 aristas abiertas a edad `0.75`; el worker
   rechazó ese ensamble y publicó una malla cerrada con cero bordes abiertos.
8. El fallback cefálico contiene únicamente el campo de la cabeza; el axial
   contiene el barrido continuo, y cada apéndice usa sus propios conectores.
   Los AABB de raymarch usan el campo actual porque la máscara semántica ya
   retira los triángulos antiguos aunque estén fuera de la nueva posición.
9. El dirty espacial se consume después de construir y validar un snapshot
   completo, incluso si hubo que usar el mallador exacto de seguridad. Se
   exponen contadores de ladrillos procesados y todavía sucios.
10. Si un módulo deja de existir en el campo actual, la región queda en
    estado `MORPH_REP_ABSENT`: se quita su malla vieja y no se envía un campo
    SDF vacío al renderer. La prueba de unidad simula desaparición de los
    conectores de cinco módulos.
11. Cuando la cabeza pasa a SDF local, el demostrador omite la cabeza,
    mandíbula y ojos del snapshot viejo y genera los ojos del snapshot visual
    actual. Así la geometría auxiliar sigue la misma propiedad que la cabeza.

## Evidencia y límites actuales

- Capturas iniciales: `build/morph-repair-before-04.ppm` y
  `build/morph-repair-before-05.ppm`.
- Captura de propiedad: `build/morph-repair-debug-05.ppm`; cian indica malla
  explícita y el color de material indica SDF. La malla etiquetada con edad
  `0.80` registró un residual superior a `3` frente al SDF visual actual.
- La primera vista filtrada únicamente por conectores dejaba el torso vacío;
  se corrigió la selección de malla para conservar regiones de tronco. Esa
  captura descartada se conserva como `build/morph-repair-module-04.ppm`.
- [Fallo original](validation/ager-chunked-repair/before-double-body.png):
  cuerpo viejo y SDF actual visibles juntos.
- [Ciclo de cuatro vistas a 3000 ms](validation/ager-chunked-repair/four-views-3000ms.png)
  y [ciclo final con pelaje](validation/ager-chunked-repair/furred-cycle-head-owned-3000ms.png):
  en las capturas inspeccionadas se ve una sola criatura. El
  [ciclo con AABB compactos](validation/ager-chunked-repair/compact-regions-cycle-3000ms.png)
  mantiene esa forma. Estas capturas no demuestran ausencia de costuras en
  todos los fotogramas ni todas las criaturas.

## Estado de fases

| Fase | Estado | Evidencia y trabajo pendiente |
| --- | --- | --- |
| 0. Reproducción e instrumentación | PARCIALMENTE IMPLEMENTADO | Se reprodujo a edad `1.00` con geometría `0.50`; CSV, cajas, edades y modo cian registran la doble propiedad. Las métricas de chunks renderizados aún representan una malla global. |
| 1. Propiedad de representación | PARCIALMENTE IMPLEMENTADO | La malla filtrada omite triángulos de módulos sustituidos y el recorte de shader cubre superficie y pelos de guarda. Las vistas inspeccionadas no duplican el cuerpo; existe una prueba visual para un extremo, pero falta propiedad por chunk y cobertura continua. |
| 2. Múltiples regiones | PARCIALMENTE IMPLEMENTADO | Cinco apéndices independientes más regiones axial y cefálica cuando sus residuos son inseguros. Los módulos ausentes se retiran; falta tratar más de 32 regiones. |
| 3. SDF semántico | PARCIALMENTE IMPLEMENTADO | Campos prestados filtrados de conectores, barrido axial y cabeza. Faltan collar de unión certificado y partición exacta del barrido axial respecto a la cola. |
| 4. Anclaje congelado | PARCIALMENTE IMPLEMENTADO | El restore conserva la referencia transportada a la pose; la prueba compara con el ancla actual. Falta una revisión visual continua de manos y pies. |
| 5. Caché espacial | PARCIALMENTE IMPLEMENTADO | Dirty consumido tras snapshots validados, con contadores. No hay rejilla persistente de muestras reutilizada por el mesher. |
| 6. Render por chunk | NO IMPLEMENTADO | Continúan `MorphChunkManager_AssembleMesh`, mapeo global, binding global y un buffer GPU corporal. |
| 7. Deformación frente a topología | PARCIALMENTE IMPLEMENTADO | Residuos graves se promueven a topología; ambos flags pueden llegar al remallador. |
| 8. Publicación por chunk | NO IMPLEMENTADO | Generaciones y descarte globales existen; no hay swap independiente de cada chunk. |
| 9. Pelaje y escamas por chunk | PARCIALMENTE IMPLEMENTADO | La malla filtrada evita raíces de triángulos omitidos y el SDF conserva material. Raíces y recursos siguen globales. |
| 10. Rendimiento | PARCIALMENTE IMPLEMENTADO | Los benchmarks se ejecutaron; el GPU regional no cumple p99 de 33.3 ms. |
| 11. Regresión visual | PARCIALMENTE IMPLEMENTADO | Capturas de cuatro vistas a 3000 ms y vista de tres cuartos a 0/500 ms. `make visual-morph-ownership` detecta el cuerpo extra en la edad final; falta ampliar la comparación a todas las edades, vistas y escenas. |
| 12. Aceptación final | NO IMPLEMENTADO | La arquitectura por chunk y el objetivo GPU aún no se cumplen. |

## Mediciones actuales

- `make -B -j6 test demos`: pasó después de la última edición de código.
- `benchmark_morph_dynamic --worker-delays`: terminó con retardos de
  `0, 20, 50, 100, 250, 500, 1000, 3000` ms; se registraron 3 builds y 18
  peticiones coalescidas por caso. Es un benchmark de worker, no mide GPU.
- `benchmark_ager_realtime`: a 60 Hz pasó, con `p95` de actualización
  `3.582 ms` y lag de edad `0.0192`. `--unpaced` falló la meta de lag
  (`p95 = 0.0471`); esa modalidad procesa 600 edades en `1.15 s` en lugar de
  los diez segundos nominales.
- Ciclo visual a 3000 ms, sin pelaje, tras filtrar campos regionales y
  compactar AABB: GPU `p50=25.91`, `p95=41.20`, `p99=43.71 ms`;
  CPU de render `p99=13.70 ms`. El volumen AABB sumado máximo bajó de
  `183.4` a `96.8` unidades cúbicas. El objetivo `p99<33.3 ms` **falla**.
- Ciclo final con pelaje a 3000 ms: 663 fotogramas registrados, GPU
  `p50=25.84`, `p95=41.03`, `p99=43.63 ms`; CPU de presentación
  `p95=3.17 ms`.
  Los 10 fotogramas capturados muestran una criatura, pero el GPU sigue
  por encima de 33.3 ms durante parte del ciclo.
- `make visual-morph-ownership`: pasó. La captura averiada original tenía
  `46 269` píxeles extra (`1.172` veces la silueta exacta) y el comparador la
  rechaza; la última captura reparada tenía `267` (`0.007`) y la acepta. Esta prueba
  cubre una vista y la edad `1.00`, no todo el ciclo.

## Puertas de aceptación pendientes

- Inspeccionar cuatro vistas en edades intermedias y extremos con retardos de
  `0`, `500` y `3000` ms, con y sin pelaje.
- Automatizar la comparación de silueta y propiedad a lo largo de todos los
  fotogramas; las diez capturas por ciclo no bastan para certificar cada frame.
- Implementar publicación, mapeo, binding, buffers GPU y raíces de pelaje por
  chunk; retirar el ensamble global de la ruta interactiva.
- Reutilizar muestras de esquina persistentes del caché SDF y medir aciertos.
- Separar realmente el camino de deformación del remallado topológico.

**Estado global: PARCIALMENTE IMPLEMENTADO.** No se acepta todavía la
migración ni el objetivo de rendimiento de 30 FPS.
