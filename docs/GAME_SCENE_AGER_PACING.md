# Ager, límite de frecuencia y navegación

Medición del 7 de octubre de 2026 en AMD Renoir, Mesa 25.2.8, 1200 × 850,
MSAA 4, pelaje activo, plataforma de 56 × 56 y voxel SDF de 0,075.

La demo del mundo limita las presentaciones a **60 FPS por defecto**. Espera
mediante `SDL_Delay` antes de presentar el siguiente fotograma, usando la
presentación anterior como referencia. Redondea la espera hacia arriba; no
consume CPU mediante espera activa ni acumula retrasos para presentar ráfagas.
La duración interactiva de Ager sigue siendo de tres segundos reales.
`--benchmark-uncapped` desactiva deliberadamente el límite sólo para diagnóstico.

## Causa y corrección del coste de Ager

La interpolación de anatomía medida costaba aproximadamente 0,50 ms; la
reconfiguración del rig, 0,26 ms. Recompilar el contacto de la anatomía cambiante
y resolver las patas explicaban una parte mayor del coste CPU.

- Dentro de cada iteración de FABRIK se actualizaban descendientes ajenos a la
  cadena que se estaba resolviendo. Ahora las pasadas internas actualizan sólo
  esa cadena; al finalizar se publican todas sus ramas. Las restricciones,
  tolerancias, iteraciones y búsqueda de la mejor solución se conservan.
- La compilación del contacto consultaba intervalos de todas las celdas por
  separado. Una certificación previa de grupos de ocho celdas descarta los
  grupos enteramente vacíos. Las celdas inciertas siguen el mismo algoritmo,
  con los mismos soportes de skinning y retícula de 12³. Las bocas se incluyen
  en ambas certificaciones. No se descartan detalles por tamaño ni por muestras
  de esquinas; el criterio sigue siendo el intervalo conservador del SDF.
- El CSV separa interpolación, rig, construcción SDF, contacto, trabajo y espera.
  `wall_ms` incluye espera; `work_ms` la excluye y se mide antes del intercambio.
  `presentation_ms` mide el intervalo real entre presentaciones, incluyendo
  trabajo, espera e intercambio. Los tiempos GPU llegan por consultas diferidas.

## Comparación ejecutada

Ejecuciones secuenciales, sin tests ni otras demos en paralelo, semilla 42,
300 fotogramas por ejecución, misma calidad y encuadre. Medianas de los
fotogramas 35–208 de una mutación continua desde Emerald Climber. La referencia
es el ejecutable inmediatamente anterior a estos cambios; ambas mediciones son
sin límite de frecuencia. Estos tiempos no certifican todos los encuadres ni
las diez variantes.

| Destino | Fotograma antes / después (ms) | Actualización CPU antes / después (ms) | p95 del fotograma antes / después (ms) |
|---|---:|---:|---:|
| Blue Runner | 22,191 / 20,954 | 13,632 / 10,337 | 28,979 / 24,800 |
| Volcanic Spiny | 32,511 / 28,389 | 19,888 / 16,335 | 34,733 / 30,304 |
| Sailback | 35,467 / 30,380 | 21,071 / 16,023 | 40,773 / 33,946 |

La actualización CPU mejora entre el 18 y el 24 % en estos casos. La mediana
del fotograma completo mejora entre el 6 y el 14 %; el trabajo GPU restante y
su solapamiento con CPU limitan la ganancia. La compilación de contacto de
Sailback baja de 13,647 a 9,803 ms. Sigue siendo un coste relevante durante el
cambio continuo de anatomía. Sailback aún supera 33,33 ms en p95; no se certifica
un mínimo universal de 30 FPS ni 60 FPS durante las mutaciones.

El límite se comprobó en 240 presentaciones interactivas: intervalo mínimo
16,910 ms, máximo 59,137 FPS, mediana 17,493 ms. La espera media fue 9,240 ms y
el trabajo medio antes de presentar 8,399 ms. La cuantización de la espera puede
situar la frecuencia algo por debajo de 60.

## Persecución circular de objetivos

Al alcanzar el punto de escape, la IA seguía calculando `atan2` del vector al
objetivo. Cruzarlo invertía el rumbo deseado; la condición de salida también
exigía espacio para girar todo el cuerpo, y podía mantener esa persecución.
Ahora se reconoce la llegada dentro de un radio relacionado con velocidad y
giro, y se conserva el rumbo al abandonar el objetivo.

`NavigationProgress` sigue la mejor distancia al objetivo y el giro acumulado.
El recorrido total ya no cuenta como evidencia de avance hacia el objetivo.
Si no mejora durante el plazo de orientación, o completa un giro persistente
sin acercarse, se elige otro objetivo interior. Cambiar objetivo reinicia el
monitor; los giros iniciales y los avances continuos conservan su plazo.

## Validación y reproducción

- `make -B -j4 test demos`: todas las suites pasan, sin avisos del compilador.
- Comparación con FABRIK anterior: 3.200 cadenas de las diez variantes completas,
  mismas iteraciones, convergencia, posiciones y rotaciones; error máximo cero.
- Tests de llegada coincidente, llegada antes de cruzar el objetivo, órbita con
  recorrido positivo, bloqueo sin giro, orientación inicial y avance continuo.

- Tres recorridos de 60 segundos simulados (semillas 173, 42 y 914), con tres
  mutaciones completas por recorrido y colocación inicial junto a las paredes.
  Bloqueo máximo 0,30 s; ningún objetivo permaneció sin avance más de 1,40 s.
  La semilla 173 también fuerza un objetivo coincidente para comprobar su salida.
  Todos mantuvieron avance locomotor y la contención dentro de la tolerancia
  de 0,0001: error exterior máximo de redondeo 0,00000191.
- Comparación visual A/B con Volcanic Spiny en ocho edades: píxeles idénticos
  fuera del HUD de rendimiento. La firma final de triángulos, posiciones y
  normales es idéntica a la referencia; se publican 142.092 índices. Paridad
  GPU/CPU máxima 0,000000477; cero overflow y errores de páginas o referencias.
  Se inspeccionaron las capturas intermedias y de navegación.


```sh
./demos/demo_game_scene
./demos/demo_game_scene --headless --benchmark-uncapped --frames=300 --target=7 --seed=42 --profile=/tmp/ager.csv
./demos/demo_game_scene --headless --validate-navigation --navigation-orbit-stress --frames=1200 --cycles=3 --seed=173 --fixed-dt=.05
./demos/demo_game_scene --headless --frames=240 --target=4 --seed=42 --stationary --validate-gpu --capture-prefix=/tmp/ager-parity
```

Los CSV, logs y resúmenes de estas ejecuciones se conservan en
[`validation/game-scene/ager-pacing/`](validation/game-scene/ager-pacing/).
