# Escritorio de comandos Nexus

```sh
make demos/interface_demo
./demos/interface_demo
# Alternativas
make run-interface
make interface-test
```

La demo abre una ventana de 1280 × 720 con el terminal enfocado. El ejecutable requiere SDL2 2.0.18 o posterior. El escritorio admite OpenGL 2.1 de escritorio. CreatureVisualizer solicita OpenGL 3.3 o posterior con perfil de compatibilidad. `make demos` también lo construye. El modelo y sus pruebas se compilan en C sin dependencias gráficas.

## Controles

| Entrada | Acción |
| --- | --- |
| Barra de título y esquina inferior derecha | Arrastrar y redimensionar la ventana |
| Botón «−» y entradas del dock | Minimizar y recuperar una ventana conservando su estado. El dock también inicia las aplicaciones |
| Botón «×» de Files y CreatureVisualizer | Cerrar la aplicación y liberar su sesión. El terminal queda disponible |
| Clic en una ventana, clic en el escritorio | Activar una ventana o retirar el foco |
| Ctrl+Tab, Ctrl+Mayús+Tab | Recorrer las ventanas visibles |
| Intro, ↑ y ↓ | Ejecutar y recuperar comandos. Volver abajo restaura el borrador |
| Clic en el prompt, ←, →, Inicio y Fin | Posicionar el cursor |
| Retroceso, Supr y Ctrl+V | Editar y pegar texto |
| Rueda sobre el registro, Re Pág y Av Pág | Desplazar la salida |
| Files, doble clic o Intro sobre una carpeta | Abrir la carpeta en Files |
| Files, Back o Retroceso, Up | Volver en el historial de Files o subir al directorio padre |
| Files, barra lateral, flechas y rueda | Elegir una ubicación, cambiar selección o desplazar la lista |

## Comandos y sistema de archivos virtual

Cada sesión del terminal comienza en `/`. El prompt `nexus:/ $` muestra la ubicación sin incluirla en el texto editable ni en el historial. El árbol en memoria contiene `/recepies`, con esa grafía. Sus rutas y las rutas del sistema anfitrión son independientes.

| Comando | Resultado |
| --- | --- |
| `pwd` | Muestra la ruta virtual actual |
| `ls [ruta]` | Lista el directorio indicado o el actual, una entrada por línea. Las carpetas terminan en `/` |
| `cd [ruta]` | Cambia de directorio. Sin argumento vuelve a `/` |
| `Files` | Abre, enfoca o recupera la ventana Files |
| `Files.close` | Cierra Files, incluso minimizado. La próxima apertura empieza en la raíz |
| `CreatureVisualizer` | Abre la vista vacía o enfoca la existente |
| `CreatureVisualizer.close` | Cierra la vista y libera la escena |
| `CreatureVisualizer.view.<ruta>` | Abre la ventana y muestra un adulto |
| `CreatureVisualizer.view.interpolation.<ruta1>.<ruta2>` | Alterna en ambos sentidos entre dos adultos durante unos cinco segundos por trayecto |
| `print [texto]` | Escribe texto en el registro |

Las rutas admiten `.`, `..`, separadores repetidos y navegación relativa o absoluta. En la raíz, `..` permanece en la raíz. `cd` conserva la ubicación si la ruta falla. `ls` acepta un directorio y no tiene opciones.

Prueba `print Hello world` o `print "¡Qué tal!  Café y pingüinos"`. La separación inicial entre el nombre y su argumento puede usar espacios o tabuladores. El resto del argumento conserva sus espacios. Si empieza por comillas dobles, debe cerrarse con comillas dobles y puede contener `\"` para una comilla literal y `\\` para una barra invertida. `print` sin argumento agrega una línea vacía. Los comandos desconocidos generan un error en el registro.

Files inicia en `/` y comparte el árbol de recetas con el terminal. Su ubicación, selección e historial son propios. La lista distingue archivos externos y recetas incorporadas, y permite seleccionar las entradas de solo lectura. Activar un archivo muestra el estado de la vista previa. El dock y Ctrl+Tab permiten volver al terminal en cualquier momento.

### CreatureVisualizer

```text
CreatureVisualizer.view./recepies/Perro.recipe
CreatureVisualizer.view./recepies/elefante.creature.json
CreatureVisualizer.view.interpolation./recepies/cerdo.creature.json./recepies/elefante.creature.json
```

El registro entrega el sufijo al prefijo inscrito más largo. Una vista recibe la ruta completa. La interpolación prueba los puntos como posibles separadores y requiere un único par de archivos válidos. Ambas rutas pueden ser relativas al directorio del terminal. Los archivos se resuelven en el VFS. Para los JSON, el adaptador abre la entrada autorizada del catálogo y aplica la validación completa del motor antes de construir el adulto. Las entradas `.recipe` usan las recetas compiladas del motor.

La ventana nueva tiene el contenido negro. Abrirla desde el dock o el comando mientras está minimizada conserva su escena y edad. Cerrar con el botón «×» o el comando libera la escena y deja la próxima apertura vacía. El terminal conserva su historial y contenido. Ver [renderizado integrado y validación](CREATURE_VISUALIZER_INTEGRATION.md).

