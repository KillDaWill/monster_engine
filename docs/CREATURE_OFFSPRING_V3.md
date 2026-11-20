# CreatureOffspring V3

V3 conserva la API de lotes de CreatureOffspring y los comandos de Nexus y CreatureVisualizer. Cada hijo es una receta JSON v1 independiente, con las cuatro etapas, semilla de propuesta y versión del generador. `CO.info.N` muestra la semilla del lote y de mutación, los aportes regionales de Y, los errores de ajuste, los resultados de selección y las reparaciones.

## Herencia y reconstrucción

1. **Referencias parentales.** Se copian las recetas, se resuelven sus grafos en las cuatro etapas y se conservan los adultos para CreatureMorph. La correspondencia de módulos se basa en la región, la ranura, el rol y el lado. Las cabezas se resuelven con su representación original, incluidas las de V2 y V3, y se alinean en el cuello en reposo.
2. **Genoma.** La contribución global se estratifica entre 35 % y 65 % por defecto. Cada propuesta deriva su semilla de la semilla del lote, el estrato, la revisión y el intento. Siete flujos deterministas controlan la posición global, la cabeza, el torso, las extremidades, la superficie, las estructuras y la mutación. Se reutiliza el genoma en las cuatro etapas. Las desviaciones de los pesos siguen una distribución triangular acotada. Cada región parte del peso de su sistema o de una mezcla de regiones relacionadas. El orbitario parte del cráneo, el mandibular toma parte del rostro y las proporciones de los segmentos vecinos comparten un peso de miembro. `regionalVariance` y `mutationIntensity` ajustan las amplitudes en [0,2], con 1 por defecto.
3. **Perfil cefálico.** Se interpola la anatomía resuelta en un marco local común. Seis secciones craneales conectan el hocico con la nuca, con una influencia del rostro de 100 %, 100 %, 32 %, 15 %, 0 % y 0 %. El rostro conserva una longitud y una sección relativas a la bóveda. Otras seis estaciones describen la mandíbula con su propio peso y se ajustan al corte oral y al cóndilo. Mejillas, órbitas, cuello y nariz conservan sus propios objetivos. Las inserciones se reasientan en el cráneo o el rostro recompuesto.
4. **Orejas.** Presencia discreta cuando un progenitor carece de pinna. Altura, anchura y curvatura reciben pesos independientes y mutación acotada. El perfil se expresa relativo a la bóveda recompuesta. La orientación interpola marcos mediante cuaterniones y el pabellón derecho refleja al izquierdo. La raíz se apoya en el cráneo y se comprueba contra el SDF sin la pinna.
5. **Ajuste y editabilidad.** El perfil admite directamente las secciones objetivo; una proyección determinista ordena estaciones y ajusta las uniones, el cóndilo y las narinas. El resolutor, los slots, la boca, el rig y el SDF consumen el mismo resultado. Los controles semánticos de anchura de cráneo, longitud de hocico, profundidad mandibular, tamaño de oreja y escala ocular se aplican respecto al valor de referencia guardado. La envolvente y las secciones también se pueden editar en la receta. El hijo guarda el perfil final en unidades axiales, con hitos y parámetros geométricos. Los campos de base de V2 siguen disponibles para leer sus recetas.
6. **Torso y extremidades.** Las estaciones homólogas de cuello, hombro, tórax, abdomen y pelvis se interpolan en torno al pectoral. El ajuste combina secciones parentales locales, el morph de postura para planes incompatibles y la continuidad del perfil. La topología de pie procede de un padre. Longitudes, espesor, orientación, inserción y dimensiones de pies homólogos usan pesos distintos. Las colas compatibles combinan longitud y curvatura. Los apéndices conservan una topología válida y se dimensionan respecto a su anfitrión.
7. **Generaciones sucesivas.** La resolución parental usa la geometría completa que consume el rig y se guarda un único perfil por etapa en el descendiente, con tamaño acotado. La interpolación ontogenética usa el mismo evaluador de secciones y pinnae. El lote rechaza incompatibilidades de grafo y comprueba las cuatro etapas y tres edades intermedias.

## Apariencia

- El material interpreta RGB/255 como albedo lineal. Se recombinan colores dorsales, ventrales, faciales, de extremidades y de cola. Los ojos y los materiales orales heredan sus colores en canales separados. La luminancia y la saturación varían dentro de los límites del genoma. Los colores regionales se mezclan con los pesos de tejidos del mapeo de reposo antes de evaluar las capas.
- Los dos bancos de pigmento conservan familias, semillas enteras y dominios anatómicos. El peso global de patrón es independiente del color y de la forma. Fuerza, frecuencia, nitidez, orientación y tamaño del dominio reciben mutaciones acotadas. Se remapean las anclas por correspondencia antes de resolver el material.
- Cada progenitor híbrido se condensa en un banco. Se conserva el orden del banco dominante y las capas distintas del otro banco se añaden por opacidad de mayor a menor hasta agotar la capacidad de cuatro. El informe cuenta los descartes. El hash efectivo también depende del banco y de la posición de la capa, por lo que una capa que cambia de banco o de posición puede cambiar de fase aunque conserve su semilla.
- El relieve de escamas y la densidad y longitud de pelo tienen pesos propios. La mezcla combina los perfiles de región que admite el motor. Puede conservar relieve de escamas debajo del pelo. El pelo largo de ambos lados se reasigna a módulos existentes, conserva sus semillas y pasa por el sistema habitual de guías, anclas y colisiones. La prioridad de capacidad sigue el peso de pelo.
- El compilador de superficie añade albedo regional y giro de patrón a sus filas de GPU. Malla, SDF y fibras comparten el evaluador. La sonda de CPU reproduce el albedo procedural sin el filtrado de huella del shader y comprueba el resultado en compute.

