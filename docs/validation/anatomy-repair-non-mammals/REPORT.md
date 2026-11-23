# Informe de Reparación Anatómica — Especies No-Mamíferas (1016, 1018, 1019)

**Fecha:** 2026-11-23  
**Commit base de referencia:** `08700490107fd8e868db80a0f9ea2e73ba052a7b`  
**Especies reparadas:**
1. **Halcón Peregrino** (*Falco peregrinus*, ID `1016`) — `recipes/halcon_peregrino.creature.json`
2. **Libélula Emperador** (*Anax imperator*, ID `1019`) — `recipes/libelula_emperador.creature.json`
3. **Pulpo Gigante del Pacífico** (*Enteroctopus dofleini*, ID `1018`) — `recipes/pulpo_gigante.creature.json`

**Especies preservadas sin cambios funcionales:** Mantarraya Gigante (`1017`) y Medusa Ortiga (`1020`), además de todas las especies mamíferas y reptiles existentes.

---

## 1. Diagnóstico de Causas Raíz (Pre-Reparación)

Antes de editar código o recetas se inspeccionaron las capturas de referencia de `five_non_mammals_report.zip`, se generaron capturas base en modo **neutral** y **surface**, y se ejecutó un volcado diagnóstico de nodos, cajas SDF y aislamiento por módulo.

### 1.1 Halcón Peregrino (ID 1016)
- **H1 — Cráneo esférico sobredimensionado:** `Head.c` escalaba `HEAD_ARCHETYPE_AVIAN` sobre el bloque genérico de `HeadModule` con `sweptSkull=false`, resultando en `craniumRadii=(0.233, 0.159, 0.216)` (~98 % del ancho del tórax) y engullendo `faceRoot`/`faceMid` dentro de la esfera.
- **H2–H3 — Sin pico rapaz 3D ni cierre mandibular limpio:** `MonsterSDFMouth` no consumía `isBeak`; solo proyectaba un hocico genérico minúsculo y el recortador oral mamífero `entrance`/`oralPlane` seccionaba el gancho al abrir la boca.
- **H4–H5 — Ojos caricaturescos y cuello mal integrado:** Órbitas y aros orbitales desproporcionados sin continuidad cervical (`neckCollarContinuity=0`).
- **H6 — Alas mecánicas con abanicos sueltos:** `LimbAvian.c` emitía segmentos sólidos desnudos hasta la mano distal y el álula, y `SupplementaryMesh.c` carecía de un perfil alar emplumado continuo (propatagio + coberteras escapulares).
- **H7 — Extensión caudal rígida:** `Tail.c` emitía toda la cadena de `TAIL_ARCHETYPE_FEATHER_FAN` como volumen sólido `BODY_CONNECTION_AXIAL_LOFT`, creando un palo rígido largo antes de las rectrices.
- **H9 — Pigmentación facial y tarsos amarillos ausentes:** Faltaban dominios cefálicos anclados (casco + bigoteras malares); además, se descubrió un fallo transversal del motor en `shaders/monster_mesh.vert` / `shaders/monster_surface.frag`, donde `regionalPigment` se calculaba en el *vertex shader* pero no se reenviaba al *fragment shader* (`regionalPigmentOut`), anulando `regionalAlbedo` en mallas estándar.

### 1.2 Libélula Emperador (ID 1019)
- **D1 — Primer par de patas naciendo de la cabeza:** En `AxialBodyArthropod.c`, el nodo `pro` (`z=0.3423`) alojaba simultáneamente `AXIAL_SLOT_CERVICAL` y `AXIAL_SLOT_THORACIC_LEG_1_*`, mientras la cápsula cefálica SDF se extendía atrás hasta `z=0.1499`.
- **D2 — Cabeza ancha tipo rana y muesca bucal:** `Head.c` aplicaba `x*1.12, y*0.98, z*0.68` sobre `HeadModule` (`craniumRadii=(0.314, 0.118, 0.111)`, relación ancho/alto `2.65:1`), conservaba mejillas/rebordes orbitales mamíferos y `MonsterSDF.c` seguía recortando `oralPlane` en artrópodos/cefalópodos, creando una hendidura rectangular con dos esquinas colgantes.
- **D3 — Ojos con aspecto vertebrado y brillo húmedo:** Se renderizaban con `PUPIL_ROUND` y `cornealGloss=0.92`.

