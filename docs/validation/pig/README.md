# Cerdo doméstico — implementación y revisión visual

## Segunda revisión anatómica

La cola enrollada volvía sobre el eje longitudinal y hacía fallar el
barrido axial completo. El SDF acababa trazando el tronco con uniones
redondeadas de respaldo. La espiral ahora conserva sus propios tramos.
En un morph hacia una cola longitudinal, el tronco se interpola hasta la
grupa y la cola se deforma por el grafo. Así se evita extender el diámetro
de la pelvis hacia la punta de la cola del otro extremo.

Con el barrido restaurado, las secciones de la receta dibujan un pecho
profundo, un vientre ancho y bajo, y una grupa descendente que deja espacio
para el muslo. Las inserciones cambian de altura y se ajusta la longitud
de cada par de patas para conservar el plano de apoyo. El nuevo perfil
opcional de miembro ungulado compacto interpola un peso de mezcla en las
uniones del hombro, la babilla, el corvejón y la caña. Usa el suavizado
existente del SDF sin cambiar los pivotes ni el tipo de las conexiones.

El maxilar y el masetero encierran la parte posterior de la mandíbula. El
perfil inferior tiene ahora una sínfisis más estrecha y redondeada. Se
reposicionaron las órbitas, se ajustó su profundidad y se redujo la
prominencia del reborde. La demo dibuja los globos texturizados de la
anatomía interpolada con la profundidad del cuerpo. Las capturas de raymarch,
SDF exacto, GPU denso, GPU disperso y cesión asíncrona permiten comparar
la misma vista cefálica.

- [Antes y después en perfil y tres cuartos](revision-anatomica.png)
- [Silueta neutra, mandíbula abierta, marcha y cochinillo](revision-silueta.png)
- [Ojos en los cinco modos de la demo](revision-ojos-morph.png)
- [Ciclos de ida y vuelta revisados](revision-transiciones.png)

La malla adulta de captura pasó de 261.784 a 256.586 vértices con el mismo
vóxel solicitado de .018. Las hojas adaptativas aumentaron de 912.516 a
1.382.168. La cifra de vértices no mide el coste de reconstrucción ni el
rendimiento interactivo. La cara todavía muestra una línea clara bajo el
disco nasal y los modos de extracción GPU conservan bordes más facetados
cerca de la órbita y de las orejas. En el perfil lateral se distingue
una transición de sombreado entre la nuca y el cuello.

Pasaron `make test`, `make editor-test` y la comparación del SDF CPU,
compute y fragmento en las cuatro etapas. El error máximo muestreado fue
`7.16e-7` en 5.200 puntos por etapa. Se inspeccionaron 11 capturas por
ciclo en Caballo ↔ Cerdo y Cerdo ↔ Lagarto, además de los perfiles de
este último a 0,49, 0,50, 0,51 y 0,99.

## Primera revisión anatómica y de superficie

La revisión parte de una receta funcional y compara capturas del mismo motor
con las fotografías lateral y oblicua de Large White. Se amplió el cuello,
se dio más volumen al pecho, al bajo vientre y a los muslos, y se compensó
la altura de las secciones para conservar el dorso casi horizontal. Las orejas
son más estrechas en proporción a su altura y se inclinan algo más hacia
delante. Se acortó la comisura y se adelgazó y retrasó el perfil rostral de la
mandíbula. Desde el frente se distinguen las dos narinas geométricas y el
disco rosado sobre el labio inferior.

La receta usa textura procedimental en coordenadas anatómicas de reposo.
Dos capas de ruido dan variación de color en el flanco y grano fino en el
primer plano. Los dominios de piel aportan rugosidad mate y dejan visible el
color propio del disco nasal. Las cerdas cortas y escasas, y la queratina de
las pezuñas, usan los materiales existentes.

