# Integración anatómica en CreatureOffspring V3

La revisión de partida fue `6043b9499aedb3893dff62e79fd0b79767f04a8b`. El archivo experimental `build/offspring_report.zip` tiene SHA-256 `85683865438ab9f156c87bf62aaa0a3a2a55bf106d8756969431336ada459082`. Se conservó el ZIP y se extrajo una copia bajo `build/offspring-repair/source`. El análisis utiliza el informe, los CSV de combinaciones, individuos y distancias, los registros JSONL y las recetas y galerías originales. Se repitió el lote de 10 crías por pareja con la semilla `48216`.

## Diagnóstico del archivo

| Evidencia original | Causa en el motor |
| --- | --- |
| Lagarto × Rinoceronte #1 y #3 y Rinoceronte × Rata #4 y #9 | La rama de ornamentos de `Build` copiaba el cuerno y usaba el cambio de arquetipo cefálico como condición de ajuste. Los cuatro conservaban un cuerno anterior de longitud **1,15** y radio **0,245** en unidades axiales, pese a que la semianchura de la primera estación craneal abarcaba **0,094–0,488**. |
| Cerdo × Elefante #2, #4 y #9 y Elefante × Rata #1 | La trompa usaba proporciones globales del cráneo y una única ranura anterior. Los incisivos mantenían longitudes **2,03** y **1,93** y partían de centros maxilares heredados. La mitad anterior de la cara podía cambiar considerablemente entre crías. Elefante × Rata #5 carecía de trompa y colmillos en la receta original. |
| Lagarto × Asno, Lagarto × Perro, Lagarto × Rinoceronte y Caballo × Rata | Los pesos de mezcla de longitud, perfil axial y grosor podían producir cambios de altura o flexión incompatibles con la postura del plan elegido. En las vistas laterales sobresalían apoyos esbeltos, corvejones muy plegados y raíces delgadas bajo el torso. |
| Caballo × Cerdo #2, Cerdo × Rata #2 y Lagarto × Cerdo #8 | El maxilar y la mejilla heredaban posiciones relativas al cráneo de forma independiente de la pendiente del rostro. En Caballo × Cerdo #2 la prominencia maxilar quedó muy por encima de la trayectoria de las secciones rostrales. También había cambios abruptos entre secciones del cuerpo mandibular. |
| Orejas de Lagarto × Asno y colas de cruces con Rata | La pinna podía tener una inserción pequeña para su altura. El grosor caudal global seguía la anchura de pelvis aunque el inicio real dependía de la última sección axial y del tipo de cola. |
| Lagarto × Velkar y Velkar × Velkar | La posición del centro ocular se resolvía con la geometría cefálica completa mientras el globo se reducía durante la emergencia. La distancia centro–órbita ya podía superar el umbral en la semilla. |
| Caballo × Caballo | En la muestra 6, edad 5/6, la línea del labio evaluada en la bisagra combina pendiente y distancia. Interpolar ambos factores y la altura de la bisagra por separado podía dejar el cóndilo por encima de la línea y fallar `HEAD_INVALID_ORDER`. |

La calidad original concedía **0,9114** de media a las 630 crías. La validez del grafo y la distancia al objetivo de morph no bastaban para identificar una desproporción local.

## Implementación

