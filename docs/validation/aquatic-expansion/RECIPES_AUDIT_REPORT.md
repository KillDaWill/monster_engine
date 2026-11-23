# Reporte Técnico y Auditoría Crítica: Nuevas Recetas Acuáticas (IDs 1021–1025)

**Fecha:** 2026-11-23  
**Commit base de referencia:** `03ba39a1d1a6c2b0c5976c9bed43c42ce8ff9ae0`  
**Generador determinista:** `tools/generate_five_aquatic.py`  
**Catálogo:** `recipes/catalog.txt`

---

## 1. Resumen de Metadatos de las Recetas

Todas las recetas utilizan formato JSON v1 (`"version": 1`) con las cuatro etapas ontogenéticas obligatorias (`seed`, `larva`, `juvenile`, `adult`), IDs únicos en `1021..1025` y soporte completo para `CreatureRecipeIO`, `CreatureMorph`, `CreatureOffspring` V4 y el editor.

| ID | Nombre (`name`) | Archivo | Gait (`CreatureGait`) | Módulos (`bodyPlan`) | Axial (`AxialArchetype`) | Cabeza (`HeadArchetype`) | Extremidades / Aletas | Cola / Propulsor (`CaudalFinType`) | Caparazón (`Shell`) | Tegumento (`IntegumentType`) |
| ---: | --- | --- | --- | ---: | --- | --- | --- | --- | --- | --- |
| **1021** | `Atun Rojo` | `recipes/atun_rojo.creature.json` | `12` (`THUNNIFORM_SWIM`) | 12 | `9` (`AQUATIC_FUSIFORM`) | `14` (`TELEOST`) | `0` limbs, `9` fins (incl. 2 series de pínulas) | `1` tail, `1` (`HOMOCERCAL_LUNATE`) | No (`0.0`) | `1` (`SCALES`) |
| **1022** | `Tiburon Martillo` | `recipes/tiburon_martillo.creature.json` | `13` (`CARANGIFORM_SHARK_SWIM`) | 10 | `9` (`AQUATIC_FUSIFORM`) | `15` (`ELASMOBRANCH`) | `0` limbs, `7` fins | `1` tail, `2` (`HETEROCERCAL`) | No (`0.0`) | `0` (`SMOOTH_SKIN`) |
| **1023** | `Caballito de Mar` | `recipes/caballito_mar.creature.json` | `14` (`SEAHORSE_SWIM`) | 6 | `10` (`CURVED_VERTICAL`) | `16` (`SYNGNATHID`) | `0` limbs, `3` fins | `1` tail (`PREHENSILE`), `4`→`0` (`ROUNDED_VERTICAL`→`NONE`) | No (`0.0`) | `1` (`SCALES`) |
| **1024** | `Tortuga Marina` | `recipes/tortuga_marina.creature.json` | `15` (`TURTLE_SWIM`) | 7 | `0` (`SPRAWLING_TETRAPOD`) | `17` (`TESTUDINE`) | `4` limbs (`MARINE_FLIPPER`), `0` fins | `1` tail, `0` (`NONE`) | Sí (`1.0`, 4 pares costales) | `1` (`SCALES`) |
| **1025** | `Delfin Mular` | `recipes/delfin_mular.creature.json` | `16` (`CETACEAN_SWIM`) | 6 | `9` (`AQUATIC_FUSIFORM`) | `18` (`CETACEAN`) | `2` limbs (`MARINE_FLIPPER`), `1` fin (dorsal) | `1` tail, `3` (`HORIZONTAL_FLUKES`) | No (`0.0`) | `0` (`SMOOTH_SKIN`) |

### Parámetros Ontogenéticos por Etapa (`seed` → `larva` → `juvenile` → `adult`)

