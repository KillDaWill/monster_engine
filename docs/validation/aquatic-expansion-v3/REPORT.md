# Monster Engine — Expansión Acuática V3: Refinamiento Anatómico y Reparación Visual

**Commit base revisado:** `f7add0f7bf3ee8281acb506a6fa2a15854ef470a` (`feat(anatomy): add aquatic anatomical modules and five aquatic species (1021-1025)`)  
**Recetas actualizadas (IDs 1021–1025):** `recipes/atun_rojo.creature.json`, `recipes/tiburon_martillo.creature.json`, `recipes/caballito_mar.creature.json`, `recipes/tortuga_marina.creature.json`, `recipes/delfin_mular.creature.json`  
**Generador determinista:** `tools/generate_five_aquatic.py`  
**Galería de validación V3:** `docs/validation/aquatic-expansion-v3/sheets/`

---

## 1. Resumen de Archivos Modificados

| Área | Archivos | Propósito |
| --- | --- | --- |
| **Fenotipos y Esquema (v1 compatible)** | `include/CreaturePhenotype.h`, `src/CreaturePhenotype.c`, `src/Fin.c`, `src/CreatureMorph.c`, `tools/generate_recipe_schema.py`, `src/CreatureRecipeSchema.inc` | Campos opcionales reutilizables `TailPhenotype.caudalKeelStrength` (quillas caudales laterales) y `FinPhenotype.freeRearTip` (punta posterior libre de aletas) con normalización, interpolación, morphing y esquema JSON regenerado. |
| **Propulsores caudales y pedúnculo** | `src/Tail.c`, `src/MonsterSDF.c`, `src/SupplementaryMesh.c` | Perfil terminal de transición para colas con propulsor caudal (eliminación total del muñón terminal en escotadura), constructor dedicado `EmitCetaceanFlukes` con escotadura mediana abierta y `EmitPeduncleKeelRidge` para quillas caudales centrales y accesorias. |
| **Cabezas acuáticas y boca hidrodinámica** | `src/Head.c` | Perfiles mandibulares propios (`hasMandibularProfile = true`) y landmarks orales ajustados para `TELEOST`, `ELASMOBRANCH`, `SYNGNATHID`, `TESTUDINE` y `CETACEAN` (eliminando rebabas labiales horizontales); cefalofoil festoneado continuo (*Sphyrna lewini*) sin lóbulos posteriores; transición cervical continua en delfín; cráneo compacto y abombado en tortuga y caballito. |
| **Proyección SDF y mallas suplementarias** | `src/SupplementaryMesh.c`, `src/LimbFin.c`, `src/AxialBodyCurvedVertical.c` | Proyector reutilizable `ProjectOntoSDFSurface` (sphere-tracing + bisección) para opérculo, espiráculo dorsal, 5 hendiduras branquiales curvas, placas cefálicas de tortuga (1 par de prefrontales), coroneta orgánica y armadura segmentada por longitud de arco (11 anillos troncales + 35 caudales con crestas longitudinales); garras en aletas marinas (`clawScale > 0`); espaldar de borde liso sin festoneado artificial y plastrón contorneado con suturas anatómicas; eliminación de barras oscuras proximales sobre el plastrón (`LimbFin.c`); suavizado de hombros en `AxialBodyCurvedVertical.c`. |
| **Recetas deterministas y pruebas** | `tools/generate_five_aquatic.py`, `recipes/*.creature.json`, `tests/test_aquatic_anatomy.c` | Regeneración determinista de las 5 recetas con contracoloración ventral realista, microescamas en atún, moteado en caballito de mar, garras en tortuga, quillas caudales y punta posterior libre en 2.ª dorsal de martillo; nueva batería cuantitativa `test_aquatic_v3_anatomical_repairs`. |

---

## 2. Causas Raíz y Reparaciones Transversales Reutilizables