- [Adulto de pie y en marcha](adulto.png)
- [Cabeza antes y después](cabeza.png)
- [Cuatro etapas, edad intermedia, adulto neutro y mandíbula abierta](crecimiento.png)
- [Avance y retorno de dos ciclos de metamorfosis](metamorfosis.png)
- [SDF exacto, GPU denso y cesión con y sin retraso](renderizado.png)

Las capturas definitivas usan `MonsterVisual_Render`, incluida la ruta normal
de ojos. Las imágenes de este informe agrupan fotogramas del motor sin retocar
al animal. Los fotogramas completos, los registros y la receta inicial quedan
en `build/pig-review/`.

En el adulto, la comparación de recetas con el mismo ejecutable de captura
(`voxelSize=.018`) dio 281.299 vértices y 1.001.437 hojas adaptativas en la
receta anterior, frente a 261.784 vértices y 912.516 hojas en la revisada.
La prueba con espolones algo mayores cruzó un umbral de subdivisión y llegó a
727.266 vértices. Se conservó el tamaño original del espolón. Estas cifras
miden una reconstrucción de alta resolución y no establecen el tiempo por
fotograma ni el coste del worker en uso interactivo.

## Alcance y estado

Receta externa `recipes/cerdo.creature.json`, ID **1007**, nombre **Cerdo**.
Siete módulos: tronco, cabeza, cuatro extremidades unguladas de dos dedos
principales y cola. Las cuatro etapas representan cerdos; `cephalic=1` evita
suprimir ojos y nariz en el recién nacido. El crecimiento cambia proporciones,
longitud del hocico, cuerpo y cola, además de la escala.

**Estado: integrado y validado funcionalmente; realismo anatómico todavía
parcial.** La silueta, el disco nasal perforado, las orejas erectas y la cola enrollada
identifican un cerdo tipo Large White. La comisura y el labio inferior todavía
se marcan en el primer plano, y se distinguen transiciones entre algunas
secciones del tronco y facetas en los bordes pequeños.

## Referencias e iteración

