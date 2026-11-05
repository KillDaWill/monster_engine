# Pelo largo: volumen, peinado y contacto

Esta validación amplía [la fundación anterior](../long-hair-foundation/README.md),
sin sustituir el sistema de guías ni introducir fibras persistentes por hijo.
Las recetas son optativas: no se modifican los animales del catálogo.

## Cambios

- Raíces estratificadas sobre el dominio anatómico, manteniendo los identificadores
  derivados del grupo y del ordinal. Variación determinista de dirección,
  longitud y curvatura; gravedad gradual y salida normal a la piel.
- Hijos dentro de tubos tridimensionales, con transición del marco cutáneo al
  marco de la curva. Ocho fibras comparten un mechón; longitudes, grosor y color
  conservan variación individual. Se mantienen 96 guías × 48 hijos en crines y
  colas, y 64 × 48 en barbas: la mejora no procede de multiplicar fibras.
- Crin equina lateral y larga; crin asnal corta y erguida; cola equina amplia;
  penacho asnal distal. La barba canina activa explícitamente el paso canino:
  la receta anterior tenía `gait=0` y no demostraba animación.
- Antialiasing por derivadas, ancho mínimo de pantalla compensado en cobertura,
  afinado de raíz a punta y iluminación longitudinal. Con MSAA se usa cobertura
  alfa; sin MSAA, un núcleo opaco escribe profundidad y un segundo pase mezcla
  únicamente los bordes sin escribir profundidad. No hay ordenación por fibra
  ni buffers de transparencia de tamaño de pantalla.

## Contacto reutilizable

`LongHairSet_Collide()` acepta un campo de distancia prestado. La construcción
proyecta las raíces al SDF exacto y resuelve también el resto de las curvas.
Las raíces permanecen fijas; hasta ocho iteraciones alternan restricciones de
longitud y proyección normal. Se examinan los extremos y puntos intermedios de
la curva suave, no sólo cuerdas rectas. Hasta tres pasadas de pulido reparten
el contacto residual entre los controles positivos de Catmull–Rom. El contacto tiene prioridad sobre una longitud perfectamente
inextensible cuando ambas restricciones son incompatibles.

En cada pose, estaciones elipsoidales y barridos entre estaciones se transportan
con el esqueleto. Una fase amplia por guía y descartes escalares por cápsula
reducen consultas; las normales son analíticas. El ajuste a la raíz exacta de reposo evita
que una envolvente anatómica demasiado gruesa estire una barba o crin corta.
Las envolventes respetan la raíz transportada cuando su aproximación la contiene;
los obstáculos conservan su transformación independiente. Este ajuste prioriza
el anclaje frente a un contacto imposible justo en el punto inmóvil y no pretende
resolver poses donde el propio cuerpo atraviesa la inserción.

Las raíces ventrales cuya superficie pertenece al campo mandibular se vinculan
a la articulación auxiliar de esa superficie, manteniendo sus identidades y
dominio anatómico. La construcción usa desde el principio la unión del SDF
corporal y la mandíbula visual: proyectar sucesivamente en ambos puede dejar
una raíz dentro del volumen anterior. Un snapshot autónomo y pequeño de la
mandíbula permanece en el runtime; su campo local se transforma con la
articulación y participa en el mismo mínimo de contacto que las envolventes.
Así no se sustituye una mandíbula articulada por una esfera cefálica demasiado
gruesa. El SDF corporal completo sigue siendo temporal y no se consulta por
hijo en CPU. No existe un tipo de pelo especial para la barba. Las capturas con
raíces mandibulares incluyen apertura creciente de la boca en sus tres poses.
El marco cutáneo se transporta explícitamente para no invertir fibras cuando
la tangente casi coincide con la normal. Una corrección de caída mundial suaviza
el transporte de la gravedad al inclinar la cabeza, sin integrar velocidades.

La holgura incluye el tubo de hijos y su grosor, y limita la expansión cerca
de la raíz según la longitud disponible. Además se vuelve a muestrear la curva
Catmull–Rom final: cada intervalo obtiene un radio libre, descontando el máximo
desplazamiento entre muestras a partir de la cota de velocidad de su derivada
Bézier. Ese radio viaja en la componente `w` de las diez filas ya existentes;
`Child()` limita su expansión con él. No hay consultas SDF por hijo en CPU
durante la animación. El buffer sigue ocupando 16 `vec4` por guía.
La derivada analítica comparte las cuatro lecturas de la curva en el shader,
en lugar de volver a leerla para calcular cada tangente.