1. **Muñón caudal terminal (`Tail.c` + `MonsterSDF.c` + `SupplementaryMesh.c`):**  
   *Causa:* `Tail_Resolve` mantenía radios distales gruesos y el barrido SDF en `MonsterSDF.c` remataba con una tapa hemisférica sobresaliendo detrás de `tipNode`, mientras `BuildCaudalPropulsors` desplazaba `rootCenter` demasiado hacia adelante (`+0.36 * chord`).  
   *Reparación:* Ahusamiento terminal progresivo para `t > 0.58` en `Tail.c`, retracción controlada de la última estación SDF en `MonsterSDF.c` dentro de la raíz anterior de la aleta, anclaje posterior limpio en `BuildCaudalPropulsors` y geometría dedicada `EmitCetaceanFlukes` con puente peduncular y escotadura mediana genuinamente abierta.
2. **Rebaba oral horizontal (`Head.c`):**  
   *Causa:* Los arquetipos `TELEOST`, `CETACEAN`, `SYNGNATHID` y `TESTUDINE` no definían `hasMandibularProfile`, heredando el fallback de `jawStations` con anchuras de cráneo completo y `cornerX = rootWidth * 0.88`.  
   *Reparación:* Perfiles orales y mandibulares propios (`hasMandibularProfile = true`) para los 5 arquetipos acuáticos, con comisuras estrechas alineadas al rostro/hocico y articulación mandibular verificada tanto cerrada (`0.0`) como abierta (`0.55`).
3. **Detalles ocultos por radios teóricos (`SupplementaryMesh.c`):**  
   *Causa:* Opérculo, espiráculo, branquias y anillos se colocaban sobre `cranialRadii * 1.01`, quedando enterrados tras el suavizado SDF.  
   *Reparación:* `ProjectOntoSDFSurface` proyecta cada detalle sobre la superficie cero real del `MonsterSDF` resuelto con normal saliente por diferencias finitas y desplazamiento controlado anti-z-fighting.
4. **Contracoloración ventral (`Surface.c` / `generate_five_aquatic.py`):**  
   *Causa:* `ventralLightening = 0.42` limitaba la mezcla ventral al 42 % incluso en `ventral = 1.0`.  
   *Reparación:* Ajuste de `ventralLightening` (`0.92–0.94`), `dorsalDarkening` y `ventralColor` en las especies pelágicas, verificado en CPU (`Surface_EvaluatePigmentAlbedo`, luminancia ventral > `0.72`) y render GPU.

---

## 3. Evaluación Anatómica por Especie y Evidencia Visual (Antes v2 vs. Después v3)

### 3.1 Atún Rojo (*Thunnus thynnus*, ID 1021)
- **Comparativa antes/después:** [compare_v2_v3_tuna.png](sheets/compare_v2_v3_tuna.png)
- **Vistas V3 completas:** [tuna_v3_main.png](sheets/tuna_v3_main.png) | [tuna_v3_extra.png](sheets/tuna_v3_extra.png) | [tuna_v3_neutral.png](sheets/tuna_v3_neutral.png)
- **Resultado:** Cuerpo fusiforme liso con microescamas sutiles (`relief=0.012`), margen opercular curvo visible tras el ojo, aletas pectorales cortas y oscuras (< 80 % longitud cefálica), boca cónica limpia sin rebaba lateral, quilla lateral central y quillas accesorias en el pedúnculo caudal, cola lunada simétrica sin muñón y vientre plateado brillante.

### 3.2 Tiburón Martillo Común (*Sphyrna lewini*, ID 1022)
- **Comparativa antes/después:** [compare_v2_v3_hammerhead.png](sheets/compare_v2_v3_hammerhead.png)
- **Vistas V3 completas:** [hammerhead_v3_main.png](sheets/hammerhead_v3_main.png) | [hammerhead_v3_extra.png](sheets/hammerhead_v3_extra.png) | [hammerhead_v3_neutral.png](sheets/hammerhead_v3_neutral.png)
- **Resultado:** Cefalofoil arqueado y convexo con muesca central prominente y festoneado lateral característico de *S. lewini*, eliminación total de los lóbulos posteriores artificiales, 5 hendiduras branquiales curvas proyectadas al SDF, 2.ª dorsal baja con punta posterior libre larga (`freeRearTip=1.10`), pélvicas con degradado natural sin puntas blancas artificiales, cola heterocerca sin muñón y vientre crema pálido.