## Selección y costes

Se conservan hasta seis propuestas válidas por estrato por defecto, con un máximo de 32 intentos por estrato. Cada propuesta se somete a validación de proporciones, soporte y anclas. Se comprueban las cotas SDF, las patas, las uniones, las inserciones nasales y auriculares y el asiento de los ojos visibles. La selección mide perfiles y razones anatómicas junto a 200 muestras del SDF cefálico en un marco común derivado de los padres. El color se mide con pigmento evaluado, intensidad y frecuencia de capas y características del integumento.

Se elige primero la mejor calidad y a continuación se realiza una selección de máxima distancia mínima entre todas las propuestas de los estratos libres. La calidad penaliza las desviaciones regionales y los pases de reparación. Una bonificación acotada conserva variedad de planes anatómicos. Se descartan identificadores repetidos y morfologías casi idénticas. La aportación del color queda limitada por la distancia de forma. El orden publicado conserva los estratos de contribución. La regeneración se calcula contra el lote vivo y se publica como una sustitución transaccional. Las propuestas se validan sin remallar.

## Archivos principales

| Área | Implementación |
| --- | --- |
| Genoma, selección y diagnóstico | `src/CreatureOffspring.c`, `include/CreatureOffspring.h`, `src/interface/UiOffspringApp.c` |
| Perfil, ajuste y reparación | `src/HeadBlend.c`, `src/HeadRegionalProfile.c`, `src/OffspringMorphFitter.c`, `src/OffspringRepair.c` |
| Desarrollo y JSON | `src/CreaturePhenotype.c`, `src/CreatureRecipeIO.c`, `tools/generate_recipe_schema.py` |
| Superficie y shader | `src/Surface.c`, `src/PigmentPattern.c`, `shaders/surface/pigment*.glsl`, `src/OpenGLRenderer.c` |
| Pruebas y galerías | `tests/test_creature_offspring.c`, `tools/export_offspring_batch.c`, `tools/capture_offspring_comparison.c`, `tools/offspring_v3_validation.py`, `tools/validate_recipe_gpu.c` |

## Reproducción

La referencia V2 es `c3ddb4ead55440e17b95ac983c6f184de7764066`, confirmada en el checkout y en el remoto antes del cambio. Para compilar el exportador en una copia de V2 se puede usar el mismo archivo `tools/export_offspring_batch.c` con `-DOFFSPRING_V2_BASELINE` y el objetivo de Makefile `export_offspring_batch` de V3.

```sh
make build/export_offspring_batch build/capture_offspring_comparison
python3 tools/offspring_v3_validation.py build/offspring-v3 --v2-export /ruta/export-v2
```

El índice está en `build/offspring-v3/index.html`. Cada par usa la misma cámara ortográfica, alineación anatómica y luz para ambas revisiones. Incluye lateral, frontal, tres cuartos, dos vistas centradas en la cabeza, normales y albedo. La comparación de híbridos entre revisiones usa los mismos dos padres V2 recargados. Una galería adicional cruza dos hijos V3 recargados. Las mediciones cuantitativas y los datos por par están en `build/offspring-v3/metrics.json` y `distances.tsv` bajo cada galería.

## Resultados medidos

Se usaron las mismas recetas, la semilla 48216 y lotes de cinco en ambas revisiones. Rata × rata usó diez. Los tiempos son medianas de 12 lotes por cruce en un Ryzen 5 5600H. Las capturas se hicieron en una RTX 3050 Laptop.

