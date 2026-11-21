# Continuidad nasal y cierre oral en Monster Engine

## Punto de partida

La revisión pública era `6043b9499aedb3893dff62e79fd0b79767f04a8b`. El árbol local contenía la reparación anatómica V3 sin confirmar, incluidos los sitios de inserción ajustados, la validación de raíces y las herramientas de diagnóstico. La comparación usa un binario de ese árbol, guardado antes de editar. El archivo estudiado fue `build/offspring-repair.zip` con SHA-256 `5f22c50aa8fc75090d0aab4529441a500304ae497e1bf93bae1b20ae5aae361b`. Identifica sus capturas como `6043b94-dirty` y usa la semilla de lote `48216`.

Se revisaron las 66 parejas del archivo mediante hojas de perfiles laterales, con tres muestras originales y tres reparadas por pareja cuando estaban disponibles. Las combinaciones que fallaron en el experimento original contienen sólo la tanda reparada. Se examinaron también las ampliaciones de cara, inserción y mandíbula para los cruces problemáticos. La generación, los hitos y la puntuación anatómica se interpretaron por separado de la inspección visual.

| Hallazgo visual | Propietario y causa |
| --- | --- |
| Cerdo puro, Cerdo × Cerdo y varios cruces con perro, caballo, asno y rata tienen un reborde circular bajo la luz neutra. | `HeadAnatomy_Resolve` define las secciones cefálicas. `MonsterSDF_EvalUpperHeadDistance` trunca el barrido en el extremo y une después otra elipsoide. La unión queda sobre una sección cuya anchura y derivada cambian al pasar por el casquete. |
| Perro × Cerdo, Lagarto × Cerdo y Explorador × Cerdo presentan líneas orales fuertes y sínfisis que parecen barras. Algunos cruces sin ascendencia suida también muestran el defecto. | `Morph_HeadJawStations` y `HeadAnatomy_BlendRegional` mezclan la mandíbula por separado del rostro y del maxilar. Los planos superior e inferior y el cortador dependen de una pendiente media. El techo mandibular puede quedar bajo el labio en la zona incisiva. |
| La trompa de los cruces repite la forma de un tubo y aparece o desaparece por familia de cráneo. Algunas combinaciones tienen una nariz visible sobre una trompa corta. | `HeadModule_ResolveProfileWithSites` publica la inserción y `FlexibleAppendage_Resolve` genera la cadena. Las narinas faciales y el conector terminal tallan cavidades por rutas independientes. `CreatureMorph_Interpolate` colapsaba las estaciones contra la ranura de un progenitor. `SupportedGraft` exigía el mismo arquetipo. |

