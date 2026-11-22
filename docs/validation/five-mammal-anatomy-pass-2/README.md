# Segundo refinamiento anatómico de cinco mamíferos

Comparación con `260e7e4e0d47faef9564b1f2c3757ececeb9dfd4`. El [diagnóstico inicial](baseline.md) se hizo con capturas nuevas de la revisión de partida y se contrastó con las fotos de `report/` y las láminas históricas. Las cuatro columnas de las láminas de adulto y desarrollo muestran arcilla inicial, arcilla revisada, superficie inicial y superficie revisada. Dentro de cada par se conservan la proyección ortográfica, centro, escala, dirección y luz. El detalle del pincel usa el mismo fondo claro y encuadre ampliado en ambas revisiones.

| Receta | Adulto en seis vistas | Cabeza e inserciones | Cuatro etapas y edades intermedias | Animación |
| --- | --- | --- | --- | --- |
| Jirafa, 1011 | [Adulto](jirafa-adulto.jpg) | [Nuca, hombro y cola](jirafa-detalle.jpg) | [Desarrollo](jirafa-edades.jpg) | [Reposo, marcha, bebida y mirada alta](jirafa-poses.jpg) |
| Ornitorrinco, 1012 | [Adulto](ornitorrinco-adulto.jpg) | [Bóveda, pico y pala](ornitorrinco-detalle.jpg) | [Desarrollo](ornitorrinco-edades.jpg) | [Reposo, marcha y mirada baja](ornitorrinco-poses.jpg) |
| Pangolín chino, 1013 | [Adulto](pangolin-adulto.jpg) | [Cabeza con y sin coraza](pangolin-detalle.jpg) | [Desarrollo](pangolin-edades.jpg) | [Marcha y enrollamiento](pangolin-poses.jpg) |
| Canguro rojo, 1014 | [Adulto](canguro-adulto.jpg) | [Hombro, cadera, mano, pie y suela](canguro-detalle.jpg) | [Desarrollo](canguro-edades.jpg) | [Progresión y fases](canguro-poses.jpg), [ciclo de salto](canguro-ciclo.jpg) |
| Murciélago orejudo pardo, 1015 | [Adulto](murcielago-adulto.jpg) | [Cara, orejas y ala](murcielago-detalle.jpg) | [Desarrollo](murcielago-edades.jpg) | [Extensión, plegado y batida](murcielago-poses.jpg) |

## Cambios observados

