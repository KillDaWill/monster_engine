# Coordinación — programa de optimización de pelo largo y construcción procedural

**Coordinador:** agente lead de OpenCode
**Repositorio:** `KillDaWill/monster_engine`
**Baseline revisado:** `bc83fd586d7b1b8edae881c65ee25e96536ac08f`
**HEAD de `main` al arrancar:** `bc83fd5` (coincide con el baseline revisado)
**Estado:** ola 1 en ejecución

## Documentos de referencia

- `AGENTS.md` — contratos del repositorio.
- `OPTIMIZACION_CRECIMIENTO_PELO.md` — baseline medido y estrategias.
- `PROMPTS.md` — misión autocontenida de cada agente.
- `README.md` — contexto general del motor.
- `INFORME_OPTIMIZACION_MONSTER_ENGINE_SDF_PELO_2026-11-05.md`: **no existe en este checkout**. El coordinador trabaja con `AGENTS.md`, `OPTIMIZACION_CRECIMIENTO_PELO.md` y `PROMPTS.md`; se deja constancia para que ningún agente invente su contenido.

## Baseline no negociable

- 288 guías visibles, 9.216 hijos con raíz independiente.
- 857.381 llamadas SDF por reconstrucción adulta (705.205 auditoría, 51.079 lift/raíz, 101.097 guía/colisión).
- ~1,47 s por paso de crecimiento; 21 pasos ~31 s.
- Campo real de construcción: unión `min(bodySDF, jawRestDistance)`.
- `MonsterSDF_GetCellRangeWrapper` no cubre necesariamente todas las configuraciones; el SDF aproximado no es 1-Lipschitz demostrado.
- `range == UNKNOWN/false` ⇒ auditoría exacta original, nunca espacio libre.
- Objetivo: ≤100 ms p95 por rebuild (aspiracional ≤50 ms), sin perder 288/9.216 raíces, IDs estables, semillas deterministas, transaccionalidad, colisión de mandíbula, cero penetración GPU observada ni aspecto visual.
- Prohibido: tocar shader/receta/densidad/margen de colisión/capturas de referencia para hacer pasar el benchmark; submuestrear la auditoría; desactivar fallback tubular; editar fuentes generadas a mano.

## Aislamiento de trabajo

Cada agente trabaja en un worktree disperso propio (sin los PPM grandes de `docs/validation`):

| Agente | Rama | Worktree | Ola |
|---|---|---|---|
| A | `agent/a` | `/mnt/storage/monster-engine-agents/agent-a` | 1 |
| B | `agent/b` | `/mnt/storage/monster-engine-agents/agent-b` | 1 |
| C | `agent/c` | `/mnt/storage/monster-engine-agents/agent-c` | 1 |
| D | `agent/d` | `/mnt/storage/monster-engine-agents/agent-d` | 1 |
| J | `agent/j` | `/mnt/storage/monster-engine-agents/agent-j` | 1 |
| E | `agent/e` | pendiente (se crea desde main tras A+B+C) | 2 |
| F | `agent/f` | pendiente (tras D+E) | 3 |
| G | `agent/g` | pendiente (tras F) | 4 |
| H | `agent/h` | pendiente (tras G) | 5 |
| I | `agent/i` | pendiente (tras H) | 6 |

Reglas:

- Ningún agente hace commit en `main`, push, reset ni force-push.
- Solo se editan archivos del ownership asignado. Los archivos compartidos (`src/LongHair.c`, `src/MonsterVisual.c`, `src/MonsterVisualAsync.c`, `src/MonsterSDF.c`, `src/SDFSamplingPool.c`, `Makefile`) quedan bloqueados hasta que el coordinador asigne al integrador de la ola.
- Los worktrees son dispersos: si falta un documento de referencia, se lee del checkout principal por ruta absoluta; no se copia ni se commitea.
- `PROMPTS.md` y `OPTIMIZACION_CRECIMIENTO_PELO.md` están sin seguimiento en `main`; no deben commitearse desde las ramas de agente.
- No se toca `docs/validation/long-hair-styling/resultado/` (referencia congelada). Las capturas van a `/mnt/storage/monster-engine-agents/scratch/<agente>/`.

## Ventanas de medición

