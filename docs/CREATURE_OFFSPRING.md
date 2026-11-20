# CreatureOffspring en Nexus

Construye y abre Nexus con `make run-interface`. CreatureOffspring y CO comparten la misma ventana y generación. Las rutas de los comandos se resuelven desde la carpeta actual del terminal. El catálogo de Nexus usa la ruta virtual `/recepies/`.

| Comando | Resultado |
| --- | --- |
| `CO` | Abre o restaura la ventana. También acepta `CreatureOffspring`. |
| `CO.create.5."/recepies/rinoceronte.creature.json"."/recepies/rata.creature.json"` | Solicita cinco candidatos. La ventana y el terminal muestran la semilla elegida. |
| `CO.create.5."rinoceronte.creature.json"."rata.creature.json".seed.48216` | Reproduce un lote desde `/recepies/` con la semilla indicada. |
| `CO.list` | Enumera los candidatos validados y su estado de guardado. |
| `CO.info.4` | Muestra pesos, origen de los sistemas, injertos, omisiones, intentos y reparaciones. |
| `CV.view.CO.4` | Abre el candidato en CreatureVisualizer. También acepta `CreatureVisualizer.view.CO.4`. |
| `CO.save.4` | Publica una receta JSON con nombre generado. Repetir el comando devuelve la ruta guardada. |
| `CO.save.4."MiCruce"` | Guarda con nombre personalizado. Admite letras ASCII, números, guiones y guiones bajos. |
| `CO.reroll.4` | Genera un reemplazo reproducible para el candidato indicado. Conserva los archivos guardados. |
| `CO.clear` | Descarta la generación temporal y deja la ventana abierta. |
| `CO.close` | Cierra CO y descarta la generación temporal. |
| `CO.help` | Muestra la sintaxis y los comandos disponibles. |

En los comandos de CO también se puede escribir `CreatureOffspring` como prefijo. En la galería se puede seleccionar una fila, hacer doble clic para verla o usar **Preview** y **Save**. La rueda desplaza la galería. En CV, arrastrar orbita la cámara y la rueda ajusta el encuadre. Las recetas guardadas se pueden abrir en Files o mediante `CV.view."/recepies/MiCruce.creature.json"`.

## Generación anatómica

`CreatureOffspring_Generate` recibe dos recetas y una configuración. Toma instantáneas de los padres, valida los campos del formato JSON v1 y resuelve sus grafos en las cuatro etapas. El emparejamiento usa la familia de módulo, la familia anfitriona, el landmark resuelto y la ranura semántica. Para las extremidades también compara rol y lado. Las correspondencias ambiguas conservan el módulo del plan base.

El genoma distribuye la contribución global de Y entre 35 y 65 por ciento por defecto. Usa pesos correlacionados por región y variaciones en constitución, cara, postura y cola. El muestreo usa el PRNG de CreatureGenome. Las subsemillas dependen de la versión del generador, la semilla de lote, el índice, la revisión y el intento. Repetir la misma secuencia de operaciones genera el mismo JSON. El perfil de cada candidato identifica la procedencia de las categorías discretas.

Cada candidato parte de uno de los planes corporales. Los arquetipos de cabeza y cola se eligen como sistemas completos. El generador conserva el sistema locomotor del plan base cuando las correspondencias, el arquetipo axial o la marcha impiden transferir todos los miembros juntos. Al combinar formas compatibles interpola proporciones según la longitud del torso o la altura de la cruz. La escala física se mezcla en espacio logarítmico. Los dígitos, el perfil del pie y las uñas acompañan a su extremidad.

Los injertos admitidos usan landmarks del hospedador, una identidad libre y un índice fenotípico coherente en las cuatro etapas. Ejemplos verificados incluyen cuernos nasales, colas, una probóscide y colmillos con una cabeza que publica las ranuras maxilares correspondientes. Los cuernos transferidos a otra familia cefálica se dimensionan con la envolvente del cráneo receptor. Los apéndices compuestos con rutas o membranas conservan sus dependencias cuando pertenecen al plan base. El informe indica injertos incompatibles, ranuras ocupadas, límites de capacidad y omisiones por selección.