### 3.3 Caballito de Mar de Hocico Largo (*Hippocampus reidi*, ID 1023)
- **Comparativa antes/después:** [compare_v2_v3_seahorse.png](sheets/compare_v2_v3_seahorse.png)
- **Vistas V3 completas:** [seahorse_v3_main.png](sheets/seahorse_v3_main.png) | [seahorse_v3_extra.png](sheets/seahorse_v3_extra.png) | [seahorse_v3_neutral.png](sheets/seahorse_v3_neutral.png)
- **Resultado:** Eliminación completa de escamas reptilianas hexagonales; armadura ósea continua parametrizada por longitud de arco con **11 anillos troncales** y **35 anillos caudales** más crestas longitudinales; hombros suavizados en `AxialBodyCurvedVertical.c`; coroneta ósea orgánica redondeada; aletas dorsal y pectorales finas con radios; patrón moteado pardo y blanco (`PigmentLayer` x 2); cabeza compacta sin boca sobresaliente; preservación de la aleta caudal juvenil temporal que desaparece en adulto.

### 3.4 Tortuga Marina Verde (*Chelonia mydas*, ID 1024)
- **Comparativa antes/después:** [compare_v2_v3_turtle.png](sheets/compare_v2_v3_turtle.png)
- **Vistas V3 completas:** [turtle_v3_main.png](sheets/turtle_v3_main.png) | [turtle_v3_extra.png](sheets/turtle_v3_extra.png) | [turtle_v3_neutral.png](sheets/turtle_v3_neutral.png)
- **Resultado:** Cráneo más compacto y abombado con placas cefálicas semánticas proyectadas al SDF (incluyendo **1 par único de escamas prefrontales** entre los ojos, placa frontal, supraoculares y postorbitales); borde del espaldar completamente liso sin festoneado artificial manteniendo 4 pares costales y 5 vertebrales; eliminación de las barras oscuras proximales sobre el plastrón; plastrón contorneado con suturas anatómicas legibles y puente lateral curvo; uña queratinizada anclada en el borde de ataque de las aletas (`clawScale > 0`).

### 3.5 Delfín Mular (*Tursiops truncatus*, ID 1025)
- **Comparativa antes/después:** [compare_v2_v3_dolphin.png](sheets/compare_v2_v3_dolphin.png)
- **Vistas V3 completas:** [dolphin_v3_main.png](sheets/dolphin_v3_main.png) | [dolphin_v3_extra.png](sheets/dolphin_v3_extra.png) | [dolphin_v3_neutral.png](sheets/dolphin_v3_neutral.png)
- **Resultado:** Rostro grueso continuo y comisura bucal limpia sin rebaba horizontal; **espiráculo dorsal único en media luna** claramente visible detrás del melón en `head-top` y `top`; transición occipital-cervical continua sin cintura marcada; flukes horizontales dedicados con escotadura mediana limpia y cero muñón caudal; contracoloración ventral pálida natural; sin garras en las aletas pectorales.

---

## 4. Validación de Desarrollo, Nado, Boca Abierta, Morph y Offspring V4

- **Etapas de desarrollo (`seed 0.00`, `larva 0.33`, `mid 0.50`, `juvenile 0.66`, `adult 1.00`):** [development_stages_v3.png](sheets/development_stages_v3.png)
- **Animación de nado (5 modos sin remallado SDF):** [swimming_poses_v3.png](sheets/swimming_poses_v3.png)
- **Apertura y cierre oral (`opening = 0.0` vs `0.55`):** [mouth_open_closed_v3.png](sheets/mouth_open_closed_v3.png)
- **Pruebas automatizadas (`make test` + `make editor-test` + `make demos`):**
  - `make editor-test`: **PASS**
  - `./run_tests` (todas las suites, incluyendo `test_aquatic_v3_anatomical_repairs`, 7 pares de morphing bidireccionales × 7 fases y 11 pares × 2 órdenes = 44 híbridos de `CreatureOffspring` V4 incluyendo la regresión `Elefante × Halcón`): **PASS**
  - `make demos`: **PASS** (0 advertencias bajo `-Wall -Wextra -Wpedantic -O2`)

---

## 5. Tabla de Resolución de Defectos (G01–G06, T01–T08, H01–H06, S01–S07, C01–C07, D01–D06)