La máquina tiene 12 CPUs, 14 GB de RAM, GPU AMD Renoir / Mesa 25.2.8 y DISPLAY `:0`.

- Compilación funcional concurrente: `make -j2` como máximo por agente (5 agentes ≈ 10 hilos).
- Cualquier benchmark, captura OpenGL o medición que se vaya a reportar debe ejecutarse dentro de una ranura exclusiva:
  ```sh
  /mnt/storage/monster-engine-agents/run-bench.sh <comando>
  ```
  El wrapper usa `flock` sobre `/mnt/storage/monster-engine-agents/bench.lock` y registra la carga antes/después.
- No se comparan tiempos tomados con cargas distintas. Registrar `uptime`, versión de gcc, flags y SHA.
- `make test`/`make editor-test` funcionales pueden correr fuera de la ranura, pero no se usan como benchmark.
- El disco raíz está casi lleno: todo artefacto temporal va a `/mnt/storage/monster-engine-agents/scratch/` o al `build/` del worktree. Evitar `/tmp` del sistema salvo archivos pequeños.

## Grafo de dependencias

1. **Ola 1:** A, B, C, D, J en paralelo. A/B/C/D/J solo añaden archivos nuevos y prueban de forma aislada; no editan archivos compartidos ni `Makefile`.
2. **Ola 2 (E):** tras aceptar A+B+C. E es el único escritor de `LongHair.c`/`MonsterVisual.c`; integra certificación conservadora con fallback exacto.
3. **Ola 3 (F):** tras aceptar D y E. F es el único escritor del camino caliente e integra el scheduler de D.
4. **Ola 4 (G):** tras F medido y sin regresiones. Optimiza el cuello real restante, no una caché especulativa.
5. **Ola 5 (H):** tras G. Reutilización incremental con invalidación demostrada contra el campo completo cuerpo+mandíbula.
6. **Ola 6 (I):** tras H. Edad/scrubbing no bloqueante con publicación coherente cuerpo+pelo de la misma generación.
7. **J** entrega su matriz por especie por separado; sus hallazgos no se mezclan en el PR de pelo sin autorización y medición.

## Puertas del coordinador tras cada integración

- Revisar archivos cambiados, ownership y API pública.
- `make test`, `make editor-test`, `git diff --check`.
- `python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /mnt/storage/monster-engine-agents/scratch/validation-<ola>`.
- Comparar 4 etapas: 288/288 guías, 9.216/9.216 raíces propias, IDs estables, digest de raíces/descriptores, campo cuerpo+mandíbula, cero penetración GPU, paridad visual side/front/rear/root y crecimiento.
- Comparar p50/p95/max, llamadas SDF exactas, intervalos, fallback, hilos y memoria. Una reducción de llamadas que aumente el tiempo total se rechaza o queda como modo experimental.
- TSan/ASan cuando sea viable. Si no se puede verificar una afirmación visual/GPU, se marca `unverified` y no se acepta como completa.

## Plantilla de reporte de agente

```
BASE_SHA:
OUTPUT_SHA:
CHANGED_FILES:
REQUESTED_SHARED_EDITS:
TESTS_AND_RESULTS:
DIFFERENTIAL_RESULT:
MEASURED_PERFORMANCE_OR_NOT_MEASURED:
LIMITATIONS:
READY_FOR_NEXT_AGENT:
```

## Registro de integraciones

| Ola | Agente | BASE_SHA | OUTPUT_SHA | Estado | Notas |
|---|---|---|---|---|---|
| 1 | A | `bc83fd5` | — | en ejecución | harness de baseline |
| 1 | B | `bc83fd5` | — | en ejecución | intervalos conservadores |
| 1 | C | `bc83fd5` | — | en ejecución | cotas Bézier/AABB |
| 1 | D | `bc83fd5` | — | en ejecución | scheduler determinista |
| 1 | J | `bc83fd5` | — | en ejecución | perfil por especie |
| 2 | E | — | — | bloqueado | espera A+B+C |
| 3 | F | — | — | bloqueado | espera D+E |
| 4 | G | — | — | bloqueado | espera F |
| 5 | H | — | — | bloqueado | espera G |
| 6 | I | — | — | bloqueado | espera H |
