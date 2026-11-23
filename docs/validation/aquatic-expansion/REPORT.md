# Monster Engine — Expansión Anatómica Acuática (Recetas 1021..1025)

**Commit base:** `03ba39a1d1a6c2b0c5976c9bed43c42ce8ff9ae0`  
**Recetas externas añadidas:** IDs `1021..1025` (`atun_rojo`, `tiburon_martillo`, `caballito_mar`, `tortuga_marina`, `delfin_mular`)  
**Generador determinista:** `tools/generate_five_aquatic.py`

---

## 1. Cambios de Arquitectura y Componentes Reutilizables

El objetivo principal no fue crear cinco mallas aisladas, sino dotar al núcleo independiente de renderizado de un vocabulario anatómico acuático general, parametrizado y reutilizable para futuras especies, criaturas fantásticas, `CreatureMorph` y `CreatureOffspring` V4.

| Subsistema | Archivos | Descripción |
| --- | --- | --- |
| **Cuerpo fusiforme acuático** | `include/AxialBodyAquaticFusiform.h`, `src/AxialBodyAquaticFusiform.c` | Arquetipo `AXIAL_ARCHETYPE_AQUATIC_FUSIFORM` con 6 estaciones longitudinales continuas, barrido elíptico SDF suave hacia el pedúnculo caudal y ranuras semánticas (`CERVICAL`, `PECTORAL_*`, `PELVIC_*`, `DORSAL`, `DORSAL_POSTERIOR`, `VENTRAL`, `ANAL`, `CAUDAL`, `SHELL`). Reutilizado por atún rojo, tiburón martillo y delfín mular. |
| **Cuerpo vertical curvado** | `include/AxialBodyCurvedVertical.h`, `src/AxialBodyCurvedVertical.c` | Arquetipo `AXIAL_ARCHETYPE_CURVED_VERTICAL` con columna en S erguida, curvatura cervical independiente (`cervicalCurvature`, `cervicalPitch`), ranura dorsal posterior, pectorales cervicales y ranura caudal descendente. |
| **Sistema genérico de aletas** | `include/Fin.h`, `src/Fin.c`, `CREATURE_MODULE_FIN` | Módulo anatómico de primera clase (`FinPhenotype`, hasta 12 por criatura) que soporta aletas pareadas radiadas, medianas dorsales/anales, cartilaginosas de escualo, de tejido fibroso cetáceo y series compactas de pínulas (`FIN_ROLE_FINLET_SERIES`) en una sola instancia modular. |
| **Propulsor caudal reutilizable** | `include/CreaturePhenotype.h`, `src/Tail.c`, `src/SupplementaryMesh.c` | Extensión opcional de `TailPhenotype` con `CaudalFinType` (`HOMOCERCAL_LUNATE`, `HETEROCERCAL`, `HORIZONTAL_FLUKES`, `ROUNDED_VERTICAL`, `NONE`) y rizado sagital ventral en `TAIL_ARCHETYPE_PREHENSILE` para colas verticales prensiles. |
| **Caparazón anatómico** | `include/CreaturePhenotype.h`, `src/AxialBodySprawlingTetrapod.c`, `src/SupplementaryMesh.c` | `ShellPhenotype` con convexidad dorsal, perfil cordiforme, espaldar con escudos vertebrales/costales/marginales, puente lateral y plastrón ventral rígido vinculado al tronco sin deformación blanda. |
| **Aletas de tetrápodo marino** | `src/LimbFin.c`, `src/SupplementaryMesh.c` | Nuevo perfil `LIMB_PROFILE_MARINE_FLIPPER` separado de las aletas batoideas de mantarraya (`LIMB_PROFILE_LEGACY`), modelando extremidades propulsoras hidrofoil para tortugas marinas y cetáceos. |
| **5 perfiles cefálicos acuáticos** | `include/Head.h`, `src/Head.c`, `src/SupplementaryMesh.c` | `HEAD_ARCHETYPE_TELEOST` (opérculos, hocico aguzado), `HEAD_ARCHETYPE_ELASMOBRANCH` (cefalofoil continuo con ojos en los extremos y 5 hendiduras branquiales por lado), `HEAD_ARCHETYPE_SYNGNATHID` (hocico tubular largo y coroneta), `HEAD_ARCHETYPE_TESTUDINE` (cráneo reptiliano compacto con pico romo) y `HEAD_ARCHETYPE_CETACEAN` (melón frontal, rostro grueso y espiráculo dorsal único). |
| **5 modos de nado acuático** | `include/CreatureRecipe.h`, `include/GaitPresets.h`, `src/GaitPresets.c`, `src/ProceduralAnimator.c`, `src/Creature.c`, `src/EcosystemVisual.c` | `CREATURE_GAIT_THUNNIFORM_SWIM`, `CARANGIFORM_SHARK_SWIM`, `SEAHORSE_SWIM`, `TURTLE_SWIM` y `CETACEAN_SWIM`. Propulsión lateral para peces/escualos, **dorsoventral** para cetáceos, batido de aletas anteriores con caparazón rígido para tortugas y vibración dorsal rápida para signátidos. |
| **Esquema JSON y UI** | `tools/generate_recipe_schema.py`, `src/CreatureRecipeSchema.inc`, `src/CreatureRecipeIO.c`, `demos/demo_morph_species.c`, `recipes/catalog.txt`, `recipes/README.md` | Compatibilidad total con recetas v1 existentes (campos nuevos opcionales), registro en catálogo y alias en `demo_morph_species`. |
| **Morph y CreatureOffspring V4** | `src/CreatureMorph.c`, `src/CreatureOffspring.c`, `src/OffspringRepair.c` | Interpolación continua de aletas, pínulas, propulsores caudales y caparazón; homología explícita de `LIMB_PROFILE_MARINE_FLIPPER`; injertos de `CREATURE_MODULE_FIN` y herencia coherente de caparazón solo sobre troncos compatibles; preservación de reparaciones V4. |

