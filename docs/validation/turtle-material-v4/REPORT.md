# Monster Engine — Tortuga Marina Verde V4 (*Chelonia mydas*): Rediseño Procedural de Materiales y Texturas

**Commit base revisado:** `85f178034b7ea460bc46b06324638eb6fe018d08`  
**Receta principal actualizada:** `recipes/tortuga_marina.creature.json` (ID `1024`, geometría y anatomía preservadas)  
**Receta complementaria:** `recipes/delfin_mular.creature.json` (ID `1025`, `flipperSurface = 1` explícitamente liso)  
**Generador determinista:** `tools/generate_five_aquatic.py`  
**Galería de validación V4:** `docs/validation/turtle-material-v4/sheets/`

---

## 1. Diagnóstico de Causas Raíz (Defectos M01–M07)

La auditoría del pipeline de materiales y las capturas baseline V3 revelaron cinco causas raíz estructurales:

1. **Bypass del shader de superficie en `v->detailMesh`:** `SupplementaryMesh_Build` fijaba `opaqueOut->hasSurface = false`, haciendo que el espaldar, el plastrón, las aletas marinas y las placas cefálicas se dibujaran mediante el backend fijo heredado (`OpenGLRenderer_RenderMesh` con iluminación OpenGL 1.x y color interpolado por vértice) en lugar de `monster_surface.frag` (`SurfaceLighting`). Esto explicaba la incoherencia lumínica y de rugosidad (**M07**) y la sobreexposición blanca del plastrón y las aletas ventrales (**M03**, **M05**).
2. **Evaluación por vértice sobre mallas gruesas con fórmulas senoidales regulares:** El espaldar (`36×28`), el plastrón (`24×20`), las aletas (`20×12`) y las placas cefálicas (`4×4`) evaluaban toda la textura por vértice con `sin(radAng * 13)` y `sin(u * 6 * PI)`, generando estrías homogéneas repetitivas (**M01**, **M02**), un damero de líneas gruesas rectas en el plastrón (**M03**) y celdas rectangulares `floorf` con bordes claros gruesos en las aletas (**M04**).
3. **Orientación invertida (back-facing winding) en caras del caparazón, plastrón, placas dorsales y aletas derechas:** Bajo `monster_surface.frag` (`if (!gl_FrontFacing) n = -n;`), los quads con devanado invertido respecto a su normal anatómica volteaban la normal hacia el interior del cuerpo, recibiendo solo luz ambiental.
4. **Acoplamiento de aletas escamadas solo a `INTEGUMENT_SCALES`:** `BuildMarineFlippers` no disponía de un perfil semántico independiente para distinguir aletas reptilianas escamadas de aletas cetáceas lisas en morfismos e híbridos.
5. **Desajuste de escala y contraste en cabeza y cuello (M06):** La receta usaba `scale_size = 0.022`, `scale_relief = 0.04` y `coverage = 0.65`, produciendo puntos diminutos de alto contraste en toda la piel facial, mientras las placas cefálicas proyectadas eran parches lisos verde plano con bordes oscuros gruesos.

---

## 2. Arquitectura de Materiales Reutilizable Implementada

