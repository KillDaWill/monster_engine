# Auditoría de seguridad del morph perro ↔ lagarto (27-09-2026)

## Estado

Esta implementación es una **etapa de contención verificada**, no la arquitectura local terminada. Impide que una predicción sin certificado desplace vértices de módulos con cambios de blueprint y rechaza mallas del worker con plegamientos evidentes. La demo usa el SDF exacto del fotograma completo si la edad certificada queda a más de un paso geométrico. Todavía faltan la interfaz proximal compartida, el remallado SDF local, el caché incremental y trabajos publicables por módulo. Por ello no se cumplen todos los criterios de aceptación del encargo.

## Causa comprobada y recorrido de edades

`MonsterVisualAsync_UpdateWithAppearance` separa la edad cuantizada del `monster` solicitado y la edad continua de `appearance`. Encola `pendingSnapshot`; el worker registra `workingGeometryAge`, construye o reutiliza el ancla exacta de edad 0,25, llama `MorphSurfaceTracker_Predict` y después `MorphSurfaceTracker_Correct`. El corrector proyecta vértices, conserva los índices y audita orientación, por lo que no cambia la conectividad. El parche existente elegía un módulo con `problemTriangles` ya presentes y solo se habilitaba con `enableLocalMorphPatches`.

El worker colocaba el resultado en `readyMesh` y el hilo de presentación lo intercambiaba con `displayMesh`, asignando `displayedGeometryAge=readyStats.workingGeometryAge`. En el mismo fotograma, la presentación podía llamar `MorphSurfaceTracker_Predict` con `appearance->growthAge`, ya posterior; esa función no consulta el SDF exacto. `MonsterVisualAsync_Flush` tenía un segundo camino de publicación y también debía verificar el certificado. `WorkerShouldCancel` conserva la excepción para movimiento continuo que evita el livelock por reinicios.

La sonda `benchmark_morph_dynamic --region-audit` del estado inicial midió en edad 1,00 **76 262 caras opuestas antes de proyectar** y **76 322 después**. La mayoría pertenece a miembros y dedos; la distancia p95 tras la proyección era solo 0,005577. Esto confirma que la cercanía de vértices al SDF no prueba conectividad correcta. La malla exacta de referencia no tiene ese patrón de plegamiento.

## Cambios realizados

- `CreatureMorph_CollectTopologyEvents` identifica cinco módulos del par de prueba: 10, 11, 12, 13 y 20. `MonsterVisualAsync_SetMorphEndpoints` crea estados `MorphModuleState` para ellos y avanza `morphEndpointGeneration`.
- Cada estado guarda edad solicitada y certificada, versiones y estado de fallback. Los resultados de otros extremos o más alejados de la edad solicitada que el certificado visible se descartan en **ambos** caminos de publicación. La comprobación de versiones de interfaz/SDF existe en el token; aún no hay parches independientes que las hagan avanzar.
- El display conserva una copia inmutable de la última malla certificada. Una máscara derivada de las influencias anatómicas identifica vértices de módulos topológicos. `AnatomyDeformer_DeformMasked` y `MorphSurfaceTracker_PredictMasked` mueven el resto de la superficie sin extrapolar esos vértices. El borde entre ambas regiones se comprueba por estiramiento, pérdida de área, inversión relativa de cada cara mixta y caras opuestas; la deformación estable se reduce hasta pasar la auditoría. Si ninguna amplitud es segura, se restaura toda la malla certificada.
- El worker rechaza una malla dinámica si la orientación, degeneración, borde, conectividad o validez básica fallan. Compara el número de componentes de la malla dinámica con el ancla, pues el SDF exacto de edad 0,25 tiene tres componentes válidos. Si falla la ruta dinámica, usa el generador SDF exacto existente como respaldo. La validación se ejecuta antes de publicar.
- `MonsterVisualAsync_NeedsExactFallback` expresa la decisión de representación. La demo conserva la malla explícita para retrasos cortos y dibuja el SDF exacto a la edad visual cuando el retraso de un módulo topológico supera un paso geométrico. Así un worker retrasado no desplaza una pata antigua a una anatomía incompatible ni deja al animal fuera de la cámara al avanzar la edad.
- El respaldo también empieza en el primer fotograma, antes del primer `readyMesh`; el avance de la demo ya no espera la primera reconstrucción cuando conoce módulos topológicos. La primera espera medida llegó a 15,5 s, durante la cual la versión anterior mostraba el cuerpo vacío.
- `MonsterSDF_Build` adjunta al campo exacto la receta superficial compilada del mismo snapshot. El trazador GPU comparte las funciones de pigmento, escamas y pelaje del material de malla. La demo conserva color, patrón y relieve de escamas al usar respaldo implícito; el material canino usa fibras procedurales evaluadas en el punto de impacto. El interruptor `--no-fur` también controla esas fibras.
- Se corrigió el desplazamiento de las capas de pigmento en el shader común: comienzan en la fila 34 de la receta de 46 filas, igual que `SurfaceRecipe_Compile`.

## Comportamiento según latencia

| Worker | Representación visible actual | Límite |
|---|---|---|
| A tiempo | Malla explícita certificada; las regiones estables se deforman. | El resultado puede usar remallado exacto completo si la predicción del worker falla. |
| Ligeramente retrasado | Vértices topológicos congelados; los estables se mueven si la interfaz pasa la auditoría. | El pie conserva la edad certificada durante ese intervalo corto. |
| Varios segundos retrasado | SDF exacto a la edad visual en la demo. | El respaldo todavía dibuja el cuerpo entero y omite el detalle de malla/pelaje. |

