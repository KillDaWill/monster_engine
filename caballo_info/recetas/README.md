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