| Componente | Archivos | Cambios |
| --- | --- | --- |
| **Materiales semánticos SDF** | `include/SDFOperations.h`, `src/Mesh.c` | Nuevos identificadores `SDF_MATERIAL_CARAPACE` (11), `SDF_MATERIAL_PLASTRON` (12), `SDF_MATERIAL_REPTILIAN_FLIPPER` (13) y `SDF_MATERIAL_CEPHALIC_PLATE` (14), validados en `Mesh_Validate`. |
| **Transporte de material suplementario** | `include/Mesh.h`, `src/OpenGLRenderer.c`, `src/MonsterVisual.c` | Bloque compacto `Mesh.detailMaterial[12][4]` (192 bytes) subido como `uniform vec4 detailMaterial[12]` en `RenderSurfaceMesh` sin alterar `SURFACE_RECIPE_ROWS` (154) ni afectar a especies sin materiales procedurales suplementarios (`detailMesh.hasSurface` permanece `false` en delfín, atún, martillo, caballito, etc.). |
| **Evaluadores procedurales GLSL en espacio local/reposo** | `shaders/surface/reptile_materials.glsl`, `shaders/monster_surface.frag`, `src/MonsterSurfaceShader.generated.h` | Cuatro evaluadores deterministas, anti-aliased (`fwidth`) y anclados en coordenadas locales de reposo (`restCoord`): `EvaluateCarapaceMaterial`, `EvaluatePlastronMaterial`, `EvaluateReptilianFlipperMaterial` y `EvaluateCephalicPlateMaterial`, todos integrados en `SurfaceLighting` y compatibles con los modos `surfaceDebug = 6` (albedo puro), `7/8` (normales) y `10` (arcilla neutra). |
| **Fenotipos, Esquema, Morph y Offspring** | `include/CreaturePhenotype.h`, `src/CreaturePhenotype.c`, `src/CreatureMorph.c`, `src/CreatureOffspring.c`, `tools/generate_recipe_schema.py`, `src/CreatureRecipeSchema.inc` | Campos opcionales retrocompatibles JSON v1 en `ShellPhenotype` (`carapaceDarkColor`, `carapaceAmberColor`, `plastronSeamColor`, `carapaceMottling`, `carapaceRadialStrength`, `carapaceRoughness`, `sutureContrast`, `plastronSeamSoftness`, `plastronMottling`, `plastronRoughness`, `patternSeed`) y en `LimbPhenotype` (`FlipperSurfaceStyle`, `flipperScaleSize`, `flipperScaleIrregularity`, `flipperBorderContrast`, `flipperRoughness`). |
| **Mallas suplementarias y corrección de devanado** | `src/SupplementaryMesh.c` | Coordenadas locales estables por escudo, plastrón, aleta `(u, v, fore)` y placa cefálica `(eu, ev, plateId)`; corrección de devanado front-facing en `caraTop`, `caraRim`, `plasG`, `BuildMarineFlippers` (ambos lados) y `EmitProjectedQuadPatch`; garras con `SDF_MATERIAL_HORN`. |
| **Receta y progresión ontogénica** | `tools/generate_five_aquatic.py`, `recipes/tortuga_marina.creature.json`, `recipes/delfin_mular.creature.json` | Paletas realistas y continuas de *Chelonia mydas* en las 4 etapas (`seed`, `larva`, `juvenile`, `adult`), `flipperSurface = 2` en tortuga, `flipperSurface = 1` en delfín, piel facial/cervical coordinada (`scale_size = 0.062`, `scale_relief = 0.011`, `coverage = 0.30` + capa de moteado sutil). |

---

## 3. Referencias Biológicas (*Chelonia mydas*)

Contrastado frente a las referencias fotográficas y anatómicas de **USFWS**, **NOAA Fisheries**, **NOAA AOML**, **Texas A&M Marine Species**, **ReefGuide** y **Reptiles of Ecuador**:
- **Espaldar:** 5 escudos vertebrales hexagonales entrelazados, 4 pares de escudos costales curvos (1.er par separado del escudo nucal), 11 pares de marginales; cada escudo con semilla, centro de crecimiento (aréola), moteado pardo-oliva/castaño/ámbar y estrías radiales curvas e irregulares; suturas delgadas e irregulares; respuesta de queratina dura sin aspecto plástico.
- **Plastrón:** 6 pares de escudos anatómicos (gular, humeral, pectoral, abdominal, femoral, anal) más 4 escudos inframarginales en el puente lateral; tono marfil-crema cálido con gradiente sutil, moteado mínimo y suturas orgánicas de muy bajo contraste sin rejilla.
- **Aletas:** Mosaico Voronoi deformado de escamas poligonales irregulares oliva oscuro/pardo-gris/carbón en dorsal con progresión continua de tamaño proximal→distal y bordes finos ocre-oliva; cara ventral más clara, desaturada y suave (claramente diferenciada del plastrón) con escamas oscuras dispersas hacia el borde de ataque y la punta; 100 % anclado durante el nado.
- **Cabeza y cuello:** 1 par diagnóstico de escamas prefrontales más frontal, supraoculares, parietales y postorbitales con moteado interno orgánico, bordes finos y paleta unificada con la piel escamada facial y cervical.

---

## 4. Evidencia Visual (Antes V3 vs. Después V4)