## Validación ejecutada

`make -B -j6 test demos` pasó después de incorporar la apariencia y la ruta del primer fotograma. La prueba de 3 s de retraso comprueba movimiento del cuerpo estable limitado por la validez de la interfaz, pie congelado, rechazo de tokens fuera de orden y publicación exacta a edades 0,75 y 1,00. `test_morph_exact_sdf_appearance_age` compara la receta del SDF con cinco edades visuales. El capturador de la demo también acepta un respaldo implícito antes del primer `readyMesh`.
Tras añadir la comprobación explícita de inversión en caras mixtas, `make test` y `make demos` volvieron a pasar. La regresión asíncrona mide cero caras invertidas en la interfaz a edad 0,26 mientras el pie permanece certificado a 0,25.

La demo de edad fija publicó mallas exactas con 85 354 vértices a edad 0,00 (3 943 ms), 323 968 a edad 0,50 (12 435 ms) y 170 265 a edad 1,00 (4 814 ms). Las capturas de autopodio posterior están en `build/morph-safety/dog-foot-close.png`, `intermediate-foot-close.png` y `lizard-foot-close.png`. `intermediate-foot-exact.png` confirma que la forma extraña de los dedos a edad 0,50 coincide con el SDF exacto; no procede de triángulos plegados en esa captura.

Dos recorridos de 30,3 s de la demo, con y sin respaldo implícito, llegaron a las diez capturas de un ciclo perro → lagarto → perro. En ambos el desfase geométrico llegó a 0,95 unidades de edad. Sin respaldo, la captura de edad 1,00 mostraba un pie antiguo fuera de cámara. Con respaldo, las capturas a 0,49, 0,75, 1,00 y durante el regreso mostraron los pies descritos por el SDF de cada edad. La comparación no es una medición aislada del worker: cambió la representación visible y se ejecutó con pantalla y GPU reales.

| Métrica durante fotogramas con display inicializado | Sin respaldo | Con respaldo |
|---|---:|---:|
| Fotogramas visibles en el recorrido | 1 094 | 362 |
| Fotogramas con SDF exacto | 0 | 337 |
| GPU p95 de render | 7,26 ms | 96,95 ms |
| Caras opuestas máximas en mallas publicadas | 15 | 20 |

Estos números muestran el coste del respaldo completo: preserva la forma visible, pero no cumple el objetivo de rendimiento. El contador de caras opuestas usa normales de malla; todavía faltan gradiente SDF local, autointersecciones y auditoría de cada parche.

La nueva captura de cabeza canina con material implícito está en `build/morph-safety/dog-head-exact-material.png`; las capturas intermedia y final del pie con pigmento/escamas están en `intermediate-foot-exact-material.png` y `lizard-foot-exact-material.png`. En primer plano de la cabeza canina, 80 fotogramas SDF con y sin fibras dieron respectivamente 157,3/419,7 ms y 155,4/419,8 ms de GPU p50/p95. La evaluación adicional de fibras fue pequeña frente al trazado completo en ese encuadre, pero el respaldo implícito sigue siendo demasiado costoso. Son dos corridas interactivas, no un benchmark aislado ni comparables directamente con la tabla de cámara general anterior.

El recorrido final `build/morph-safety/cycle-material-correct.csv` completó perro → lagarto → perro en 313 fotogramas. Usó respaldo exacto en 312, con desfase geométrico máximo de 1,00 y GPU p50/p95 de 57,55/113,10 ms durante los fotogramas de respaldo. Las capturas `cycle-material-correct-00.png`, `-02.png`, `-05.png`, `-07.png` y `-09.png` muestran las edades de ambos sentidos. La captura inicial se hizo con cero vértices explícitos publicados, pero el SDF ya era visible. Este recorrido demuestra que el resultado visible no depende de que el primer worker termine; no demuestra aún que el coste sea adecuado para tiempo real.

## Trabajo pendiente para cerrar la arquitectura

1. Definir una sección proximal estable y una frontera de vértices compartidos por módulo antes de que diverjan los dedos. `MorphSurfacePatcher_TryModule` sigue descubriendo bordes después de seleccionar triángulos problemáticos; no satisface esta condición.
2. Extraer y publicar mallas SDF locales con conectividad variable por pie/mano/cola. La ruta actual puede caer en remallado exacto del cuerpo completo.
3. Compartir muestras en bloques SDF persistentes, ensuciar solo los bloques afectados y publicar trabajos independientes por módulo/bloque. Un único worker aún construye snapshots completos.
4. Integrar el SDF local en el renderizador con recorte de la malla estable por la misma interfaz. El respaldo actual es del cuerpo completo y pierde pelaje y detalle de superficie al activarse.
   El trazador ya conserva pigmento, escamas y material de pelaje en la superficie exacta, pero no dibuja todavía pelos de cobertura ni un contorno de conchas como la ruta de malla; la silueta canina de respaldo sigue siendo lisa.
   La distinción actual entre piel y cavidades del shader implícito usa la luminosidad del color SDF; debe sustituirse por identidad de material compilada para admitir recetas de piel muy oscura sin ambigüedad.
5. Medir y bloquear autointersecciones, lazos inesperados y orientación respecto al gradiente SDF por parche; ejecutar varios ciclos completos de regresión con retrasos y completaciones fuera de orden reales.

No debe considerarse esta etapa como la corrección final de topología local.
