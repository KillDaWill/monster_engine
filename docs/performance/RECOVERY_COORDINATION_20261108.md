# Recuperación de rendimiento — registro acumulativo, 2026-11-08

Este informe nuevo no modifica el ledger histórico. El prompt de recuperación del
usuario sustituye expresamente la dependencia E→F. Todos los resultados de tiempos
históricos siguen siendo históricos, no mediciones del nuevo runtime.

## Entrada 01 — recuperación verificada

- Checkout original: `/home/guillermo/Escritorio/monster_engine`, `main`
  `0eb37d709433aa08ef6c3416cd76edf1a26108f0`; `origin/main`:
  `bc83fd586d7b1b8edae881c65ee25e96536ac08f`.
- Integración anterior disponible en Git y limpia antes de añadir copias solicitadas:
  `30e549684a7b7f14d59db290c937ae7184e4f4ca`.
- Nueva integración: `agent/perf-20261108-integration`,
  `/mnt/storage/monster-engine-agents/recovery-20261108/integration`.
- Se preservan la eliminación local del ledger original y los documentos sin seguimiento.
  Worktrees antiguos A/B contienen archivos sin seguimiento; no se modifican.
- ZIP histórico: SHA256 `1afe8ee6b9ccd6c2392acaad216581ff6bb8261ab2210bd9a73f86859886aed8`,
  2507 entradas, CRC comprobado de nuevo. Snapshot `project/` sin `.git`.
- Copias accesibles de los ocho informes: `review-20261106/`; paquete completo:
  `performance-review-20261106.zip`, también en `docs/performance/` del checkout original.
  Estas copias no restauran su ledger borrado.
- D y J recuperados archivo por archivo desde `deliverables/`, comparados byte a byte con
  sus worktrees finales. Ningún cherry-pick masivo ni cambio runtime en esta recuperación.
- CodeGraph no inicializado en la integración anterior; se inspeccionan fuentes actuales.
  No hay `graphify-out/graph.json`. No se reconstruyen índices durante mediciones.
- OpenAI almacenado confirmado mediante `opencode auth list`; catálogo confirma
  `openai/gpt-6-luna#high`. No se cambia proveedor ni autenticación de los agentes.
- Raíz: 5 GB libres; `/mnt/storage`: 1,4 TB libres. Nuevos builds y evidencia en storage.
- Evidencia nueva: `/mnt/storage/monster-engine-agents/scratch/recovery-20261108/`.
  Wrapper exclusivo: `/mnt/storage/monster-engine-agents/run-bench.sh`.

### Inventario histórico (fuentes disponibles; no aceptación runtime)

| Trabajo | SHA final verificado | Estado/uso |
|---|---|---|
| A | `849e88fa145d51a03fb441b17adb8699181675c0` | Harness incorporado; fases internas null |
| B/B3 | `5338c9c9259be5a680af8cedc5c627f5ab46981d` | Diagnóstico UNKNOWN; informe disponible |
| B2 | `301bb4a147f35795a176c32c9a57c3147a9c23aa` | Aislado; no bypass |
| C/C2 | `09470add6d7aac4f04977bba22bf6eaaf350a0f9` | Diagnóstico, no prueba universal CPU/GPU |
| D | `feadfb73fe520ae10d1443284e2cc963483a649f` | Scheduler privado recuperado; repetir tests |
| E | `3927f8088fd2191a1b8fc68047897510b8aa2095` | NO-GO; no bloquea F exacto |
| J | `ef75c8ef5fb6afbc7736aef790af8d480f9d3762` | Harness recuperado; voxel 0,18, CPU mesh-only |

### Matriz inicial

| Tarea | Estado | Dependencias/ownership |
|---|---|---|
| W0 recuperación/baselines | active | Coordinador; benchmarks/informes, runtime intacto |
| F-EXACT | queued | W0 + tests D; LongHair.c/h, scheduler/glue/tests propios |
| M-PROFILE | queued | W0; instrumentación aislada, sin algoritmo runtime |
| H-DESIGN | queued | W0; sólo informe de diseño |
| G-EXACT | blocked | F integrado y validado |
| M-OPT | blocked | Perfil M revisado; PR separado |
| H-IMPLEMENT | blocked | G estable + diseño revisado |
| I-ASYNC | blocked | Runtime incremental aceptado |

### Decisiones y límites

- Preservar las 72 muestras exactas, semillas/vecinos/reintentos, 80 vec4/guía,
  campo completo cuerpo+mandíbula, todas las raíces y publicación transaccional.
- D: destruir sólo tras quiescencia externa; no compartir pool con mesher.
- Comparador A conserva su gate específico de certificación off/on: **no** sirve como
  aceptación automática entre binarios serial/paralelo. Diferencial F debe comparar
  explícitamente datos completos contra la referencia original.
- Mediciones nuevas pendientes; <=100 ms, mejora mesher y UX editor **no demostrados**.
- No hay otro agente ejecutando carga pesada. Wave 1 no se lanza antes de completar W0.