### 1.3 Pulpo Gigante del Pacífico (ID 1018)
El aislamiento geométrico por componentes (`04_octopus_diagnostic_isolation.png`) demostró sin ambigüedad el origen de cada defecto:
- **O1 — Pico dorsal agudo y mamelón posterior:** `AxialBodyCephalopod.c` saltaba de `ax[4]` a `ax[5]` (`w=0.471, h=0.448, y=0.757`) y caía bruscamente a `ax[6]` (`0.187`) y una estación extra `ax[7]` (`0.127`), formando un pico dorsal y un mamelón terminal.
- **O2 — Cabeza aplanada en forma de hocico y pupila vertical:** `Head.c` carecía de perfil craneal cefalópodo (`hasCranialProfile=false`) y proyectaba un rostro frontal adelantado; además, `generate_five_non_mammals.py` pasaba `pupilShape: 1` (`PUPIL_VERTICAL`) en lugar de `2` (`PUPIL_HORIZONTAL`).
- **O3 — Sifón sobresaliendo como clavija ciega:** Confirmado como módulo `20` (`AXIAL_SLOT_SIPHON`), sobresalía lateralmente (`length=0.36`) sin abertura terminal modelada.

---

## 2. Mejoras Reutilizables del Motor

| Subsistema | Archivos modificados | Cambio reutilizable |
| --- | --- | --- |
| **Sistema paramétrico de pico aviar 3D** | `include/Head.h`, `include/MonsterSDF.h`, `src/Head.c`, `src/HeadBlend.c`, `src/MonsterSDF.c`, `src/MonsterSDFRayBounds.h`, `shaders/sdf_scene.glsl`, `tools/generate_sdf_shader.py`, `tools/generate_recipe_schema.py` | Perfil 3D Bézier cuadrático de culmen con cera, diente tomial opcional, arista dorsal, mandíbula inferior articulada sobre `jawPivot`, narinas sobre cera y materiales separados (cera/cuerno/anillo orbital). Compatible con perfiles rapaces ganchudos, cónicos cortos, rectos largos y curvos tipo loro. |
| **Cabezas aviar, artrópoda y cefalópoda** | `src/Head.c`, `src/MonsterSDF.c` | Perfiles craneales barridos (`hasCranialProfile=true`) específicos para aves, insectos y cefalópodos; supresión de masas faciales mamíferas; guarda `entranceHalfExtents.y >= 0.00095f` en `oralPlane` para evitar muescas bucales vertebradas en invertebrados. |
| **Anatomía aviar (torso, alas, cola)** | `src/AxialBodyAvian.c`, `src/LimbAvian.c`, `src/Tail.c`, `src/SupplementaryMesh.c` | Pecho aquillado suave en gota; alas con núcleo proximal cubierto y mano distal en `BODY_CONNECTION_SUPPORT`; perfil alar emplumado 3D continuo (propatagio + rémiges + terciarias + 2 filas de coberteras) válido en vuelo y plegado; uropigio corto (`i==0`) con abanico de 12 rectrices + 6 coberteras. |
| **Tagmosis artrópoda y ojos compuestos** | `src/AxialBodyArthropod.c`, `src/LimbArthropod.c`, `include/Eye.h`, `src/EyeTexture.c`, `src/Creature.c`, `src/SupplementaryMesh.c` | Separación explícita cabeza / cuello / protórax / sintórax oblicuo; 6 patas estrictamente torácicas (par 1 detrás de la cápsula cefálica); nuevo `PUPIL_COMPOUND` con omatidios hexagonales procedurales, gradiente dorsal-ventral y pseudopupila difusa + `cornealGloss=0.34f`; alas `nu=16`. |
| **Manto ovoide y sifón con embudo abierto** | `include/Anatomy.h`, `src/AxialBodyCephalopod.c`, `src/FlexibleAppendage.c`, `src/MonsterSDF.c`, `shaders/sdf_scene.glsl`, `src/SupplementaryMesh.c` | Manto cefalópodo sobre sección elipsoidal suave sin estación terminal extra; nuevo conector `BODY_CONNECTION_FUNNEL_OPENING` con cavidad circular centrada `MonsterSDF_FunnelConnectorCavity` (CPU + GLSL + intervalo analítico) y lumen oscuro; espaciado de boquilla del sifón para evitar oclusión por el segmento previo; membrana interbraquial reforzada. |
| **Albedo regional en shader de malla** | `shaders/monster_mesh.vert`, `shaders/monster_surface.frag`, `src/MonsterSurfaceShader.generated.h` | Propagación de `regionalPigmentOut` del *vertex shader* al *fragment shader*, restaurando paridad entre CPU, morph y renderizado estándar para `regionalAlbedo` (tarsos/dedos amarillos del halcón, cabeza/cuello verde y patas oscuras de la libélula). |
| **Generador y recetas canónicas** | `tools/generate_five_non_mammals.py`, `recipes/halcon_peregrino.creature.json`, `recipes/libelula_emperador.creature.json`, `recipes/pulpo_gigante.creature.json` | Proporciones, pigmentación malar/casco anclada al nodo cefálico, `PUPIL_COMPOUND` en libélula y `PUPIL_HORIZONTAL` en pulpo. `mantarraya.creature.json` y `medusa_ortiga.creature.json` sin cambios. |

---

## 3. Evidencia Visual Generada