| Especie | Etapa | `totalScale` | `trunkLength` | `thoraxWidth` | `thoraxHeight` | `tail.length` | `caudalSpan` | Rasgo ontogenético clave |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| **Atún Rojo (1021)** | `seed` / `larva` / `juvenile` / `adult` | 0.28 / 0.46 / 0.74 / 1.00 | 3.65 | 0.694 / 0.874 / 1.030 / 1.120 | 1.104 / 1.388 / 1.638 / 1.780 | 0.68 | 1.325 / 1.546 / 1.730 / 1.840 | Profundidad corporal y envergadura caudal progresivas; ojos relativamente mayores en fases tempranas (`0.44`→`0.34`). |
| **Tiburón Martillo (1022)** | `seed` / `larva` / `juvenile` / `adult` | 0.32 / 0.50 / 0.76 / 1.00 | 4.55 | 0.920 | 1.020 | 1.12 | 1.558 / 1.742 / 1.927 / 2.050 | Anchura del cefalofoil (`0.84`→`1.00`) y proporción de aletas (`0.76`→`1.00`). |
| **Caballito de Mar (1023)** | `seed` / `larva` / `juvenile` / `adult` | 0.30 / 0.48 / 0.76 / 1.00 | 2.45 | 0.640 | 0.760 | 2.55 | 0.120 / 0.050 / 0.000 / 0.000 | Aleta caudal temporal posliberación (`CAUDAL_FIN_ROUNDED_VERTICAL`) que desaparece en juvenil/adulto; enrollamiento caudal (`0.55`→`0.98`) y elongación del hocico (`0.72`→`0.94`). |
| **Tortuga Marina (1024)** | `seed` / `larva` / `juvenile` / `adult` | 0.28 / 0.46 / 0.74 / 1.00 | 2.65 | 1.560 | 0.560 | 0.56 | 0.000 | Crías con aletas relativamente más largas (`1.18`→`1.00`) y caparazón en expansión (`0.85`→`1.00`). |
| **Delfín Mular (1025)** | `seed` / `larva` / `juvenile` / `adult` | 0.38 / 0.56 / 0.78 / 1.00 | 4.25 | 1.100 | 1.200 | 1.12 | 1.282 / 1.495 / 1.673 / 1.780 | Cría con rostro más corto (`0.56`→`1.00` multiplicador) y frente/melón proporcionalmente más redondeada. |

---

## 2. Capturas Multi-Vista (Edad Adulta `1.0`, Modo `surface`)

Cada especie se capturó en 12 ángulos distintos mediante `build/capture_creature` (lateral, tres cuartos, cenital, frontal, detalle cefálico, detalle caudal, ventral `bottom`, trasera `rear`, tres cuartos trasero, frontal cefálico, cenital cefálico y cenital caudal).

### 2.1 Atún Rojo (*Thunnus thynnus*, ID 1021)
- Vistas principales: [tuna_v2_sheet.png](sheets/tuna_v2_sheet.png)
- Vistas complementarias (ventral, trasera, detalles): [tuna_v2_extra_sheet.png](sheets/tuna_v2_extra_sheet.png)

### 2.2 Tiburón Martillo (*Sphyrna lewini*, ID 1022)
- Vistas principales: [hammerhead_v2_sheet.png](sheets/hammerhead_v2_sheet.png)
- Vistas complementarias (ventral, trasera, detalles): [hammerhead_v2_extra_sheet.png](sheets/hammerhead_v2_extra_sheet.png)

### 2.3 Caballito de Mar (*Hippocampus reidi*, ID 1023)
- Vistas principales: [seahorse_v2_sheet.png](sheets/seahorse_v2_sheet.png)
- Vistas complementarias (ventral, trasera, detalles): [seahorse_v2_extra_sheet.png](sheets/seahorse_v2_extra_sheet.png)

### 2.4 Tortuga Marina Verde (*Chelonia mydas*, ID 1024)
- Vistas principales: [turtle_v2_sheet.png](sheets/turtle_v2_sheet.png)
- Vistas complementarias (ventral, trasera, detalles): [turtle_v2_extra_sheet.png](sheets/turtle_v2_extra_sheet.png)

### 2.5 Delfín Mular (*Tursiops truncatus*, ID 1025)
- Vistas principales: [dolphin_v2_sheet.png](sheets/dolphin_v2_sheet.png)
- Vistas complementarias (ventral, trasera, detalles): [dolphin_v2_extra_sheet.png](sheets/dolphin_v2_extra_sheet.png)

---

## 3. Análisis por Especie vs. Referencias Reales y Errores Detectados

