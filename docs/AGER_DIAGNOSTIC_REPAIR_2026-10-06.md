# Correcciones del diagnóstico perro ↔ lagarto — 6 de octubre de 2026

## Estado real

**PARTIALLY IMPLEMENTED.** Se han reparado errores concretos de trazado, invalidación, finalización y aceptación, y reducido trabajo GPU. La misión de 30 FPS estables sin defectos visuales **no está cumplida**. Las capturas actuales siguen rechazando la cobertura regional y la auditoría de extracción encuentra triángulos opuestos y degenerados.

Base de comparación: `75b199b1fa69a3c1a0772c4c61d104eea8529742`. La referencia se compiló en un worktree separado; el diagnóstico original se conserva sin modificaciones. Equipo medido: AMD Radeon Graphics/Renoir, Mesa 25.2.8, OpenGL 4.6, 1100 × 820, voxel 0,0175 y dos iteraciones de refinamiento. Los perfiles finales incluyen manifest de dispositivo, revisión, argumentos, modo y calidad. No se cambió la velocidad normal, el voxel ni la morfología.

En esta primera fase el arranque todavía utilizaba `gpu-sdf-dense`. Posteriormente se restauró `sdf-raymarch` como ruta interactiva para corregir la regresión a menos de 1 FPS; véase [la reparación interactiva](AGER_INTERACTIVE_REPAIR_2026-10-06.md). El disperso sigue siendo una selección explícita; mejorar su coste no acredita cobertura anatómica. Se corrigió también el target `visual-morph-ownership`: ahora selecciona realmente `legacy-handoff` al probar retraso del worker.

## Cambios implementados

- **Primera raíz propietaria:** el trazador continúa después de una raíz rechazada por cesión, evita repetir su banda y busca impactos posteriores en los intervalos restantes. También refina cruces de signo en el trazador normal. No cambia el criterio espacial de propiedad ni certifica el collar entre malla y campo.
- **Cola de raíces dispersas:** cada arista modificada entra una sola vez por generación. Un despacho indirecto procesa esa cola en lugar de los 2.097.152 slots de la tabla hash. Conserva claves compartidas, posiciones y normales. Coste adicional: aproximadamente 8 MiB GPU.
- **Poda conservadora GPU:** utiliza los grupos contiguos y cotas ya construidos por el núcleo, tanto para distancia como para material. Conserva el orden de smooth unions y respeta listas de tiles filtradas. `MONSTER_SDF_NO_GROUP_PRUNING=1` permite A/B.
- **Retirada y asignación:** la limpieza de tombstones realiza una consulta por slot, sin recorrer cadenas enteras; las reservas terminadas dejan de repetir CAS. La retirada de bricks también actualiza aristas y contadores cuando no existe remuestreo.
- **Caché densa estática:** reutiliza la salida cuando la escena empaquetada y la retícula son idénticas. Mover la cámara no remalla; cambiar el campo o el refinamiento invalida la salida. Durante un morph cambiante sigue siendo extracción global. Colores empaquetados también pueden invalidar esta caché.
- **CPU dispersa:** dos arreglos reutilizables y comparación ordenada por identidad sustituyen malloc/free por frame y búsquedas cuadráticas de features. Los dominios actuales, anteriores y retirados siguen ensuciándose.
- **Finalización:** `Submit` registra envío, sin marcar CLEAN. `Commit` se ejecuta después de comprobar fence y overflow, sin limpiar versiones posteriores. Un error de capacidad detiene esa instancia; esto no implementa conservación mediante doble buffer de la última geometría válida.
- **Aceptación:** defectos topológicos, degenerados, orientación, datos no finitos y residuales fuera de tolerancia producen fallo efectivo. La auditoría de capacidad también falla ante overflow/integridad inválida. El comparador regional comprueba profundidad en ambos sentidos, incluye features de un píxel, deja cobertura no observada pendiente y condiciona aceptación a normales/material cuando se solicitan. Sus límites son explícitos; los gates de píxel no certifican por sí solos la primera raíz.
- **Perfiles:** `--gpu-trace` registra wall también en legacy. El CSV distingue envío y finalización dispersos. `analyze_morph_profile.py` deduplica consultas y relaciona tiempos con la generación realmente medida; informa consultas pendientes y todos los frames sobre presupuesto.

## Mediciones y evidencia

A edad fija 0,35, con pelaje habilitado en la ruta GPU existente:

| Ensayo | Muestreo inicial GPU | Emisión/raíces inicial GPU | Dibujo inicial GPU | Total inicial GPU |
|---|---:|---:|---:|---:|
| Referencia original, 8 frames | 1544,29 ms | 401,01 ms | 85,83 ms | 2053,99 ms |
| Final, poda de grupos desactivada, 16 frames | 1581,96 ms | 61,62 ms | 97,01 ms | 1762,36 ms |
| Final, poda activada, 16 frames | 1428,28 ms | 41,25 ms | 61,47 ms | 1553,61 ms |

