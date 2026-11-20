# CreatureOffspring V2

V2 genera recetas procedurales independientes a partir de referencias resueltas por CreatureMorph. Las dos formas parentales contribuyen al cráneo, la cara y la mandíbula aunque tengan arquetipos distintos. Nexus conserva los comandos, la previsualización temporal, el guardado y la regeneración por candidato. `CO.info.N` muestra la posición solicitada, el peso geométrico tras el suavizado, el peso cefálico aplicado y tres errores normalizados. El generador V2 reproduce sus propios resultados a partir de la semilla. Una semilla de V1 produce ahora el lote de V2.

## Síntesis y representación

V1 copiaba el fenotipo cefálico, la envolvente y los ojos de un padre. Solo combinaba la mayoría de los controles faciales cuando coincidían el arquetipo, el hocico, el cráneo y la disposición ocular. Los parámetros se interpretaban dentro del arquetipo escogido. Cambiar el ancho y la longitud no permitía reproducir las proporciones y volúmenes independientes de la cabeza intermedia. El torso y los miembros también amortiguaban la mezcla al cruzar familias, y las capas de pigmento retenían gran parte del perfil seleccionado.

1. El lote prepara las instantáneas, los grafos del desarrollo y ambos monstruos adultos. Cada candidato obtiene una posición estratificada en el intervalo 35–65 % por defecto. `CreatureMorph_Interpolate` construye la referencia. El ajuste usa el mismo peso suavizado del morph y compara posiciones relativas al cuello o al pectoral en unidades axiales normalizadas. La escala física del descendiente se calcula por separado.
2. El plan y las ranuras mantienen la correspondencia semántica de V1. El perfil opcional `headHybrid` añade un segundo `HeadPhenotype`, dos envolventes en unidades axiales, un peso y un indicador. El primer `HeadPhenotype` sigue en `head`. El resolutor construye ambas formas y comparte `HeadAnatomy_Blend` con CreatureMorph. Se interpolan perfiles del cráneo y de la cara, mejillas, maxilar, mandíbula, órbitas, orejas y planos de la boca. Las fosas nasales se limitan al plano nasal interpolado y el ajuste de la bisagra conserva el orden entre pivote, corte oral y mandíbula. Las ranuras publicadas provienen del resolutor normal, que también dispone la unión con el cuello, los materiales faciales y el asiento ocular.
3. El ajustador acerca centros y secciones de seis estaciones axiales a la referencia. En cruces de postura baja y erguida, también acerca la altura de la cruz y el alcance de las patas si se usa un plan erguido. Comprueba las proporciones de las articulaciones y los radios proximales y distales de los miembros homólogos. Revierte propuestas que empeoran ese error local o invalidan el grafo. Un descenso por coordenadas de 24 pruebas ajusta los tres ejes de la envolvente cefálica mediante 18 posiciones y medidas, con una penalización por alejarse del valor inicial. Las correcciones se propagan a los fenotipos de desarrollo. La diversidad proviene de distintos objetivos, de variación proporcional determinista y de los sistemas discretos que admite el plan.
4. Ambas capas de pigmento conservan sus colores y patrones, con anclajes remapeados al descendiente. La interpolación combina los colores corporal, ventral, dorsal y secundarios, además de los oculares y nasales, el relieve y la cobertura en los canales compatibles. El estado de mezcla se conserva durante el desarrollo. Cuando coinciden pelaje y escamas, el shader evalúa el sustrato bajo el pelo. Los grupos de pelo largo siguen usando anclajes del plan seleccionado.
5. La reparación comprueba las dos bases oculares, las uniones y proporciones, el apoyo, las referencias de pigmento, el rig y el SDF en las cuatro etapas y tres edades intermedias. Conserva las dimensiones cefálicas ajustadas y descarta un candidato cuando supera el umbral de error cefálico o incumple una restricción anatómica. Los intentos siguen acotados y cancelables.

Los campos nuevos son opcionales en el formato JSON v1. Los archivos previos siguen la ruta habitual de un solo arquetipo. Cada etapa guarda los parámetros de sus formas y el peso. La interpolación ontogenética conserva ambas familias y actualiza sus parámetros durante el crecimiento. El mismo resolutor y el mismo SDF se usan en la previsualización, al cargar desde JSON y al articular la mandíbula. La receta guarda parámetros editables y carece de dependencias sobre los padres originales.