| Cruce | SDF mínimo V2 → V3 | SDF medio V2 → V3 | Torso medio V2 → V3 | Pigmento medio V2 → V3 | Celdas V2 → V3 | Elegibles V3 | Lote V2 → V3 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| Rata × elefante | 0.0061 → 0.0241 | 0.0200 → 0.0531 | 0.0435 → 0.0542 | 0.0270 → 0.0416 | 3 → 5 | 30/30 | 39 → 247 ms |
| Rinoceronte × rata | 0.0145 → 0.0286 | 0.0317 → 0.0872 | 0.0506 → 0.0413 | 0.0309 → 0.0604 | 1 → 5 | 30/30 | 28 → 148 ms |
| Caballo × asno | 0.0013 → 0.0124 | 0.0042 → 0.0236 | 0.0099 → 0.0099 | 0.0292 → 0.0561 | 3 → 5 | 30/32 | 17 → 112 ms |
| Cerdo × elefante | 0.0018 → 0.0133 | 0.0165 → 0.0359 | 0.0204 → 0.0260 | 0.0715 → 0.1521 | 3 → 5 | 30/30 | 27 → 147 ms |
| Perro × lagarto | 0.0023 → 0.0072 | 0.0107 → 0.0364 | 0.0332 → 0.0429 | 0.0183 → 0.0432 | 4 → 5 | 30/35 | 19 → 150 ms |
| Rata × rata | 0.0002 → 0.0033 | 0.0008 → 0.0078 | 0.0053 → 0.0050 | 0.0000 → 0.0271 | 1 → 7 | 60/60 | 45 → 284 ms |
| Híbridos V2 guardados | 0.0017 → 0.0091 | 0.0216 → 0.0185 | 0.0129 → 0.0153 | 0.0054 → 0.0337 | 5 → 5 | 30/30 | 34 → 202 ms |

La distancia mínima de cabeza aumentó en los siete cruces. En el par de híbridos V2 la media bajó aunque el hermano más parecido quedó más separado. La diversidad de torso varió por cruce. La búsqueda de V3 aumentó la latencia de generación en todos los casos medidos.

- En rata × elefante, los candidatos 8 y 10 del lote controlado comparten un aporte global Y del 50 % y el arquetipo roedor. Sus cocientes de longitud de hocico a profundidad de cráneo fueron 0.810 y 0.660. Los de altura de oreja a altura de cráneo fueron 2.121 y 0.844. La separación de ojos respecto a la anchura craneal varió de 0.578 a 0.814 y la distancia de campo cefálico fue 0.0728. El montaje está en `build/offspring-v3/controlled/fixed-global.png`.
- En el control de apariencia perro × lagarto se mantuvo la misma geometría y el mismo plan receptor. Aplicar la superficie de un hermano con el mismo plan dio distancia cefálica cero y distancia de pigmento 0.0939. El montaje está en `build/offspring-v3/controlled/same-geometry.png`.
- Los cinco descendientes de dos híbridos V3 guardados se generaron con 30 propuestas elegibles de 30. El mínimo cefálico fue 0.0113 y la media 0.0265.
- La paridad GPU se comprobó en cuatro etapas de rata × elefante, un adulto perro × lagarto y un híbrido de segunda generación. El máximo error SDF fue 0.000000716 unidades y el error máximo de pigmento lineal fue 0.000000239. Las regresiones de morph GPU también pasaron.

La inspección de las primeras capturas llevó a limitar el tamaño auricular en unidades de la bóveda, asentar la trufa en el maxilar y proyectar los ojos sobre el barrido craneal. Algunos cortes labiales de perro × lagarto todavía son angulares. Las capturas de marcha y apertura mandibular complementan las galerías de reposo.

## Alcance

- La reconstrucción cefálica proyecta objetivos de hitos y perfiles. El SDF aporta restricciones de soporte y una distancia de selección. Un ajuste inverso iterativo del campo completo y una evaluación de contacto a lo largo de cada zancada quedan pendientes.
- Cuernos, probóscides, tipos de cola y pies conservan clases discretas. La continuidad que se comprueba geométricamente se centra en la pose de reposo y las edades muestreadas. El volumen puntual, el rig y la articulación se evalúan por separado.
- El comparador usa el SDF OpenGL exacto y los ojos. Las hebras largas se validan en capturas del motor visual. Las capturas de albedo muestran pigmento y parte de la respuesta tisular, mientras que la distancia de pigmento CPU usa 66 sondas comunes sin relieve ni iluminación.
- El SDF de cabeza cuantitativo incluye pinnae y excluye apéndices con módulos propios. La distancia se mide en una cuadrícula 16³ a lo largo de la unión de las bandas cercanas a la superficie. La cobertura cuenta celdas en cinco cocientes cefálicos físicos con los mismos anchos de celda para V2 y V3.

## Referencias

- Correspondencia semántica y preservación del rig. [CharacterMixer](https://seanxzhan.github.io/projects/CharacterMixer.html) · [Topology-Varying 3D Shape Creation via Structural Blending](https://honghuali.github.io/projects/topo_sig14.pdf)
- Controles explícitos para reconstruir geometría implícita editable. [Direct Manipulation of Procedural Implicit Surfaces](https://eliemichel.github.io/SdfManipulation/)
- Selección de fenotipos de calidad en regiones distintas del espacio de descriptores. [Illuminating Search Spaces by Mapping Elites](https://arxiv.org/abs/1504.04909)
- Capas de apariencia, dominios de textura y representación del desarrollo. [A Three-Level Approach to Texture Mapping and Synthesis on 3D Surfaces](https://vci.rwth-aachen.de/publication/03310/) · [Virtual Creature Morphology](https://onlinelibrary.wiley.com/doi/full/10.1111/cgf.142661)
