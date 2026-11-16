# Expediente completo del Caballo — Monster Engine (v3, commit `4ac3f80`)

Carpeta recopilatoria actualizada con la reconstrucción craneofacial y anatómica del **Caballo** (`id = 1005`) en el motor (`monster_engine`, rama `main`, commit `4ac3f80`, SHA-256 de receta `4e1a333adee3a9f73cc7a8a3251bff4912f075662122ee8fbe6f1bf66e0a9e74`), incluyendo recetas, referencias anatómicas reales, capturas multi-ángulo y por partes, animaciones, código/especificaciones de validación y metadatos estructurados.

---

## 1. Cambios principales en v3 (Cabeza y rostro)

- **Mandíbula y cierre bucal (`src/Head.c`, `src/MonsterSDF.c`):** rama y ángulo mandibular anidados bajo el carrillo buccinador sin barra ventral desprendida; sínfisis y labio inferior compactos con apertura articulada válida (`closed`, `partial`, `open`, `eat`, `attack`) y sin `jawNotch`.
- **Cráneo, puente nasal y flujo facial (`src/Head.c`, `src/SurfaceMapper.c`):** eliminación de bulbos yugales/temporales posteriores, perfil craneal-frontal-nasal continuo sin escalón en la frente, y flujo tangencial de pelaje continuo a lo largo del puente nasal sin remolino en línea media (`x = 0`).
- **Órbitas, ollares, orejas y pigmento compartido (`src/Head.c`, `src/MonsterSDF.c`, `recipes/caballo.creature.json`):** ojos recogidos en cuencas orbitales cálidas sin anillo circular oscuro; ollares oblicuos en lágrima sobre el ala nasal carnosa; orejas proporcionadas al cráneo; y dominio de pigmentación del morro sincronizado en los cuatro estadios (`seed..adult`).

---

## 2. Estructura de la carpeta

- [recetas/](recetas/) — Receta canónica reconstruida (`caballo.creature.json`), variante folicular de crin larga (`caballo-crin-foliculos.creature.json`), `catalog.txt`, `README.md` y `generate_equine_recipes.py`.
- [referencias_anatomicas/](referencias_anatomicas/) — 5 fotografías anatómicas reales de referencia (`01_cuerpo_perfil.jpg` .. `05_perfil_cabeza_cuello_crin.jpg`).
- [imagenes/](imagenes/) — Renderizados PNG generados con las herramientas OpenGL/CPU del motor sobre el commit `4ac3f80`:
  - `cuerpo_y_partes/estadio_0_seed/` .. `estadio_3_adulto_superficie/` y `estadio_3_adulto_arcilla_musculos/` (`anatomy.txt`, `jaw.txt`).
  - `morfologia_intermedia/edad_035/` y `edad_070/`.
  - `morro_y_superficie/estadio_2_juvenile/` y `estadio_3_adulto/`.
  - `animaciones/` y `animaciones_cabeza/` (16 animaciones, `_sheet_caballo_*.png` y `metrics.json` / `summary.json`).
  - `pelaje_largo_crin_cola/` y `variante_crin_foliculos/`.
- [codigo_y_specs/](codigo_y_specs/) — Pruebas unitarias y validadores (`test_equine_anatomy.c`, `validate_equine_anatomy.py`, `validate_equine_muscles.py`, `validate_equine_muzzle.py`).
- [metadatos/](metadatos/) — `caballo_metadatos.json`, `manifiesto_archivos.json`, `capture_logs.txt`.

---

## 3. Resumen técnico observado al generar las capturas v3

| Estadio / Edad | Vértices | Triángulos | Componentes conexas | Marcha (`walk`) |
| --- | ---: | ---: | ---: | --- |
| `stage = 0` (`seed`) | 93 801 | 187 084 | 1 | `CAPTURED` |
| `stage = 1` (`larva`) | 100 178 | 200 206 | 1 | `CAPTURED` |
| `stage = 2` (`juvenile`) | 115 963 | 231 792 | 1 | `CAPTURED` |
| `stage = 3` (`adult`  superficie) | 108 127 | 216 140 | 1 | `CAPTURED` |
| `stage = 3` (`adult`  arcilla) | 108 127 | 216 140 | 1 | `CAPTURED` |
| `age = 0.35` (morfo) | 100 610 | 201 066 | 1 | `CAPTURED` |
| `age = 0.70` (morfo) | 115 089 | 230 033 | 1 | `CAPTURED` |

- **Pelo largo (crin y cola, `caballo.creature.json`, estadio 3):** `474` raíces (`472` visibles), `11664` folículos, `21136` hebras (`24` segmentos), `penetrating = 0`, error máximo CPU/GPU `1.43206285e-06`.
- **Variante folicular (`caballo-crin-foliculos.creature.json`, estadio 3):** `288` raíces (`288` visibles), `9216` folículos, `9216` hebras, `penetrating = 0`, error CPU/GPU `1.43051147e-06`.
- **Materiales/Pigmento GPU del morro (estadio 3):** paridad válida en todos los frames y patrones (`valid = 1`), malla asíncrona canónica (`async vertices = 80742 canonical = 1`).
