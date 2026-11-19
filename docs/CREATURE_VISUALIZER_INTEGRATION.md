# CreatureVisualizer en Nexus

## Construcción y uso

```sh
make demos/interface_demo demos/demo_morph_species
make interface-test interface-engine-test
make run-interface
```

El ejecutable gráfico enlaza el motor. El gestor de ventanas, las rutas, el VFS y los comandos conservan pruebas en C independientes de SDL, OpenGL y criaturas. `interface-engine-test` añade validación de recetas y anatomía con el motor sin crear una ventana.

Nexus intenta crear un contexto OpenGL 3.3 de compatibilidad. Si el contexto no se puede crear, intenta OpenGL 2.1 para el escritorio. En este último, una orden de visualización indica que necesita OpenGL 3.3 de compatibilidad. El adaptador utiliza los shaders GLSL del trazador SDF y comprueba el perfil y los buffers de textura.

## Propiedad y renderizado

El ID de la ventana pertenece al estado de la aplicación. `UiDesktop_Close` retira el marco, actualiza foco y captura y después llama a la limpieza del propietario. El botón y el comando siguen ese mismo camino. La plataforma destruye las aplicaciones con el contexto activo antes de liberar el contexto SDL.

Una orden resuelve uno o dos nodos del VFS y construye un candidato. Los documentos JSON, los adultos, el interpolador y el campo viven en la escena propietaria. El adaptador lee entradas externas con la misma raíz de proyecto que el catálogo y abre archivos regulares sin seguir enlaces simbólicos. El ID validado debe coincidir con el nodo. El candidato sustituye la escena anterior después de completar su primer pase gráfico. Los errores de ruta, receta, construcción, shader o framebuffer dejan la última textura válida disponible.

El pase de criaturas se ejecuta antes de dibujar la interfaz. Cada instancia del renderizador puede seleccionar su framebuffer de destino con `OpenGLRenderer_SetFrameTarget`. La demo morph conserva su destino anterior. Nexus mantiene dos destinos reutilizables con color opaco RGB8 y profundidad de 24 bits. El frontal conserva el fotograma publicado y el otro recibe el pase de trabajo. Los bordes lógicos se convierten a píxeles de framebuffer con la misma función que el recorte de la interfaz. Antes de enviar sus propios lotes, Nexus restablece framebuffer, programa, estados de vértices, texturas, mezcla, profundidad y matrices. El callback del marco dibuja la textura invertida en Y bajo el recorte habitual del contenido. El pelo se mezcla una sola vez sobre el fondo 3D.

La vista reutiliza la cámara de la demo morph, ajusta su encuadre al área integrada y mantiene la escena sin animación de cámara. Los adultos y los estados intermedios usan el mismo trazado SDF con pigmentos, escamas y pelaje implícito. Los ojos usan instancias y texturas del motor. El pelo largo usa el trabajador acotado del motor con transporte al estado anatómico actual durante el morph. La geometría de pelo largo se publica de forma asíncrona y el cierre drena ese trabajador antes de liberar documentos.

`MonsterAger` mantiene los extremos y la edad continua. El adaptador compila un campo por nueva edad y reutiliza el resultado para cámara y dibujo. Desactiva el snapshot geométrico adicional porque este modo no solicita mallas CPU. La presentación conserva la resolución física del contenido. Un fence consultado sin espera evita enviar una cola de pases 3D. El reloj está acotado a 100 ms por paso y conserva ambos extremos al cambiar de sentido. Minimizar pausa la animación. Un adulto estático reutiliza su textura hasta un cambio de dimensiones o una publicación de pelo.

## Diagnóstico

`NEXUS_VISUALIZER_PROFILE=1 make run-interface` registra cada pase con dimensiones, edad, dirección, tiempo CPU y la última consulta GPU SDF disponible. La consulta GPU mide el trazado interno, y el tiempo CPU mide la preparación y el envío del pase.

El controlador puede compilar shaders al primer uso. Con KHR/ARB parallel shader compile, el adaptador consulta la finalización del enlace mientras sigue procesando la interfaz. El primer dibujo aún puede requerir especialización síncrona del controlador. La vista integrada usa el modo SDF de producción con composición a su framebuffer. Su encuadre, resolución y publicación de pelo pueden diferir de una captura fija de la demo morph. Las rutas de extracción GPU y de malla experimental pertenecen a la demo standalone.

## Validación de esta integración

Se comprobó el escritorio y los tres programas juntos en una RTX 3050 Laptop con OpenGL 4.6 de NVIDIA. En un trayecto de Cerdo a Elefante, a 476 × 362 píxeles con Files y el terminal abiertos, Nexus envió 53,7 fotogramas de criatura por segundo después de los tres primeros fotogramas. La mediana y el p95 fueron 0,98 y 1,52 ms de CPU para el pase y 13,21 y 16,03 ms para las consultas GPU SDF. El extremo se alcanzó en 5,00 s. Una muestra de 2,5 s del proceso midió 0,0 % de un núcleo con el escritorio vacío y 0,4 % con una criatura estática. El proceso no envió pases 3D durante la minimización.

Las capturas en `build/interface-demo/` incluyen la vista inicial de 476 × 362 completamente negra, Perro, Elefante, Caballo, el morph alrededor de 25 %, 50 % y 75 %, y los estados redimensionado y cerrado. `creature-comparison.png` compara los mismos adultos y edades aproximadas con la demo independiente. En ella se conservaron crin, cola, ojos, colmillos, trompa y pezuñas. Cambian el encuadre y el nivel de detalle visible por la resolución de cada captura.

Con la caché de shaders de NVIDIA desactivada, el terminal procesó una orden durante el enlace. La primera criatura tardó unos 10,8 s desde el comienzo de la prueba, de los cuales el primer pase ocupó 3,76 s de CPU durante la especialización del controlador. Esta pausa de primer uso se distingue del rendimiento en caliente. La conversión de escala fraccionaria está cubierta por pruebas de coordenadas y el hardware se probó con escala 1×.
