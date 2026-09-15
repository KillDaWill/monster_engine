# Continuidad del desarrollo del Ager

Validación del 14-09-2026 en Ryzen 5 5600H, Radeon integrada, compilación `-O2`.
Este informe distingue la continuidad de la deformación de la frescura de la
geometría. La segunda sigue limitada por el coste del mallado del worker.

## Causas y cambios

- La larva conservaba conexiones adultas colapsadas. Ahora el grafo mantiene sus
  identidades, pero marca conexiones dormidas; SDF, bindings y superficie omiten
  esas conexiones. Los miembros se despliegan por etapas continuas: segmento
  proximal, distal, palma, falanges y finalmente unguales.
- Los dedos comienzan como brotes redondeados antes de la mitad del desarrollo
  visible. Sus radios acompañan el crecimiento del dedo completo; las falanges
  alargan progresivamente el brote. Las uñas tienen una etapa posterior separada.
- La cabeza larvaria tiene proporciones propias de una cabeza pequeña, con
  cráneo más redondeado y hocico corto. Se elimina la segunda mezcla SDF por edad
  que ocultaba la receta cefálica ya resuelta. `HeadMorph` vincula cada vértice
  cefálico a cuatro de 18 volúmenes semánticos y deforma desde una base inmutable.
  Las manos quedan fuera del dominio cefálico. La mandíbula conserva su ruta
  articulada; cuerpo, cabeza, ojos y mandíbula comparten la publicación.
- El paso al mallador adaptativo dependía de que aparecieran regiones de detalle.
  Ahora acepta cero regiones y conserva la misma ruta durante toda la vida.
  Los brotes solicitan detalle por su sección real; las uniones axiales no
  refinan el torso. El requisito extra de muestras para miembros cortos converge
  suavemente al del adulto. No se filtran componentes para ocultar defectos.
- El evaluador escalar SDF omitía la poda conservadora usada por la ruta con
  atributos. Ahora ambos podan grupos y conectores. La unión suave devuelve
  exactamente el operando elegido fuera de la mezcla, evitando cancelación
  numérica repetida. No se introducen cortes puntuales de AABB.
- El mapeo del cuerpo reutiliza las mismas influencias, coordenadas longitudinales,
  desplazamientos y pesos del deformador. Los expresa en una anatomía adulta
  canónica persistente. La cabeza utiliza su jaula canónica. La superficie no
  vuelve a clasificarse en cada frame ni cambia sus coordenadas por pigmentación.
- `Lizard_ResolveAppearance` permite resolver el fenotipo y los landmarks sin
  reconstruir el rig de animación en cada actualización.
- El worker conserva una sola petición pendiente. Descarta trabajo obsoleto con
  un umbral configurable de edad (0,10 por defecto), sin cancelar cada pequeño
  avance. Se registran por separado edad objetivo, edad del morph presentado,
  edad geométrica publicada, edad en construcción y edad pendiente. El benchmark
  mide ahora el retraso geométrico real y usa tiempo real a 60 Hz por defecto.

## Verificación reproducible

Los resultados y capturas locales están en `build/ager-continuity/verified/`.
Son artefactos ignorados por Git; las herramientas para regenerarlos sí forman
parte del cambio.

```sh
make -B -j4 test demos benchmarks/benchmark_growth benchmarks/benchmark_lizard \
    benchmarks/benchmark_head_morph benchmarks/benchmark_ager_realtime
./benchmarks/benchmark_growth
./benchmarks/benchmark_lizard 1
./benchmarks/benchmark_head_morph
./benchmarks/benchmark_ager_realtime
DISPLAY=:1 ./demos/demo_ager_3d --validate-gpu
DISPLAY=:1 ./demos/demo_ager_3d --validate-cycle=build/ager-continuity/verified/cycle
DISPLAY=:1 ./demos/demo_ager_3d --mesh --morph \
    --capture-prefix=build/ager-continuity/verified/age-0450 .45
```

La suite comprueba paridad escalar con/sin poda (20.000 puntos deterministas y
muestras próximas a apéndices), identidad y continuidad de la jaula, exclusión
anatómica de manos, coordenadas canónicas en múltiples tamaños, conexiones
activas y mallado adaptativo sin regiones. Conserva las pruebas previas de
apéndices en MORPH y SETTLED, superficie, anatomía, animación y asincronía.

Las capturas cubren edades 0; 0,25; 0,40; 0,425; 0,45; 0,475; 0,65 y 1, con doce
vistas por edad. La secuencia manual muestra dedos cortos que se separan y se
alargan progresivamente. `head-views.png` permite comparar vistas oblicua, frontal
y dorsal; `manus-dorsal.png` muestra el desarrollo de la mano.

## Resultados ejecutados

- `make -B -j4 test demos` junto con los cuatro benchmarks: todas las suites
  pasaron tras el último cambio de código; compilación sin advertencias.
- ASan/UBSan: suites de superficie y optimización de poda sin errores; no se
  ejecutó toda la suite bajo sanitizadores.