Todos los paneles se conservan en `docs/validation/anatomy-repair-non-mammals/` sin sobrescribir `docs/validation/five-non-mammals/`:

- [`00_neutral_geometry_repaired.png`](00_neutral_geometry_repaired.png) — Vista neutral gris sin pigmentación de las tres especies reparadas (lateral, 3/4, dorsal, cabeza lateral y frontal).
- [`01_halcon_peregrino_before_after.png`](01_halcon_peregrino_before_after.png) — Halcón Peregrino: antes vs después, pico articulado abierto (`0.55`) y alas plegadas (`sprawl=0.95`).
- [`02_libelula_emperador_before_after.png`](02_libelula_emperador_before_after.png) — Libélula Emperador: antes vs después, separación cabeza-protórax-pata I, ojos compuestos hexagonales y etapa ninfa (`age=0.20`).
- [`03_pulpo_gigante_before_after.png`](03_pulpo_gigante_before_after.png) — Pulpo Gigante: antes vs después, manto ovoide liso, cabeza sin hocico con pupila horizontal, sifón en embudo abierto y pose de reptación.
- [`04_octopus_diagnostic_isolation.png`](04_octopus_diagnostic_isolation.png) — Aislamiento diagnóstico por componentes del pulpo (cuerpo axial, sifón en rojo, corona de brazos y prueba sin cap terminal).

---

## 4. Tabla de Defectos (Estado Final)

| Código | Descripción | Estado | Evidencia |
| --- | --- | --- | --- |
| H1–H5 | Cabeza esférica, pico inexistente, mandíbula extraña, ojos caricaturescos, cuello | **Resuelto** | `01_halcon_peregrino_before_after.png`, `TestFalconAnatomicalInvariants` |
| H6–H8 | Alas mecánicas, extensión caudal rígida, silueta de torso aviar | **Resuelto** | `00_neutral_geometry_repaired.png`, `01_halcon_peregrino_before_after.png` |
| H9–H10 | Casco, bigoteras malares, cera/patas amarillas, reconocibilidad | **Resuelto** | `01_halcon_peregrino_before_after.png` |
| D1 | Primer par de patas naciendo de la cabeza | **Resuelto** | `leg1.z=0.267 < headMinZ=0.333`, `TestDragonflyAnatomicalInvariants` |
| D2–D3 | Cabeza ancha tipo rana, muesca facial, ojos vertebrados húmedos | **Resuelto** | `02_libelula_emperador_before_after.png`, `PUPIL_COMPOUND` |
| D4 | Refinamiento de patas, alas, coloración y ninfa | **Resuelto** | `02_libelula_emperador_before_after.png` |
| O1 | Pico dorsal y mamelón posterior del manto | **Resuelto** | `03_pulpo_gigante_before_after.png`, `TestOctopusAnatomicalInvariants` |
| O2 | Cabeza tipo hocico y pupila vertical errónea | **Resuelto** | `03_pulpo_gigante_before_after.png`, `PUPIL_HORIZONTAL` |
| O3 | Sifón sobresaliendo como clavija sin boca | **Resuelto** | `BODY_CONNECTION_FUNNEL_OPENING`, `dMouth > 0 && dWall < 0` |
| O4 | Raíces de brazos, membrana interbraquial y pose orgánica | **Resuelto** | `03_pulpo_gigante_before_after.png` |

---

## 5. Verificación Ejecutada

- **`make test`** (`./run_tests`, incluidas las nuevas pruebas de invariantes anatómicos de halcón, libélula y pulpo en `tests/test_five_non_mammals.c`): **PASS** completo.
- **`make editor-test`**: **11/11 PASS** (esquema JSON v1, round-trip, transacciones de agente y mallado de etapas).
- **`DISPLAY=:0 make visual-morph-gpu-regressions`** (NVIDIA RTX 3050, OpenGL 4.6): **PASS** (`error_rgb=0`, paridad compute/fragment `< 1.43e-6`).
- **`DISPLAY=:0 make visual-morph-ownership`**: **PASS** (`referencia=25459, extra=89 (0.003), faltante=2 (0.000)`).
- **`make benchmark-animation`**: `vértices=101333, frames=180, pose_ms=0.289, deformación_ms=18.874, malloc/calloc/realloc=0, SDF_Build=0, SDFMesher=0`.

---

## 6. Limitaciones Conocidas

- En `capture_creature` con malla de resolución estándar (`voxel=0.018`), el barrado ventral fino del halcón es deliberadamente sutil bajo iluminación difusa frontal y depende del contraste de la capa pigmentaria regional `SURFACE_REGION_VENTRAL_TRUNK`.
- El diente tomial del halcón es un detalle milimétrico del borde del pico que se resuelve a resoluciones cefálicas altas (`head-side`), mientras que en vistas lejanas se integra suavemente en el perfil ganchudo sin artefactos de malla.