### 3.1 Atún Rojo (*Thunnus thynnus*) — ID 1021
- **Referencia biológica (FAO / Florida Museum):** Cuerpo fusiforme muy robusto con altura máxima (~25–27% de la longitud furcal) situada bajo la primera aleta dorsal; aletas pectorales **cortas** (< 80% de la longitud cefálica, nunca alcanzan el espacio interdorsal); 8–10 pínulas dorsales y 7–9 anales de tono amarillo con borde oscuro; pedúnculo caudal muy estrecho con quillas laterales carnosas; cola lunada simétrica; dorso azul oscuro metálico y vientre blanco-plateado.
- **Lo que funciona bien en la receta:** Silueta lateral general reconocible, dos dorsales diferenciadas, 9 pínulas dorsales y 8 anales bien espaciadas en amarillo, estrechamiento pronunciado del pedúnculo y cola lunada simétrica.
- **Errores detectados en la implementación:**
  1. **Escamas excesivamente marcadas (`INTEGUMENT_SCALES`):** En `tuna_v2_sheet.png` (`head-side` y `tail-three-quarter`) la textura de escamas es muy gruesa y profunda, más parecida a un reptil que a las microescamas casi lisas de un túnido (donde solo el corselete pectoral tiene escamas visibles).
  2. **Opérculo poco visible:** Aunque `BuildAquaticHeadAndArmorDetails` genera una banda opercular en `SupplementaryMesh.c`, queda casi absorbida o enrasada por el campo SDF y la textura de escamas en `head-side`.
  3. **Aletas pectorales algo largas y azuladas en vista cenital:** En `tuna_v2_sheet.png` (`top`) se proyectan bastante hacia afuera con color azul claro, mientras que en *T. thynnus* son cortas y oscuras dorsalmente.
  4. **Labio/boca en vista frontal (`tuna_v2_extra_sheet.png`, `head-front`):** La hendidura bucal deja un reborde lateral plano que sobresale ligeramente de la silueta cónica del rostro.
  5. **Contracoloración ventral apagada (`bottom`):** En `tuna_v2_extra_sheet.png` (`bottom`), el vientre se ve grisáceo medio en lugar de blanco-plateado brillante.
  6. **Ausencia de quillas caudales laterales:** El pedúnculo estrecho carece de las quillas laterales horizontales características de los escómbridos grandes.

### 3.2 Tiburón Martillo (*Sphyrna lewini*) — ID 1022
- **Referencia biológica (Florida Museum / NOAA):** Cefalofoil ancho con borde anterior arqueado que presenta una **muesca central prominente y muescas laterales** (perfil "festoneado" / *scalloped*); ojos en los extremos laterales del martillo; boca ventral en media luna; 5 hendiduras branquiales por lado; primera dorsal alta y ligeramente falcada; **segunda dorsal baja** (menor que la anal) con punta posterior libre muy larga; pélvicas con margen posterior recto; cola heterocerca con lóbulo superior muy desarrollado.
- **Lo que funciona bien en la receta:** Proporciones hidrodinámicas del cuerpo, primera dorsal alta y falcada, 5 hendiduras branquiales bien visibles lateralmente, cola heterocerca con lóbulo superior dominante y ojos en los extremos del cefalofoil.
- **Errores detectados en la implementación:**
  1. **Perfil del cefalofoil (`hammerhead_v2_extra_sheet.png`, `head-top`):** El borde anterior del martillo es demasiado recto y carece de las indentaciones festoneadas (muesca central + muescas laterales) que definen a *S. lewini*. Además, detrás del martillo aparecen dos lóbulos/ondulaciones laterales adicionales antes del cuello por la transición entre `maxillaryCenter`/`cheekCenter` y el cráneo axial.
  2. **Proporción 2.ª dorsal vs. anal:** En la receta la 2.ª dorsal (`span=0.26`) y la anal (`span=0.28`) son casi equivalentes y simétricas, sin el alargamiento posterior característico de la 2.ª dorsal de *S. lewini*.
  3. **Aletas pélvicas con contraste artificial (`bottom`, `side`):** El blanco puro de las puntas (`tip_col=belly`) y el ángulo diedro hacen que parezcan triángulos blancos muy llamativos desde abajo/lado en lugar de continuar el tono pálido gradual ventral.
  4. **Muñón caudal terminal (`hammerhead_v2_sheet.png`, `tail-three-quarter`):** La punta del módulo `Tail` termina en una cápsula redondeada visible entre los dos lóbulos de la aleta caudal.
  5. **Vientre poco luminoso (`bottom`):** La cara ventral aparece gris oliva oscuro en lugar de blanco/crema claro.

