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