## Experimentos

Con el punto medio exacto de rata × elefante, un ajuste de 26 controles ordinarios con arquetipo fijo redujo el error cefálico de 0.48736 a 0.09128 con la base roedora y de 0.57981 a 0.03000 con la base proboscídea. Ambas bases moldearon la cara dentro de sus respectivos arquetipos. El perfil V2 del candidato 3 obtuvo 0.00687 frente a ese punto medio exacto y 0.00335 frente a su propio objetivo de 49.354 % (49.031 % geométrico). La prueba con arquetipo fijo resuelve el fenotipo directamente y puede partir de una envolvente distinta de la receta de cada padre usada en el montaje completo.

Las filas siguientes comparan la tercera receta de lotes de cinco con semilla 48216 y el mismo objetivo designado. Se usó V1 de `45273047f6e4a832a3238f8007930f6d02dbffbd`. Menor error es mejor. La intersección sobre unión (IoU) de la silueta lateral aumenta al acercarse a la referencia.

| Cruce | SDF de cabeza V1 → V2 | Estaciones del torso V1 → V2 | IoU lateral V1 → V2 | Mediana CPU por lote V1 → V2 |
| --- | ---: | ---: | ---: | ---: |
| Caballo × asno | .01373 → .00096 | .00767 → .00480 | .914 → .957 | 15 → 19 ms |
| Rata × elefante | .04699 → .00266 | .10643 → .01671 | .369 → .773 | 42 → 39 ms |
| Rinoceronte × rata | .03430 → .00444 | .03895 → .01559 | .699 → .780 | 20 → 29 ms |
| Cerdo × elefante | .01995 → .00229 | .05506 → .00944 | .657 → .914 | 25 → 28 ms |
| Perro × lagarto | .02877 → .00102 | .03982 → .01578 | .519 → .660 | 16 → 20 ms |
| Rata × rata | .00419 → .00071 | .01596 → .00559 | .819 → .965 | 20 → 24 ms |

El SDF cefálico es una diferencia media absoluta en una banda cerca de la superficie sobre una cuadrícula de 16³, con el cuello alineado y escala normalizada. Excluye apéndices independientes. Los montajes usan el mismo encuadre ortográfico, iluminación y escala para padre X, morph, V1, V2 y padre Y. Los montajes incluyen lateral, frontal, tres cuartos y dos vistas cefálicas. El muestreo incluye también los morphs de 35, 42, 50, 58 y 65 %. La medición de CPU usó 12 lotes por cruce y revisión en el mismo Ryzen 5 5600H. En una medición de rata × elefante, el proceso del banco completo pasó de 5872 a 6592 KiB de RSS máximo, incluida la carga de las recetas. Las capturas usaron la RTX 3050 Laptop.

En la inspección de rata × elefante, el candidato sin probóscide muestra cráneo, orejas y hocico intermedios. La variante con probóscide conserva una unión más gruesa que la de algunos fotogramas del morph y mantiene un apéndice articulado. En perro × lagarto, bajar la postura mejoró la silueta lateral y de tres cuartos. Los pies y parte de la postura conservan la topología del plan, de modo que el error de proporciones de miembro subió de .202 a .257 frente a V1. La revisión de los cinco candidatos de rata × elefante detectó que el torso de dos variantes ocultaba buena parte de las patas. Expresar la altura de las secciones torácicas respecto a la cruz del descendiente recuperó el espacio bajo el vientre. La nueva prueba verifica esa proporción en todo el lote. El montaje de poses incluye la probóscide recogida y extendida, la marcha adulta y una edad intermedia de 0.52 con la mandíbula abierta.

El caso de rata × elefante reconstruido desde JSON produjo píxeles idénticos en los cinco encuadres comparados con la receta aún en memoria. La prueba automatizada verifica diferencias SDF inferiores a `1e-6` en 256 puntos, igualdad del material compilado, apertura mandibular y actualización de marcha.

