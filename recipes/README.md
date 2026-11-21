# Recetas externas

El editor guarda aquí archivos `*.creature.json`, con versión explícita y las cuatro
etapas completas. Las recetas nuevas usan identificadores desde 1000. Las cuatro
recetas incorporadas se importan directamente del catálogo compilado del motor;
guardarlas en el editor crea una copia externa sin modificar sus fuentes C.

`brumacerno.creature.json` conserva el ID 3 y sustituye al herbívoro de la demo.
`catalog.txt` enumera los archivos que cargan la demo ecológica y su versión sin
render al arrancar. El editor actualiza el manifiesto al guardar. Las recetas
incorporadas siguen disponibles como respaldo si se usa un catálogo vacío.

Ejecutar `make creature-editor` y abrir http://127.0.0.1:8765.

`explorador.creature.json` (ID 1000) es una receta externa de ejemplo, derivada
del cánido con otra paleta, cuello más largo y dos cuernos dorsales.

Para cargar un archivo en una aplicación del motor, usar
`CreatureRecipeIO_LoadFile()` y pasar `&document.recipe` a
`Creature_BuildMonster()`. Mantener el documento vivo mientras se usa su receta.
La aplicación puede cargar el manifiesto con `CreatureRecipeRegistry_LoadDirectory()`
antes de iniciar actores o workers. No hace falta recompilar para modificar o
cargar estos datos; reiniciar la demo carga la versión guardada.

`cerdo.creature.json` (ID 1007, Cerdo) define un suino doméstico tipo Large
White en cuatro edades posnatales. Usa `HEAD_ARCHETYPE_SUID` (5),
`TAIL_ARCHETYPE_COILED` (6) y miembros ungulados de dos uñas. El perfil
opcional `LIMB_PROFILE_COMPACT_UNGULATE` (2) define la sección de la pata
corta y suaviza las articulaciones durante la metamorfosis.
`head.nasalColor` es opcional: alfa cero o campo omitido conserva el pigmento
nasal oscuro de las recetas anteriores. La demo universal admite `cerdo`,
`pig`, `swine` e ID `1007`; no requiere una demo específica.

`elefante.creature.json` (ID 1008, Elefante) representa un macho de elefante
africano de sabana en cuatro edades posnatales. Usa cabeza proboscídea,
miembros columnares y dos incisivos de dentina. La trompa es un módulo
`CREATURE_MODULE_FLEXIBLE_APPENDAGE` (5), anclado a la ranura nasal del
módulo cefálico. `flexibleAppendages` admite longitud, radios, proporción
elíptica, curvatura y tipo de extremo. `limbs[].nailCount` cuenta las placas
visibles sin generar dedos externos. Los controles opcionales
`surface.wrinkleStrength`, `surface.wrinkleScale`,
`surface.wrinkleDirectionality` y `surface.wrinkleRegionMask` definen el
relieve de piel en reposo. Los campos nuevos pueden omitirse en documentos
v1 anteriores. En la demo, Elefante admite los alias `elephant` y
`african-elephant`. [Validación anatómica y visual](../docs/validation/elephant/README.md).

`rinoceronte.creature.json` (1009) y `rata.creature.json` (1010) definen
rinoceronte blanco sureño y rata parda en cuatro etapas posnatales.
La demo universal también admite `rhino`, `white-rhino`, `rat` y
`norway-rat`. Nexus Files descubre ambas recetas en `/recepies/`.
Consulta las [capturas de validación](../docs/validation/rhino-rat/README.md).

Los pies ungulados admiten entre uno y tres dedos portantes.
En miembros mamíferos con dedos, `limbs[].clawScale` añade una uña
terminal. `tails[].tipLift` añade curvatura vertical en el extremo de
la cola en reposo. Ambos campos se pueden omitir en documentos v1. Los
arquetipos cefálicos 7 y 8 resuelven los perfiles rinocerótido y roedor.
Las ranuras 15 y 16 del módulo cefálico corresponden al puente nasal
anterior y posterior.

`jirafa.creature.json` (ID 1011, Jirafa) utiliza cuello cervical articulado multiestación (`cervicalSegments`, `cervicalPitch`), osiconos craneales (`ORNAMENT_ARCHETYPE_OSSICONE`) y pelaje reticulado (`PIGMENT_PATTERN_RETICULATED`).

`ornitorrinco.creature.json` (ID 1012, Ornitorrinco) emplea cabeza monotrema con pico en espátula flexible (`HEAD_ARCHETYPE_MONOTREME`), membrana interdigital retráctil en `limbs[].webbing*` y cola plana (`TAIL_ARCHETYPE_PADDLE`).

`pangolin.creature.json` (ID 1013, Pangolín Chino) incorpora cabeza cónica (`HEAD_ARCHETYPE_PHOLIDOTE`), armadura geométrica imbrincada de queratina (`ArmorScales` configurable mediante `surface.integument.plates.*`) y enrollamiento defensivo (`defensiveCurl`).

`canguro.creature.json` (ID 1014, Canguro Rojo) añade perfil macrópodo (`LIMB_PROFILE_MACROPOD`) con tibia larga y cuarto dedo dominante junto a marcha de salto bípedo sincronizado (`CREATURE_GAIT_BIPEDAL_HOP`).

`murcielago.creature.json` (ID 1015, Murciélago Orejudo Pardo) implementa alas quiropteras (`LIMB_ARCHETYPE_WING`, `LimbBatWing`), patagios explícitos en `membranes[]` (plagiopatagio, propatagio, uropatagio) y dactilopatagios interdigitales II-V.