El cambio anatómico reconstruye guías y envolventes; la pose sólo transporta y
resuelve contactos. La presentación sigue siendo propietaria del runtime y el
worker asíncrono no suministra pelo de una apariencia antigua. La resolución
parte del mismo reposo en cada llamada: una pose fija no acumula deriva.
Al volver exactamente al reposo se recupera el radio libre calculado con el campo
exacto, en vez de sustituirlo por el de las envolventes aproximadas.

Si una mandíbula cubre una inserción que pertenece a otra superficie, no existe
una solución que simultáneamente conserve esa raíz fija y la deje fuera del
obstáculo. Su cobertura se desvanece cerca del contacto y no emite fibras mientras
queda enterrada; no se elimina la identidad ni se mueve la raíz. Los logs
`pose N covered_roots M` hacen explícita esta oclusión. Los contactos del resto
de la curva sí se corrigen: no se ocultan guías simplemente por tocar un obstáculo.

## Evidencia y reproducción

```bash
make test
make editor-test
make visual-long-hair
make visual-morph-ownership
make visual-morph-gpu-regressions
```

`make visual-long-hair` escribe en `after/`, sin sobrescribir la fundación.
Produce seis casos en cuatro etapas, tres cámaras por caso, calidades 0–4 y
tres poses animadas. `captures.json` registra los comandos y el SHA-256 del
ejecutable. `root-stability.json` compara las identidades en las cuatro etapas.
El archivo conserva las calidades 0, 3 y 4 nuevas y la calidad 3 de referencia;
se retiraron PNG intermedios redundantes, no los logs ni los datos de auditoría.
El capturador vuelve a generar también las calidades 1 y 2 al repetir el banco.

`before/` procede del ejecutable anterior, conservado antes de recompilar, y de
las recetas de la fundación. `LONG_HAIR_CAMERA` fija objetivo y altura ortográfica:
las imágenes antes/después usan exactamente la misma cámara, no autoencuadres
distintos. Las comparaciones `*-before-after.png` muestran antes a la izquierda.
La referencia corresponde a `f5f6444dc563dd12cf8151479c6879aecfcb59b7` con
`before/camera-override.patch`: sólo añade el encuadre externo al capturador,
sin modificar pelo ni renderizador. Para repetirla, aplicar ese parche en una
copia de dicha revisión, compilar `build/capture_long_hair` y pasar la ruta
absoluta del `before/<caso>-stage-<etapa>/camera.txt` mediante `LONG_HAIR_CAMERA`.
Las `*-comparison.png` comparan apagado/encendido de la implementación nueva.
Las imágenes `*-q4.png` muestran raíces cian y guías coloreadas por corrección
máxima: verde sin contacto importante, naranja/rojo con corrección mayor.

Selección de capturas reales:

- [Crin equina, frontal antes/después](after/caballo-curve-stage-3-front-before-after.png).
- [Cola equina, tres cuartos antes/después](after/caballo-tail-stage-3-rear-three-quarter-before-after.png).
- [Penacho asnal, lateral antes/después](after/asno-tail-stage-3-side-before-after.png).
- [Barba canina con mandíbula abierta](after/perro-patch-stage-3/pose-2.png).
- [Raíces, guías y correcciones](after/caballo-curve-stage-3/side-q4.png).
- [Seis casos y cuatro etapas](after/stages-three-quarter.png).
- [Tres poses por etapa, barba canina](after/perro-patch-animated-stages.png)
  y [cola equina](after/caballo-tail-animated-stages.png).

La auditoría offline lee centros de los hijos realmente generados en GPU mediante
transform feedback y los contrasta con el SDF exacto de reposo. Es deliberadamente
ajena al bucle normal de dibujo. Se registran también penetración de guías,
residuo del campo posado, error CPU/GPU y deformación relativa máxima de segmentos.
La mandíbula se comprueba también contra su campo exacto articulado en las tres
poses con apertura, no únicamente contra la envolvente de la cabeza.
Los logs conservan los resultados completos, incluidos fallos; no se infiere
ausencia de intersección a partir de una captura bonita.

## Métricas

[Comparación de adultos](after/benchmarks.md), [datos de las cuatro etapas](after/before-after-performance.json)
y [resultados de los cinco comandos de regresión](checks/results.json).

Los cinco comandos finalizaron correctamente. Las 24 combinaciones de caso y
etapa conservan identidades y paridad CPU/GPU; la auditoría registrada no detectó
penetración de centros de hijos en el SDF de reposo, ni en el campo mandibular
articulado, ni residuo de guías en los proxies posados. Esto no convierte los
proxies corporales en una prueba contra la malla corporal deformada completa.