### 3.3 Caballito de Mar (*Hippocampus reidi*) — ID 1023
- **Referencia biológica (Florida Museum / STRI / Project Seahorse):** Tronco vertical curvado con **11 anillos troncales** y ~**35 anillos caudales** (31–39) con crestas óseas longitudinales continuas; cabeza angulada con hocico tubular largo y grueso; coroneta baja-media redondeada; aleta dorsal con ~17 radios sobre 2 anillos troncales + 1 caudal; pectorales pequeñas tras el opérculo; cola prensil enrollada ventralmente sin aleta caudal adulta; patrón profusamente moteado de puntos marrones y blancos sobre amarillo/naranja/pardo.
- **Lo que funciona bien en la receta:** Postura erguida en S característica, ángulo cefálico, hocico tubular largo, cola prensil enrollada en plano sagital hacia adelante, aleta dorsal posterior y desarrollo ontogenético con aleta caudal temporal en etapas tempranas.
- **Errores detectados en la implementación:**
  1. **Anillos óseos mal integrados (`seahorse_v2_sheet.png`, `seahorse_v2_extra_sheet.png`):** Los anillos generados en `SupplementaryMesh.c` apenas destacan como pequeños salientes amarillos discontinuos o puntos en el dorso/cola, mientras que la textura uniforme de escamas hexagonales (`INTEGUMENT_SCALES`) recubre todo el animal como si fuera un lagarto dorado. No se percibe la estructura poligonal segmentada de 11 anillos troncales + anillos caudales con quillas longitudinales.
  2. **Hombros/tórax angulosos en vista posterior y frontal (`rear`, `front`):** El tronco presenta ensanchamientos laterales abruptos en los nodos `PECTORAL` y `THORAX_POSTERIOR` con transición brusca hacia el abdomen.
  3. **Coroneta geométrica facetada (`head-side`):** La coroneta de 6 vértices parece una corona poligonal rígida amarilla pegada sobre el cráneo en lugar de una cresta ósea orgánica continua.
  4. **Aleta dorsal y pectorales rígidas (`side`, `tail-top`):** La dorsal es una cuña gruesa amarillenta poco traslúcida y las pectorales salen muy horizontales/afiladas vista desde arriba.
  5. **Ausencia de patrón moteado:** La receta deja `pigment.layers = []`, por lo que carece del moteado pardo/blanco característico de *H. reidi*.

### 3.4 Tortuga Marina Verde (*Chelonia mydas*) — ID 1024
- **Referencia biológica (USFWS / NOAA):** Caparazón liso ovalado-cordiforme relativamente aplanado con **4 pares de escudos costales**, 5 vertebrales y ~11 marginales por lado; **un solo par de escamas prefrontales** entre los ojos (rasgo diagnóstico); cabeza reptiliana pequeña con pico córneo sin dientes; aletas anteriores largas en forma de ala con **una uña** en el borde anterior; aletas posteriores cortas en timón; plastrón pálido continuo unido al espaldar por un puente lateral amplio.
- **Lo que funciona bien en la receta:** Silueta dorsal cordiforme del espaldar, proporción de aletas anteriores largas y posteriores cortas, integración cerrada del caparazón con el plastrón sin hueco inferior flotante (en v2), y presencia de 4 pares costales en el patrón del espaldar.
- **Errores detectados en la implementación:**
  1. **Cabeza demasiado ancha y escamación genérica (`turtle_v2_extra_sheet.png`, `head-front`, `head-top`):** En vista frontal la cabeza es excesivamente ancha/aplastada lateralmente y usa microescamas hexagonales uniformes en vez de placas cefálicas grandes (no modela el par único de escamas prefrontales ni placas supraoculares).
  2. **Transición axilar/inguinal en `bottom` (`turtle_v2_extra_sheet.png`, `bottom`):** En la vista ventral, las raíces proximales de las cuatro extremidades (`BODY_CONNECTION_LIMB_SEGMENT` con escamas oscuras) se ven pegadas/recortadas de forma brusca sobre el plastrón blanco brillante (que además queda sobreexpuesto sin suturas de escudos plastrales).
  3. **Borde del espaldar festoneado (`turtle_v2_sheet.png`, `side`):** El cálculo de suturas en `BuildAnatomicalShell` ondula el borde marginal del caparazón en lugar de mantener un perfil liso continuo con escudos bien delimitados por surcos finos.
  4. **Falta de la uña única en el borde de ataque de las aletas:** `LIMB_PROFILE_MARINE_FLIPPER` omite la uña única característica de *Chelonia mydas*.