| ID | Defecto | Código modificado | Validación | Estado | Evidencia |
| --- | --- | --- | --- | --- | --- |
| **G01** | Muñón caudal terminal en peces/tiburón/delfín | `src/Tail.c`, `src/MonsterSDF.c`, `src/SupplementaryMesh.c` | Unit test + capturas `tail-top`/`tail-three-quarter` | **RESUELTO** | `compare_v2_v3_tuna.png`, `compare_v2_v3_hammerhead.png`, `compare_v2_v3_dolphin.png` |
| **G02** | Rebaba labial horizontal en cabezas hidrodinámicas | `src/Head.c` (`hasMandibularProfile` + comisura) | Unit test + `head-front` + `mouth_open_closed_v3.png` | **RESUELTO** | `compare_v2_v3_tuna.png`, `compare_v2_v3_dolphin.png`, `mouth_open_closed_v3.png` |
| **G03** | Detalles suplementarios enterrados en el SDF | `src/SupplementaryMesh.c` (`ProjectOntoSDFSurface`) | Unit test + `head-side`/`head-top` | **RESUELTO** | `compare_v2_v3_tuna.png`, `compare_v2_v3_dolphin.png`, `compare_v2_v3_turtle.png` |
| **G04** | Contracoloración ventral débil | `tools/generate_five_aquatic.py`, `recipes/*.json` | CPU albedo test + capturas `bottom` | **RESUELTO** | `compare_v2_v3_tuna.png`, `compare_v2_v3_hammerhead.png`, `compare_v2_v3_dolphin.png` |
| **G05** | Falta de capturas de desarrollo y nado | Pipeline V3 (`build/capture_creature`) | 5 etapas × 5 especies + 5 modos de nado | **RESUELTO** | `development_stages_v3.png`, `swimming_poses_v3.png` |
| **G06** | Compatibilidad Morph y CreatureOffspring V4 | `src/CreatureMorph.c`, `src/CreaturePhenotype.c`, `src/Fin.c`, `tests/test_aquatic_anatomy.c` | 7 pares morph × 2 dir × 7 fases + 44 híbridos V4 | **RESUELTO** | `tests/test_aquatic_anatomy.c` (PASS) |
| **T01** | Escamas reptilianas gruesas en atún | `tools/generate_five_aquatic.py`, `recipes/atun_rojo.creature.json` | Unit test + `head-side`/`tail-three-quarter` | **RESUELTO** | `compare_v2_v3_tuna.png` |
| **T02** | Opérculo oculto en atún | `src/SupplementaryMesh.c` (`ProjectOntoSDFSurface`) | `head-side` + `side` | **RESUELTO** | `compare_v2_v3_tuna.png`, `tuna_v3_main.png` |
| **T03** | Pectorales largas y azul claro | `tools/generate_five_aquatic.py` (`span=0.72`, `pec_dark`) | `top` + `side` | **RESUELTO** | `tuna_v3_main.png` |
| **T04** | Rebaba bucal frontal en atún | `src/Head.c` (`HEAD_ARCHETYPE_TELEOST`) | `head-front` + boca abierta/cerrada | **RESUELTO** | `compare_v2_v3_tuna.png`, `mouth_open_closed_v3.png` |
| **T05** | Vientre grisáceo en atún | `tools/generate_five_aquatic.py` (`ventralLightening=0.94`) | CPU albedo + `bottom` | **RESUELTO** | `compare_v2_v3_tuna.png` |
| **T06** | Ausencia de quillas caudales | `include/CreaturePhenotype.h`, `src/SupplementaryMesh.c` (`EmitPeduncleKeelRidge`) | Unit test + `tail-three-quarter`/`tail-top` | **RESUELTO** | `compare_v2_v3_tuna.png`, `tuna_v3_extra.png` |
| **T07** | Muñón caudal en atún | `src/Tail.c`, `src/MonsterSDF.c`, `src/SupplementaryMesh.c` | Unit test + `tail-three-quarter` | **RESUELTO** | `compare_v2_v3_tuna.png` |
| **T08** | Ojos algo saltones en atún | `src/Head.c`, `tools/generate_five_aquatic.py` | `head-front` + `head-top` | **RESUELTO** | `compare_v2_v3_tuna.png` |
| **H01** | Margen anterior recto en cefalofoil | `src/Head.c` (`HEAD_ARCHETYPE_ELASMOBRANCH`) | Unit test + `head-top` | **RESUELTO** | `compare_v2_v3_hammerhead.png` |
| **H02** | Lóbulos posteriores artificiales tras martillo | `src/Head.c` (cráneo/cuello esbelto + mandíbula propia) | `head-top` + `bottom` | **RESUELTO** | `compare_v2_v3_hammerhead.png` |
| **H03** | 2.ª dorsal sin punta posterior libre larga | `include/CreaturePhenotype.h`, `src/Fin.c`, `src/SupplementaryMesh.c`, `generate_five_aquatic.py` | Unit test (`freeRearTip=1.10`) + `side` | **RESUELTO** | `compare_v2_v3_hammerhead.png`, `hammerhead_v3_main.png` |
| **H04** | Puntas blancas artificiales en pélvicas | `tools/generate_five_aquatic.py` (`pelvic_tip`) | `side` + `bottom` | **RESUELTO** | `compare_v2_v3_hammerhead.png` |
| **H05** | Muñón caudal en martillo | `src/Tail.c`, `src/MonsterSDF.c`, `src/SupplementaryMesh.c` | Unit test + `tail-three-quarter` | **RESUELTO** | `compare_v2_v3_hammerhead.png` |
| **H06** | Vientre poco luminoso en martillo | `tools/generate_five_aquatic.py` (`ventralLightening=0.92`) | CPU albedo + `bottom` | **RESUELTO** | `compare_v2_v3_hammerhead.png` |
| **S01** | Aspecto de escamas reptilianas en caballito | `tools/generate_five_aquatic.py` (`SMOOTH_SKIN` bajo armadura) | `side` + `head-side` | **RESUELTO** | `compare_v2_v3_seahorse.png` |
| **S02** | Armadura segmentada insuficiente | `src/SupplementaryMesh.c` (11 troncales + 35 caudales por longitud de arco + crestas) | Unit test + `side`/`front`/`tail-three-quarter` | **RESUELTO** | `compare_v2_v3_seahorse.png`, `seahorse_v3_main.png` |
| **S03** | Hombros angulosos | `src/AxialBodyCurvedVertical.c` | `front` + `rear` | **RESUELTO** | `compare_v2_v3_seahorse.png`, `seahorse_v3_extra.png` |
| **S04** | Coroneta poligonal rígida | `src/SupplementaryMesh.c` (coroneta orgánica de 16 segs y 5 lóbulos suaves) | `head-side` + `head-front` | **RESUELTO** | `compare_v2_v3_seahorse.png`, `seahorse_v3_extra.png` |
| **S05** | Aletas dorsal/pectorales gruesas | `tools/generate_five_aquatic.py`, `src/SupplementaryMesh.c` | `side` + `top` | **RESUELTO** | `seahorse_v3_main.png` |
| **S06** | Ausencia de moteado pardo/blanco | `tools/generate_five_aquatic.py` (`PigmentLayer` × 2) | Unit test + `head-side`/`side` | **RESUELTO** | `compare_v2_v3_seahorse.png` |
| **S07** | Cabeza globular y ojos saltones | `src/Head.c`, `tools/generate_five_aquatic.py` | `head-front` + `head-side` | **RESUELTO** | `seahorse_v3_extra.png` |
| **C01** | Cabeza demasiado ancha/plana en tortuga | `src/Head.c`, `tools/generate_five_aquatic.py` | `head-front` + `head-top` | **RESUELTO** | `compare_v2_v3_turtle.png` |
| **C02** | Falta de placas cefálicas (1 par prefrontal) | `src/SupplementaryMesh.c` (`EmitProjectedQuadPatch`) | `head-top` + `head-front` + `head-side` | **RESUELTO** | `compare_v2_v3_turtle.png`, `turtle_v3_main.png` |
| **C03** | Barras oscuras de extremidades sobre plastrón | `src/LimbFin.c`, `src/SupplementaryMesh.c` | `bottom` + `side` | **RESUELTO** | `compare_v2_v3_turtle.png` |
| **C04** | Borde del espaldar festoneado | `src/SupplementaryMesh.c` (`BuildAnatomicalShell`, `edgeFade`) | `side` + `tail-three-quarter` | **RESUELTO** | `compare_v2_v3_turtle.png`, `turtle_v3_main.png` |
| **C05** | Ausencia de uña en aletas de tortuga | `src/SupplementaryMesh.c` (`BuildMarineFlippers`), `generate_five_aquatic.py` | Unit test (`clawScale=0.85`, delfín `0.0`) + `top`/`bottom` | **RESUELTO** | `turtle_v3_main.png`, `compare_v2_v3_turtle.png` |
| **C06** | Plastrón blanco sin suturas | `src/SupplementaryMesh.c` (`BuildAnatomicalShell`) | `bottom` | **RESUELTO** | `compare_v2_v3_turtle.png` |
| **C07** | Ojos saltones y boca plana en tortuga | `src/Head.c` (`HEAD_ARCHETYPE_TESTUDINE`) | `head-front` + `head-side` | **RESUELTO** | `compare_v2_v3_turtle.png` |
| **D01** | Rebaba labial horizontal en delfín | `src/Head.c` (`HEAD_ARCHETYPE_CETACEAN`) | Unit test + `head-front` + `mouth_open_closed_v3.png` | **RESUELTO** | `compare_v2_v3_dolphin.png`, `mouth_open_closed_v3.png` |
| **D02** | Espiráculo dorsal oculto | `src/SupplementaryMesh.c` (`ProjectOntoSDFSurface`, media luna) | `head-top` + `top` | **RESUELTO** | `compare_v2_v3_dolphin.png`, `dolphin_v3_main.png` |
| **D03** | Estrechamiento cervical tras el cráneo | `src/Head.c`, `tools/generate_five_aquatic.py` | `top` + `head-top` + `bottom` | **RESUELTO** | `dolphin_v3_main.png`, `compare_v2_v3_dolphin.png` |
| **D04** | Muñón caudal entre los flukes | `src/Tail.c`, `src/MonsterSDF.c`, `src/SupplementaryMesh.c` (`EmitCetaceanFlukes`) | Unit test + `tail-top`/`tail-three-quarter` | **RESUELTO** | `compare_v2_v3_dolphin.png`, `dolphin_v3_main.png` |
| **D05** | Contraste ventral bajo en delfín | `tools/generate_five_aquatic.py` (`ventralLightening=0.92`) | CPU albedo + `bottom` | **RESUELTO** | `compare_v2_v3_dolphin.png` |
| **D06** | Silueta facial y ojos del delfín | `src/Head.c`, `tools/generate_five_aquatic.py` | `head-front` + `head-side` | **RESUELTO** | `dolphin_v3_main.png`, `dolphin_v3_extra.png` |