El barrido craneal también aporta la bóveda, el puente y el rostro. Las elipsoides temporal, cigomática y maxilar conservan sus contribuciones locales. El labio superior proviene del límite oral del barrido; la mandíbula inferior conserva su barrido, cóndilo y transformación de apertura. Los sitios nasales de cuernos y los sitios maxilares de incisivos siguen publicándose dentro del módulo cefálico. La resolución de nariz y labio se inspiró en la musculatura del disco porcino, el crecimiento del septo y la fusión nasolabial de la trompa. [Musculatura del disco](https://pubmed.ncbi.nlm.nih.gov/10652825/) · [Septo porcino y crecimiento facial](https://pubmed.ncbi.nlm.nih.gov/36965023/) · [Anatomía craneofacial porcina](https://journals.sagepub.com/doi/10.1258/la.2012.012062) · [Anatomía de proboscídeos](https://doi.org/10.1002/ar.25578) · [Anatomía terminal](https://pmc.ncbi.nlm.nih.gov/articles/PMC10250425/) · [Desarrollo](https://pmc.ncbi.nlm.nih.gov/articles/PMC11461087/) · [Mecánica](https://pubmed.ncbi.nlm.nih.gov/34428468/)

## Geometría

- **Disco y puente.** `HeadSurfaceRecipe_NasalTerminal` reconstruye las narinas a partir de la sección terminal. El perfil suido aprovecha el barrido elíptico de Hermite y anula sus tangentes al conectar con la tapa elíptica. Una intersección redondeada aplana la superficie rostral. La elipsoide nasal conserva la región de pigmentación y pierde gradualmente su contribución sólida según `nasalDiscWeight`. La nariz del cerdo adulto usa toda la sección del barrido. Se conserva la receta de especie y se ajusta el techo anterior del barrido mandibular.
- **Prolongación y rig.** `HeadModule_NasalRoot` coloca el pedículo dentro de la premaxila, donde la sección craneal y el tejido del labio sostienen el nacimiento del órgano. Un mismo sitio alimenta las cadenas antiguas y ajustadas. La primera sección toma anchura y altura de ese apoyo. Una prolongación corta retiene un ápice ancho y una dirección más rostral. Al aumentar la longitud aparece la curvatura de reposo heredada. La cadena conserva sus 20 estaciones, los dedos terminales y los gestos de flexión. Una unión menor entre tramos limita la acumulación de volumen en cada eslabón.
- **Narinas.** `MonsterSDF_ResolveNasalTerminal` expresa un único par de cortadores en el marco cefálico, interpolado hacia el plano de la última sección. Al moverse la sección, cambian el centro, los ejes y los semirradios. El conector terminal aporta la superficie del ápice y delega las aberturas del módulo nasal al par compartido. La resta se aplica tras unir cara y prolongación. Los terminales dobles de otros apéndices conservan su evaluación local.
- **Cierre oral.** `Head_OralBend` define una elevación continua entre la comisura y el rostro distal. El ajuste regional coloca las secciones maxilar y mandibular alrededor de la misma curva. El SDF evalúa con ella el labio superior, el inferior y la hendidura. La mandíbula rota sobre el cóndilo existente. Cuando un progenitor aporta una mandíbula implícita, el perfil regional construye seis estaciones entre cóndilo y sínfisis, con una punta redondeada y un ángulo posterior ahusado. Las cotas incluyen tanto la amplitud de la curva como su derivada máxima. La cavidad interna sigue limitada por el apoyo de la cabeza.
- **Render.** El generador traduce los mismos evaluadores para GLSL ordinario, el caché cefálico y compute. El muestreo adaptativo refina los extremos y conserva cotas para los cortadores orientados. La huella de la malla asíncrona incluye la tapa nasal y el cierre oral. `HeadMorph` usa los hitos derivados de la misma sección en su jaula.

## Herencia y desarrollo

En Ager, los perfiles compatibles siguen el desarrollo de `CreaturePhenotype_Interpolate` y la resolución normal de la criatura. En morphs cruzados, `CreatureMorph_Interpolate` conserva la unión del grafo y reexpresa sus estaciones nasales en el marco de la cabeza combinada. La longitud proviene de las estaciones mezcladas. El perfil del ápice conserva las proporciones de la receta y se adapta al tramo corto. En los extremos se conservan las estaciones exactas y el orden de unión del progenitor activo, junto con los identificadores dormidos del grafo de morph. Un destino de Ager con rig activo actualiza su esqueleto a partir del nuevo grafo y conserva el estado del controlador. Las capturas en reposo y los procesos que sólo construyen campos usan los resultados ligeros habituales.

CreatureOffspring V3 muestrea `headGenes.nasalExtension` para longitud, taper y curvatura y utiliza el peso nasal para el ápice. La topología de la cara admite la prolongación cuando la herencia la aporta, con independencia de la familia del cráneo. Los controles de la prolongación ya están en `flexibleAppendages`. El perfil regional guarda dos campos opcionales v1, `nasalDiscWeight` y `oralArch`. La lectura de perfiles anteriores reconstruye el peso del disco cuando hay ascendencia suida reconocible y los controles históricos de planum y casquete. La reproducción de segunda generación usa la anatomía guardada. Ningún paso necesita cargar los progenitores originales de una cría.

## Evidencia

El [seguimiento facial y caudal](validation/offspring-facial-followup/README.md) amplía la verificación de Cerdo × Elefante y Caballo × Cerdo con dos semillas, piel de la trompa, cola en marcha y primeros planos del morro.

- [Cerdo puro con la mandíbula cerrada y abierta](validation/unified-facial-nasal/pig-parent.jpg)
- [Elefante en reposo y tres gestos, con ampliaciones del ápice](validation/unified-facial-nasal/elephant-parent.jpg)
- [Repetición exacta de cuatro recetas archivadas](validation/unified-facial-nasal/exact-replay.jpg)
- [Cerdo → Elefante](validation/unified-facial-nasal/pig-to-elephant.jpg) · [Elefante → Cerdo](validation/unified-facial-nasal/elephant-to-pig.jpg) · [Cortes y estaciones](validation/unified-facial-nasal/nasal-diagnostics.jpg)
- [Diez crías Cerdo × Cerdo](validation/unified-facial-nasal/pig-pig.jpg) · [Diez crías Cerdo × Elefante](validation/unified-facial-nasal/pig-elephant.jpg)
- [Matriz de 66 lotes](validation/unified-facial-nasal/regression.csv) · [Medidas bidireccionales de Ager](validation/unified-facial-nasal/morph.tsv)

La repetición exacta pasa los mismos archivos de `offspring-repair/review` a los dos binarios. Las dos galerías de diez crías y la matriz corresponden a la regeneración completa, con selección nueva. Cada hoja indica la vista, el material y el origen de los candidatos.

## Resultados medidos

| Comprobación | Resultado |
| --- | --- |
| Matriz V3 | Las 66 parejas produjeron diez recetas cada una. Otra ejecución reprodujo las 660 recetas byte por byte. Las tres hojas laterales muestran dos crías por pareja y las dos galerías principales muestran sus diez crías. Se inspeccionaron 148 crías en las hojas de la tanda nueva. |
| Cerdo × Elefante | Extensiones resueltas de 0; 0,040; 0,491; 0,761; 0,982; 2,060; 2,098; 2,217; 2,517 y 2,616 unidades de escena. Los ápices cortos conservan mayor sección relativa. |
| Segunda generación | Se recargó la cría #6, con longitud de receta 0,692, y se cruzó con Cerdo. Ocho de diez crías heredaron longitudes entre 0,008 y 0,692 en unidades de receta. [Datos guardados](validation/unified-facial-nasal/generation-two.tsv) |
| Ager bidireccional | Nueve edades en cada sentido. La longitud aumentó de 0 a 3,63 unidades. La mayor diferencia entre direcciones, tanto de longitud como de posición de la narina izquierda, fue de 0,000005 unidades. La comparación de 1728 puntos del campo visual en cada extremo dio error cero en las cuatro comparaciones. En las tres vistas neutras, como máximo un 0,063 % de los canales RGB difirió en más de un nivel al invertir el recorrido. |
| Narinas y apoyo | En las muestras de Ager los dos centros dieron distancia positiva y el tabique dio distancia negativa. Después de aparecer la prolongación, su raíz dio un SDF cefálico entre −0,267 y −0,199. Los casos con prolongación muestreados en ambas galerías conservaron dos aberturas y tabique. |
| Cierre, rig y cobertura | Las pruebas comprueban labios suinos en tres secciones a lo largo de la línea oral, tabique, tangencia del casquete y narinas distales del elefante en desarrollo. El rig conserva el anclaje, la longitud segmentaria y los gestos de flexión. Se probó también la actualización de un rig ya activo durante Ager. |
| GPU | La prueba muestrea 2660 puntos por edad de Cerdo → Elefante, incluidos 250 alrededor de las narinas. Comparó distancia CPU, fragmento y compute. El mayor error del recorrido fue menor de 0,000002 unidades. |

[Galerías de las 66 parejas, 1/3](validation/unified-facial-nasal/matrix-1.jpg) · [2/3](validation/unified-facial-nasal/matrix-2.jpg) · [3/3](validation/unified-facial-nasal/matrix-3.jpg)

### Rendimiento

Se realizaron tres repeticiones por pareja con ambos exportadores, fijados a la CPU lógica 9 del Ryzen 5 5600H. La captura y la latencia del motor de render quedan fuera de los tiempos de generación. La mediana se calcula sobre las medianas de cada pareja.

| Métrica | V3 con reparación previa | Revisión facial |
| --- | ---: | ---: |
| Mediana por lote de 10 | 313,08 ms | 283,79 ms |
| Percentil 95 de las medianas por pareja | 661,66 ms | 621,44 ms |
| Candidatos elegibles / evaluados | 3954 / 4833 | 3954 / 4853 |
| Diversidad de forma media | 0,10793 | 0,10800 |
| Diversidad de apariencia media | 0,06226 | 0,06241 |
| Pico RSS del lote Cerdo × Elefante, ejecución adicional | 9684 KiB | 9960 KiB |

La comparación de extracción CPU produjo tiempos más altos en alguna captura. Los tiempos de clasificación y contorno en el elefante adulto se tomaron durante la ejecución de otras capturas y sirven como observaciones, con un coste combinado de 52,7 s y 60,4 s respectivamente. La paridad en RTX 3050 Laptop verifica campo y extracción, y queda pendiente un perfil de FPS durante navegación y arrastre del control de edad.

### Límites de la revisión

Los perfiles alargados de algunos cruces conservan comisuras marcadas y labios incisivos planos. También se aprecian artefactos de pabellón e inserción alrededor de algunas órbitas, que quedan fuera del cambio nasal. La trompa sigue discretizada en eslabones animables y se ven pequeños cambios de normal en ciertos ángulos y flexiones. Las pruebas de colisión verifican núcleos y poses concretas. La holgura de toda la superficie en cualquier pose, las vías respiratorias internas y los morphs con picos quedan sin validación específica.

## Reproducción

```bash
unzip -n build/offspring-repair.zip -d build/facial-nasal/archive
make build/capture_creature build/capture_offspring_comparison build/export_offspring_batch
mkdir -p build/facial-nasal/forward
build/capture_offspring_comparison --nasal-morph build/facial-nasal/forward \
  recipes/cerdo.creature.json recipes/elefante.creature.json
python3 tools/validate_offspring_anatomical_repair.py \
  build/facial-nasal/archive/offspring-repair/source/offspring_report \
  build/facial-nasal/regeneration --views none --repeats 3
make test
make editor-test interface-engine-test visual-morph-gpu-regressions
```

`--faces <directorio> <recetas…>` saca vistas laterales, frontales, superiores y tres cuartos en un encuadre común. La variante `--anatomy` añade malla, raíces, hitos y corte sagital con articulación oral. `--nasal-morph` captura las nueve edades solicitadas y registra longitud, radios y SDF de inserción, dos aberturas, tabique y errores de extremo. El plano de color rosa representa el interior geométrico de los cortadores en los cortes transversales.
