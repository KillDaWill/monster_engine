# Expansión de Diversidad Mamífera — Cinco Nuevas Especies y Sistemas Anatómicos Genéricos

Documento técnico de implementación y verificación para la expansión de mamíferos en `monster_engine`:

1. **Jirafa** (*Giraffa camelopardalis*, ID `1011`, [recipes/jirafa.creature.json](../recipes/jirafa.creature.json))
2. **Ornitorrinco** (*Ornithorhynchus anatinus*, ID `1012`, [recipes/ornitorrinco.creature.json](../recipes/ornitorrinco.creature.json))
3. **Pangolín Chino** (*Manis pentadactyla*, ID `1013`, [recipes/pangolin.creature.json](../recipes/pangolin.creature.json))
4. **Canguro Rojo** (*Osphranter rufus*, ID `1014`, [recipes/canguro.creature.json](../recipes/canguro.creature.json))
5. **Murciélago Orejudo Pardo** (*Plecotus auritus*, ID `1015`, [recipes/murcielago.creature.json](../recipes/murcielago.creature.json))

Todas las capacidades anatómicas, esqueléticas, de locomoción y de superficie se diseñaron como extensiones genéricas impulsadas por fenotipo y datos de receta, compatibles con las cuatro etapas de desarrollo (`seed`, `larva`, `juvenile`, `adult`), `MonsterAger`, `CreatureMorph`, `CreatureOffspring`, GPU SDF/fragment shaders y `CreatureVisualizer`.

---

## 1. Capacidades Genéricas Implementadas

| Sistema / Capacidad | Archivos Principales | Especies Beneficiadas |
| --- | --- | --- |
| Cadena cervical multiestación articulada y flexión dorsal continua (`cervicalSegments`, `cervicalPitch`, `spineFlexion`) | `include/CreaturePhenotype.h`, `src/AxialBodyUprightTetrapod.c`, `src/CreatureRig.c`, `src/ProceduralAnimator.c` | Jirafa, Canguro, futuros vertebrados de cuello largo |
| Osiconos craneales con inserción amplia y punta redondeada (`ORNAMENT_ARCHETYPE_OSSICONE`) | `include/CreaturePhenotype.h`, `src/Ornament.c` | Jirafa, ungulados con apéndices cutáneos |
| Pigmentación reticulada poligonal por celdas celulares en CPU y GLSL (`PIGMENT_PATTERN_RETICULATED`) | `include/PigmentPattern.h`, `src/PigmentPattern.c`, `shaders/surface/pigment.glsl` | Jirafa, especies reticuladas |
| Arquetipos cefálicos monotrema y folidoto (`HEAD_ARCHETYPE_MONOTREME`, `HEAD_ARCHETYPE_PHOLIDOTE`, `isBill`) con control de orejas (`hasEars`) | `include/Head.h`, `src/Head.c`, `src/MonsterSDF.c`, `shaders/sdf_scene.glsl` | Ornitorrinco, Pangolín Chino |
| Cola plana dorsoventral tipo remo (`TAIL_ARCHETYPE_PADDLE`) | `include/CreaturePhenotype.h`, `src/Tail.c` | Ornitorrinco, mamíferos semiacuáticos |
| Membranas interdigitales retráctiles (`limbs[].webbing*`) y patagios explícitos entre cadenas del grafo (`Membrane`, `AnatomicalMembraneProfile`, `AnatomyDeformer_BindExplicitSkin`) | `include/Membrane.h`, `src/Membrane.c`, `include/AnatomyDeformer.h`, `src/AnatomyDeformer.c`, `src/MonsterVisual.c`, `src/AnimatedVisual.c` | Ornitorrinco (palmeado retráctil), Murciélago (dactilopatagios, plagiopatagio, propatagio, uropatagio) |
| Escamas queratínicas geométricas imbricadas proyectadas sobre SDF y vinculadas rígidamente por placa (`ArmorScales`, `PlatePhenotype.armorScale`, `defensiveCurl`) | `include/ArmorScales.h`, `src/ArmorScales.c`, `include/PigmentPattern.h`, `src/ProceduralAnimator.c` | Pangolín Chino y futuras especies acorazadas |
| Perfil posterior macrópodo (`LIMB_PROFILE_MACROPOD`) y marcha de salto bípedo sincronizado (`CREATURE_GAIT_BIPEDAL_HOP`) con brazos no ambulatorios | `include/CreaturePhenotype.h`, `include/CreatureRecipe.h`, `src/LimbMammal.c`, `src/GaitPresets.c`, `src/CreatureRig.c`, `src/ProceduralAnimator.c` | Canguro Rojo y saltadores bípedos |
| Arquetipo de extremidad alada (`LIMB_ARCHETYPE_WING`, `LimbBatWing`), pulgar libre con garra y articulación de plegado/batida (`wingFold`, `wingFlap`) | `include/LimbBatWing.h`, `src/LimbBatWing.c`, `src/Limb.c`, `src/ProceduralAnimator.c` | Murciélago Orejudo Pardo y quirópteros |
| Integración completa con herencia, esquema JSON v1 y puente del editor (`CreatureOffspring`, `OffspringRepair`, `creature_editor_bridge`, `recipes/README.md`) | `src/CreatureOffspring.c`, `src/OffspringRepair.c`, `tools/generate_recipe_schema.py`, `tools/creature_editor_bridge.c` | Todas las especies nuevas e híbridos |

