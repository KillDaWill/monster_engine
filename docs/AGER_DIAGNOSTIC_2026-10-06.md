# Diagnóstico de Monster Engine: morph perro ↔ lagarto

Fecha: 6 de octubre de 2026. Repositorio: KillDaWill/monster_engine. Revisión fijada al commit [`75b199b1fa69a3c1a0772c4c61d104eea8529742`](https://github.com/KillDaWill/monster_engine/commit/75b199b1fa69a3c1a0772c4c61d104eea8529742), publicado hoy. No se ha modificado el repositorio.

## Resultado y alcance

La implementación actual no acredita 30 FPS estables ni una transición visual completa. Existen causas independientes: coste de reconstrucción del volumen completo en GPU; omisión de superficies finas por clasificación basada solamente en esquinas; cesión malla/SDF sin correspondencia geométrica en la banda; y cambios cefálicos que no están cubiertos por el certificado de topología del grafo. La ruta dispersa reduce el dominio de trabajo, pero conserva varios costes globales y no resuelve automáticamente esos defectos.

Esta es una auditoría del código y de la evidencia versionada, con dos contraejemplos numéricos ejecutados durante la revisión. No se ha ejecutado la demo OpenGL ni se han medido FPS nuevos en tu equipo. Los perfiles citados son resultados guardados en el repositorio, con sus límites. Sin una captura actual y el modo ejecutado no puede asignarse cada píxel del clipping observado a una única causa. Sí pueden identificarse los mecanismos que lo permiten y las correcciones necesarias.

La búsqueda de código devolvió inicialmente contenido de `9f06ebc`; todas las lecturas utilizadas para este diagnóstico se fijaron después a `75b199b`. Se revisaron la demo interespecies, el renderer, CreatureMorph, MonsterSDF, MonsterVisualAsync, HeadMorph, MorphHandoff, MorphChunkManager, GpuSDFSurfaceCache, los shaders de extracción/trazado/cesión y los informes de GPU, campo disperso y reparación visual.

## 1. El modo realmente predeterminado contradice los informes

En [demos/demo_morph_dog_lizard.c, líneas 168–176](https://github.com/KillDaWill/monster_engine/blob/75b199b1fa69a3c1a0772c4c61d104eea8529742/demos/demo_morph_dog_lizard.c#L168-L176), `gpuSurface=true`, `gpuSparse=false`, `exactSdf=true` y `surfaceMode="gpu-sdf-dense"`. Arrancar sin argumentos utiliza extracción densa GPU. Los informes AGER_GPU_SDF_SURFACE y AGER_SPARSE_SURFACE_FIELD_IMPLEMENTATION afirman que no se ha promovido ese backend a predeterminado. El código prevalece.

Los cuatro caminos deben diagnosticarse por separado:

| Modo | Productor visible | Problema principal |
|---|---|---|
| `gpu-sdf-dense` | Campo de edad actual, extracción completa GPU | Remuestreo de todo el volumen y geometría fina submuestreada |
| `gpu-sdf-sparse` | Bricks GPU persistentes y extracción de bricks sucios | Cobertura fina, dependencia de features, trabajo global de aristas y capacidad |
| `legacy-handoff` | Malla diferida/predicha con respaldo SDF apendicular | Cobertura no complementaria entre superficies distintas y cabeza sin respaldo |
| `exact-sdf` | Raymarch del campo actual | Coste por píxel y primera raíz todavía sin certificación completa |

En los modos GPU actuales no se llama a `MonsterVisualAsync_UpdateWithAppearance`: la demo lo limita a `!exactSdf` (líneas 638–643). Por tanto, atribuir automáticamente un hueco del modo denso al retraso del worker sería incorrecto.

**Cambio necesario:** registrar modo, commit, dispositivo, voxel y resolución en cada perfil; hacer explícita la elección normal y sincronizar los informes. Seleccionar legacy puede servir para comparar, pero no constituye la reparación: esa ruta también tiene fallos documentados.

## 2. Causa del coste GPU: el campo se reconstruye completo

[OpenGLRenderer_RenderGpuSDFSurface](https://github.com/KillDaWill/monster_engine/blob/75b199b1fa69a3c1a0772c4c61d104eea8529742/src/OpenGLRenderer.c#L823-L921) recorre todas las franjas, remuestrea todas sus esquinas, clasifica todas las celdas y reconstruye la salida en cada frame, incluso a edad fija. Compactar celdas activas evita emisión inútil, pero no reduce el muestreo inicial. Los buffers por franjas son memoria temporal, no una caché incremental.

La retícula registrada para voxel 0,0175 tiene 422 × 489 × 1011 celdas y 209.757.240 esquinas. El shader `sceneDistance` combina cuerpo, conexiones, cabeza, cavidades y mandíbula. Ejecutarlo cientos de millones de veces domina el coste.

| Evidencia guardada | Muestreo GPU p50 | Emisión GPU p50 | GPU total p50 | Frame wall p99 |
|---|---:|---:|---:|---:|
| AMD Renoir, denso compacto, voxel 0,035 | 618,02 ms | 129,56 ms | 785,05 ms | 794,13 ms |
| RTX 3050 Laptop, denso, voxel 0,0175 | 1786,73 ms | 387,52 ms | 2200,00 ms | 2203,99 ms |

Fuentes: [perfil AMD](https://github.com/KillDaWill/monster_engine/blob/75b199b1fa69a3c1a0772c4c61d104eea8529742/docs/validation/ager-gpu-sdf/mesa-compact-profile.json) y [perfil NVIDIA](https://github.com/KillDaWill/monster_engine/blob/75b199b1fa69a3c1a0772c4c61d104eea8529742/docs/validation/ager-gpu-sdf/nvidia-fence-sleep-profile.json). Son ensayos estáticos cortos, no mediciones nuevas ni una comparación controlada entre GPUs. Demuestran que esa configuración medida está muy lejos del presupuesto de 33,33 ms.

Las fences limitan dos frames pendientes; su espera revela el trabajo acumulado. `SDL_Delay(1)` reduce ocupación CPU, pero no reduce el trabajo GPU. Eliminar la espera solo permitiría acumular cola y falsear la impresión de fluidez.

**Corrección:** conservar el denso como referencia de extracción y desarrollar el disperso con evaluación localizada. En `GpuSDFSurfaceCache_Update`, el cambio de fingerprint ensucia todas las cajas de las features afectadas. Durante un morph global pueden cambiar muchas a la vez: no debe suponerse que habrá reutilización abundante. Medir `dirtyBricks/residentBricks`, muestras y tiempo por edad antes de decidir presupuesto.

En `sdf_sparse_sample.comp` cada muestra sucia llama a `MonsterSDFUseCompleteScene()` y evalúa `sceneDistance` completo. Añadir listas de dependencias por brick y poda por intervalos conservadores, respetando el orden y alcance de smooth unions y sustracciones. No basta recortar arbitrariamente a primitivas próximas. Comparar cada mejora con el campo completo CPU/GPU.

## 3. Causa geométrica verificable: superficies finas invisibles para el clasificador

[sdf_surface_classify.comp](https://github.com/KillDaWill/monster_engine/blob/75b199b1fa69a3c1a0772c4c61d104eea8529742/shaders/sdf_surface_classify.comp) solo activa una celda si el signo de las ocho esquinas produce una máscara diferente de 0 y 255. [sdf_sparse_classify.comp](https://github.com/KillDaWill/monster_engine/blob/75b199b1fa69a3c1a0772c4c61d104eea8529742/shaders/sdf_sparse_classify.comp) usa el mismo criterio para producir triángulos. `nearSurface` retiene muestras, pero no extrae la superficie perdida.

Contraejemplo ejecutado: una lámina de espesor 0,004 dentro de una celda de lado 0,0175, con `f(y)=abs(y-0,00875)-0,002`. Las ocho esquinas valen +0,00675, el centro vale −0,002 y la máscara es 0. El clasificador no emite nada aunque el campo contiene dos superficies. Es una demostración del defecto del criterio, no una medición del espesor real de tus orejas.

`CreatureMorph.c::MixHead` contrae `earShape` y `earRadii` con el peso de la especie que tiene orejas. Durante la desaparición/reaparición habrá detalles inferiores al paso de rejilla. Al desplazarse respecto a la retícula pueden desaparecer y volver a aparecer. Refinar raíces en `sdf_surface_emit.comp::edgeRoot` o `sdf_sparse_root.comp::edgeRoot` no recupera una celda que nunca se clasificó activa.

La auditoría guardada de voxel 0,0175 registra 56–179 triángulos opuestos y 241–436 degenerados según edad, aunque no hay aristas abiertas. Una superficie cerrada puede omitir una lámina completa; cero boundary edges no certifica fidelidad anatómica. Fuente: [dense-center0175-field-audit.json](https://github.com/KillDaWill/monster_engine/blob/75b199b1fa69a3c1a0772c4c61d104eea8529742/docs/validation/ager-gpu-sdf/dense-center0175-field-audit.json).

**Corrección:** clasificar también incertidumbre del intervalo del campo. Si el intervalo puede contener cero, no descartar una celda solo por sus esquinas. Subdividir o muestrear su interior hasta resolver la feature con una tolerancia geométrica explícita. Una muestra central aislada tampoco certifica ausencia de superficie. Mantener fronteras conformes y claves compartidas entre celdas/bricks; resolver casos ambiguos de MC con un método topológicamente consistente. Si se exige retícula uniforme, usar una resolución suficientemente fina en el soporte disperso; si se permite refinamiento local, añadir transición de fronteras certificada. No engrosar orejas artificialmente ni reducir resolución para ocultar el coste.

## 4. Clipping del híbrido: las máscaras son complementarias solo en el mismo punto

En [morph_handoff.glsl](https://github.com/KillDaWill/monster_engine/blob/75b199b1fa69a3c1a0772c4c61d104eea8529742/shaders/morph_handoff.glsl), `HandoffMeshKeep(root)` conserva cuando threshold ≥ coverage(root); `HandoffSDFKeep(hit)` conserva cuando threshold < coverage(hit). La malla predicha y el campo actual pueden dar posiciones diferentes para un mismo píxel. Entonces las coberturas difieren y no existe complementariedad garantizada.

Contraejemplo ejecutado: threshold 0,5, coverage de malla 0,8, coverage SDF 0,2. Ambos caminos descartan. El caso inverso puede conservar ambos. La profundidad compartida resuelve cuál queda delante si ambos existen, pero no rellena un píxel que ambos descartaron.

Además, [monster_sdf_trace.glsl, líneas 141–167](https://github.com/KillDaWill/monster_engine/blob/75b199b1fa69a3c1a0772c4c61d104eea8529742/shaders/monster_sdf_trace.glsl#L141-L167) termina al encontrar una raíz y después la descarta si no pertenece a la cesión. No continúa buscando otra raíz válida en los intervalos restantes. Eso permite omitir una superficie propietaria detrás de una primera raíz no propietaria; su frecuencia visual requiere reproducción.

**Corrección:** certificar coincidencia posicional y normal de ambas representaciones en un collar compartido antes de permitir el crossfade. Conservar el cuerpo y la frontera común; remallar únicamente el módulo afectado. Si no se obtiene coincidencia, ampliar la región reconstruida hasta una frontera válida. Otra opción es decidir propiedad una sola vez por píxel usando un punto/dominio común para ambos productores, con tratamiento explícito de cobertura y profundidad cuando solo hay un candidato. El dither por sí solo no es una solución geométrica.

Modificar el trazador para buscar la primera raíz que satisface propiedad, continuar después de una raíz rechazada y respetar intervalos y oclusión del framebuffer. Añadir contadores de píxeles con cero/doble dueño y errores de profundidad en la banda. Revisar `MorphHandoff_AddReference`, `MorphHandoff_WorkBounds` y `MonsterVisualAsync_GetFallbackRegions` para contener tanto la superficie publicada como la actual durante toda la rampa, no solo al iniciarla.

## 5. Orejas al volver al perro: cambios de superficie fuera del certificado del grafo

`CreatureMorph_CollectTopologyEvents` y `Morph_ModuleTopologyDifferent` comparan nodos y conexiones del AnatomyGraph. No comparan `HeadSurfaceRecipe.hasEars`, desaparición de pinnae, cavidades ni conectividad de la superficie cefálica. En `MixHead`, esos detalles sí cambian de forma y presencia.

[MonsterVisualAsync_GetFallbackRegions, líneas 1903–1974](https://github.com/KillDaWill/monster_engine/blob/75b199b1fa69a3c1a0772c4c61d104eea8529742/src/MonsterVisualAsync.c#L1903-L1974) construye regiones de módulos a partir de conectores y termina expresamente sin activar respaldo axial/cefálico. El consumidor permite predicción cuando los módulos de cesión están inicializados; esto no certifica las orejas. `HeadMorph_Deform` conserva la conectividad de la malla vinculada: puede mover vértices existentes, pero no crear una oreja perdida.

El [informe de reparación visual](https://github.com/KillDaWill/monster_engine/blob/75b199b1fa69a3c1a0772c4c61d104eea8529742/docs/AGER_VISUAL_HANDOFF_REPAIR.md) confirma pérdida transitoria de pabellones al volver al perro y rechaza la aceptación global. Esta causa pertenece a legacy; en los modos GPU la omisión fina debe investigarse en el extractor.

**Corrección:** añadir eventos y versiones de features de superficie, independientes de la topología esquelética. Las orejas y cavidades necesitan identidad persistente, estado activo/ausente, dominio e interfaz. Invalidar predicción que no pueda representar la feature y publicar remallado cefálico local con edad/field version coherentes. No reactivar toda la cabeza por una heurística de residual: el cuello debe tener frontera certificada para evitar costuras y nuevos picos.

Archivos: `CreatureMorph.c`, `HeadMorph.c`, `MonsterVisualAsync.c`, `MorphModuleState.h/.c`, `MonsterSDF.c`. Actualizar bindings y dominio superficial al publicar conectividad nueva; preservar fase de animación y coordenadas materiales.

## 6. Costes restantes y riesgos de la ruta dispersa

`OpenGLRenderer_RenderGpuSparseSDFSurface` ya integra bricks persistentes y dibujo indexado. No es correcto describirla como inexistente. Sin embargo:

- `InitSparseSurface` fija 2.097.152 slots de aristas y 5.461 páginas de 384 índices: 2.097.024 índices, aproximadamente 699.008 triángulos. La capacidad debe medirse frente a todas las edades y features refinadas, con crecimiento/errores explícitos.
- Cada actualización con bricks sucios despacha raíces sobre la tabla completa de 2.097.152 aristas, aunque solo una fracción cambia.
- `sdf_sparse_statistics.comp` recorre páginas, índices y toda la tabla para contar y retirar aristas. La limpieza de tombstones puede recorrer largas cadenas; no hay un coste acotado por aristas realmente cambiadas.
- `reservePages` y `reserveSample` en `sdf_sparse_common.glsl` mantienen hasta `jobCount+1` iteraciones incluso para lanes que ya reservaron. El coste escala con el lote y requiere perfil separado.
- `GpuSDFSurfaceCache_Update` compara features mediante búsquedas anidadas y reserva su arreglo actual en cada frame. Conviene sustituirlo por almacenamiento reutilizable e índice de IDs después de medir CPU; no es el cuello GPU denso demostrado.
- `GpuSDFSurfaceCache_Commit` marca CLEAN después del envío, sin condicionar la certificación a éxito de asignación/extracción. El overflow produce un diagnóstico visible, pero no debe convertir una generación incompleta en geometría certificada.

**Cambios:** compactar colas de aristas actualizadas y usadas, despachar raíces indirectamente sobre esas colas y retirar aristas mediante referencias/épocas acotadas. Mantener muestras y gradientes reutilizables, con invalidación del halo vecino. Separar generación solicitada, enviada y completada/certificada; conservar una generación visible coherente si la siguiente falla. Una publicación parcial debe incluir un grupo de bricks con fronteras y versiones compatibles, nunca edades diferentes sin certificado.

El morph puede ensuciar gran parte de los bricks; evaluar listas locales de primitivas y reutilización temporal antes de prometer 30 FPS. Si sigue siendo demasiado caro, integrar seguimiento de superficie en GPU para regiones de conectividad estable y extracción local para eventos, manteniendo evaluación del campo continuo y auditoría de deformación. Evitar extrapolar una malla fija a nuevas ramas anatómicas.

## 7. Las validaciones actuales permiten falsos éxitos

`OpenGLRenderer_ValidateGpuSurface` calcula fronteras, no manifold, orientación, triángulos colapsados y residuales, pero retorna únicamente `OpenGLRenderer_CheckErrors()`. Un resultado geométricamente malo puede producir salida exitosa mientras OpenGL no indique errores.

En `tools/validate_ager_features.py::regions`, una feature con menos de ocho píxeles se considera no visible y pasa; se tolera casi 5% de pérdida y solo se marca profundidad defectuosa cuando el candidato queda más lejos que la referencia por más de dos voxels. Las métricas de normales y material se registran pero no condicionan el gate final. El script de captura fuerza `--no-fur`; esa matriz no acepta pelaje. La comprobación de historia es útil, pero la referencia raymarch tampoco tiene certificación exhaustiva de primera raíz, según los propios informes.

**Corrección:** gates separados de extracción, fidelidad regional, temporalidad, material, capacidad y rendimiento. Convertir los errores geométricos relevantes en fallos efectivos; tratar regiones no observadas como cobertura de prueba pendiente. Usar error de profundidad en ambos sentidos, métricas de normal/color con límites explícitos y captura con/sin pelaje. Certificar primera raíz en casos finos y tangentes mediante intervalos o una referencia independiente suficientemente convergida. Igualdad CPU/GPU del valor del SDF no prueba que ambos renders hayan encontrado la primera superficie.

## Orden de implementación recomendado

1. Fijar baseline por modo, commit y dispositivo. Unificar documentación y defaults; registrar tiempo wall, GPU completo y generaciones realmente completadas.
2. Reparar aceptación: geometría defectuosa debe fallar. Añadir contraejemplos de láminas entre esquinas, máscaras a distinta profundidad y primera raíz no propietaria.
3. Completar cobertura de features finas y eventos cefálicos. Verificar perro→lagarto y vuelta sin materiales para separar forma de iluminación.
4. Certificar y reparar collar/profundidad de cesión. Comprobar cero huecos artificiales antes de suavizar el cambio de representación.
5. Optimizar la ruta dispersa: evaluación local conservadora, colas de celdas/aristas activas, reutilización de muestras/gradientes y retirada acotada. Medir una mejora cada vez.
6. Integrar pelaje y dominio material persistentes sobre la superficie aceptada. El pelaje evaluado en el hit GPU no equivale a recuperar silueta de shells/guard hairs.
7. Promover el backend normal solo después de superar la matriz temporal y de rendimiento completa.

## Criterios de aceptación para nuestra misión

Presupuesto de frame: 33,33 ms. Medir en el equipo objetivo, con resolución explícita —la matriz legacy usa 1100×820— y calidad final, pelaje, cámara y controles normales. Registrar GPU y wall por frame; informar p50/p95/p99, máximo y número de frames sobre presupuesto. Para afirmar 30 FPS estables durante un tramo probado, ningún frame de ese tramo debe superar 33,33 ms; p99 solo no garantiza ausencia de parones. Separar arranque/compilación, y registrar sus máximos también.

Ejecutar al menos cinco ciclos completos y scrubbing abrupto, cubriendo edades 0, 0,1, 0,2, 0,35, 0,5, 0,65, 0,8, 0,9 y 1. Añadir cámaras frontal, laterales, posterior, superior/inferior y primeros planos de orejas, dedos, cola y cuello. En legacy, forzar workers de 0/20/50/100/250/500/1000/3000 ms y completaciones obsoletas. Los modos GPU deben superar la prueba sin depender de esos workers.

Exigir cero desapariciones artificiales de features, cero agujeros de cesión, cero overflow/integridad inválida y fronteras compatibles. La superficie a una edad dada debe ser equivalente con historia de ida o de vuelta dentro de tolerancias numéricas justificadas. Medir desplazamiento y cambio de normal entre frames, separando el movimiento anatómico esperado de saltos por publicación. No congelar edad ni reducir calidad para alcanzar el objetivo; cualquier ajuste de velocidad debe declararse y evaluarse aparte.

Comandos existentes para comenzar la reproducción, en una copia local compilada de la revisión auditada:

```bash
make -B -j6 test demos
./demos/demo_morph_dog_lizard --surface-mode=gpu-sdf-dense --gpu-trace=/tmp/morph-dense.csv --frames=300
./demos/demo_morph_dog_lizard --surface-mode=gpu-sdf-sparse --gpu-trace=/tmp/morph-sparse.csv --frames=300
./demos/demo_morph_dog_lizard --surface-mode=legacy-handoff --validate-cycle=/tmp/morph-legacy --frames=300
python3 tools/validate_ager_features.py --mode=gpu-sdf-sparse --history --out=build/audit-features
```

La duración por número de frames no sustituye la prueba de ciclos completos. `--validate-cycle` también hace capturas y sirve para diagnóstico: medir rendimiento en una ejecución aparte sin capturas ni readbacks síncronos, añadiendo registro wall a legacy. Las métricas de los scripts actuales deben interpretarse con los límites anteriores hasta corregir sus gates. Los comandos no se han ejecutado durante esta auditoría.

## Decisión técnica

La dirección adecuada es campo continuo de edad actual, superficie dispersa con eventos de conectividad explícitos y publicación coherente. La prioridad es recuperar todas las features y certificar fronteras; después reducir trabajo del campo y de aristas. Los cambios de dither, las normales o la espera de fences aisladamente no reparan las causas geométricas ni el volumen de trabajo. No puede garantizarse 30 FPS con los perfiles disponibles: hay que demostrarlo tras las correcciones en el hardware objetivo.