### Catálogo de recetas

`UiRecipeCatalog` localiza `../recipes/catalog.txt` respecto al ejecutable en `demos/`. Comprueba que `demo_morph_species.c` acompaña al ejecutable para identificar el árbol del proyecto. Se puede arrancar desde otro directorio de trabajo. Si falta el árbol o el manifiesto, el terminal avisa y Files conserva las recetas incorporadas.

El adaptador inspecciona nombres del manifiesto, el formato JSON v1 y los metadatos `recipe.id` y `recipe.name`. Rechaza nombres con separadores o recorridos de directorio, archivos ausentes, enlaces simbólicos y metadatos inválidos. La vista de catálogo utiliza esos metadatos. CreatureVisualizer vuelve a leer únicamente la entrada elegida y comprueba su ID y anatomía antes de construirla. Las entradas externas conservan el orden del manifiesto y tienen precedencia por ID. En caso de duplicado se conserva la primera entrada válida. Las recetas compiladas restantes se agregan en orden de ID a partir de la tabla de metadatos de `UiRecipeCatalog.c`. Sus extensiones `.recipe` representan entradas virtuales. Por ejemplo, el JSON externo con ID 3 sustituye la entrada incorporada de Brumacerno.

El prompt admite hasta 1023 bytes UTF-8. El historial conserva 64 comandos y el registro 256 entradas. Las líneas pegadas se unen con espacios. La navegación usa puntos de código UTF-8. El atlas cubre ASCII visible, Latin-1 y Latin Extended-A con un glifo de sustitución para los caracteres ausentes. Los caracteres fuera de la cobertura siguen en la entrada y en el registro. El renderizado usa celdas monoespaciadas con un glifo por punto de código.

## Módulos y propiedad

- `UiPlatform` crea la ventana y el contexto GL, traduce SDL a `UiEvent` y activa la entrada de texto para la ventana que tiene el foco. El bucle espera eventos, el siguiente cambio del cursor o el turno de una aplicación animada. Las coordenadas de entrada y diseño son lógicas. El viewport y los clips usan el tamaño real del framebuffer.
- `UiDesktop` guarda los marcos, los IDs y el orden de dibujo. Gestiona foco, captura, redimensionado y activación. Copia la descripción de cada ventana. El programa conserva la propiedad del título y de su estado hasta retirar la ventana. Los punteros al arreglo interno son transitorios. Los callbacks de dibujo y pérdida de foco deben conservar la estructura del escritorio durante su ejecución.
- `UiRenderer` posee el atlas OpenGL y un lote reutilizable de vértices. `UiTheme` dibuja cada marco y recorta el callback de contenido. El dibujo conserva el orden cuando cambia la textura o el clip. Los recursos del renderizador se liberan antes de destruir el contexto.
- `UiTerminal` posee la edición, el historial y el registro acotado. Cuando el usuario lee hacia atrás, conserva un ancla por entrada y posición UTF-8 hasta que la entrada sale del historial. `UiTerminalView` transforma ese estado en dibujo e interacción.
- `UiCommands` copia nombres registrados y llama al handler con su contexto. Un handler puede entregar una respuesta en el búfer, escribir varias líneas con `UiCommandOutput` o devolver `UI_COMMAND_SILENT`. El receptor del terminal consume cada línea al momento y la incorpora al registro. Un `print` vacío sigue produciendo una línea, mientras `cd` y `Files` exitosos solo dejan su orden en el historial de salida.
- `UiVfs` posee hasta 192 nodos con IDs estables, orden de inserción y metadatos de solo lectura. El anfitrión puede agregar carpetas y archivos con `UiVfs_Add` durante la construcción, antes de compartir el árbol con las aplicaciones. Una ruta puede ocupar hasta 1023 bytes UTF-8. `UiShell` toma prestado el árbol y posee el directorio actual del terminal.
- `UiFilesApp` conserva la navegación, la selección y el ID de su ventana. `UiFilesView` dibuja el explorador y traduce las interacciones en acciones de ese modelo. La demo pasa una descripción de ventana mediante `UiFilesLaunch` y registra el handler de apertura en `UiCommands`. Las aplicaciones futuras pueden seguir el mismo patrón con su propio estado y callbacks `UiWindowDraw` y `UiWindowInput`.

El atlas integrado se generó con Fira Code Regular 6.2, distribuida bajo SIL OFL 1.1. La licencia está en [assets/interface/fonts/OFL.txt](../assets/interface/fonts/OFL.txt). Para regenerar el atlas se necesita Pillow y una copia de Fira Code. La compilación y la ejecución usan el atlas incluido.

```sh
python3 tools/generate_interface_atlas.py /ruta/a/FiraCode-Regular.ttf
```

La retícula técnica, los marcos compactos y la interacción de escritorio toman como referencia la [guía del terminal de IFSCL](https://www.ifscl.org/fr/versions/current/article/65), su [guía de interacción](https://www.ifscl.org/en/versions/47X/article/131) y las [capturas en IndieDB](https://www.indiedb.com/games/ifscl/images/ifscl-48x-screenshots2).
