# Escritorio de comandos Nexus

```sh
make demos/interface_demo
./demos/interface_demo
# Alternativas
make run-interface
make interface-test
```

La demo abre una ventana de 1280 × 720 con el terminal enfocado. El ejecutable requiere SDL2 2.0.18 o posterior y OpenGL de escritorio 2.1. `make demos` también lo construye. El modelo y sus pruebas se compilan en C sin dependencias gráficas.

## Controles

| Entrada | Acción |
| --- | --- |
| Barra de título y esquina inferior derecha | Arrastrar y redimensionar el terminal |
| Botón «−» y botón Terminal del dock | Minimizar y recuperar el terminal |
| Clic en una ventana, clic en el escritorio | Activar una ventana o retirar el foco |
| Ctrl+Tab, Ctrl+Mayús+Tab | Recorrer las ventanas visibles |
| Intro, ↑ y ↓ | Ejecutar y recuperar comandos. Volver abajo restaura el borrador |
| Clic en el prompt, ←, →, Inicio y Fin | Posicionar el cursor |
| Retroceso, Supr y Ctrl+V | Editar y pegar texto |
| Rueda sobre el registro, Re Pág y Av Pág | Desplazar la salida |

Prueba `print Hello world` o `print "¡Qué tal!  Café y pingüinos"`. La separación inicial entre el nombre y su argumento puede usar espacios o tabuladores. El resto del argumento conserva sus espacios. Si empieza por comillas dobles, debe cerrarse con comillas dobles y puede contener `\"` para una comilla literal y `\\` para una barra invertida. `print` sin argumento agrega una línea vacía. Los comandos desconocidos generan un error en el registro.

El prompt admite hasta 1023 bytes UTF-8. El historial conserva 64 comandos y el registro 256 entradas. Las líneas pegadas se unen con espacios. La navegación usa puntos de código UTF-8. El atlas cubre ASCII visible, Latin-1 y Latin Extended-A con un glifo de sustitución para los caracteres ausentes. Los caracteres fuera de la cobertura siguen en la entrada y en el registro. El renderizado usa celdas monoespaciadas con un glifo por punto de código.

## Módulos y propiedad

- `UiPlatform` crea la ventana y el contexto GL, traduce SDL a `UiEvent` y activa la entrada de texto para la ventana que tiene el foco. El bucle espera eventos o el siguiente cambio del cursor. Las coordenadas de entrada y diseño son lógicas. El viewport y los clips usan el tamaño real del framebuffer.
- `UiDesktop` guarda los marcos, los IDs y el orden de dibujo. Gestiona foco, captura, redimensionado y activación. Copia la descripción de cada ventana. El programa conserva la propiedad del título y de su estado hasta retirar la ventana. Los punteros al arreglo interno son transitorios. Los callbacks de dibujo y pérdida de foco deben conservar la estructura del escritorio durante su ejecución.
- `UiRenderer` posee el atlas OpenGL y un lote reutilizable de vértices. `UiTheme` dibuja cada marco y recorta el callback de contenido. El dibujo conserva el orden cuando cambia la textura o el clip. Los recursos del renderizador se liberan antes de destruir el contexto.
- `UiTerminal` posee la edición, el historial y el registro acotado. Cuando el usuario lee hacia atrás, conserva un ancla por entrada y posición UTF-8 hasta que la entrada sale del historial. `UiTerminalView` transforma ese estado en dibujo e interacción.
- `UiCommands` copia los nombres registrados y llama a un handler con sus argumentos y su contexto. La salida del handler se escribe en un búfer del llamador. El programa puede registrar otro handler cuyo contexto apunte a su propio estado y que llame a `UiDesktop_Add` o `UiDesktop_Activate`. Cada nueva ventana puede registrar su propio dibujo y entrada.

El atlas integrado se generó con Fira Code Regular 6.2, distribuida bajo SIL OFL 1.1. La licencia está en [assets/interface/fonts/OFL.txt](../assets/interface/fonts/OFL.txt). Para regenerar el atlas se necesita Pillow y una copia de Fira Code. La compilación y la ejecución usan el atlas incluido.

```sh
python3 tools/generate_interface_atlas.py /ruta/a/FiraCode-Regular.ttf
```

La retícula técnica, los marcos compactos y la interacción de escritorio toman como referencia la [guía del terminal de IFSCL](https://www.ifscl.org/fr/versions/current/article/65), su [guía de interacción](https://www.ifscl.org/en/versions/47X/article/131) y las [capturas en IndieDB](https://www.indiedb.com/games/ifscl/images/ifscl-48x-screenshots2).