### 3.5 Delfín Mular (*Tursiops truncatus*) — ID 1025
- **Referencia biológica (NOAA):** Cuerpo fusiforme hidrodinámico completamente continuo sin cuello marcado ni extremidades posteriores; melón frontal redondeado bien diferenciado de un rostro grueso y moderadamente largo; comisura bucal ascendente hacia el ojo; **único espiráculo dorsal** detrás del melón; aleta dorsal alta y falcada de tejido fibroso; aletas pectorales curvadas; pedúnculo estrecho comprimido lateralmente que termina en **dos lóbulos horizontales (flukes)** con escotadura central limpia.
- **Lo que funciona bien en la receta:** Silueta lateral general reconocible (melón, rostro, aleta dorsal falcada, pectorales, flukes horizontales), ausencia total de branquias o aletas pélvicas, y cinemática de nado **dorsoventral** verificada en C.
- **Errores detectados en la implementación:**
  1. **Rebaba horizontal en la boca (`dolphin_v2_extra_sheet.png`, `head-front`):** Al igual que en el atún, el cierre bucal heredado de `Head`/`Mouth` genera un labio horizontal plano que sobresale a ambos lados del rostro visto de frente.
  2. **Espiráculo dorsal oculto (`dolphin_v2_extra_sheet.png`, `head-top`):** El cuadrilátero oscuro generado para el espiráculo queda sumergido bajo el perfil craneal SDF y no es visible en la vista cenital de la cabeza.
  3. **Abultamiento cervical/torácico (`top`, `bottom`, `side`):** Todavía se aprecia un ligero estrechamiento detrás de la zona occipital antes del tórax, rompiendo ligeramente la transición fusiforme continua típica de los delfínidos.
  4. **Muñón caudal saliente (`dolphin_v2_extra_sheet.png`, `tail-top`):** El extremo del tronco/cola `Tail` sobresale redondeado por detrás de la escotadura central entre ambos flukes horizontales.
  5. **Contraste ventral bajo (`bottom`):** La parte ventral en `bottom` es demasiado grisácea en lugar de blanco-rosáceo/gris muy pálido delimitado claramente respecto al dorso gris.

---

## 4. Errores Transversales del Motor / Pipeline Detectados

1. **Integración raíz/punta de `Tail` con `CaudalFinType` (`src/Tail.c` + `src/SupplementaryMesh.c`):** El barrido SDF de la cola termina en una cápsula redondeada que no se ahúsa a cero ni se funde dentro del borde de ataque del propulsor caudal, produciendo un muñón visible en la escotadura trasera (`dolphin`, `hammerhead`, `tuna`).
2. **Artefacto de comisura/labio plano en `Mouth`/`Head` para cabezas hidrodinámicas (`src/Head.c` + `src/MonsterSDF.c`):** Las cabezas `TELEOST` y `CETACEAN` heredan el cutter/labio oral estándar que produce rebabas laterales planas en vista frontal (`head-front`).
3. **Ausencia de captura visual de etapas tempranas y animación:** La validación visual se ejecutó únicamente sobre `age=1.0` en pose de reposo; aunque las etapas `seed`/`larva`/`juvenile` y los 5 modos de nado pasan todas las pruebas geométricas y cinemáticas en C (`tests/test_aquatic_anatomy.c`), **no se han generado capturas renderizadas de crías ni de fotogramas de nado**.
4. **Detalles de `SupplementaryMesh` ocultos por el offset voxel del SDF:** El borde opercular del atún y el espiráculo del delfín se colocaron justo sobre el radio teórico del cráneo (`* 1.01`), por lo que el suavizado SDF los cubre parcialmente.
