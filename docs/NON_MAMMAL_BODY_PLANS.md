# Planes Corporales No-Mamíferos y Geometría Suplementaria Reutilizable

Este documento describe la arquitectura genérica incorporada en Monster Engine para soportar planes corporales más allá de mamíferos y tetrápodos (aves, batoideos, cefalópodos, artrópodos y cnidarios radiales), junto con las cinco recetas externas de referencia (`1016`–`1020`).

## 1. Estrategias axiales y marcos radiales

El despachador `AxialBody` (`src/AxialBody.c`) resuelve siete arquetipos axiales completos sin imponer columna vertebral, pelvis o cabeza vertebrada:

| Arquetipo | Módulo | Características principales |
| --- | --- | --- |
| `AXIAL_ARCHETYPE_AVIAN` | `src/AxialBodyAvian.c` | Torso aerodinámico compacto, cuello corto integrado, ranuras alares dorsolaterales, ranuras pélvicas bajo el centro de gravedad y uropigio corto. |
| `AXIAL_ARCHETYPE_DISC_RAY` | `src/AxialBodyDiscRay.c` | Cuerpo aplanado dorsoventralmente con ranuras pectorales/pélvicas continuas, ranuras cefálicas pareadas (`AXIAL_SLOT_CEPHALIC_LEFT/RIGHT`) y aleta dorsal pequeña. |
| `AXIAL_ARCHETYPE_CEPHALOPOD` | `src/AxialBodyCephalopod.c` | Manto abombado posterior, collar muscular, base cefálica corta, sifón lateroventral (`AXIAL_SLOT_SIPHON`) y anillo circumoral radial (8 brazos por defecto). |
| `AXIAL_ARCHETYPE_ARTHROPOD` | `src/AxialBodyArthropod.c` | Tagmosis real (cabeza, protórax/mesotórax/metatórax y abdomen segmentado de hasta 12 somitos), 6 ranuras de patas torácicas (`AXIAL_SLOT_THORACIC_LEG_*`) y 4 ranuras alares independientes (`AXIAL_SLOT_WING_FORE/HIND_*`). |
| `AXIAL_ARCHETYPE_MEDUSA_RADIAL` | `src/AxialBodyMedusa.c` | Eje vertical aboral-oral sin cabeza vertebrada, 8 nodos marginales pulsátiles (`AXIAL_MEDUSA_RIM_NODE_BASE`) y anillos radiales para tentáculos y brazos orales. |

### Marco de coordenadas radiales
`AttachmentSlotSet_PublishRadialRing` (`include/Attachment.h`, `src/Attachment.c`) publica cualquier anillo radial de anclajes con ejes ortonormales `(forward, up, side)` a partir de un centro, eje normal, referencia radial, radio y arco angular. Reutilizado tanto en la corona circumoral del pulpo como en el margen y subumbrella de la medusa.

## 2. Sistemas genéricos de apéndices

- **Miembros (`src/Limb.c`)**:
  - `LIMB_ARCHETYPE_AVIAN` (`src/LimbAvian.c`): ala aviar auténtica (húmero, radio-ulna, carpo y mano distal reducida + álula, nunca ala de murciélago con 4 dedos largos) y pata rapaz anisodáctila (3 dedos anteriores + 1 hallux posterior con garras curvas).
  - `LIMB_ARCHETYPE_FLIPPER` (`src/LimbFin.c`): esqueleto spanwise continuo para aletas pectorales/pélvicas batoideas con unión suave al disco.
  - `LIMB_ARCHETYPE_ARTHROPOD_LEG` (`src/LimbArthropod.c`): cadena quitinosa segmentada (coxa, trocánter, fémur, tibia, tarso y uñas pareadas).
  - `LIMB_ARCHETYPE_INSECT_WING` (`src/LimbInsectWing.c`): vena costal fina independiente para alas anterior y posterior; en etapas tempranas (`development <= 0.25`) resuelve esbozos ninfales hemimetábolos sin alas adultas.
- **Apéndices flexibles (`src/FlexibleAppendage.c`)**: capacidad elevada de forma segura (`CREATURE_MAX_FLEXIBLE_APPENDAGES = 12`) con perfiles semánticos (`PROBOSCIS`, `MUSCULAR_ARM`, `CEPHALIC_LOBE`, `SIPHON`), orientación acimutal, enrollamiento distal y metadatos de ventosas.
- **Apéndices repetidos compactos (`RepeatedAppendageGroup`)**: descriptor compacto (`CREATURE_MAX_REPEATED_APPENDAGES = 4`) para estructuras de alta multiplicidad (24 tentáculos marginales filamentosos + 4 brazos orales rizados) con identidades estables deterministas (`RepeatedAppendage_InstanceId`).