- Barrido real larva → adulto, 101 edades: todas las mallas tienen un componente,
  son válidas y estancas. No hubo fallos ni filtrado de fragmentos.
- Ciclo asíncrono 0 → 1 → 0: 67 mallas, cero fallos; paso geométrico máximo
  0,031251. Se inspeccionaron las capturas del ciclo y las vistas fijas.
- Paridad CPU/GPU: todas las muestras pasaron; error máximo observado 7,15e-7.
- 600 actualizaciones a 60 Hz: p95 CPU **5,045 ms**, máximo 12,604 ms; 20/20
  dedos. El benchmark **falla** sus límites de retraso geométrico: p95 **0,1492**
  frente a 0,02; máximo **0,1637** frente a 0,04. Completó 27 snapshots y
  coalesció 36 peticiones; en esta ejecución no hubo cancelaciones por antigüedad.
- GPU, adulto estático, 120 frames: **1,745 ms con escamas**, 0,458 ms sin ellas.
  Tres uploads, 357 aciertos de caché, 6.081.936 bytes; último upload 0,146 ms
  con escamas. Son tiempos GPU de dibujo, no el coste total de presentación.

| Adulto, generación del worker | Antes (ms) | Actual (ms) |
|---|---:|---:|
| MORPH total | 800,423 | 371,667 |
| MORPH mallado corporal | 655,809 | 313,560 |
| MORPH mapeo superficial | 100,212 | 16,873 |
| SETTLED total | 5034,929 | 2950,348 |
| SETTLED mallado corporal | 4185,908 | 2597,874 |
| SETTLED mapeo superficial | 620,846 | 119,292 |

Son muestras únicas, no intervalos estadísticos; la línea base se tomó al inicio
con otra actividad de validación. MORPH cambia de 139.946 a 119.726 triángulos y
el paso grueso efectivo de 0,071387 a 0,133816, ambos dentro del objetivo 0,14;
por tanto, su aceleración no debe atribuirse íntegramente a la poda. SETTLED
conserva prácticamente la teselación (856.382 → 856.362 triángulos) y el paso
0,071387. No se bajaron los presupuestos de calidad. Las pruebas de detalle
local y visibilidad pasan en ambos tiers.

En el barrido larval, el adulto empleó 338,617 ms: 282,830 en mallado, 16,632 en
mapeo, 27,012 en binding corporal y 1,810 en binding cefálico. La poda evitó
13.450.434 de 14.762.538 candidatos contabilizados. Los contadores anteriores
no incluían correctamente la ruta escalar y no son directamente comparables.

El residuo p95 de la cabeza larvaria pasó de 0,002838 a 0,002922 al deformar
edad 0 → 0,075 sin remallar. En edad 0,20 → 0,275 pasó de 0,002796 a 0,006709.
Son distancias del mundo del modelo, no píxeles ni una garantía de igualdad SDF.
El CSV conserva todos los intervalos medidos.

## Alcance de las conclusiones

El ciclo sincronizado espera snapshots publicados y sirve para comprobar ida,
vuelta y extremos coherentes. No demuestra el retraso de una reproducción libre:
eso lo mide `benchmark_ager_realtime` por separado.

La jaula aproxima el cambio de superficie entre remallados; no es una inversión
exacta del SDF. `benchmark_head_morph` informa residuo inicial y tras incrementos
de edad para separar error de discretización y error añadido por deformación.
Las pruebas del dominio canónico demuestran invariancia en segmentos y durante
la deformación de una malla fija; no prueban igualdad exacta de fase en todas las
uniones entre regiones al cambiar la topología. No se afirma que toda transición
posible esté libre de variación subpíxel.

Las mediciones previas de «lag cero» usaban la escala presentada por el morph y
no la edad de la geometría. No son evidencia de ausencia de retraso del worker.
El mallado adaptativo sigue siendo el coste principal. Las escamas del renderer
de malla y el raymarcher SDF son rutas distintas; la paridad GPU del campo no
implica que el raymarcher implemente esas escamas.

## Archivos principales de esta intervención

`src/Lizard.c`, `src/Head.c`, `include/Anatomy.h`, `src/Anatomy.c` y
`src/AnatomyDeformer.c` contienen el desarrollo y la exclusión de estructuras
inactivas. `include/HeadMorph.h` y `src/HeadMorph.c` añaden la jaula cefálica.
`src/MonsterSDF.c`, `src/SDFOperations.c`, `src/SDFMesher.c` y
`src/SDFAdaptiveMesher.c` contienen la poda y la continuidad del mallado.
`src/SurfaceMapper.c` implementa el dominio compartido; `src/MonsterVisualAsync.c`
y su cabecera coordinan bindings, publicación y edades. `Monster`, `MonsterAger`
y la demo transportan la edad y exponen diagnósticos. El shader SDF generado se
regenera desde las fuentes. Los benchmarks nuevos son `benchmark_growth` y
`benchmark_head_morph`; se corrige `benchmark_ager_realtime` y se amplían pruebas
superficiales y de poda. Había otros cambios previos en el árbol de trabajo;
este informe no atribuye todo el diff del repositorio a esta intervención.
