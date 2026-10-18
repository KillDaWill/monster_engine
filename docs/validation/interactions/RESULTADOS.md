# Resultados: interacciones y articulación de morros

Validación final del 13 de octubre de 2026, sobre el checkout existente.
El diagnóstico, contrato de IA, arquitectura y lista de archivos están en
[ANATOMICAL_INTERACTIONS.md](../../ANATOMICAL_INTERACTIONS.md).

## Causa y corrección de los morros

Las longitudes óseas eran constantes. El cráneo integrado en la malla general
recibía mezcla de transformaciones cervicales, mientras los ojos y la mandíbula
explícita seguían rígidamente la cabeza. La GPU tampoco aplicaba la articulación
mandibular publicada: conservaba la apertura de la geometría de referencia.

Ahora cráneo y morro conservan forma rígida, la mandíbula gira sobre su bisagra
y sólo el collar y el puente posterior admiten flexión de tejido. Se usan
landmarks, ejes y dimensiones resueltas; no nombres de especies. El cambio de
proporciones durante mutación llegó a mostrar otra desviación de 0.047069 unidades:
la proximidad al tórax ganaba la selección de influencias. La propiedad del campo
cefálico ahora prevalece sobre esa proximidad, sin ocultar ni eliminar geometría.

La regresión CPU reproduce desviaciones anteriores de 0.310119 unidades en el
lagarto y 0.180242 en el perro. Después son menores de 0.000001 unidades y se
conservan las distancias faciales. La apertura de mandíbula se comprueba en 121
pasos, sin cambiar longitud ni radio respecto a la bisagra. También se prueba
una boca con orientación propia y el conflicto de propiedad con el tórax.

## Pruebas ejecutadas

- `make -B -j6 test demos`: todas las suites pasaron, sin warnings de compilación.
- Seis casos de 720 fotogramas: cuenco, suelo, altura intermedia, diez unidades
  por encima de la boca, objetivo móvil y objetivo obstruido. Los dos actores
  completan el consumo alcanzable y no consumen objetivos imposibles u obstruidos.
- Escena normal: 1800 fotogramas con `--validate-ai`; perro y lagarto completan bebida.
- Mutación durante bebida: 2400 fotogramas con `--validate-ai --validate-skinning`;
  termina sin errores, conserva la mente y recompila las restricciones.
- El shader GPU real se verifica con transform feedback: diez muestras de cráneo,
  mandíbula y anclajes del puente, con mandíbula cerrada y abierta al 75%.
  Error máximo final: 0.00000027 unidades. Se comprueba también finitud de normales.
- La malla GPU del perro coincide con la evaluación CPU: error posicional cero
  en los fotogramas muestreados; error de normal inferior a 0.000001.
- La suite incluye límites, longitudes, continuidad a 30/60/120 Hz, contacto 3D,
  consumo condicionado, apoyos, objetivos inválidos y cachés incompatibles.

Evidencia: [build y tests](muzzle-final-build-tests.log),
[casos](muzzle-final/cases-validation.txt), [escena](muzzle-final/scene.log),
[mutación](muzzle-final/mutation.log), [benchmark de alcance](muzzle-final/reach-benchmark.txt).
El log `mutation-rejected-before-ownership.log` documenta el fallo encontrado
antes de corregir propiedad craneal; no es un resultado aceptado.

## Capturas revisadas

| Vista lateral, semilla 137 | Antes | Después |
|---|---|---|
| Perro, cuenco, fotograma 349 | [PNG](muzzle-before-dog.png) | [PNG](muzzle-after-dog.png) |
| Lagarto, cuenco, fotograma 349 | [PNG](muzzle-before-lizard.png) | [PNG](muzzle-after-lizard.png) |

En el fotograma 349 el perro está consumiendo y el lagarto ya ha entrado en
retirada. La comparación del lagarto usa exactamente la misma pose temporal;
la mandíbula ya no conserva una apertura ajena al rig. También se revisó el
[lagarto en contacto estable real](muzzle-gpu-ai-3-stable.png) y la
[criatura tras mutar](muzzle-final/mutation-after.png). Son capturas del motor.

## Rendimiento

NVIDIA RTX 3050 Laptop, OpenGL 4.6, escena headless a resolución normal y tiempo
simulado fijo de 1/60 s. Ejecuciones secuenciales sin otras pruebas en paralelo;
se omiten los primeros 30 fotogramas para medir régimen estable. El escenario
con mutación de esta tabla no incluye captura ni readback de diagnóstico.

| Ejecución | Fotogramas | Animación CPU media ms | Fotograma medio ms | p95 ms | Máximo ms |
|---|---:|---:|---:|---:|---:|
| Antes | 600 | 0.654 | 5.362 | 14.379 | 18.583 |
| Después: escena | 1800 | 0.811 | 3.189 | 3.783 | 6.244 |
| Después: mutación | 2400 | 1.034 | 4.389 | 11.296 | 15.409 |

La escena final sostiene 314 FPS de media y
228 con mutación; el p95 corresponde a
89 FPS con mutación. El inicio con caché de shaders
caliente cuesta 373.7 ms. Una compilación de shaders
sin caché puede durar mucho más: no se garantiza 30 FPS durante inicialización.
La ejecución con capturas/readback tuvo un pico de
51.5 ms; no representa
el coste del camino normal. Datos completos en [performance.json](muzzle-final/performance.json).

El microbenchmark final tarda 0.54–0.60 ms por actualización en los tres objetivos
alcanzables y 0.22–0.29 ms en el imposible. El anterior tardaba 0.30–0.46 ms y
0.74–0.80 ms respectivamente: naturalidad y trayectoria acotada tienen coste,
y el descarte de objetivos imposibles evita trabajo inútil. Las trayectorias de
IA antes/después difieren, por lo que el FPS no aísla el coste de una sola corrección.

## Reproducir

```sh
make -B -j6 test demos benchmarks/benchmark_interactions
benchmarks/benchmark_interactions
for caso in bowl ground raised high moving blocked; do
  demos/demo_game_scene --headless --cycles=0 --interaction-case="$caso" --frames=720 --fixed-dt=.016666667 --seed=137 --benchmark-uncapped > "/tmp/$caso.log" 2>&1
done
python3 tools/validate_interactions.py /tmp
demos/demo_game_scene --headless --cycles=0 --validate-ai --ai-mutate=drink --validate-skinning --frames=2400 --fixed-dt=.016666667 --seed=137 --benchmark-uncapped
```

Para las vistas laterales: `--interaction-case=bowl --frames=350 --inspect-dog`
o `--inspect-lizard`, junto con `--inspect-azimuth=1.57 --inspect-elevation=.12`
y `--capture=/tmp/vista.ppm`. `--capture-prefix=/tmp/vista` guarda también el
primer contacto estable. El modo normal del demo sigue disponible sin estos flags.

## Límites actuales

El collar y el puente gular son tejido blando, con conservación de volumen
aproximada. La prevención corporal usa envolventes, cilindros y el soporte de la
locomoción existente: no es física completa ni una prueba universal de ausencia
de intersecciones en todas las anatomías futuras. La ruta mandibular GPU admite
una boca por criatura. La determinación a distintas frecuencias se valida para
el solver; no se promete identidad de todas las decisiones y rutas de IA.