El tegumento se hereda como perfil y combina controles continuos cuando coincide su familia. Los colores admiten mezcla continua. El pigmento anclado y los grupos de pelo largo se remapean y se cotejan con la anatomía resultante. Se descartan los descriptores que pierden el anfitrión o la estación.

## Reparación y validación

`OffspringRepair.c` ajusta la receta y vuelve a resolver la anatomía. Limita proporciones de cuello, cabeza, patas, pies, cola y apéndices. Corrige la altura de apoyo aproximada con las estaciones de las suelas. Para tetrápodos bajos se usa un plano relativo a la postura del padre que aporta el plan. Los apéndices flexibles que invaden el plano de apoyo se curvan y ajustan en longitud antes de resolver de nuevo. La reparación también interviene en el asiento ocular, el anclaje muscular de la extremidad y la raíz de la cola cuando el diagnóstico lo permite.

Se construyen anatomía, rig y SDF en las cuatro etapas y en tres edades intermedias. El control verifica identidades y conectividad del grafo, cotas finitas, apoyo, separación de los pies y continuidad de muestras SDF en uniones locomotoras y caudales. La firma morfológica compara torso, cráneo, extremidades, pies, cola y rasgos discretos. La pigmentación por sí sola no aporta diversidad suficiente.

La configuración admite entre 1 y 16 candidatos, hasta 32 intentos por candidato y un presupuesto de reparación de hasta 16 pasadas. El valor inicial es de seis pasadas. Un rechazo agota el intento o entrega un diagnóstico. CO publica el lote completo en una sola operación. El reemplazo de un candidato se prepara aparte y se publica en el mismo índice tras validarlo.

## Propiedad y guardado

El núcleo posee los documentos de los padres y de los candidatos. Cada copia vincula el nombre a la memoria de su propio documento. La ventana de CO lanza el trabajo CPU en un hilo con cancelación cooperativa y publica resultados en el hilo de Nexus. Cerrar o limpiar cancela y espera al trabajador. Minimizar conserva la sesión.

CV posee una copia de la receta previsualizada. CO invalida la vista temporal pendiente o visible al cerrar, limpiar, reemplazar el lote o regenerar ese candidato. CV libera la geometría y espera a sus trabajos de pelo al destruir la escena. Las solicitudes desde una ruta permanente tienen propiedad normal de CV.

El guardado resuelve el directorio del proyecto, reserva un ID entre los archivos existentes y valida el JSON escrito antes de publicar. Bajo un bloqueo de directorio, enlaza un archivo nuevo sin sobrescribir y sustituye el manifiesto mediante renombrado. Una publicación fallida del manifiesto elimina el enlace recién creado. Nexus añade la entrada al VFS después de confirmar el guardado. Los nodos mantienen dirección e ID estables con capacidad para 4096 entradas. El catálogo del motor amplía su tabla manteniendo la dirección de los documentos.

## Verificación

La matriz automatizada genera, reconstruye y serializa cinco candidatos por pareja para caballo × asno, rinoceronte × rata, cerdo × elefante, perro × lagarto, rata × rata, caballo × elefante, lagarto × rata, rata × rinoceronte y Brumacerno × Velkar. También se prueban un plan de laboratorio sin extremidades, una renumeración de módulos y fenotipos, un lote de 16, cancelación, entradas inválidas y regeneración determinista. El barrido con semilla 48216 cubrió las 121 combinaciones ordenadas de las 11 recetas disponibles. Se completaron otros 30 lotes de 16 candidatos distribuidos entre seis parejas y cinco semillas.

