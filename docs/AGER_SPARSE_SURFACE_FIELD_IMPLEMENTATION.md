# Campo superficial disperso del Ager

Estado global: PARTIALLY IMPLEMENTED. Hay un backend disperso real integrado; no hay aceptación de calidad regional ni de 30–60 FPS durante morph.

Implementación en curso; las comprobaciones regionales prevalecen sobre la métrica global. La resolución objetivo es 0,0175, sin LOD ni relajación de calidad. El modo disperso no será predeterminado hasta superar las puertas completas.

## Fase 0: Modos explícitos y reloj real

STATUS: PARTIALLY IMPLEMENTED

FILES CHANGED: `demos/demo_morph_dog_lizard.c`, `tools/validate_ager_features.py`.

TESTS: Compilación ejecutada; capturas regionales densas en edades 0, 0,5 y 1, vistas 4 y 7. La cobertura de pies se está ampliando.

VISUAL RESULT: La edad 0,5 muestra errores regionales de profundidad en orejas y dígitos; la puntuación global previa no los detectaba. No hay prueba completa de aceptación.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 1 y registrar pruebas ejecutadas.

## Fase 1: Validación anatómica e historia

STATUS: PARTIALLY IMPLEMENTED

FILES CHANGED: `demos/demo_morph_dog_lizard.c`, `tools/validate_ager_features.py`.

TESTS: Compilación ejecutada; capturas regionales densas en edades 0, 0,5 y 1, vistas 4 y 7. La cobertura de pies se está ampliando.

VISUAL RESULT: La edad 0,5 muestra errores regionales de profundidad en orejas y dígitos; la puntuación global previa no los detectaba. No hay prueba completa de aceptación.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 2 y registrar pruebas ejecutadas.

## Fase 2: Diagnóstico geométrico/material

STATUS: FAILED

FILES CHANGED: `demos/demo_morph_dog_lizard.c`, `tools/validate_ager_features.py`.

TESTS: Compilación ejecutada; capturas regionales densas en edades 0, 0,5 y 1, vistas 4 y 7. La cobertura de pies se está ampliando.

VISUAL RESULT: La edad 0,5 muestra errores regionales de profundidad en orejas y dígitos; la puntuación global previa no los detectaba. No hay prueba completa de aceptación.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 3 y registrar pruebas ejecutadas.

## Fase 3: Rejilla y metadatos persistentes

STATUS: PARTIALLY IMPLEMENTED

FILES CHANGED: `include/GpuSDFSurfaceCache.h`, `src/GpuSDFSurfaceCache.c`, `include/MorphSpatialCache.h`, `src/MorphSpatialCache.c`, `src/MonsterSDF.c`.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Control CPU con bricks 16: 0,3–3,9 ms en seis estados; almacenar todos los candidatos requeriría hasta 548 MB. No es rendimiento del backend GPU.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 4 y registrar pruebas ejecutadas.

## Fase 4: Siembra independiente de features

STATUS: PARTIALLY IMPLEMENTED

FILES CHANGED: `include/GpuSDFSurfaceCache.h`, `src/GpuSDFSurfaceCache.c`, `include/MorphSpatialCache.h`, `src/MorphSpatialCache.c`, `src/MonsterSDF.c`.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Control CPU con bricks 16: 0,3–3,9 ms en seis estados; almacenar todos los candidatos requeriría hasta 548 MB. No es rendimiento del backend GPU.

KNOWN LIMITATIONS: Las cotas actuales necesitan contrastarse contra el volumen denso; no son una certificación matemática de todos los subniveles.

NEXT PHASE: Continuar con la fase 5 y registrar pruebas ejecutadas.

## Fase 5: Muestras GPU persistentes

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 6 y registrar pruebas ejecutadas.

## Fase 6: Banda estrecha y residencia

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 7 y registrar pruebas ejecutadas.

## Fase 7: MC por brick y publicación

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 8 y registrar pruebas ejecutadas.

## Fase 8: Aristas globales e índices

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 9 y registrar pruebas ejecutadas.

## Fase 9: Normales desde muestras

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 10 y registrar pruebas ejecutadas.

## Fase 10: Restauración bidireccional

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 11 y registrar pruebas ejecutadas.

## Fase 11: Semántica anatómica

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 12 y registrar pruebas ejecutadas.

## Fase 12: Escamas canónicas

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 13 y registrar pruebas ejecutadas.

## Fase 13: Folículos persistentes

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 14 y registrar pruebas ejecutadas.

## Fase 14: Raycast opcional

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 15 y registrar pruebas ejecutadas.

## Fase 15: Intervalos y poda posteriores

STATUS: PARTIALLY IMPLEMENTED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 16 y registrar pruebas ejecutadas.

## Fase 16: Perfil por etapas

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 17 y registrar pruebas ejecutadas.

## Fase 17: Resolución uniforme

STATUS: PARTIALLY IMPLEMENTED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 18 y registrar pruebas ejecutadas.

## Fase 18: Matriz de aceptación

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 19 y registrar pruebas ejecutadas.

## Fase 19: Selección por defecto

STATUS: FAILED

FILES CHANGED: Pendiente.

TESTS: Pendiente de validación del backend disperso.

VISUAL RESULT: Sin aceptación visual aún.

PERFORMANCE: Sin aceptación de 30–60 FPS.

KNOWN LIMITATIONS: La fase completa todavía no está aceptada.

NEXT PHASE: Continuar con la fase 19 y registrar pruebas ejecutadas.