- **Cuernos nasales.** `HeadModule` publica sitios nuevos sólo cuando el plan los solicita. Los sitios conservan su homología con las ranuras originales al recombinar descendientes guardados. Se toma la sección rostral correspondiente, se intersecta el barrido craneal con un rayo local y se estima la inclinación a partir de su gradiente. La inclinación está acotada para conservar la dirección heredada. La integración limita radios y palanca a la sección receptora y conserva el perfil de curvatura y punta. El SDF cefálico completo se consulta en la raíz durante la validación.
- **Trompa e incisivos.** La trompa usa una sección nasal anteromedial con penetración y radios limitados por la profundidad y anchura disponibles. Las primeras estaciones del barrido conservan más sección al comenzar el ahusamiento. Los dos incisivos usan las regiones laterales bajas del rostro anterior y conservan su módulo de dentina. Se limita su palanca y se abre la dirección lateral para liberar la línea media. Los 20 nodos de la trompa, el terminal y las poses conservan sus identidades.
- **Postura y pelvis.** La altura objetivo responde al cociente cruz–torso de ambos padres en el marco del plan elegido, acotada alrededor de la altura de partida. La extremidad conserva el barrido y flexión de su tipo locomotor. El grosor de la cadena informa la anchura del pie y se conserva masa proximal suficiente para la unión. El ajuste de longitud a suelo, el solapamiento de raíces y la construcción de rig usan el motor existente. `Tail_Resolve` ya admite acoplamiento caudal con la sección pélvica resuelta. Ahora se activa con más peso en colas delgadas y el efecto se atenúa con el perfil proximal.
- **Cara, mandíbula y pinna.** El maxilar sigue parcialmente el marco rostral y tira de la mejilla hacia el puente temporal. En etapas suficientemente desarrolladas se muestrean las secciones craneales y se asientan las masas superficiales que quedan demasiado expuestas. Sus radios heredados se conservan y la proyección orbital se ejecuta después. Se acotan las discontinuidades de anchura entre secciones mandibulares internas y se corrige la restricción del cóndilo al interpolar edades. La raíz de una pinna alta recibe un pequeño incremento de penetración y apertura basal según su discrepancia con el cráneo. El globo regional emergente limita su exposición respecto al centro orbital y su profundidad visible.
- **Validación y selección.** Los huecos graves en raíces faciales se filtran además de las comprobaciones existentes de grafo, apoyos, órbitas, pinnae y SDF. La coherencia anatómica es una puntuación independiente en `[0,1]` con penalizaciones por escaso apoyo, saltos de secciones mandibulares y desnivel de suelas. Influyen suavemente en la calidad. La diversidad morfológica y cromática mantienen sus descriptores y su selección por distancia. `CO.info` muestra la coherencia y el exportador la registra en las filas `anatomy`.

El formato JSON v1, la versión de generación 3, las identidades de módulos, la semilla y las ranuras antiguas se conservan. Las nuevas recetas guardan los sitios ajustados como identificadores de ranura del plan, sin campos nuevos en el JSON. El replayer usa la misma generación y validación de propuestas que el lote. La herramienta opera sobre copias y conserva las recetas archivadas.

## Código y herramientas

| Archivos | Cambio |
| --- | --- |
| `src/CreatureOffspring.c`, `src/OffspringRepair.{c,h}`, `include/CreatureOffspring.h` | Integración de fenotipos, medida de coherencia y selección. |
| `src/HeadModule.c`, `include/HeadModule.h`, `src/FlexibleAppendage.c`, `src/Creature.c` | Sitios geométricos, sección proximal y exposición ocular emergente. |
| `src/HeadBlend.c`, `src/HeadRegionalProfile.c`, `src/CreaturePhenotype.c`, `include/Head.h` | Dependencias faciales y continuidad durante el desarrollo. |
| `src/CreatureOffspringInternal.h`, `tools/export_offspring_batch.c` | Repetición de una propuesta exacta por lote, posición e intento y adaptación aislada de una receta guardada. |
| `src/interface/UiOffspringApp.c`, `tests/test_creature_offspring.c` | Coherencia en Nexus, regresiones del desarrollo y pruebas de soporte y articulación. |
| `tools/capture_offspring_comparison.c`, `tools/validate_offspring_anatomical_repair.py`, `shaders/sdf_surface_material.glsl` y sus salidas generadas | Galerías emparejadas, material neutro, normales, malla, grafo, hitos, cortes de mandíbula y capturas de poses. |

## Reproducción

```bash
unzip -n build/offspring_report.zip -d build/offspring-repair/source
make build/export_offspring_batch build/capture_offspring_comparison
python3 tools/validate_offspring_anatomical_repair.py \
  build/offspring-repair/source/offspring_report build/offspring-repair/comparison
```

Para comparar el coste de generación se puede pasar `--baseline-exporter` con un binario compilado en la revisión de partida. La repetición puntual usa `export_offspring_batch --replay padreX padreY 10 48216 plaza intento salida.creature.json` con plaza e intento a partir de uno. `capture_offspring_comparison --anatomy directorio original.creature.json repetida.creature.json` evalúa ambos sujetos en un encuadre compartido.