Son ensayos cortos, no una aceptación temporal ni un benchmark estadístico de arranque. La comparación A/B final se ejecutó consecutivamente, sin otro consumidor de GPU. El total GPU p50 de esos perfiles, incluyendo la primera extracción, pasó de 89,09 a 59,59 ms: todavía supera 33,33 ms. El muestreo completo sigue dominando una actualización.

El denso estático deja de remuestrear sus 211.208.130 esquinas después del primer frame; la prueba sintética acredita la invalidación al modificar geometría/refinamiento. Esto no acelera un morph global mediante seguimiento superficial.

Cinco recorridos muestreados por las edades del diagnóstico, seguidos de saltos abruptos, ejecutaron 94 frames y terminaron con salida 0. Wall: p50 2297,86 ms, p95 2826,79 ms, p99 3176,30 ms, máximo 3619,76 ms; **94/94 exceden presupuesto**. Se recogieron 92 consultas GPU; las dos últimas quedaron pendientes. Esos recorridos discretos no sustituyen cinco ciclos continuos ni certifican las últimas generaciones sin inspección adicional. El registro incluye el arranque; no se excluyó calentamiento.

Los CSV, manifests, resúmenes y logs relevantes están en [validation/diagnostico-20261006](validation/diagnostico-20261006). Capturas y salidas extensas de esta sesión están en `build/diagnostico-20261006/`.

## Validación ejecutada

- `make -B -j6 test demos build/validate_morph_gpu_regressions`: todas las suites pasan; compilación sin warnings nuevos.
- `make visual-morph-gpu-regressions`: pasa en GPU real. Dos esferas superpuestas verifican que una primera raíz ajena no oculta la esfera propietaria posterior, en trazado normal y de referencia. El mismo test **falla contra el renderer original**. También comprueba caché e invalidación densa y paridad CPU/compute/fragment en once edades, incluidas vueltas; máximo observado 0,00004083.
- `python3 tools/test_validate_ager_features.py`: cuatro contraejemplos pasan; profundidad adelantada/retrasada, feature de un píxel perdida, cobertura pendiente y profundidad idéntica.
- Equivalencia original/final a edad 0,35: 384.564 triángulos, 192.288 aristas únicas y firmas idénticas de triángulos, posiciones y normales. Imagen y profundidad se comparan también, sin pelaje ni MSAA.
- Matriz regional inicial: edad 0,35, vistas 3/4/5/7, con historia. Historia y asignador pasan; geometría regional falla.
- Matriz cefálica final: edades 0/0,35/0,9/1, vista 4, con historia. Profundidad de ida/vuelta idéntica, igualdad de bricks/aristas, overflow e integridad inválida cero. Cobertura regional **falla en las cuatro edades**. Prueba neutra sin pelaje; no acepta apariencia ni silueta de pelo.
- `--surface-mode=gpu-sdf-sparse --age=.35 --validate-compute --frames=1 --no-fur`: termina con **salida 2**, como corresponde a 108 triángulos opuestos y 314 degenerados. Antes esa misma clase de defecto podía devolver éxito. Fronteras y no manifold son cero; eso no acredita fidelidad.
- `git diff --check` y compilación sintáctica de los scripts Python: pasan.

## Trabajo pendiente para cumplir la misión

1. Extraer features subvoxel mediante intervalos y refinamiento con fronteras conformes. Las celdas de ocho esquinas con signo uniforme siguen perdiendo superficies interiores; refinar raíces de aristas no las recupera.
2. Versionar eventos cefálicos de superficie y publicar remallado local con cuello certificado. El grafo esquelético no acredita pinnae ni cavidades.
3. Resolver propiedad malla/SDF sobre un dominio común o un collar coincidente. Continuar tras una raíz rechazada corrige un fallo del trazador, pero no la complementariedad de máscaras evaluadas en puntos diferentes.
4. Evaluación localizada del campo por brick con dependencias conservadoras; colas de retiro/referencias que eliminen los barridos globales restantes. La poda de grupos evita trabajo, pero no convierte millones de muestras en seguimiento incremental.
5. Publicación con conservación de última generación válida ante capacidad insuficiente, certificación geométrica completa y pelaje sobre la superficie aceptada.
6. Ejecutar aceptación continua completa: cámaras, workers legacy, temporalidad, materiales, pelaje y ningún frame sobre 33,33 ms. No se ha validado esa matriz ni rendimiento NVIDIA.
