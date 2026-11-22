# Refinamiento anatómico de cinco mamíferos

Revisión sobre `368077497c9255ba2b091e34e9c41a73bc31f3bb`, que coincidía con `HEAD` de GitHub y la rama local. La fuente visual inicial fue `report_five_mammals.zip`. El ZIP pasó la comprobación CRC y se extrajo en un directorio de trabajo separado. Las imágenes del ZIP y las recetas iniciales se conservaron. El trabajo incluyó una reproducción de la revisión original, un ensayo limitado al error cefálico, una pasada de proporciones y dos pasadas de geometría y animación.

## Referencias anatómicas

| Especie | Rasgos utilizados en la comparación | Referencias |
| --- | --- | --- |
| Jirafa septentrional | Cuello ancho en la cintura escapular y más fino arriba, perfil lateral corto de cabeza, osiconos dorsales romos, marcas castañas irregulares y extremidades largas. Se tomó la jirafa septentrional como referencia de coloración. | [Giraffe Conservation Foundation](https://giraffeconservation.org/giraffe-species/northern-giraffe/), [fotografías de las tres subespecies septentrionales](https://www.flickr.com/photos/usfwshq/albums/72177720322036101/), [cráneo adulto](https://preview.wellcomecollection.org/works/sh473qj8) |
| Ornitorrinco | El pico es ancho en planta y carnoso de perfil. Los ojos se ven junto a su inserción. El cuerpo es bajo y ovalado, con patas cortas, manos más palmeadas y cola ancha con remate redondeado. | [Australian Museum, fotografía y micro-TC](https://australian.museum/learn/animals/mammals/platypus/), [Museums Victoria, perfil en natación](https://collections.museumsvictoria.com.au/species/8436), [Parks & Wildlife Tasmania, fotografía](https://parks.tas.gov.au/discovery-and-learning/wildlife/land-mammals/platypus-and-echidnas) |
| Pangolín chino | Perfil dorsal arqueado, rostro estrecho con punta de volumen visible, pabellón pequeño, escamas anchas imbricadas orientadas hacia atrás, vientre despejado y cola que se afina desde la pelvis. | [Taipei Zoo, fotografías](https://www.zoo.gov.taipei/News_Content.aspx?n=CC7DF0D6519201FD&s=442EFCA914E4F830&sms=D5A439C2736D52BF), [estudio morfológico y anatómico](https://www-ws.gov.taipei/Download.ashx?icon=.pdf&n=NTQyMjExNDQzNTcxLnBkZg%3D%3D&u=LzAwMS9VcGxvYWQvNDMyL3JlbGZpbGUvMjE0OTcvMzQyMTA2NS81NDIyMTE0NDM1NzEucGRm) |
| Canguro rojo | Cabeza pequeña, orejas alargadas, tórax estrecho, pelvis y muslos potentes, metatarso largo, dedo IV dominante y cola muscular afilada. | [Australian Museum, fotografía y cráneo](https://australian.museum/learn/animals/mammals/red-kangaroo/), [Mammal Diversity Database, apoyo lateral](https://www.mammaldiversity.org/taxon/1000292/), [revisión anatómica de la especie](https://academic.oup.com/mspecies/article/52/998/143/6046133) |
| Murciélago orejudo pardo | Alas anchas con puntas digitales situadas a distintas distancias, unión al flanco y a la pata posterior, cara corta, pabellones largos y casi ocultos al descansar. | [Bat Conservation Trust](https://www.bats.org.uk/about-bats/what-are-bats/uk-bats/brown-long-eared-bat), [vuelo filmado por Alison Barnes](https://www.youtube.com/watch?v=sAwU4Zt8EPA), [People’s Trust for Endangered Species](https://ptes.org/get-informed/facts-figures/brown-long-eared-bat/) |

Las fotos en perspectiva se usaron para relaciones de forma y orientación. Las vistas ortográficas del motor permitieron comparar cambios de silueta.

## Resolución cefálica compartida

`HeadAnatomy_Resolve` ejecutaba exactamente el mismo bloque del monotrema y del folidoto tres veces. El primer bloque establecía las órbitas, las comisuras, los anclajes y las radios iniciales. El segundo aparecía después de poblar gran parte de `HeadSurfaceRecipe`. El tercero dejaba los perfiles craneal y mandibular usando radios ya reducidos de nuevo. En el monotrema, los barridos tenían una altura de cráneo derivada de `0.78³` mientras otros datos procedían de `0.78`. El ancho del folidoto se multiplicaba por `0.88³` en el barrido final.

`src/Head.c` aplica cada transformación una sola vez. La comisura se lleva sobre la pendiente rostral y el perfil mandibular del monotrema sigue esa misma línea. Se amplió el espesor útil del pico y se ubicaron los ojos de ambos arquetipos más lateralmente. Se añadió un anclaje auricular discreto para el folidoto. La prueba de regresión recorre los once arquetipos y comprueba la escala aplicada y la posición relativa en ambos perfiles afectados.

## Auditoría de los defectos

### Jirafa

| ID y tipo | Defecto y causa | Reparación y evidencia | Pendiente |
| --- | --- | --- | --- |
| J01 · cara | El resolver equino imprimía un rostro largo y descendente. | El generador reduce longitud y pendiente, expone el ojo lateral y usa pupila redonda. El perfil de la cara es más corto en arcilla. | La ganacha y el cierre de los labios conservan parte del perfil equino. |
| J02 · silueta | Cuello de sección uniforme con abombamientos en las uniones cervicales del SDF. | El generador aumenta el número de estaciones y ajusta la curva, el ancho y la masa dorsal. `MonsterSDF.c` reduce la unión suave local entre segmentos cervicales. La prueba verifica el afinamiento de la cadena. | La transición hacia la cruz aún conserva un cambio de pendiente. |
| J03 · apéndice | Las ranuras occipitales inclinaban hacia atrás estructuras demasiado largas. | `Ornament.c` deriva el eje dorsal de la bóveda. El generador recorta el tallo y ensancha su raíz. El tejido ya se resolvía como piel. | El vello específico de la punta no se verificó en movimiento. |
| J04 · extremidades | Cañas finas y dos mitades de casco muy marcadas. | Aumento moderado de grosor en la receta y capturas de manos, pies y suelas. El perfil ungulado existente conserva la hendidura y el apoyo. | La banda coronaria y el contacto trasero del casco requieren más escultura. |
| J05 · superficie | Celdas de Voronoi de borde recto y tono repetido. | `PigmentPattern.c` y `pigment.glsl` comparten una deformación suave del dominio y una variación de tono por celda. Se redujo la densidad de parches del generador. | Falta una validación cuantitativa del patrón por región y durante la locomoción. |
| J06 · pose | La flexión fuerte estiraba el tejido sobre la cruz. | Se ensayaron pesos cervicales más amplios y se descartaron al aumentar la compresión. Se redistribuyeron pesos del tronco y la cola en `AnatomyDeformer.c` y se aumentaron las articulaciones cervicales. Se localizaron las zonas en mapas de deformación. | El alcance al suelo, la compresión de la garganta y la separación de las patas delanteras siguen abiertos. |
| J07 · silueta | El pecho tenía poca profundidad en relación con el cuello. | Aumento de profundidad torácica, ajuste de cruz y miembros, y perfil cervical más afinado. Se comparó en arcilla a la misma escala. | Las patas siguen estilizadas en la unión proximal. |

### Ornitorrinco

| ID y tipo | Defecto y causa | Reparación y evidencia | Pendiente |
| --- | --- | --- | --- |
| O01 · cara | El pico se estrechaba verticalmente por la tercera aplicación del ajuste craneal. | Resolver único y barrido con más espesor. El radio vertical distal en la captura de producción aumentó de 0,0137 a 0,0392 m. El perfil conserva una punta redondeada. | La línea de contacto visible entre las dos valvas necesita continuidad fina en los laterales. |
| O02 · unión | El pico plano salía de una cabeza elevada por la posición cervical de reposo. | Cuello más corto y ligeramente inclinado desde el pectoral. Órbita lateral pequeña, sin pabellón externo. | El nudo caudal del pico todavía se distingue en arcilla lateral. |
| O03 · silueta | Tronco largo con altura y anchura casi constantes. | Tronco adulto de 3,40 a 2,66 unidades de receta, tórax y abdomen más redondos. La comparación muestra una mayor relación altura/longitud. | La longitud del abdomen sigue siendo estilizada. |
| O04 · cola | Cola demasiado larga y estrecha al final. | Longitud adulta de 2,35 a 1,50, barrido con media elipse distal y menor peso pélvico. Se comprueban anchura y aplanamiento en planta y en C. | La curva en natación no se estudió. |
| O05 · manos | Palmas estrechas y borde palmeado corto. | Se aumentó la palma anterior, se abrió el apoyo y se redujo la retracción en manos respecto a pies posteriores. `Membrane.c` calcula las normales sobre una rejilla uniforme antes de alterar su comba. | La retracción al caminar conserva el modelo anterior basado en fenotipo. |
| O06 · pose | Los mapas iniciales situaron deformación en el área de cuello y extremidades. | Se trasladó parte de la flexión de reposo hacia el atlas y el cráneo en los cuellos cortos y se midió la pose de mirada baja sobre la malla primaria y el palmeado. | Hay compresión y estiramiento residuales. Véanse las cifras de la pose. |
| O07 · superficie | Perfil muy bajo con pelaje poco distinguible bajo luz oscura. | Se conserva el pelaje marrón corto, la máscara de dedos y el pico coriáceo. El contorno bajo se aprecia mejor en la versión compacta. | Se conservó el coste del pelaje y falta medir su cobertura perceptual en otras condiciones de luz. |

### Pangolín chino

| ID y tipo | Defecto y causa | Reparación y evidencia | Pendiente |
| --- | --- | --- | --- |
| P01 · silueta | Tronco largo y plano, con dorsales de altura parecida. | Generador con tronco más corto, vértice dorsal alto y cuello ajustado. El arco se ve de perfil. | Falta una comparación con la armadura desactivada en todas las edades. |
| P02 · cola | El perfil pesado se ensanchaba en el centro y conservaba mucho volumen. | Cola prensil con menor acoplamiento pélvico y afinamiento continuo. Se conserva el arco lateral y se aprecia una cintura pélvica más clara. | Falta certificar contacto durante la vuelta completa de la cola. |
| P03 · cara | La escala del ancho se aplicaba tres veces al perfil. | Corrección común y receta con hocico más corto y menos puntiagudo. El contorno estrecho tiene ahora una punta con volumen. | La transición nasal sigue estilizada. |
| P04 · oreja | `earSize=0` desactivaba el pabellón. | Pabellones pequeños y redondeados por fenotipo con anclaje lateral. La prueba exige su presencia. | En perfil parte de la oreja queda tapada por escamas de la nuca. |
| P05 · escama | Filas muy regulares y placas de punta acusada. Dos triángulos duplicaban una cara en la malla de la placa. | `ArmorScales.c` varía posición, ángulo y tamaño de forma determinista. Amplía raíz y extremo, baja la quilla y cierra el sólido de 14 vértices. La prueba verifica el sentido de ambas caras de cada arista. | La teselación de los bordes sigue visible cerca de la cámara. |
| P06 · cobertura | Transición dorsal y lateral en anillos muy perceptibles. | Se mantiene la proyección por SDF y la exclusión de la región ventral. La variación de paso rompe filas uniformes y las escamas de cola disminuyen con el radio. | El cambio entre la zona cefálica y dorsal requiere suavizarse, así como inspección de colisiones entre placas. |
| P07 · extremidades | Patas cortas con poca masa aparente debajo de la coraza. | Engrosamiento proximal y mayor exposición con el arco corporal. Se conserva el perfil de garra del módulo de miembros. | Excavación y agarre se conservan como comportamientos genéricos. Falta validar la curva de cada garra comparada con ejemplares. |
| P08 · pose | El tronco se enrosca pero aparecen tensiones en el cuello, el vientre y las patas recogidas. | Más secciones cervicales y mezcla axial distribuida. La coraza se mide como malla independiente. Se capturan estados parcial y completo. | La coraza conserva área por placa, con riesgo de intersección. El gesto completo todavía deja un hueco ventral amplio. |

### Canguro rojo

| ID y tipo | Defecto y causa | Reparación y evidencia | Pendiente |
| --- | --- | --- | --- |
| K01 · cara | Frente y morro equinos sobredimensionados. | Se redujo la longitud de la envolvente y se acortó el fenotipo dentro del rango que mantiene un grafo válido. El primer ensayo sobre longitud y pendiente se descartó tras fallar la carga. | Se conserva el arquetipo equino y parte de su perfil. |
| K02 · orejas | Pabellones largos y rígidos por el perfil equino. | Menor tamaño e inclinación, con punta más definida. | El resolver equino conserva espesor alto y una concavidad reducida. |
| K03 · extremidad posterior | Los volúmenes del muslo y pelvis parecían esferas unidas. | `LimbMammal.c` baja el abultamiento proximal macrópodo. El generador estrecha la raíz y aumenta el embebido del muslo en la pelvis. | Aún se distingue un volumen redondo en algunas vistas tres cuartos. |
| K04 · pie | Longitud digital derivada del grosor del pie, insuficiente en pies estrechos. | El tramo metatarsiano es más largo y el alcance digital depende de la longitud del miembro y `footScale`. El ancho se regula por separado. Se verifica el tramo tobillo–dedo IV. | Falta una verificación de presión sobre la suela durante el apoyo. |
| K05 · silueta | El tronco era demasiado largo y tendido. | Eje más corto, tórax anterior más alto y pelvis proporcionalmente ancha. | Falta curvatura lumbar específica en las fases de salto con la pelvis rotada. |
| K06 · cola | Punta demasiado gruesa para un contrapeso muscular y afilado. | Se conserva la base musculosa y se reduce el diámetro distal. | La marcha lenta de cinco apoyos no tiene controlador propio. |
| K07 · locomoción | El salto sincronizado requería inspección por fases. | Se mantiene el gait bípedo y se capturan impulso, vuelo y aterrizaje con fase registrada. El ensayo de locomoción verifica vuelo sincronizado. | Persiste la deformación cerca de rodillas y pies. El deslizamiento no se cuantificó con suelo renderizado. |
| K08 · apoyo | Postura de descanso demasiado inclinada con pies cortos. | Tórax elevado y más corto, metatarsos alargados y cola más delgada. Comparación lateral e inferior. | La descarga parcial de peso sobre la cola no se verifica estáticamente. |

### Murciélago orejudo pardo

| ID y tipo | Defecto y causa | Reparación y evidencia | Pendiente |
| --- | --- | --- | --- |
| B01 · silueta | Cuerpo y cuello demasiado largos. | Acortamiento del tronco y cuello y tórax más ancho. Vista cenital con el ala desplegada. | El perfil de reposo conserva una postura horizontal. |
| B02 · esqueleto | Dedos casi rectos con diferencias insuficientes de longitud. | `LimbBatWing.c` curva progresivamente cada radio digital. La receta diferencia II–V y la cadena del pulgar queda libre. | Falta una comparación métrica con un esqueleto de referencia. |
| B03 · membrana | Los paneles cortos dejaban juntas abiertas y la comba alteraba el cálculo de la normal de las celdas vecinas. | `Membrane.c` calcula primero la normal de la rejilla plana y luego aplica la comba. Los bordes completos alcanzan la última estación a todas las edades. | Continúan bordes angulosos y pueden aparecer pliegues cuando los dedos se acercan. |
| B04 · unión | Propatagio limitado al antebrazo distal y uropatagio sin alcanzar el pie posterior. | Se lleva la cadena anterior de hombro a carpo y la posterior al tarso. La planta muestra una superficie anterior más continua. | Falta una prueba de contacto de borde por cada pose y etapa. |
| B05 · plegado | Los dedos se rotaban sólo en la raíz y la selección alar se infería de los hijos del carpo. | El rig marca extremidades alares. `ProceduralAnimator.c` combina hombro, codo, muñeca y falanges, conservando el pulgar libre. Se mide la deformación del patagio por separado. | Persisten compresión local y deformación importante de malla corporal cerca de las alas. |
| B06 · oreja | Pabellones semejaban conos por su perfil delgado visto de lado. | Se ensancha la concha con la herramienta de pabellón curvo existente. | El pabellón sigue rígido en reposo y no se recoge bajo el ala. |
| B07 · cara | Ojos demasiado expuestos y morro largo para la especie. | Menor longitud, tamaño ocular y exposición. Se mantiene un hocico redondo. | El tragus no está representado como estructura propia. |
| B08 · cola y patas | Uropatagio reducido y cola larga para la superficie interfemoral. | Cola más corta y unión explícita a los tarsos. | No existe una pose de colgado validada para este rig. |
| B09 · render | El SDF primario omite las mallas explícitas. CreatureVisualizer dibujaba únicamente el campo, ojos y pelo largo. | CreatureVisualizer y demo universal dibujan membranas y escamas con el mismo búfer de profundidad que el cuerpo, y preparan las mallas al cambiar de edad. `MonsterVisual` ya dibuja y `AnimatedVisual` actualiza ambas mallas. | La paridad numérica del SDF cubre el campo primario. Las imágenes del compuesto y del SDF puro deben interpretarse por separado. |

## Galerías

Las primeras dos columnas de las hojas anatómicas reutilizan las mismas posiciones de cámara, escalas ortográficas, orientación y luces. Las cámaras se calcularon en la revisión original y las aceptó la herramienta nueva mediante `MONSTER_CAPTURE_CAMERA_REFERENCE`. La tercera columna reproduce el archivo original. Cuando el ZIP no incluía una vista, esa celda queda vacía. Las hojas de pose reutilizan las cámaras de la revisión original para el gesto correspondiente. El ciclo de salto conserva además una sola cámara por vista en toda la secuencia. La representación de deformación colorea estiramientos en rojo y compresiones en azul.

| Especie | Anatomía y acabado | Cuatro etapas y tres edades intermedias | Poses y deformación |
| --- | --- | --- | --- |
| Jirafa | [Antes y después](validation/five-mammal-refinement/jirafa-anatomia.jpg) | [Desarrollo](validation/five-mammal-refinement/jirafa-desarrollo.jpg) | [Locomoción, miradas y beber](validation/five-mammal-refinement/jirafa-poses.jpg) |
| Ornitorrinco | [Antes y después](validation/five-mammal-refinement/ornitorrinco-anatomia.jpg) | [Desarrollo](validation/five-mammal-refinement/ornitorrinco-desarrollo.jpg) | [Marcha y cabeza baja](validation/five-mammal-refinement/ornitorrinco-poses.jpg) |
| Pangolín | [Antes y después](validation/five-mammal-refinement/pangolin-anatomia.jpg) | [Desarrollo](validation/five-mammal-refinement/pangolin-desarrollo.jpg) | [Marcha y enrollamiento](validation/five-mammal-refinement/pangolin-poses.jpg) |
| Canguro | [Antes y después](validation/five-mammal-refinement/canguro-anatomia.jpg) | [Desarrollo](validation/five-mammal-refinement/canguro-desarrollo.jpg) | [Impulso, vuelo y aterrizaje](validation/five-mammal-refinement/canguro-poses.jpg), [doce muestras de un ciclo](validation/five-mammal-refinement/canguro-ciclo.jpg) |
| Murciélago | [Antes y después](validation/five-mammal-refinement/murcielago-anatomia.jpg) | [Desarrollo](validation/five-mammal-refinement/murcielago-desarrollo.jpg) | [Extensión, pliegue parcial, pliegue y batida](validation/five-mammal-refinement/murcielago-poses.jpg) |

En la [comparación de caminos de render](validation/five-mammal-refinement/composicion.jpg), la columna de campo de distancia y la de CreatureVisualizer comparten cámara. La extracción densa de `validate_recipe_gpu` depende también del paso de malla. A 0,045 m pierde parte de la anatomía primaria fina del murciélago. Para la comparación de composición se capturó el campo primario mediante raymarch. Las dos composiciones conservan los patagios o la coraza y se comprobó su presencia visual.

## Cambios por capa

- Las cinco recetas externas de `recipes/` se regeneran con sus scripts `tools/generate_*_recipe.py`. Los cinco generadores reproducen exactamente los JSON registrados. Se conservan los IDs y el formato de archivo.
- `Head.c`, `Tail.c`, `Ornament.c`, `LimbMammal.c` y `LimbBatWing.c` corrigen las formas de los arquetipos existentes. Los metatarsos macrópodos usan `footScale` para la longitud y `footWidthScale` para la anchura. Se usan los campos existentes.
- `AnatomyDeformer.c` distribuye la mezcla a lo largo de conexiones axiales de tronco y cola. `ProceduralAnimator.c` usa la longitud cervical relativa a la nuca para repartir la mirada y articula las falanges alares. Al soltar el plegado, el ala recupera la extensión. El ala conserva el pulgar libre.
- `Membrane.c` mantiene las cadenas de anclaje y separa el cálculo de la normal de la comba. Sólo asigna memoria cuando hay paneles candidatos. `ArmorScales.c` conserva una sola malla por criatura y la cantidad de caras de cada placa. Los vértices de las placas y membranas existentes se deforman con el esqueleto.
- `PigmentPattern.c` y `shaders/surface/pigment.glsl` mantienen los mismos parámetros públicos y la misma semilla. El dominio tiene una deformación suave y variación de color celda a celda. Los encabezados generados se reconstruyeron con Make.
- `UiCreatureScene`, `UiCreatureEngine` y `demo_morph_species.c` componen cuerpo y mallas explícitas. Conservan la geometría mientras la edad se mantenga. En una metamorfosis activa regeneran el recubrimiento al evaluar una edad nueva.
- `tools/capture_creature.c` añade la vista tres cuartos trasera, detalles anatómicos y cámaras reproducibles. `tools/capture_animations.c` añade mediciones por malla, una vista de deformación y el muestreo del ciclo de salto.

## Animación y deformación

Se midieron el gesto original y el revisado con la misma resolución nominal de malla diagnóstica. La columna de triángulos conserva el subconjunto cefálico/cervical de la herramienta. El subconjunto cambia de tamaño entre versiones porque cambia la geometría. La fracción de área extrema se calcula por separado sobre todo el cuerpo, contando los triángulos con área deformada inferior al 35 % o superior al 200 % de su área de reposo. Se descartan triángulos de reposo casi degenerados con un umbral proporcional a la escala. La compresión fuerte usa el 25 %. Los mapas de color indican la posición de las regiones, y el área normalizada evita confundir un cambio de densidad del mallador con una mejora.

| Gesto | Triángulos del subconjunto | Extremos | Compresión fuerte | Área extrema del cuerpo | Área extrema de mallas explícitas |
| --- | ---: | ---: | ---: | ---: | --- |
| Jirafa · `drink` | 39473 → 37268 | 2922 → 1534 | 159 → 226 | 2,65 % → 2,83 % | — |
| Ornitorrinco · `look_down` | 148816 → 63603 | 1715 → 1432 | 16 → 81 | 0,40 % → 0,67 % | 0,00 % → 0,00 % |
| Pangolín · `curl` | 32595 → 130907 | 2903 → 2802 | 193 → 137 | 2,47 % → 2,14 % | 0,00 % → 0,00 % |
| Canguro · `hop_flight` | 38081 → 38468 | 0 → 0 | 0 → 0 | 2,02 % → 2,80 % | — |
| Murciélago · `wing_folded` | 107258 → 110405 | 1781 → 1504 | 88 → 0 | 0,78 % → 0,55 % | 7,09 % → 3,83 % |

La jirafa bajó el error de alcance de 0,820 a 0,725 m en ese escenario. El área afectada subió ligeramente y la cruz todavía concentra estiramiento. El ornitorrinco mueve el cráneo sin deformación extrema en el palmeado, aunque la unión cuello-tórax conserva picos. El enrollamiento del pangolín mejora el área total y deja el área de las placas dentro de los umbrales medidos. Queda un hueco visible bajo el vientre. El canguro sincroniza la fase aérea y se verificó la suela en reposo. El área afectada cerca del corvejón aumenta en vuelo. El batiente mejora el área de deformación de cuerpo y patagio durante el plegado, con compresión localizada y un pliegue todavía abierto.

## Regresión e integración

- `make test` completó la suite general, incluidos once arquetipos cefálicos, genética, Ager, SDF, animación, morph y las pruebas nuevas. `make editor-test` completó 11 pruebas. `make interface-engine-test` verificó ambos visualizadores de escena, sus mallas explícitas y la reutilización de la escena a edad fija.
- ASan y UBSan ejecutaron la suite específica de cinco mamíferos, con detección de fugas y parada por comportamiento indefinido. Se verificaron los extremos exactos del grafo Ager y la validez de edades intermedias. Los adultos, las otras tres etapas y tres edades intermedias se capturaron para las cinco especies.
- `validate_recipe_gpu` pasó los 28 casos ejecutados, cuatro etapas de cada uno de los cinco mamíferos y cuatro de Brumacerno y Rata. El máximo error de compute y fragment fue 2,56 × 10⁻⁶ m, el de intervalos 2,94 × 10⁻⁵ m y el de pigmento lineal 6,56 × 10⁻⁷. `make visual-morph-gpu-regressions` pasó los casos existentes. La galería de composición tiene una prueba visual independiente de esta medición de campo.
- El demo universal dibujó la coraza y el patagio en raymarch y se capturaron las rutas `gpu-sdf-dense`, `gpu-sdf-sparse` y `legacy-handoff` en ambos casos. El emparejamiento directo Pangolín–Murciélago devuelve un error al preparar la metamorfosis en CreatureVisualizer. Las escenas de ambas especies por separado y sus extremos válidos sí funcionan.
- `export_offspring_batch` usó semilla 48216, tres crías por pareja y el flujo normal de reparación y validación. Se obtuvieron 45 crías de 15 parejas después del refinamiento. La revisión original produjo 42 y rechazó el primer candidato de Jirafa × Jirafa con la máscara geométrica 16. Se probaron Jirafa × Caballo y Canguro, Ornitorrinco × Rata y Pangolín, Pangolín × Rata y Rinoceronte, Canguro × Caballo y Rata, Murciélago × Rata y Ornitorrinco, y los cinco autocruces. Las recetas de Murciélago × Rata y de Jirafa × Canguro coincidieron byte a byte al repetir el lote. Esta matriz verifica crías generadas, con inspección visual de los cinco parentales.
- Se verificó que los cinco generadores reproducen sus recetas. El archivo original y el directorio de informe recibido quedan disponibles en el árbol de trabajo.

## Rendimiento

La siguiente tabla usa las medianas de tres ejecuciones alternadas en la misma máquina. El benchmark de animación usa la malla de referencia de 101.333 vértices y 180 fotogramas. Registra cero asignaciones y remallados por fotograma en ambas revisiones.

| Benchmark | Original | Revisión |
| --- | ---: | ---: |
| Anatomía, receta de referencia 1 | 48,290 µs | 48,981 µs |
| Anatomía, receta de referencia 2 | 33,209 µs | 33,246 µs |
| Anatomía, receta de referencia 3 | 33,429 µs | 33,416 µs |
| Anatomía, receta de referencia 4 | 28,630 µs | 28,319 µs |
| Pose por fotograma | 0,274 ms | 0,276 ms |
| Deformación por fotograma | 20,293 ms | 21,008 ms |

La densidad de extracción se midió también en las capturas anatómicas de adulto, con los mismos parámetros solicitados y los mismos ángulos. Los tiempos concurrentes de estas capturas sirven como diagnóstico local y se omiten de la comparación de latencia.

| Especie | Vértices de cuerpo original → revisión | Paso efectivo original → revisión, m |
| --- | ---: | ---: |
| Jirafa | 240559 → 680701 | 0.017015 → 0.0101954 |
| Ornitorrinco | 514863 → 410862 | 0.00631808 → 0.00631808 |
| Pangolín | 661871 → 557497 | 0.00689998 → 0.00689998 |
| Canguro | 101090 → 99943 | 0.0141974 → 0.0141974 |
| Murciélago | 370921 → 393073 | 0.00627963 → 0.00627963 |

La jirafa cruza un umbral de refinamiento del octree. El refinamiento de detalle condiciona la retícula de todo el cuerpo y el paso efectivo cae de 0,0170 a 0,0102 m, lo que eleva el coste aunque la configuración solicitada siga en 0,018 m. La regresión es material. El coste por fotograma y la memoria GPU de una metamorfosis con escamas proyectadas siguen sin cuantificarse. Las cifras de la malla estática no certifican el rendimiento de ese caso.

## Reproducción

Los PPM, pruebas por etapa, registros de cámara, datos de deformación, matrices de cría y capturas intermedias están bajo `build/five-refinement/`. Se puede repetir una comparación desde la raíz con:

```sh
make build/capture_creature build/capture_animations build/validate_recipe_gpu
mkdir -p build/refinement-review/{adult,ages,poses}
./build/capture_creature recipes/ornitorrinco.creature.json build/refinement-review/adult 1 surface
MONSTER_CAPTURE_STAGE_VIEWS=1 ./build/capture_creature recipes/murcielago.creature.json build/refinement-review/ages 0.5 neutral
./build/capture_animations recipes/pangolin.creature.json build/refinement-review/poses --anims=idle,curl_partial,curl --views=side,top --flat --strain
./build/validate_recipe_gpu recipes/murcielago.creature.json 3
```

Las cinco siluetas ganan rasgos distintivos. La postura de beber y el coste de malla de la jirafa, el cierre del enrollamiento del pangolín, la deformación durante el apoyo del canguro y el plegado en reposo del murciélago quedan como límites de esta revisión.
