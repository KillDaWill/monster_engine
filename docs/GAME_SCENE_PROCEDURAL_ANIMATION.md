# Marcha y giros procedurales de la escena

La demo del juego usa ahora `GaitPreset_CanidWalk`: paso de cuatro tiempos,
zancada calculada con todos los segmentos de las patas y elevación proporcional
al metapodio más corto. La receta estática del perro conserva su comportamiento;
la escena instala el perfil sobre su rig resuelto.

## Correcciones

- El rig mamífero separa flexión y torsión del hombro/cadera. El eje de torsión
  sigue el hueso; antes el límite de torsión restringía la flexión sagital.
- Codo, rodilla y corvejón conservan el sentido de flexión de su anatomía de
  reposo. El carpo/tarso tiene un recorrido propio, distinto del proximal.
- Las cadenas digitígradas de cuatro articulaciones reciben una preferencia
  distal y una solución proximal de dos huesos. Esa preferencia se relaja en el
  tiempo y la IK de la cadena completa corrige después el contacto real.
- El descenso de apoyo reserva alcance sin estirar huesos. Las curvas del paso
  tienen velocidad y aceleración nulas al despegar y aterrizar.
- Ambos animales giran alrededor del centro de apoyos. `Locomotion_TurnFrame`
  transporta los contactos al nuevo marco del modelo con el giro **real**;
  el llamador compensa la posición del modelo para conservar ese pivote mundial.
- El rumbo integra una velocidad angular con aceleración y frenada. El límite
  de giro deriva del radio de los apoyos y de la capacidad de la zancada. La
  pata exterior determina el recorrido disponible durante la curva.
- La posición de la escena integra la traslación realmente producida por la
  animación. Ya no avanza a una velocidad superior a la admitida por las patas.
  La anticipación de paredes y el seguimiento de objetivos usan esa capacidad.

## Verificación

Las regresiones de `tests/test_animation.c` recorren 900 pasos por caso para
perro y lagarto: recta, curvas a izquierda/derecha, pivote sin avance y marcha
atrás. Comprueban contactos mundiales, longitudes óseas, sentido de flexión,
continuidad articular e inmutabilidad de la anatomía. También comprueban el
escalado del perfil cánido y su independencia del orden de las extremidades.

Residuo máximo del efector: perro `0.003833`, lagarto `0.002000` unidades.
El objetivo nominal de IK es `0.002`; el caso extremo de pivote del perro queda
por debajo del límite de regresión de `0.004`. No se declara convergencia cuando
el residuo supera el objetivo nominal.

Las capturas y perfiles están en
[`validation/game-scene/animation-20261008`](validation/game-scene/animation-20261008/).
Las hojas de cuatro fotogramas muestran vistas lateral/frontal del perro y el
lagarto girando. Las tres secuencias usan las dos criaturas, pelaje activo,
212 fotogramas, semilla 137 y paso fijo de 1/60 s.

La escena normal con mutación completó 600 fotogramas. La prueba de paredes y
objetivo alcanzado completó 1200 fotogramas: una llegada, cero recuperaciones,
bloqueo máximo de `0.35 s` y escape máximo numérico de `0.00000191` unidades.

Después de los 30 primeros fotogramas, la animación conjunta tuvo un p95 de
`1.12 ms` en la escena normal y `1.13 ms` en la prueba de navegación. La
presentación de la escena normal tuvo p95 de `31.00 ms`, con un máximo de
`42.91 ms`; las vistas de marcha sin mutación tuvieron p95 entre `20.46` y
`21.28 ms`. Estas mediciones corresponden a esta ejecución, no garantizan un
presupuesto de 30 FPS para todas las variantes de mutación.

## Reproducir

```sh
make -B -j6 test demos
demos/demo_game_scene --headless --cycles=0 --animation-stress --inspect-dog --inspect-azimuth=1.57 --inspect-elevation=.18 --frames=212 --fixed-dt=.016666667 --seed=137 --capture-prefix=/tmp/dog-walk
demos/demo_game_scene --headless --cycles=0 --animation-stress --inspect-lizard --inspect-elevation=.35 --frames=212 --fixed-dt=.016666667 --seed=137 --capture-prefix=/tmp/lizard-turn
demos/demo_game_scene --headless --cycles=0 --validate-navigation --navigation-orbit-stress --frames=1200 --fixed-dt=.016666667 --seed=137
```

`--cycles=0` desactiva la mutación automática durante estas revisiones.
`--animation-stress` alterna el rumbo deseado para revisar la aceleración,
frenada e inversión del giro. Las otras demos revisadas no integraban giros de
navegación; usan la mejora común de apoyos y trayectoria del paso donde procede.