- **Comparativa directa Antes V3 / Después V4 (`top`, `bottom`, `side`, `three-quarter`, `head-top`, `head-side`):**  
  [before_after_v3_v4.png](sheets/before_after_v3_v4.png)
- **Vistas generales y pose de nado con aletas flexionadas (`top`, `bottom`, `side`, `three-quarter`, `swim-three-quarter`, `swim-top`):**  
  [v4_overview_and_swim.png](sheets/v4_overview_and_swim.png)
- **Primeros planos de materiales (espaldar, escudo individual, plastrón, aleta anterior dorsal/ventral, aleta posterior dorsal/ventral, cabeza superior y lateral):**  
  [v4_material_closeups.png](sheets/v4_material_closeups.png)
- **Inspección Albedo puro (`surfaceDebug=6`) e iluminación neutra (`surfaceDebug=10`):**  
  [v4_albedo_and_neutral.png](sheets/v4_albedo_and_neutral.png)
- **Progresión ontogénica en 4 etapas (`seed 0.00`, `larva 0.33`, `juvenile 0.66`, `adult 1.00`, dorsal y ventral):**  
  [v4_development_stages.png](sheets/v4_development_stages.png)

---

## 5. Resolución de Defectos M01–M07

| ID | Defecto | Estado | Evidencia |
| --- | --- | --- | --- |
| **M01** | Patrón artificial repetitivo en espaldar | **RESUELTO** | `before_after_v3_v4.png`, `v4_material_closeups.png` (`carapace-closeup`) |
| **M02** | Variación insuficiente por escudo | **RESUELTO** | `v4_material_closeups.png` (`scute-closeup`), `v4_albedo_and_neutral.png` |
| **M03** | Rejilla en damero en el plastrón | **RESUELTO** | `before_after_v3_v4.png` (`bottom`), `v4_material_closeups.png` (`plastron-closeup`) |
| **M04** | Escamas geométricas repetidas en aletas dorsales | **RESUELTO** | `v4_material_closeups.png` (`foreflipper-dorsal`, `hindflipper-dorsal`) |
| **M05** | Aletas ventrales blancas fundidas con plastrón | **RESUELTO** | `before_after_v3_v4.png` (`bottom`), `v4_material_closeups.png` (`foreflipper-ventral`, `hindflipper-ventral`) |
| **M06** | Desajuste entre piel facial diminuta y placas verdes planas | **RESUELTO** | `before_after_v3_v4.png` (`head-top`, `head-side`), `v4_material_closeups.png` |
| **M07** | Incoherencia de iluminación y paleta entre regiones | **RESUELTO** | `v4_overview_and_swim.png`, `v4_albedo_and_neutral.png` |

---

## 6. Pruebas Automatizadas, Morph, Offspring y Rendimiento

- **`test_turtle_material_v4`** (`tests/test_aquatic_anatomy.c`): verifica round-trip JSON de los campos V4, emisión de los 5 materiales semánticos (`CARAPACE`, `PLASTRON`, `REPTILIAN_FLIPPER`, `CEPHALIC_PLATE`, `HORN`) con 0 materiales inválidos en `Mesh_Validate`, preservación de aletas lisas en delfín (`hasSurface == false`, 0 vértices reptilianos), continuidad ontogénica con semilla determinista (`1024`), y morphing bidireccional/cruzado **Tortuga → Ornitorrinco / Pangolín / Delfín / Lagarto**.
- **Suites ejecutadas:** `make test`, `make editor-test`, `make demos` (verificado sin advertencias bajo `-Wall -Wextra -Wpedantic -O2`).
- **Rendimiento:** Sin aumento de resolución geométrica en espaldar/plastrón/aletas, sin texturas en VRAM (192 bytes en `detailMaterial[12]`), sin remallado SDF para cambios de material, y coste 0 en CPU por fotograma durante la animación de nado (evaluación puramente en fragment shader en espacio local de reposo).

---

## 7. Limitaciones Conocidas

- En trazadores GPU puros de campo SDF sin malla suplementaria, el caparazón y las placas proyectadas continúan dependiendo del renderizador híbrido (`v->detailMesh`), igual que en V3.
- Los colores de vértice CPU en `SupplementaryMesh.c` actúan como respaldo coherente de menor frecuencia espacial cuando no se usa el shader GLSL (`monster_surface.frag`).