---

## 6. Rendimiento y Limitaciones Restantes

- **Comparativa de rendimiento (`test_aquatic_anatomy`, 5 especies × 5 edades = 25 construcciones):**
  - *Baseline v2:* `build=1.6ms`, `sdf=0.6ms`, `supp=4.6ms`, morph `0.14 ms/eval`, offspring V4 (44 híbridos) `1201.9 ms`.
  - *V3 (con proyección SDF exacta sphere-traced para opérculo, espiráculo, branquias, placas cefálicas y 46 anillos óseos del caballito de mar):* `build=1.7ms`, `sdf=0.6ms`, `supp=274.6ms` (~`11.0 ms`/criatura, frente a `414.0ms` antes del sphere-tracing), morph `0.12 ms/eval`, offspring V4 `1139.3 ms`. El coste de `SupplementaryMesh_Build` se ejecuta una sola vez al construir/remallar la criatura (0 coste por fotograma durante la animación).
- **Limitaciones conocidas:**
  - Los microdetalles proyectados sobre la superficie SDF (opérculo, espiráculo dorsal, hendiduras branquiales, placas cefálicas y anillos óseos) viven en `SupplementaryMesh` con skinning esquelético exacto para mantener el coste del campo SDF principal constante; en trazadores GPU puros de SDF sin malla suplementaria dependen del renderizador híbrido.
