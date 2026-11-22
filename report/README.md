# Reporte de Revalidación — Cinco Nuevos Mamíferos (`monster_engine`)

Generado: `2026-11-21T23:24:05.794652+00:00`

## Resumen por Receta

| Especie | ID | Archivo | GPU 4 Etapas | Vértices Adulto | Fotos PNG | Carpeta |
| --- | --- | --- | --- | --- | --- | --- |
| Jirafa (*Giraffa camelopardalis*) | `1011` | `jirafa.creature.json` | PASS (0..3) | 240559 | 35 | `jirafa/` |
| Ornitorrinco (*Ornithorhynchus anatinus*) | `1012` | `ornitorrinco.creature.json` | PASS (0..3) | 514863 | 31 | `ornitorrinco/` |
| PangolinChino (*Manis pentadactyla*) | `1013` | `pangolin.creature.json` | PASS (0..3) | 661871 | 31 | `pangolin/` |
| Canguro Rojo (*Osphranter rufus*) | `1014` | `canguro.creature.json` | PASS (0..3) | 101090 | 33 | `canguro/` |
| Murcielago Orejudo (*Plecotus auritus*) | `1015` | `murcielago.creature.json` | PASS (0..3) | 370921 | 31 | `murcielago/` |

## Estructura del Paquete

- `summary.json`: resumen estructurado de todas las recetas y resultados.
- `global_checks/run_tests.log`: ejecución completa de la suite C (`run_tests`).
- `<especie>/recipe.creature.json`: receta validada.
- `<especie>/metadata/`: `summary.json`, `gpu_validation.txt` (etapas 0..3), `capture_log.txt`, `animation_metrics.json`, `animation_capture_log.txt` y `bridge_roundtrip.json`.
- `<especie>/photos/`: vistas adultas (`surface`), render raymarched GPU (`gpu_sdf_stage3.png`), subcarpeta `stages/` (`seed` y `juvenile`) y subcarpeta `animations/`.
