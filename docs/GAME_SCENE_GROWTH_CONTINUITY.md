# Crecimiento de accesorios y continuidad de pigmentos en Game Scene

7 de octubre de 2026. Evidencia: `docs/validation/game-scene/continuity-20261007/`.

## Causas y reparación

La demo aplicaba una variación, pero construía el monstruo con `CreatureRecipes_Lizard()` en vez de la receta devuelta por `CreatureVariation_Apply`. Se perdían los módulos añadidos al BodyPlan: cuernos, espinas, membranas y placas geométricas. Tanto la criatura inicial como los destinos y la envolvente de la retícula usan ahora la receta completa de cada variante.

Ager mezcla las anatomías resueltas. Un accesorio nuevo conserva su identidad, parte con desarrollo cero y alcanza su posición y radios adultos gradualmente. Su punto de colapso es la raíz superficial transportada sobre el anfitrión. Las uniones SDF usan el radio del brote, evitando engordar todo el torso al añadir espinas. Para membranas, cada tramo tiene la identidad de su conexión: el nodo destino puede ser compartido por varias costillas sin provocar identidades duplicadas en la caché GPU.

La apariencia tenía dos discontinuidades: cambiar semillas/tipo de cobertura únicamente en el extremo y reindexar capas al retirar los patrones de origen. El shader incluía el índice de capa en el ruido, por lo que ese reordenamiento alteraba el patrón final. Ahora el fenotipo transporta los campos completos de ambos extremos y su peso continuo. Cada campo conserva semillas, parámetros y composición originales, incluyendo sus cuatro capas o su patrón implícito. Ambos se evalúan sobre el mismo dominio anatómico y sus colores se mezclan. No se necesita retirar una familia de patrones para liberar espacio a la otra.

También se interpolan la forma de escama/placa, la cobertura y la contribución de las semillas celulares cuando son diferentes. El material de un accesorio nuevo se registra desde su reposo adulto resuelto por Ager, evitando coordenadas colapsadas y radios casi nulos. El dominio ya existente permanece estable.

El cambio mantiene una sola superficie GPU y no sustituye el monstruo ni su material al llegar al adulto. La animación utiliza actualizaciones de subárbol durante IK: una pata sólo actualiza sus descendientes, conservando las mismas restricciones e iteraciones. Esto reduce el trabajo que añadían los nuevos huesos de accesorios.

## Comprobaciones ejecutadas después de la última edición

- `make -B -j6 test demos`: todas las suites pasan, sin warnings ni errores de compilación.
- Test de accesorios: diez variantes, 21 edades por variante. Comprueba identidad, desarrollo monótono, posición/radios adultos, presencia en el SDF, rig válido e identidades únicas de las features, incluidas costillas compartidas.
- Test de apariencia: campos con cuatro capas por extremo, semillas de 32 bits, patrones implícitos y transición de escamas a placas. Las semillas y el orden del destino están disponibles desde el comienzo, sin truncar sus capas durante la mezcla.
- Test de pose: 100 poses con ramas intercaladas. La actualización parcial coincide con la actualización completa y conserva exactamente las ramas no modificadas; las pruebas existentes de IK, apoyos y giros también pasan.
- Capturas GPU de Desert Horned, Volcanic Spiny, Stone Armored, Sailback y Aquatic Fin-Tail, en edades intermedias y los fotogramas 208–211. En las cinco variantes, el recorte del lagarto tiene **error RGB medio cero** entre 209→210 y 210→211: no hay intercambio de patrones, colores o geometría al finalizar. La prueba usa pose y navegación estacionarias para aislar el crecimiento.
- Paridad SDF CPU/GPU y halos: residuo máximo de 0,000000715. Las cinco auditorías y el ciclo aleatorio tienen cero overflow, errores del asignador, referencias de aristas o valores compartidos inconsistentes.
- Cuatro mutaciones consecutivas: Emerald Climber → Stone Armored → Emerald Climber → Desert Horned → Blue Runner. Completadas sin fallo de publicación ni cambio de retícula.

Ver `growth-contact.png`, `continuity.json`, `build-tests.log`, `cycles.log` y los diagnósticos/signaturas de cada captura. Las lecturas de diagnóstico se ejecutaron separadas del benchmark.

## Rendimiento con los módulos completos

La medición anterior de la demo no incluía los módulos omitidos por el fallo de receta. Se conserva como referencia histórica, pero no certifica 30 FPS con todos los accesorios activos. Los siguientes perfiles corresponden a la implementación reparada, sin capturas/readbacks intermedios ni otras pruebas en paralelo.

AMD Radeon Renoir, Mesa 25.2.8, 1200 × 850, MSAA 4, pelaje activo y voxel 0,075. Cada variante parte de Emerald Climber, semilla 42; fotogramas 30–210, 181 muestras. Headless recorre edades con dt fijo de 1/60 y mide el tiempo real del fotograma.

| Variante | Media ms | FPS medios | p95 ms | Máximo ms |
|---|---:|---:|---:|---:|
| Emerald Climber | 23.02 | 43.4 | 28.82 | 29.97 |
| Desert Horned | 32.58 | 30.7 | 35.69 | 38.76 |
| Blue Runner | 22.77 | 43.9 | 29.05 | 30.43 |
| Fat-Tailed Gecko | 26.85 | 37.2 | 32.99 | 40.47 |
| Volcanic Spiny | 28.57 | 35.0 | 35.04 | 42.10 |
| Sand Burrower | 21.54 | 46.4 | 28.37 | 29.69 |
| Stone Armored | 26.34 | 38.0 | 31.52 | 34.56 |
| Sailback | 31.08 | 32.2 | 36.86 | 38.05 |
| Jewel Chameleon | 25.39 | 39.4 | 31.14 | 32.89 |
| Aquatic Fin-Tail | 22.57 | 44.3 | 28.06 | 33.17 |

Todas superan 30 FPS de media. Desert Horned, Volcanic Spiny y Sailback todavía incumplen p95 < 33,33 ms; por tanto **30 FPS estables para todas las variantes queda pendiente**. El mayor coste restante es la certificación CPU de bricks y la extracción de los campos con muchos accesorios. No se redujeron resolución, detalles, pelaje o MSAA para ocultarlo. La memoria de buffers GPU se mantiene aproximadamente en 261–262 MiB.

## Reproducción

```sh
make -B -j6 test demos
./demos/demo_game_scene
./demos/demo_game_scene --headless --frames=240 --target=1 --seed=42 --stationary --validate-gpu --capture-prefix=/tmp/horns
./demos/demo_game_scene --headless --frames=240 --target=7 --seed=42 --profile=/tmp/sail.csv
./demos/demo_game_scene --headless --frames=930 --cycles=4 --seed=42 --stationary --validate-gpu --capture-prefix=/tmp/cycles
```