- **Jirafa.** El radio transversal de la nuca baja de 0,304 a 0,216 m. Las estaciones medias se mantienen más cerca del radio de la nuca y la sección pectoral conserva su radio de 0,662 m. El cambio usa `neckWidth`, mayor exponente de altura, mayor afinamiento medio y una estación cervical adicional. `MonsterSDF.c` sigue el primer tramo cervical hacia el cuerpo al orientar el collar y reduce la proyección de la tapa inclinada sobre el hombro. El cráneo conserva sus tres radios. La raíz caudal baja de 0,159 a 0,108 m de radio y el tramo cae en un arco corto. El pincel se proyecta sobre las dos últimas estaciones reales mediante el dominio existente de pelo largo. El ensayo intermedio sin ese módulo correctamente resuelto carecía de pelo. La captura de fondo claro confirma la inserción corregida y el final desflecado. [Giraffe Conservation Foundation](https://giraffeconservation.org/giraffe-species/northern-giraffe/)
- **Ornitorrinco.** El barrido posterior de la bóveda pasa de 0,081 a 0,172 m de semialtura, mientras la segunda estación del pico pasa de 0,044 a 0,056 m. La sección de nuca más estrecha deja aparecer el abombamiento. `Head.c` obtiene el espesor del pico de la envolvente cefálica y el control rostral, con un perfil distinto del radio de la bóveda. El ojo queda cerca de la base del pico. En `Tail.c` la pala ensancha desde un pedículo más delgado, conserva anchura en el centro y cierra en media elipse. En la estación 8 la relación ancho/alto pasa de 2,98 a 4,02. El primer radio vertical cae de 0,226 a 0,135 m al reducir la influencia de la pelvis, y el eje cae unos 8 cm en vez de 24 cm entre raíz y punta. La última revisión iguala la derivada con la tapa del barrido. [Australian Museum](https://australian.museum/learn/animals/mammals/platypus/)
- **Pangolín chino.** La bóveda pasa de 0,143 a 0,197 m de semialtura y conserva su ancho y largo. El generador reduce la sección cervical. La elevación del centro rostral medio y el cambio de alturas del barrido en `Head.c` conservan la convergencia del hocico con una punta redondeada. `ArmorScales.c` desplaza y reduce las filas anteriores de la corona, dejando más visible el volumen lateral y manteniendo placas en la nuca y el resto del cuerpo. El perfil de arcilla sin coraza permite ver cuánto del cambio corresponde al cráneo. [Save Pangolins](https://www.savepangolins.org/what-is-a-pangolin)
- **Canguro rojo.** El perfil axial pasa de un tórax posterior de 0,146 × 0,164 m y un abdomen de 0,240 × 0,274 m a 0,169 × 0,211 m y 0,211 × 0,249 m, respectivamente. Son semianchos y semialturas. La primera revisión redujo el abultamiento proximal y amplió el tejido sagital del muslo. Las capturas de aislamiento con cola o patas reducidas situaron el resalto principal en la tapa del barrido pélvico. Se probó y descartó el contorno cursorial porque elevaba demasiado la cola. El barrido final llega hasta la raíz de una cola independiente y gruesa, conservando su articulación y suavizando la línea de salida. El perfil distal se eleva ligeramente para mantener la punta a ras de suelo en reposo. `LimbMammal.c` embebe la raíz, ajusta la longitud de tibia y metatarso, reduce el abultamiento terminal del metatarso, alarga la palma de la mano libre y curva los cinco dedos. Los pies tienen el grupo medial II–III reducido, un IV más largo y grueso, y un V lateral intermedio. La disposición se refleja en ambos pies. [Australian Museum](https://australian.museum/learn/animals/mammals/red-kangaroo/)
- **Murciélago orejudo pardo.** El radio transversal craneal pasa de 0,120 a 0,099 m y el vertical se mantiene en 0,063 m. La altura efectiva del pabellón permanece en 0,231 m en el perfil auricular. La anchura de la concha se compensa al reducir la envolvente. La raíz sigue la nueva posición craneal con una apertura ligeramente mayor de la base. La segunda revisión recoge el morro y reduce la exposición del ojo. El patagio conserva su geometría y el plegado conserva su recorrido. [Bat Conservation Trust](https://www.bats.org.uk/about-bats/what-are-bats/uk-bats/brown-long-eared-bat)

Los generadores de `tools/` y las cinco recetas de `recipes/` se actualizan juntos. Las dimensiones anteriores proceden del grafo y del barrido craneal adulto resueltos. La cintura pélvica, las cadenas del cuello y las raíces de pelo se comprueban sobre el campo durante las pruebas. Se usan las representaciones ya serializadas de estaciones, miembros y pelo.

## Desarrollo y animación

Se capturaron semilla, larva, juvenil y adulto en las edades normalizadas 0, 1/3, 2/3 y 1, más 0,25, 0,50 y 0,75. El encuadre se ajusta a la edad y se comparte entre revisiones. El murciélago se muestra en planta para apreciar los patagios. En las cuatro etapas se comprobaron carga, grafo, campo y límites. En las edades intermedias se comprobaron también la integridad del Ager, la conservación de membranas y la fijación de las guías caudales de la jirafa.

El indicador de deformación es la fracción del área de reposo de los triángulos que cae por debajo del 35 % o supera el 200 % de su área original con la pose aplicada. El ciclo del canguro incluye doce muestras y el ensayo de apoyo usa suelo plano. El plegado del murciélago se prueba además en el sentido inverso hasta recuperar la envergadura de reposo.

| Gesto | Área extrema del cuerpo | Área extrema de coraza o patagio |
| --- | ---: | ---: |
| Jirafa · Beber | 2,87 → 2,77 % | — |
| Ornitorrinco · Mirada baja | 0,67 → 0,71 % | 0,00 → 0,00 % |
| Pangolín · Enrollamiento completo | 2,14 → 2,20 % | 0,00 → 0,00 % |
| Canguro · Progresión lenta | 2,59 → 2,86 % | — |
| Canguro · Impulso | 3,09 → 3,43 % | — |
| Canguro · Vuelo | 2,34 → 2,63 % | — |
| Canguro · Aterrizaje | 2,70 → 3,06 % | — |
| Murciélago · Plegado | 0,55 → 0,03 % | 3,83 → 3,83 % |

El canguro mantiene sincronizadas las patas posteriores. En 180 pasos a 60 Hz y 1,5 m/s, el error horizontal máximo del efector en apoyo fue de 0,69 → 0,71 mm y la suela bajó como máximo 1,92 → 1,86 mm por debajo del plano. La medida usa los efectores del rig como muestra del apoyo. La mayor deformación de la malla aparece durante el impulso.

## Coste y densidad de malla

La consulta CPU usa la mediana de tres lotes de 200 000 puntos deterministas en el mismo volumen de partida para cada par. El paso efectivo se tomó de la extracción adaptativa de las capturas adultas con la misma configuración nominal.

| Especie | Vértices del cuerpo | Paso efectivo, mm | Tiempo CPU por lote, ms |
| --- | ---: | ---: | ---: |
| Jirafa | 680701 → 637294 | 10,20 → 10,20 | 487 → 493 |
| Ornitorrinco | 410862 → 180064 | 6,32 → 10,13 | 624 → 632 |
| Pangolín | 557497 → 315570 | 6,90 → 9,50 | 685 → 701 |
| Canguro | 99943 → 137308 | 14,20 → 13,19 | 719 → 680 |
| Murciélago | 393073 → 389763 | 6,28 → 6,03 | 588 → 588 |

El canguro cruza a una retícula más fina y aumenta el número de vértices. Ornitorrinco y pangolín caen en otro escalón de la retícula, por lo que sus recuentos menores incluyen un cambio de paso efectivo. Las cifras CPU varían según el campo y el descarte espacial. El pelo de la jirafa configura 48 guías y 20 fibras hijas por guía. Quedan por medir el coste de su animación, el tiempo por fotograma y la memoria GPU en una escena con múltiples animales.

## Verificación

- `make test` cubre los cinco mamíferos, cuatro etapas, Ager, ascendencia y cruces, continuidad del centro del campo, ojos y órbitas, cuello y pelo caudal, coraza, palma, dedos y apoyo del salto.
- `make editor-test` ejecuta 11 pruebas. `make interface-engine-test` verifica los dos visualizadores y el uso de membranas, escamas y pelo en el flujo de escena.
- Se ejecutaron `make visual-morph-gpu-regressions` y `make visual-morph-ownership` con una RTX 3050 Laptop, OpenGL 4.6. La última prueba compara el cuerpo exacto con la transición bajo retraso de trabajador. Detectó 89 píxeles extra y 2 ausentes en una referencia de 25 459 píxeles y pasó su tolerancia.
- Se compararon CPU, compute y fragmento, intervalos y pigmento para 25 casos de `validate_recipe_gpu`. Incluyen las cuatro etapas de los cinco mamíferos y los adultos de rata, caballo, rinoceronte, cerdo y elefante. Los máximos fueron 2,39 × 10⁻⁶ m para compute y fragmento, 3,76 × 10⁻⁶ m para intervalos y 6,56 × 10⁻⁷ para pigmento lineal.
- La regeneración en orden jirafa, ornitorrinco, pangolín, canguro y murciélago reprodujo los cinco SHA-256. Los identificadores 1011–1015 y el catálogo permanecen en uso. La prueba de resolutores cefálicos comprueba que los factores craneales se aplican una sola vez.

## Límites observados

- La jirafa conserva una transición visible sobre la cruz en flexión de bebida. La nueva forma mejora la silueta de reposo y mantiene las raíces del pincel durante el movimiento. El gesto aún mantiene las manos delanteras juntas.
- En el ornitorrinco queda una línea de unión de las valvas cerca de la punta. Se observaron marcha y mirada baja. La flexión del pico y la natación quedan fuera de las poses medidas.
- El pangolín deja más visible el cráneo y mantiene los pabellones. En la flexión máxima sigue abierto parte del hueco ventral. La prueba de coraza mide distorsión triangular y despeje del rostro en reposo. Las colisiones placa contra placa durante el cierre siguen pendientes.
- El canguro conserva una inflexión pequeña alrededor de la rodilla y el arranque del metatarso. La continuidad pélvica en reposo mejoró y el área deformada aumentó durante el salto. Los apoyos de las patas se verificaron sobre un plano. En reposo, el ajuste del último tramo deja su eje terminal a 3,1 cm del plano y el borde inferior de su sección a unos 6 mm del suelo. En movimiento la cola conserva su unión sacra y sigue la pose del rig. La progresión lenta usa el controlador existente y carece de una marcha pentápoda específica.
- El murciélago conserva el pliegue del patagio durante el recogido. Sus pabellones permanecen erguidos en reposo y el tragus carece de una pieza propia.

## Reproducción

Los PPM, cámaras, métricas y ensayos descartados están en `build/anatomy-pass2/`. Para repetir un par, compilar `build/capture_creature` en la revisión de partida y guardar las líneas que empiezan por `camera ` del registro como referencia. Pasar el archivo al binario revisado con `MONSTER_CAPTURE_CAMERA_REFERENCE`. Las comparaciones de cabeza del pangolín en arcilla usan `MONSTER_CAPTURE_BARE=1`. El detalle caudal de la jirafa usa `MONSTER_CAPTURE_LIGHT_BACKGROUND=1` y un encuadre común ampliado.

```sh
make build/capture_creature build/capture_animations build/validate_recipe_gpu
mkdir -p build/review/{adult,age,poses}
./build/capture_creature recipes/canguro.creature.json build/review/adult 1 neutral
MONSTER_CAPTURE_STAGE_VIEWS=1 ./build/capture_creature recipes/canguro.creature.json build/review/age .5 surface
./build/capture_animations recipes/canguro.creature.json build/review/poses \
  --anims=idle,walk,hop_launch,hop_flight,hop_landing --views=side,three-quarter,top --hop-cycle
./build/validate_recipe_gpu recipes/canguro.creature.json 3
```