`make test`, `make interface-test`, `make interface-engine-test` y `make editor-test` cubren el núcleo, la interfaz, las escenas y el formato de recetas. Las suites de CO y del adaptador pasaron con AddressSanitizer y UndefinedBehaviorSanitizer. `make validate-ecosystem` completó las cinco simulaciones de 30 días y la repetición determinista de la última. Las pruebas del adaptador ejecutan guardado, errores de sintaxis, enlaces simbólicos, colisiones, persistencia tras reinicio, invalidación y cierre durante generación. La revisión gráfica conserva montajes, JSON y registros bajo `build/offspring-validation/final/`. Para repetir una captura desde un archivo local se puede usar `build/capture_creature receta.creature.json directorio 1 surface`.

## Mediciones y archivos

En un Ryzen 5 5600H, los nueve lotes de cinco candidatos con semilla 48216 tardaron de 18 a 35 ms de CPU por lote, o de 3,6 a 7 ms por candidato aceptado. El proceso de prueba registró entre 5556 y 5892 KiB de RSS máximo, incluida la carga de los padres. Los lotes sumaron un intento rechazado y 36 pasadas de reparación en los candidatos aceptados. Nexus mostró 20,4 ms de tiempo de pared para el lote rinoceronte × rata, incluida la carga de las recetas.

En una RTX 3050 Laptop, la escena temporal de CV se construyó en aproximadamente 0,6 a 1,3 ms. Con un área de 476 × 307 píxeles, el primer envío de un fotograma en un proceso recién abierto tardó unos 429 ms e incluyó la apertura y la preparación del shader. Un cambio de candidato con el renderizador ya preparado tardó aproximadamente 2 ms hasta el envío. El tiempo de presentación depende además de la GPU y del compositor. El registro de Nexus se puede activar con `NEXUS_VISUALIZER_PROFILE=1`.

| Área | Archivos principales |
| --- | --- |
| Genoma, correspondencias, lotes y regeneración | `include/CreatureOffspring.h`, `src/CreatureOffspring.c` |
| Reparación, geometría y formato de receta | `src/OffspringRepair.c`, `src/Creature.c`, `src/CreatureRecipeIO.c` |
| Galería, comandos y trabajadores de Nexus | `src/interface/UiOffspringApp.c`, `demos/interface_demo.c` |
| Escenas temporales y órbita | `src/interface/UiCreatureVisualizer.c`, `UiCreatureScene.c`, `UiCreatureEngine.c`, `UiCreatureOrbit.c` |
| Rutas, persistencia y registro | `src/interface/UiRecipeRoutes.c`, `UiRecipeCatalog.c`, `UiVfs.c`, `src/CreatureRecipeRegistry.c` |
| Matriz automatizada y ciclo de vida | `tests/test_creature_offspring.c`, `tests/test_interface_offspring.c`, `tests/test_interface_creature.c` |

## Alcance

Los resolutores axiales disponibles son los de tetrápodo erguido y bajo. La prueba sintética sin extremidades conserva uno de esos arquetipos. Los padres con cambios categóricos de topología entre etapas requieren un modelo de desarrollo compatible antes de generar un lote. El control de colisión es local y las muestras SDF cubren uniones seleccionadas. En algunas mezclas alejadas aún se aprecian transiciones duras en la piel, los pabellones y el contorno oral. Una futura evaluación volumétrica podría examinar esas transiciones y las intersecciones de ornamentos. Los injertos de cadenas con múltiples anfitriones podrían aprovechar un remapeo de subgrafos completo.

La neutralidad de apoyo terrestre se estima con estaciones anatómicas. La evolución temporal en animación requiere pruebas de contacto adicionales. La persistencia publica dos archivos con operaciones atómicas individuales. Ante una interrupción del proceso entre la publicación de la receta y la del manifiesto, puede quedar un JSON íntegro sin entrada de catálogo. Una recuperación explícita de transacciones facilitaría registrar esos archivos. Los datos de linaje se mantienen en la sesión de CO y podrían alimentar una futura integración con la reproducción del ecosistema.