---

## 2. Resultados por Especie e Iteración Visual (v1 → v2)

Todas las especies se capturaron en modo `surface` con `build/capture_creature` (vistas lateral, tres cuartos, cenital, frontal, detalle cefálico y detalle caudal) sobre GPU NVIDIA GeForce RTX 3050 (OpenGL 4.6).

1. **Atún Rojo (*Thunnus thynnus*, ID 1021 — `recipes/atun_rojo.creature.json`)**
   - *v1 → v2:* Reducción de envolvente cefálica para eliminar el estrechamiento poscefálico en vista cenital, refuerzo de profundidad torácica anterior, ampliación de aletas pectorales, contracoloración ventral plateada más nítida y asiento continuo de la aleta caudal lunada.
   - *Evidencia:* [tuna_v2_sheet.png](sheets/tuna_v2_sheet.png)

2. **Tiburón Martillo (*Sphyrna lewini*, ID 1022 — `recipes/tiburon_martillo.creature.json`)**
   - *v1 → v2:* Ajuste de anchura y espesor del cefalofoil para eliminar aspecto de ala plana y escalón occipital, ampliación de aletas pectorales cartilaginosas y ahusamiento monotónico del pedúnculo hacia la cola heterocerca.
   - *Evidencia:* [hammerhead_v2_sheet.png](sheets/hammerhead_v2_sheet.png)

3. **Caballito de Mar (*Hippocampus reidi*, ID 1023 — `recipes/caballito_mar.creature.json`)**
   - *v1 → v2:* Sustitución de anillos radiales planos por collarines óseos biselados ajustados al volumen real del tronco (11 anillos troncales + caudales) y suavizado orgánico de la coroneta dorsal. Incluye desarrollo posliberación con aleta caudal juvenil temporal que desaparece en adulto.
   - *Evidencia:* [seahorse_v2_sheet.png](sheets/seahorse_v2_sheet.png)

4. **Tortuga Marina Verde (*Chelonia mydas*, ID 1024 — `recipes/tortuga_marina.creature.json`)**
   - *v1 → v2:* Integración cerrada del plastrón ventral y puente lateral con el borde del espaldar (eliminando el hueco inferior de v1), reducción del cráneo reptiliano a proporción realista y verificación de 4 pares de escudos costales y 5 vertebrales.
   - *Evidencia:* [turtle_v2_sheet.png](sheets/turtle_v2_sheet.png)

5. **Delfín Mular (*Tursiops truncatus*, ID 1025 — `recipes/delfin_mular.creature.json`)**
   - *v1 → v2:* Reducción de `headEnvelope` y ensanchamiento del collar cervical-torácico para flujo hidrodinámico continuo sin cuello marcado, rostro más compacto y grueso, aletas pectorales más llenas y lóbulos caudales horizontales con escotadura central sin muñón sobresaliente.
   - *Evidencia:* [dolphin_v2_sheet.png](sheets/dolphin_v2_sheet.png)

---

## 3. Validación de Morphing, Offspring V4 y Rendimiento

Resultados ejecutados en `tests/test_aquatic_anatomy.c` dentro de `make test`:

- **Carga y round-trip JSON (1021..1025):** PASS (4 etapas válidas, orden de resolución acíclico, ida y vuelta JSON idéntica).
- **Anatomía y geometría (5 especies × 5 edades = 25 construcciones):**
  - Construcción total: `1.4 ms` (`0.056 ms`/criatura).
  - Compilación SDF total: `0.5 ms` (`0.020 ms`/criatura).
  - Mallas suplementarias + vinculación de skinning exacto: `4.6 ms` (`0.184 ms`/criatura).
- **Animación acuática (5 modos sin remallado SDF):** PASS — verificado cuantitativamente que el delfín oscila en el plano dorsoventral (`maxTailDy > 1.2 * maxTailDx`) y atún/tiburón en el plano lateral (`maxTailDx > 1.2 * maxTailDy`).
- **Matriz de Morphing (7 pares × ida/vuelta × 7 fases `0.00..1.00`):** PASS (`0.13 ms/eval`), incluyendo Atún↔Martillo, Atún↔Delfín, Martillo↔Mantarraya, Caballito↔Atún, Tortuga↔Ornitorrinco, Delfín↔Martillo y Tortuga↔Delfín.
- **Matriz CreatureOffspring V4 (11 pares × 2 órdenes parentales = 44 híbridos):** PASS en `1141.9 ms` (`25.9 ms`/híbrido), incluyendo los 10 cruces acuáticos requeridos más la regresión V4 `Elefante × Halcón`, con contribución estructural real del donante (`effectiveDonorContribution > 0.01`), validación de las 4 etapas y round-trip JSON.

---

## 4. Limitaciones Conocidas

- El trazo fino de opérculos, hendiduras branquiales, espiráculo dorsal y anillos óseos se genera mediante `SupplementaryMesh` acoplado al esqueleto O(V) para mantener el coste del campo SDF constante; en trazadores GPU puros de SDF sin malla suplementaria estos microdetalles dependen del renderizador híbrido.
- Los patrones de pigmento moteado del caballito de mar y el brillo húmedo reutilizan el sistema de superficie existente sin añadir nuevas filas al búfer `SurfaceRecipe`, preservando compatibilidad 100% CPU/GLSL.