Los cortes sagitales muestran tejido superior en cian y la mandíbula en naranja. Las líneas cian y los puntos ámbar muestran el grafo y los hitos. Las cámaras, iluminación y escala de comparación son comunes a las dos columnas. Las fotos de malla muestran una extracción local independiente del pase de raymarch.

## Validación y rendimiento

La repetición en la revisión de partida reprodujo las tres incidencias y coincidió byte por byte con las **630 recetas válidas del ZIP**. El motor reparado produjo **660 recetas de 66 lotes**. La segunda ejecución con afinidad fija volvió a producir las mismas 660 recetas byte por byte.

| Cruce | Antes | Después |
| --- | --- | --- |
| Lagarto × Velkar | Candidato 2, máscara 4 en la semilla | 10/10 crías |
| Velkar × Velkar | Candidato 1, máscara 4 en la semilla | 10/10 crías |
| Caballo × Caballo | Candidato 1, resolución fallida en la muestra 6 | 10/10 crías |

Se midieron tres repeticiones por lote con ambos exportadores en el mismo Ryzen 5 5600H, fijados a la CPU lógica 9. La comparación temporal y de calidad usa las 63 parejas que completaban con ambos binarios. El CSV contiene los tres casos recuperados y sus tiempos por separado.

| Métrica comparable | V3 original | Reparación |
| --- | ---: | ---: |
| Mediana de lote de 10 | 297,69 ms | 309,44 ms |
| Percentil 95 de las medianas por pareja | 519,09 ms | 518,25 ms |
| Propuestas elegibles / evaluadas | 3772 / 4619 | 3774 / 4546 |
| Diversidad de forma media de selección | 0,10952 | 0,10968 |
| Diversidad de apariencia media de selección | 0,06328 | 0,06261 |
| Calidad media de selección | 0,91142 | 0,91327 |
| Coherencia anatómica media | — | 0,98127 |
| Pases de reparación acumulados | 685 | 634 |

Con las 66 parejas, la reparación obtuvo **3954 de 4833** propuestas elegibles y una coherencia media de **0,98208**. Entre hermanos de las 63 parejas comunes, la distancia SDF cefálica media medida en el mismo encuadre pasó de **0,032699 a 0,033021** y la distancia cromática de **0,068717 a 0,069510**. La mediana de tiempo aumentó **3,9 %** en el banco comparado. Estas mediciones incluyen generación, ajuste, validación y selección en CPU. Las capturas son un proceso aparte.

Pasaron `make test`, `make editor-test`, `make interface-engine-test` y `make visual-morph-gpu-regressions`. Las pruebas reproducen las tres parejas fallidas y recompilan sus crías en cuatro etapas y tres muestras intermedias tras guardar y recargar el JSON. En Cerdo × Elefante comprueban soporte SDF en raíces, ahusado y continuidad del barrido, apoyo sobre suelo, separación de los núcleos distales de incisivos y trompa y estabilidad de la raíz durante los cuatro gestos. El trazado comparativo se ejecutó con una RTX 3050 Laptop. El registro de paridad GPU incluye fragmento y compute.

[Datos por combinación](validation/offspring-anatomical-repair/matrix.csv) · [Identidad de los binarios de medición](validation/offspring-anatomical-repair/performance-revisions.json)

## Comparaciones visuales

Se capturaron **24 propuestas con la misma semilla individual** que sus respectivos archivos originales. Se usaron las recetas del ZIP para la columna izquierda. La derecha se reconstruyó con el mismo par, lote, plaza e intento. Las 66 galerías de regresión se capturaron por separado y reflejan la selección que recibe el usuario de V3. Las crías elegidas pueden cambiar de semilla dentro de una plaza cuando cambian su validez o su posición en la selección.