El volumen tiene un coste importante: en el adulto equino, crin/cola pasan de
0,485/0,530 ms GPU a 1,030/1,558 ms. La actualización CPU pasa de
0,159/0,166 ms a 2,862/6,799 ms. El runtime de 96 guías pasa de 32.240 a
69.956 bytes; su buffer GPU permanece en 24.576 bytes. No se alcanzó el objetivo
orientativo de aproximadamente 0,5 ms para la cola llena. Este resultado sirve
para animales destacados, no demuestra escalabilidad a cientos de animales
con este nivel de pelo y colisiones cada cuadro.

Se probaron además crin/cola equinas y barba canina adultas con **MSAA real de
cuatro muestras**, confirmado en los logs `[GPU] ... MSAA=4`. Las tres capturas
y auditorías finalizaron correctamente: [resultados](checks/msaa.json) e imágenes
en `after-msaa/`. La crin y cola costaron 1,905 y 4,199 ms GPU en esa ruta:
la cobertura multisample de una masa amplia no es gratuita. El banco principal
de 24 combinaciones utiliza cero muestras y valida la alternativa sin MSAA.

`after/performance.json` conserva las columnas de los logs:

- `roots`: guías, visibles, construcción ms, error de raíz, bytes runtime y GPU.
  La construcción nueva incluye compilar el campo y vincular superficies móviles;
  la medida antigua sólo cronometraba `LongHairSet_Build()`.
- `off/on`: ms de cuadro, FPS del banco; `on` añade tiempo GPU exclusivo del pelo.
- `pose`: actualización CPU del pelo y del animador, medidas separadamente.
- `render`: guías, hijos, segmentos de cinta.
- `parity`: resultado y error máximo CPU/GPU.
- `morph`: ms del morph y actualización del pelo, sobre 21 pasos.
- `collision_rest`: muestras, penetrantes, profundidad máxima, déficit del tubo
  solicitado y deformación relativa máxima. Un déficit del tubo solicitado no
  implica penetración: el radio enviado a GPU se reduce al espacio disponible.
- `collision_children`: centros GPU examinados y profundidad máxima en el SDF.
- `collision_pose`: residuo máximo de guías contra envolventes posadas.
- `collision_jaw`: centros GPU y penetración máxima en la mandíbula articulada.
- `jaw_roots`: raíces vinculadas a la articulación mandibular.
- `length_error`: deformación relativa máxima de la longitud total de una guía,
  distinta del peor error local de un segmento.

El equipo de referencia usa Radeon Renoir, Mesa 25.2.8 y capturas de 900 × 900.
Estos FPS son de primeros planos aislados: **no representan un ecosistema**.

## Límites que no deben ocultarse

- Las envolventes posadas aproximan la piel; no son el SDF exacto deformado ni
  una prueba exhaustiva contra cada triángulo de mandíbula, pezuña o músculo.
  La auditoría SDF corporal de hijos corresponde al reposo; la mandibular sí
  usa el campo articulado. Los residuos posados y las
  tres vistas son comprobaciones complementarias, no una garantía universal.
- No se resuelve contacto pelo–pelo, viento ni dinámica inercial. Es un peinado
  cinemático con contacto y curvatura, no una simulación completa.
- En contactos imposibles se prioriza no atravesar el campo y se admite cierta
  elongación. El radio de hijos se reduce cerca de obstáculos y raíces.
- Los bordes translúcidos sin MSAA no tienen ordenación perfecta. Los colores
  próximos y el núcleo con profundidad limitan, pero no eliminan, ese compromiso.
- El pase adicional y las colisiones cuestan más que la fundación. Las cifras
  completas y sus diferencias se conservan; no se presenta el coste antiguo
  de aproximadamente 0,5 ms como si siguiera siendo el coste nuevo.

## Archivos de implementación

- `include/LongHair.h`, `src/LongHair.c`: peinado, anclajes, colisiones y datos GPU.
- `src/MonsterVisual.c`, `include/MonsterSDF.h`, `src/MonsterSDF.c`: reconstrucción,
  propiedad del campo mandibular y distancia exacta de su snapshot.
- `include/OpenGLRenderer.h`, `src/OpenGLRenderer.c`, `shaders/long_hair.vert`,
  `shaders/long_hair.frag`: cintas, cobertura y auditoría offline.
  `src/MonsterSurfaceShader.generated.h` se regenera con el generador existente.
- `tests/test_long_hair.c`: colisiones, anclajes, determinismo y recuperación.
- `tools/capture_long_hair.c`, `tools/generate_long_hair_examples.py`,
  `tools/validate_long_hair.py`: recetas, capturas, métricas y comparaciones.
- Los dos README de validación y las evidencias nuevas de este directorio.