## 3. Geometría suplementaria acoplada a deformación y materiales

`SupplementaryMesh` (`include/SupplementaryMesh.h`, `src/SupplementaryMesh.c`) genera mallas opacas (`detailMesh`) y semitransparentes doble cara (`translucentMesh`) ligadas al mismo `AnatomyDeformer` esquelético (`AnatomyDeformer_BindExplicitSkin`), deformándose en tiempo real sin reconstruir el SDF:
- Plumas de vuelo aviares (primarias, secundarias, terciarias/escapulares y coberteras) + abanico de 12 rectrices (`TAIL_ARCHETYPE_FEATHER_FAN`).
- Perfil hidrodinámico continuo de aleta pectoral batoidea + 5 pares de hendiduras branquiales ventrales.
- Ventosas 3D cóncavas en doble hilera alterna + membrana interbraquial proximal de cefalópodo.
- Alas membranosas translúcidas de insecto con venación estructural opaca, nodus y pterostigma.
- Tentáculos filamentosos marginales y cintas orales rizadas translúcidas de alta resolución.

El SDF de la campana medusoide (`MonsterSDF_EvalMedusaBellDistance` + `MonsterSDF_MedusaBellRange`) modela una cáscara cóncava hueca analítica con margen festoneado y acotación conservadora por intervalos para el mallador adaptativo.

El sistema de superficies (`include/Surface.h`, `shaders/monster_surface.frag`, `src/OpenGLRenderer.c`) añade `INTEGUMENT_CHITIN`, `INTEGUMENT_GELATINOUS`, propiedades ópticas (`translucency`, `iridescence`, `subsurfaceGlow`) y renderizado en dos pasadas (opacos antes de translúcidos con `depthMask=false` y doble cara), manteniendo el núcleo libre de dependencias GL/SDL.

## 4. Animación, ontogenia y recombinación (`CreatureOffspring`)

- Cinco modos de marcha en `GaitPresets.c` y `ProceduralAnimator.c`: `AVIAN_FLIGHT`, `RAY_SWIM`, `OCTOPUS_CRAWL`, `INSECT_FLIGHT` y `MEDUSA_PULSE`, todos con cero reconstrucciones de SDF por fotograma.
- **Nota biológica sobre desarrollo de cnidarios (`medusa_ortiga`, ID `1020`)**: las cuatro etapas ontogenéticas modelan el crecimiento continuo post-éfira / medusoide (desde éfira/medusa joven hasta medusa adulta con campana y brazos plenamente desarrollados), no la transición desde pólipo sésil bentónico.
- **Nota sobre hemimetabolismo (`libelula_emperador`, ID `1019`)**: las etapas tempranas (`seed`/`larva`) representan ninfas acuáticas crípticas con esbozos alares dorsales; las 4 alas membranosas translúcidas emergen en las etapas `juvenile`/`adult`.
- **`CreatureOffspring`**: distingue familias de homología anatómica (`LimbHomologyFamily`) de modo que alas aviares, alas de murciélago, alas de insecto, aletas batoideas, patas vertebradas y patas artrópodas no se mezclan de forma inválida; los injertos incompatibles se contabilizan explícitamente en `unsupportedGrafts`.

## 5. Recetas externas registradas (`recipes/catalog.txt`)

| ID | Archivo | Especie | Plan axial |
| --- | --- | --- | --- |
| `1016` | `recipes/halcon_peregrino.creature.json` | *Falco peregrinus* | `AXIAL_ARCHETYPE_AVIAN` |
| `1017` | `recipes/mantarraya.creature.json` | *Mobula birostris* | `AXIAL_ARCHETYPE_DISC_RAY` |
| `1018` | `recipes/pulpo_gigante.creature.json` | *Enteroctopus dofleini* | `AXIAL_ARCHETYPE_CEPHALOPOD` |
| `1019` | `recipes/libelula_emperador.creature.json` | *Anax imperator* | `AXIAL_ARCHETYPE_ARTHROPOD` |
| `1020` | `recipes/medusa_ortiga.creature.json` | *Chrysaora fuscescens* | `AXIAL_ARCHETYPE_MEDUSA_RADIAL` |