| Caso | Observación |
| --- | --- |
| [Lagarto × Rinoceronte #1](validation/offspring-anatomical-repair/lagarto-rinoceronte-01.png) | El cuerno anterior pasa de longitud 1,150 a 0,733 y de radio 0,245 a 0,157. El posterior mantiene su longitud 0,550. El apoyo deja de dominar el extremo del rostro. |
| [Rinoceronte × Rata #9](validation/offspring-anatomical-repair/rinoceronte-rata-09.png) | El cuerno anterior pasa de 1,150 a 0,216 y su radio a 0,062. Reaparecen el contorno del hocico y el perfil oral. |
| [Cerdo × Elefante #4](validation/offspring-anatomical-repair/cerdo-elefante-04.png) · [Poses](validation/offspring-anatomical-repair/cerdo-elefante-04-poses.png) | Los incisivos más cortos se separan de la línea media. La trompa pasa de longitud 2,788 a 2,322 en unidades axiales. El tramo proximal sigue la cara en reposo y al flexionar. |
| [Elefante × Rata #1](validation/offspring-anatomical-repair/elefante-rata-01-poses.png) | El encuadre lateral deja visible el giro del ápice durante alcance, rizo y alimentación. |
| [Lagarto × Asno #5](validation/offspring-anatomical-repair/lagarto-asno-05.png) · [Marcha](validation/offspring-anatomical-repair/lagarto-asno-05-marcha.png) | El mismo individuo conserva la línea de apoyo de su arquetipo. El cambio de proporción se aprecia mejor en la cadena superpuesta que en el contorno general. |
| [Caballo × Rata #2, raíz de cola](validation/offspring-anatomical-repair/caballo-rata-02-cola.png) · [Rinoceronte × Rata #9, raíz de cola](validation/offspring-anatomical-repair/rinoceronte-rata-09-cola.png) | Se ensancha el tramo de inserción en la grupa y se mantiene la cola distal estrecha. |
| [Caballo × Cerdo #2](validation/offspring-anatomical-repair/caballo-cerdo-02.png) · [Cerdo × Rata #2](validation/offspring-anatomical-repair/cerdo-rata-02.png) | La prominencia maxilar de Caballo × Cerdo baja hacia la trayectoria rostral y sigue visible como un relieve menor. En Cerdo × Rata el cambio alrededor del ojo es discreto. |
| [Lagarto × Cerdo #8](validation/offspring-anatomical-repair/lagarto-cerdo-08.png) | Los cortes muestran la misma bisagra en cierre y apertura, con cambios contenidos en anchura mandibular. |

[Manifiesto de capturas y semillas](validation/offspring-anatomical-repair/captures.json) · [24 comparaciones y diagnósticos completos](../build/offspring-repair/review-visual/targets/) · [66 galerías de regresión](../build/offspring-repair/review-visual/galleries/)

## Límites observados

- La raíz de algunos cuernos pequeños conserva un cuello redondeado visible en material neutro. En trompas muy compactas todavía se aprecia la transición de sección entre la cara y el tramo flexible. La separación probada entre trompa e incisivos usa núcleos distales y poses concretas. La holgura de todas las superficies durante movimientos arbitrarios queda abierta.
- Los ajustes de postura son deliberadamente acotados alrededor de la cadena existente. Caballo × Rata #2 conserva corvejones muy plegados y el cambio en algunos cruces reptil–mamífero es pequeño. Queda pendiente un ajuste de articulaciones con límites y objetivos por miembro y la evaluación de un polígono de apoyo con centro de masa.
- La proyección de la mejilla y del maxilar reduce el problema de asiento en perfiles muy inclinados, pero Caballo × Cerdo #2 aún presenta un relieve y algunas caras retienen frentes o trufas marcadas. Las láminas auriculares altas pueden parecer rígidas durante una pose estática. La validación de marcha usa las recetas con rig locomotor y una fase capturada.
- Las galerías cubren el adulto. Las cuatro etapas y las tres edades intermedias se comprobaron por código. Queda pendiente una revisión visual cuadro a cuadro de todas las edades y poses, y de la continuidad superficial con pelo largo en los nacimientos.

## Referencias anatómicas

- [Hieronymus, Witmer y Ridgely, estructura y soporte dérmico de los cuernos](https://rhinorescueproject.org/wp-content/uploads/2015/03/2006_Hieronymus-Witmer-Ridgely_rhino_horn.pdf)
- [Nabavizadeh, anatomía craneofacial de proboscídeos](https://anatomypubs.onlinelibrary.wiley.com/doi/10.1002/ar.25578)
- [Fahn-Lai y colaboradores, extremidades extendidas y parasagitales](https://pubmed.ncbi.nlm.nih.gov/37609446/)
- [Brocklehurst y colaboradores, transiciones de postura](https://pubmed.ncbi.nlm.nih.gov/40554496/)
- [Significado biológico de la cola del dragón de Komodo](https://pubmed.ncbi.nlm.nih.gov/39123668/)