- [Oklahoma State University: Large White](https://breeds.okstate.edu/swine/large-white-swine):
  costados largos y profundos, piel rosada, orejas erectas y perfil facial
  ligeramente cóncavo.
- [British Pig Association](https://www.britishpigs.org.uk/breed-information/large-white):
  fotografías lateral y tres cuartos, inspeccionadas durante el ajuste.
  Fotografías originales:
  [lateral](https://www.britishpigs.org.uk/images/2020/02/25/large-white-main.jpg),
  [tres cuartos](https://www.britishpigs.org.uk/images/2020/04/30/large-white-breed-buyers.jpg).
  No se redistribuyen aquí. Las descargas fotográficas de OSU quedaron
  bloqueadas por Cloudflare; no se presentan como evidencia inspeccionada.

Se ajustaron primero tronco, cuello y patas, después soporte maxilar, perfil
craneal, órbitas, mandíbula y disco nasal. Se corrigieron ojos inicialmente
sobredimensionados por la composición `eyes.size/head.eyeSize`, y una respuesta
de superficie demasiado lanosa mediante dominios materiales de piel. La última
pasada estrecha la mandíbula y la comisura para reducir el labio lateral.

## Capacidades compartidas

- `HEAD_ARCHETYPE_SUID=5`, añadido sin renumerar arquetipos existentes:
  perfil craneal/mandibular, mejillas, órbitas pequeñas y disco nasal integrado.
  Las fosas son cavidades SDF, no manchas.
- `TAIL_ARCHETYPE_COILED=6`: espiral paramétrica cónica de aproximadamente una
  vuelta, con avance lateral para separar segmentos. Se calcula después de la
  integración que anulaba los desplazamientos de otros tipos de cola.
  Los arquetipos previos conservan su rama original.
- `head.nasalColor` opcional; alfa cero conserva el color oscuro anterior.
  IO, interpolación, huella geométrica y color CPU/GLSL usan la misma semántica.
  No se añadieron primitivas ni se editaron generados manualmente.
- El demo universal sólo añade los alias `pig` y `swine`. Catálogo, selector
  numérico, objetivos aleatorios y `--from/--to` siguen el registro compartido.
- La prueba con worker retrasado descubrió una cabeza equina residual:
  los cambios de composición cefálica no emitían eventos topológicos, y el
  dominio de cesión no cubría ambas cabezas. Ahora se detecta el cambio y se
  conservan las envolventes de ambos extremos una vez por pareja. No se
  remalla por frame ni se desactiva ninguna parte para esconder el defecto.

## Reproducción

Desde la raíz, con SDL2/OpenGL y una pantalla funcional:

```sh
make test
make editor-test
make demos/demo_morph_species build/capture_creature build/validate_recipe_gpu
./demos/demo_morph_species --list

mkdir -p build/pig/final/adult build/pig/final/seed build/pig-morph
./build/capture_creature recipes/cerdo.creature.json build/pig/final/adult 1 surface
./build/capture_creature recipes/cerdo.creature.json build/pig/final/seed 0 surface
# larva: 0.333333333; juvenile: 0.666666667; intermedio: 0.5
# neutral en lugar de surface: sin pigmentación ni pelo
# argumento final opcional: apertura mandibular 0..1
./build/validate_recipe_gpu recipes/cerdo.creature.json 3

make run-morph ARGS='--from=Caballo --to=Cerdo --validate-cycle=build/pig-morph/horse-to-pig'
make run-morph ARGS='--from=Cerdo --to=Lagarto --validate-cycle=build/pig-morph/pig-to-lizard'
```

`capture_creature` genera vistas lateral, frontal, posterior, superior, tres
cuartos, detalles cefálicos y dos vistas de una pose de marcha del animador
compartido. Usa voxel `0.018*escala`, sin malla importada. Los PPM originales se pueden convertir a PNG para inspección a resolución completa.

## Verificación y límites

Los registros y capturas masivas quedan en `build/pig/` y `build/pig-morph/`.
Las pruebas nuevas cubren enums, preset suino, disco/fosas, cola sin cruces,
JSON ida/vuelta, trece edades, materiales, miembros y 66 muestras de morph
bidireccional con caballo, Brumacerno y lagarto; también la cesión cefálica.
La prueba existente de módulos retrasados distingue ahora el campo dedicado
de cabeza de los conectores de patas/cola.

Los ciclos de captura visitan `0,.15,.35,.5,.7,.85,1,.8,.5,.25,0`.
Se probaron caballo→cerdo, Brumacerno→cerdo, perro→cerdo y cerdo→lagarto.
Un código de salida correcto por sí solo no certifica ausencia de todos los
defectos visuales; las imágenes intermedias deben acompañar la revisión.

El error máximo entre CPU, compute y fragmento fue de `4.77e-7` en
5.200 muestras por etapa. Los ciclos de 11 capturas también se repitieron
con la receta revisada. Para caballo → cerdo, el solapamiento de silueta
respecto al SDF exacto fue 98,0 % a mitad de la transición y 96,0 % en el
adulto con GPU denso. En el adulto, `--gpu-voxel=.04 --gpu-refinement=2`
recuperó el contorno de las orejas y subió el solapamiento a 98,4 %.
Con cesión y un retraso de worker de 3 s fue 99,6 % y
99,4 %, respectivamente. Se segmentó por diferencia máxima RGB mayor que
12 respecto al fondo. Esta métrica incluye bordes, color y antialiasing,
y no compara los materiales ni la geometría interna.

Los fotogramas de marcha comprueban la articulación en una pose. El contacto
de las cuatro patas durante un ciclo completo de marcha sigue sin medirse.
La validación de metamorfosis comprueba las edades muestreadas en cada
dirección. La transición continua entre ellas requiere observación
interactiva para descartar defectos de un solo fotograma.