---

## 2. Resumen Anatómico y Visual por Especie

### 2.1 Jirafa (`1011`, *Giraffa camelopardalis*)
- **Morfología**: cuello articulado en cadena multiestación sin cortes entre tórax y cráneo, cruz elevada con dorso inclinado, extremidades largas cursoriales, cabeza estrecha y dos osiconos simétricos (`ORNAMENT_ARCHETYPE_OSSICONE`).
- **Superficie**: patrón reticulado estable (`PIGMENT_PATTERN_RETICULATED`) en dominio anatómico sin estiramientos severos entre cuello y tronco.
- **Evidencia visual**: [jirafa_side.png](validation/five-mammals/jirafa_side.png).

### 2.2 Ornitorrinco (`1012`, *Ornithorhynchus anatinus*)
- **Morfología**: cráneo hidrodinámico deprimido sin pabellones auriculares externos (`hasEars = false`), pico en espátula flexible y coriáceo (`HEAD_ARCHETYPE_MONOTREME`, `isBill = true`) con narinas dorsales, extremidades cortas con cinco dedos y garras más membrana interdigital retráctil (`webbingCoverage`, `webbingFold`), y cola ancha aplanada dorsoventralmente (`TAIL_ARCHETYPE_PADDLE`).
- **Evidencia visual**: [ornitorrinco_side.png](validation/five-mammals/ornitorrinco_side.png), [ornitorrinco_head.png](validation/five-mammals/ornitorrinco_head.png).

### 2.3 Pangolín Chino (`1013`, *Manis pentadactyla*)
- **Morfología**: cabeza cónica fusiforme (`HEAD_ARCHETYPE_PHOLIDOTE`) con mandíbula sellada y narinas dorsales sin muesca anterior, garras excavadoras robustas, y coraza real tridimensional de placas queratínicas biseladas e imbricadas (`ArmorScales`) desde la frente y flancos hasta la punta de la cola, deformadas rígidamente hueso a hueso durante el enrollamiento defensivo (`defensiveCurl`).
- **Evidencia visual**: [pangolin_side.png](validation/five-mammals/pangolin_side.png), [pangolin_head.png](validation/five-mammals/pangolin_head.png).

### 2.4 Canguro Rojo (`1014`, *Osphranter rufus*)
- **Morfología**: tronco piriforme semierguido mediante estaciones paramétricas en `AxialBodyUprightTetrapod`, cabeza herbívora estrecha con grandes orejas cóncavas redondeadas, miembros anteriores cortos con cinco dedos garras (`walking = false`), miembros posteriores macrópodos (`LIMB_PROFILE_MACROPOD`) con muslo potente, tibia larga, apoyo tarsometatarsiano horizontal al suelo, cuarto dedo dominante con garra y cola musculosa de contrapeso.
- **Locomoción**: marcha `CREATURE_GAIT_BIPEDAL_HOP` con fase compartida `0.0` en ambos miembros posteriores, `stanceRatio = 0.44` y arco parabólico vertical en fase aérea.
- **Evidencia visual**: [canguro_side.png](validation/five-mammals/canguro_side.png), [canguro_three_quarter.png](validation/five-mammals/canguro_three_quarter.png).

### 2.5 Murciélago Orejudo Pardo (`1015`, *Plecotus auritus*)
- **Morfología**: tórax compacto muscular, rostro corto redondeado con ojos despejados y orejas gigantes curvadas de *Plecotus*, patas posteriores prensiles con cinco garras, cola delgada y alas quiropteras (`LIMB_ARCHETYPE_WING`) compuestas por húmero, radio/cúbito, carpo, pulgar libre con garra y radios alares II–V.
- **Membranas y articulación**: 12 paneles continuos mediante el módulo compartido `Membrane` (6 dactilopatagios interdigitales II–III, III–IV y IV–V + 2 plagiopatagios + 2 propatagios + 2 uropatagios) vinculados al esqueleto para batida (`wingFlap`) y plegado articular sin desgarros (`wingFold`).
- **Evidencia visual**: [murcielago_extended.png](validation/five-mammals/murcielago_extended.png), [murcielago_wing_partial.png](validation/five-mammals/murcielago_wing_partial.png), [murcielago_wing_folded.png](validation/five-mammals/murcielago_wing_folded.png).