## Verificación y reproducción

- `make test interface-test interface-engine-test editor-test demos/interface_demo` cubre regresiones del motor, etapas, determinismo, carga de JSON, comandos, previsualización y ciclo de vida, y compila Nexus.
- Una exploración adicional generó 121 lotes de cinco para los 121 pares ordenados de las 11 recetas de la prueba, incluidas las combinaciones de una receta consigo misma. Esto acota la cobertura al catálogo y la semilla ensayados.
- `make visual-morph-gpu-regressions` completó la regresión de morph. El validador de recetas GPU también pasó en las cuatro etapas de rata × elefante y en el adulto perro × lagarto, con un error CPU/GPU máximo de aproximadamente 0.60 µm entre esas cinco pruebas.
- La sesión de escritorio real generó y previsualizó el lote rata × elefante, guardó una receta y la volvió a abrir en otro proceso. La galería, el informe y ambas previsualizaciones se guardaron en `build/offspring-v2/nexus-*.png`.
- `make build/capture_offspring_comparison` compila el comparador con el renderizador OpenGL. Las recetas de padre se indican por ruta o, para recetas incorporadas, con `@ID`. Después de exportar el mismo índice de ambas revisiones, se puede repetir el montaje con:

```sh
mkdir -p build/comparacion
build/capture_offspring_comparison recipes/rata.creature.json recipes/elefante.creature.json \
  lote-v1/3.creature.json lote-v2/3.creature.json 0.49354 build/comparacion
python3 tools/offspring_comparison_sheet.py build/comparacion 'Rata × elefante'
```

El comparador acepta `--reference`, `--ordinary-fit` y `--roundtrip`. Para `--roundtrip` genera en memoria el lote V2 de cinco con semilla 48216 y compara el candidato correspondiente al objetivo con el archivo V2. Se pasa la misma opción al script de montaje. Los resultados locales están bajo `build/offspring-v2/references/`, con los cinco candidatos de rata × elefante en `rat-elephant/candidates.png` y las comparaciones de articulación y recarga en `with-trunk/` y `roundtrip/`. Las capturas de animación y desarrollo se generan con `build/capture_creature`, que incluye el pelo largo. El comparador de campo y silueta usa SDF exacto y ojos, y omite las hebras largas.

| Área | Archivos principales |
| --- | --- |
| Referencia, lotes y semillas | `CreatureMorph.c`, `CreatureOffspring.c`, `CreatureOffspring.h` |
| Ajuste y reparación | `OffspringMorphFitter.c`, `OffspringRepair.c` |
| Perfil, resolución y rig | `HeadBlend.c`, `HeadModule.c`, `Head.h`, `Creature.c`, `Monster.c` |
| Ontogenia y persistencia | `CreaturePhenotype.c`, `CreatureRecipe.c`, `CreatureRecipeIO.c`, `generate_recipe_schema.py` |
| Cobertura y renderizado | `Surface.c`, `sdf_surface_material.glsl`, `monster_surface.frag`, `MonsterVisual.c`, `MonsterVisualAsync.c` |
| Diagnóstico y pruebas | `UiOffspringApp.c`, `test_creature_offspring.c`, `capture_offspring_comparison.c`, `offspring_comparison_sheet.py` |

## Límites

La probóscide, los cuernos, la topología de los pies y las ranuras siguen siendo decisiones discretas. Esta elección conserva la articulación y evita reproducir colapsos transitorios del morph. El ajuste usa medidas anatómicas y un objetivo cefálico compacto. Las comparaciones SDF y de imagen se calculan fuera de la generación. Las uniones entre topologías muy dispares, las intersecciones de ornamentos y el contacto a lo largo de una zancada requerirían métricas adicionales.

Si un progenitor ya tiene un perfil híbrido, su aportación cefálica se aproxima con su base primaria para mantener el tamaño de la representación. `CO.info.N` indica esa simplificación. Para conservar más generaciones de ascendencia, una futura representación podría condensar varias bases en estaciones anatómicas explícitas. La inspección de desarrollo y movimiento está concentrada en rata × elefante. Las seis comparaciones visuales de la matriz corresponden a adultos.
