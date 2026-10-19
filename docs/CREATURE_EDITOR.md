# Editor de criaturas

Primera versión local inspirada en la edición por partes de SPORE. La interfaz
está en español y usa HTML/CSS/JavaScript y WebGL 2. Un servidor Python de la
biblioteca estándar llama a un ejecutable C sin GL para validar recetas y generar
las mallas del motor. No requiere npm, servicios externos ni conexión a Internet.

## Ejecutar

```sh
make creature-editor
```

Abrir http://127.0.0.1:8765. Requiere GCC, make, Python 3 y un navegador con WebGL 2.
Para cambiar el puerto:

```sh
python3 tools/creature_editor/server.py --port 8766
```

## Flujo de edición

1. Abrir Lagarto, Perro, Brumacerno o Velkar desde la biblioteca. Se importan del
   catálogo real compilado; no se interpretan ni duplican sus fórmulas C en Python.
2. Seleccionar una parte en la caja inferior o en los puntos de la vista 3D.
   Cambiar sus proporciones con los controles de la derecha. Arrastrar el fondo
   gira la cámara; rueda acerca; botón derecho desplaza; ⊞ encuadra.
3. Seleccionar semilla, larva, juvenil o adulto. Las proporciones se editan por
   etapa. El botón de copiar reemplaza las otras tres etapas, previa confirmación.
4. Añadir o eliminar extremidades, colas y ornamentos con ＋/−. Estos cambios de
   composición se aplican a las cuatro etapas y actualizan el plan corporal.
   La simetría enlaza el par de extremidades con anclajes equivalentes.
5. En Pintura, editar paleta, pigmento, primera capa procedural y tegumento.
   Avanzado permite editar más campos y validar el documento JSON completo.
6. Guardar crea un archivo en `recipes/`. Las recetas incorporadas se guardan
   como copias con otro ID, desde 1000. Un archivo local se puede actualizar;
   el editor evita sobrescribir otro archivo o duplicar identidades por accidente.
7. Importar abre archivos JSON externos y los valida. Exportar descarga el
   documento actual. Deshacer/Rehacer funciona con los botones o Ctrl+Z y
   Ctrl+Mayús+Z. Ctrl+S abre el diálogo de guardado fuera de campos de texto.

Nueva criatura ofrece las cuatro anatomías funcionales como punto de partida.
El editor no ofrece todavía construcción desde un tronco vacío, arrastre de piezas
desde la paleta, esculpido directo, animación ni reproducción continua del crecimiento.

## Fidelidad de la vista

La cadena es receta → `Creature_ResolveAppearance` → `MonsterSDF` →
`MonsterVisual_RebuildNow` → buffers de triángulos del navegador. Conserva cuerpo,
cabeza, boca y globos oculares. Los puntos proceden de las estaciones del grafo
anatómico. La selección de calidad cambia el presupuesto del mallado de preview.
Las respuestas obsoletas se descartan si la receta o etapa cambió durante el cálculo.

El visor web tiene iluminación propia y pigmentación aproximada evaluada con las
funciones CPU de pigmento y texturas oculares del motor. No replica el shader de
pelaje, el relieve de escamas/placas ni el material completo del visor OpenGL.
Estos datos se preservan en JSON. El motor normaliza los controles antes de resolver
anatomía; un valor fuera del dominio admitido puede quedar limitado en la vista.
La reconstrucción es diferida mientras se mueven controles; no es esculpido a 60 FPS.

## Archivos y API del núcleo

Formato v1:

```json
{"version": 1, "recipe": {"id": 1000, "name": "Mi criatura", "bodyPlan": {}, "seed": {}, "larva": {}, "juvenile": {}, "adult": {}, "gait": 0}}
```

El ejemplo muestra la envolvente; los objetos deben contener **todos** los campos.
Exportar una receta en el editor produce un documento completo. Los arrays de
módulos, extremidades, colas, ornamentos y capas contienen sus elementos activos,
con un conteo coincidente. Los arrays de tamaño fijo conservan todos sus valores.
Los enumerados conservan sus valores numéricos públicos del motor. La versión se
debe incrementar si cambia el contrato de campos; no se admiten versiones futuras.

```c
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "Monster.h"

/* Reservar documentos grandes en heap o almacenamiento de duración suficiente. */
static CreatureRecipeDocument document;
char error[256];
if (CreatureRecipeIO_LoadFile(&document, "recipes/mi-criatura.creature.json",
                              error, sizeof(error))) {
    Monster monster = Monster_Create();
    if (Creature_BuildMonster(&monster, &document.recipe, &document.recipe.adult)) {
        /* Usar la criatura en el motor. */
    }
    Monster_Free(&monster);
}
```