---

## 3. Verificación y Resultados Observados

### 3.1 Pruebas Unitarias, Integración y Saneamiento
- `make test`: **PASS** completo en todas las suites, incluida la nueva suite `tests/test_five_mammals.c` (`TestAllFiveRecipesFourStagesAndAger`, `TestGiraffeCapabilities`, `TestPlatypusCapabilities`, `TestPangolinCapabilities`, `TestKangarooCapabilities`, `TestBatCapabilitiesAndOffspring`).
- ASan + UBSan (`-fsanitize=address,undefined -O1 -g`) sobre `tests/test_five_mammals.c`: **0 errores de memoria o comportamiento indefinido** y salida `0` en las 6 subpruebas.
- `make editor-test`: **11/11 pruebas OK** (validación HTTP, esquema JSON v1, orden/ciclos de módulos y ronda completa C-editor).
- `make validate-ecosystem`: **PASS** en todos los escenarios y repetibilidad determinista tras el ajuste de cuello cervical único en `CreatureRig.c`:
  - `A`: `PASS population=0 births=0 deaths=12 kills=0 generation=0`
  - `B`: `PASS population=0 births=7 deaths=15 kills=0 generation=2`
  - `C`: `PASS population=0 births=6 deaths=24 kills=3 generation=1`
  - `D`: `PASS population=0 births=0 deaths=4 kills=0 generation=0`
  - `E`: `PASS population=0 births=13 deaths=27 kills=6 generation=4`
  - `determinism`: `PASS`

### 3.2 Paridad GPU (`build/validate_recipe_gpu`, etapa 3 adulta)

| Receta | Error Pigmento | Error Intervalo SDF (m) | Error Compute (m) | Error Fragment (m) | Resultado |
| --- | --- | --- | --- | --- | --- |
| `recipes/jirafa.creature.json` (1011) | `9.53674316e-07` | `1.44354999e-07` | `7.15255737e-07` | `8.64267349e-07` | PASS |
| `recipes/ornitorrinco.creature.json` (1012) | `5.96046448e-08` | `2.98023224e-07` | `4.76837158e-07` | `4.76837158e-07` | PASS |
| `recipes/pangolin.creature.json` (1013) | `5.96046448e-08` | `4.47034836e-08` | `3.57627869e-07` | `3.57627869e-07` | PASS |
| `recipes/canguro.creature.json` (1014) | `5.96046448e-08` | `1.08331442e-05` | `7.15255737e-07` | `7.15255737e-07` | PASS |
| `recipes/murcielago.creature.json` (1015) | `5.96046448e-08` | `2.98023224e-08` | `4.76837158e-07` | `4.76837158e-07` | PASS |
| `recipes/brumacerno.creature.json` (3, regresión) | `2.98023224e-08` | `1.49011612e-07` | `5.96046448e-07` | `8.34465027e-07` | PASS |
| `recipes/rata.creature.json` (1010, regresión) | `5.96046448e-08` | `5.96046448e-08` | `4.76837158e-07` | `4.76837158e-07` | PASS |

### 3.3 Rendimiento Comparativo vs Línea Base

| Benchmark | Línea Base Inicial | Tras la Expansión | Notas |
| --- | --- | --- | --- |
| `benchmark_anatomy` tamaños | `sizeof_recipe=52592 sizeof_phenotype=12240 sizeof_graph=125984 sizeof_monster=207536` | `sizeof_recipe=56848 sizeof_phenotype=13304 sizeof_graph=125984 sizeof_monster=212880` | +`membranes[12]`, `PlatePhenotype` extendido y controles cervicales |
| `benchmark_anatomy` resolución (`recipe 1..4`) | `27.6 .. 44.3 us` | `28.057 .. 47.186 us` | Mantiene resolución sub-50 us por individuo |
| `benchmark_animation` (101333 vértices, 180 frames) | `pose_ms=0.267`, `deformación_ms=18.453` | `pose_ms=0.272`, `deformación_ms=18.468`, `error_IK_max=0.001991` | Sin regresiones de tiempo ni asignaciones por frame |
| `benchmark_sdf` (Small / Medium / Large / Very Large) | `6.558 / 32.895 / 72.628 / 46.019 ms` | `6.834 / 31.646 / 70.931 / 45.155 ms` | Tiempo SDF/mallado sin regresión |