`document` posee el almacenamiento de `recipe.name`; no copiar el documento por
valor ni liberarlo mientras una receta/Monster copiada use su nombre. La aplicación
elige qué archivo cargar. `CreatureRecipeRegistry_LoadDirectory("recipes", ...)`
lee `recipes/catalog.txt` antes de crear actores o workers. `CreatureRecipes_Find()`
consulta primero las recetas externas y conserva las incorporadas como respaldo.
La demo ecológica y su versión headless cargan este catálogo al arrancar; reiniciar
la demo aplica lo guardado. `--recipes-dir=directorio` permite elegir otro catálogo.
Las demás demos conservan su configuración anterior.

`CreatureRecipeIO_ReadJSON()` es transaccional: no modifica el destino al fallar.
Rechaza campos ausentes, desconocidos o duplicados, versiones no admitidas,
números no finitos, arrays fuera de capacidad y discrepancias de conteos. Resuelve
la anatomía de las cuatro etapas para detectar anfitriones, ranuras y referencias
no válidas. El proceso de preview tiene un límite de 60 segundos. Los archivos
están limitados a 2 MiB; el servidor escucha exclusivamente en la interfaz local.

Los descriptores están en `src/CreatureRecipeSchema.inc`. Tras cambios de tipos:

```sh
python3 tools/generate_recipe_schema.py
make -B test
make editor-test
```

El generador sólo analiza los tipos de datos alcanzables desde `CreatureRecipe`;
falla ante declaraciones no admitidas. El archivo `.inc` se genera, no se edita.
El formato JSON enumera campos semánticos y no serializa bytes de structs, padding,
punteros ni datos dependientes de la ABI.

## Validación

`make test` incluye la suite C de persistencia y construcción de criaturas desde
JSON. `make editor-test` prueba HTTP, exportación/importación fiel, guardar/reabrir,
conflictos de archivo/ID, errores de anclaje/versión/conteos y mallas finitas con
índices válidos para las cuatro recetas y sus cuatro etapas. No requiere pantalla.
La revisión visual interactiva del navegador es una comprobación adicional.

## Control público para agentes

La UI y los agentes comparten un documento, una selección y una revisión. La UI
consulta cambios cada 400 ms y publica las ediciones humanas válidas. Ante cambios
concurrentes se muestra un conflicto; el botón «IA · conflicto» permite recargar la
sesión compartida. El proceso servidor conserva el historial hasta que termina.

```sh
python3 tools/creature_editor/agent.py capabilities
python3 tools/creature_editor/agent.py schema
python3 tools/creature_editor/agent.py open builtin:3
python3 tools/creature_editor/agent.py state
python3 tools/creature_editor/agent.py set /recipe/adult/head/muzzleLength 0.46
python3 tools/creature_editor/agent.py view --stage adult --part head --camera front
python3 tools/creature_editor/agent.py inspect
python3 tools/creature_editor/agent.py render --output /tmp/mi-criatura.png
python3 tools/creature_editor/agent.py save brumacerno.creature.json --catalog-override --overwrite
```

`apply archivo.json` admite hasta 512 ediciones `{path,value}` en una transacción.
`preview --output malla.bin` descarga los buffers geométricos. `undo` y `redo`
recuperan revisiones. Los códigos HTTP son 409 para conflictos y 422 para datos
inválidos; ambas situaciones conservan el estado anterior.

| Ruta | Operación |
|---|---|
| `GET /api/agent` | Descubrir capacidades |
| `GET /api/agent/schema` | Campos, tipos, enumerados y capacidades compilados desde C |
| `GET /api/agent/state` | Documento, revisión, origen, selección y estado de guardado |
| `POST /api/agent/action` | `open`, `patch`, `replace`, `view`, `undo`, `redo` |
| `POST /api/agent/save` | Persistir documento y actualizar catálogo |
| `POST /api/agent/inspect` | Nodos anatómicos, radios y conteos de materiales de malla |
| `POST /api/agent/preview` | Metadatos y buffers de triángulos |
| `POST /api/agent/render` | PNG diagnóstico con la cámara seleccionada |

Ejemplo de transacción HTTP:

```json
{"action":"patch","expected_revision":7,"edits":[{"path":"/recipe/adult/head/muzzleLength","value":0.46}]}
```

El cliente de consola lee la revisión automáticamente. Para secuencias que deban
ser indivisibles, enviar una sola transacción HTTP con la revisión que se leyó al
prepararla. `window.monsterEditor` ofrece `state()`, `schema()`, `action()` y `save()`
como acceso público equivalente desde herramientas de navegador.

Guardar un ID incorporado requiere `catalog_override=true`; el catálogo externo
sustituye sus datos, sin modificar el código de su receta de respaldo. Cada ID sólo
puede aparecer en un archivo del catálogo. El núcleo conserva estos documentos
inmutables hasta finalizar todos sus consumidores.

Para generar una lámina diagnóstica de las mallas sin abrir un navegador (requiere
Pillow y NumPy, opcionales e independientes del editor):

```sh
python3 tools/creature_editor/render_preview.py
```

Produce `docs/validation/creature-editor/geometria.png` y tiempos en
`geometria.json`. Es una rasterización diagnóstica de los buffers del motor,
no una captura de la interfaz web.
