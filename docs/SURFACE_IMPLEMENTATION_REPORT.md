# Informe de implementación: superficies procedurales

## Resultado

El lagarto animado dispone de escamas procedurales, relieve de normales,
pigmentación, respuesta especular/rugosidad y variación regional. La apariencia
se controla mediante datos independientes de la forma y la pose. La demo
`demo_lizard_surface` permite editarla durante la marcha sin regenerar geometría.

La descripción completa de la API y del shader está en
[PROCEDURAL_SURFACES.md](PROCEDURAL_SURFACES.md).

## Archivos añadidos

| Área | Archivos |
|---|---|
| Modelo de superficie | `include/Surface.h`, `src/Surface.c` |
| Mapeo de reposo | `include/SurfaceMapper.h`, `src/SurfaceMapper.c` |
| Resolver del lagarto | `include/LizardSurface.h`, `src/LizardSurface.c` |
| Programas GLSL | `shaders/monster_mesh.vert`, `shaders/monster_surface.frag` |
| Módulos GLSL | `shaders/surface/hash.glsl`, `scales.glsl`, `bump.glsl`, `pigment.glsl`, `lighting.glsl`, `evaluate.glsl` |
| Empaquetado | `tools/generate_surface_shader.py`, `src/MonsterSurfaceShader.generated.h` |
| Verificación | `tests/test_surface.c`, `tools/validate_surfaces.py` |
| Documentación | Este informe y `docs/PROCEDURAL_SURFACES.md` |

El nuevo ejecutable `demos/demo_lizard_surface` reutiliza el controlador de
`demos/demo_lizard_animation.c`; no duplica la infraestructura de marcha/IK.
Su binario se ignora en Git y lo construye `make demos`.

## Archivos modificados

- `.gitignore`, `Makefile`, `README.md`: binario, dependencias, generación GLSL y documentación.
- `include/Mesh.h`, `src/Mesh.c`: atributos de reposo, receta y preservación durante compactación.
- `include/Monster.h`, `src/Monster.c`: propiedad y setter de apariencia, copia de snapshots.
- `include/Lizard.h`, `src/Lizard.c`: presets, superficie del fenotipo y maduración.
- `src/MonsterAger.c`: interpolación de apariencia y extremos genéricos correctos.
- `include/MonsterVisual.h`, `src/MonsterVisual.c`: mapeo inicial de cuerpo/cabeza/mandíbula/bisagra y actualización exclusiva de recetas.
- `src/MonsterVisualAsync.c`: mapeo del snapshot worker, publicación y edición sin nuevas solicitudes SDF.
- `src/AnimatedVisual.c`: propagación de receta y sincronización de apariencia durante la pose.
- `include/OpenGLRenderer.h`, `src/OpenGLRenderer.c`: programa de malla, buffers GPU, uniforms, diagnóstico y entrada de la demo.
- `demos/demo_lizard_animation.c`: editor de superficies, capturas, controles, comparaciones e invariantes.
- `src/SDFAdaptiveMesher.c`, `tests/test_mesh.c`: inicializadores designados compatibles con los nuevos atributos.
- `tests/main_test.c`: registro de la nueva suite.
- `tests/test_perf_optimizations.c`: barrera de publicación en la prueba de selección de tier asíncrono.

## Arquitectura y diferencias respecto a la propuesta

Se agruparon pigmento, cobertura, perfiles y receta en `Surface.h/.c`, manteniendo
sus tipos independientes. Separar estos tipos pequeños en numerosos archivos no
aportaba una frontera adicional. El mapper y el resolver de especie sí son
módulos separados. El renderer no contiene lógica de lagarto.

El dominio se almacena como `MeshVertex.surface`, con coordenadas y normal de
reposo, dos regiones, peso de mezcla y gradiente ventral. Los IDs son etiquetas
semánticas suministradas por la especie; no se añadieron campos de lagarto al
mapper ni categorías de integumento a `SDFMaterial`.

La receta viaja en la malla, lo que permite conservarla en swaps y snapshots y
usar el callback genérico existente sin modificar su VTable. El tamaño relativo
de escala sustituye el par redundante tamaño/densidad. El gradiente usa diferencias
locales del perfil en lugar de diferencias de altura entre píxeles: las primeras
capturas mostraron punteado con estas últimas y dejaron de mostrarlo al cambiarlas.

## Coordenadas, regiones y animación

La posición material se calcula una vez en reposo, normalizada por la escala
corporal. La normal de reposo fija los pesos triplanares y el vientre. El mapper
compila los segmentos anatómicos y busca proximidad normalizada por radios; mezcla
regiones próximas. Cabeza, dorso, vientre, miembros, cola y dedos tienen perfiles
diferentes. En la mandíbula y bisagra se mapea primero el reposo cerrado en
espacio de criatura y se conserva el resultado en las bases de articulación.

Durante la marcha, el deformador actualiza únicamente posición y normal
geométricas. El hash de los atributos materiales de cuerpo, cabeza, mandíbula y
bisagra permanece idéntico durante movimiento, apertura oral y cambio de receta.
Las mallas heredadas también reciben un dominio de reposo válido al construirse,
por lo que activar posteriormente la superficie no exige reconstruirlas.

## Escamas, normal y LOD

Una retícula alternada y perturbada genera células con identidad entera
reproducible. Sus perfiles combinan domo redondeado, asimetría, surco y quilla.
El shader busca nueve candidatos por proyección, como máximo 27 por fragmento,
y reutiliza la muestra para altura, borde, microcolor y rugosidad.

Las diferencias centradas del perfil reutilizan las dos células encontradas;
no repiten búsquedas Worley. El gradiente material se transforma a la superficie
actual mediante derivadas de posición y coordenadas, sin tangentes UV. El relieve
no modifica silueta ni topología. Cuando el relieve es cero se omiten sus
muestras de gradiente; piel lisa omite las búsquedas celulares.

La huella en células por píxel filtra bordes y reduce detalle entre 0,18 y 0,65.
A partir de 0,65 se usa respuesta media sin células individuales. La microvariación
se atenúa antes que el relieve. El pigmento macroscópico también se filtra.

## Pigmento, individuos y edad

Manchas/bandas suaves y contraste dorsal/ventral se combinan con una variación
celular menor. Pigmento y forma de escama usan semillas independientes.
`LizardSurface_FromSeed` resuelve individuos dentro de rangos acotados; el motor
no necesita un nuevo sistema genético completo para transportar estos controles.
Un futuro genoma puede resolver directamente el mismo `SurfacePhenotype`.

Los presets de un mismo individuo conservan semillas al envejecer; cambian
cobertura, colores, contraste, relieve y quilla. El ager genérico conserva el
estado de activación y el dominio exacto de ambos extremos, con fallback neutro
para endpoints heredados sin fenotipo de superficie.

## Renderer y rendimiento

GLSL 330 compatibility convive con el camino heredado. Los nuevos módulos se
empaquetan durante la compilación y no requieren archivos de textura ni rutas
externas al ejecutar. `Surface_Evaluate` produce `SurfaceResponse`, independiente
de la iluminación. La iluminación inicial combina ambiente, difuso, relleno y
especular regulado por rugosidad; no es un BRDF PBR calibrado.

Medición en AMD Radeon integrada renoir, Mesa 25.2.8, 1100×800, 599.876 vértices,
120 fotogramas estáticos por caso, sin rejilla y con espera explícita a GPU:

| Caso, misma compilación | Deformación CPU media | Dibujo completo medio |
|---|---:|---:|
| Escamas completas | 17,839 ms | 8,835 ms |
| Relieve cero | 17,874 ms | 8,323 ms |
| Piel lisa | 17,967 ms | 7,122 ms |
| Cámara lejana | 17,713 ms | 7,205 ms |
| Camino fijo heredado | 18,099 ms | 16,762 ms |

La diferencia escamas/piel lisa es aproximadamente 1,71 ms de dibujo en esta
vista; retirar el gradiente reduce unos 0,51 ms. Son diferencias del dibujo
completo, no mediciones aisladas del fragment shader. El comparador heredado usa
el layout ampliado de vértices de esta misma compilación, por lo que no representa
por sí solo una comparación exacta contra el binario original.

El uso de buffers GPU fue importante: una primera versión con arrays cliente
costaba aproximadamente 46 ms de dibujo en primer plano; con buffers se midieron
unos 14–15 ms en esa vista. Los datos se transmiten una vez por malla y draw.
La deformación CPU sigue dominando el coste total. La vista estática medida suma
aproximadamente 26,7 ms entre pose, deformación y dibujo; no se garantiza 60 FPS.

`MeshVertex` pasa de 32 a 72 bytes: unos 24 MB adicionales por malla de 600.000
vértices, además de los buffers ya existentes. No hay texturas CPU generadas por
fotograma ni geometría de escamas. Una mejora posterior puede separar atributos
inmutables y posiciones deformadas para reducir transferencias.

## Pruebas y evidencia

La suite nueva comprueba determinismo, semillas independientes, NaN/Inf,
normalización, maduración, identidad regional, orden de nodos, pose real de
marcha, activación tardía, snapshots, setter sin cambios de rig/anatomía, edición
síncrona/asíncrona sin reconstrucción y extremos del ager genérico.

La prueba antigua de selección MORPH/SETTLED ahora espera publicación mediante
`Flush` y comprueba su huella, en lugar de imponer 30 segundos a una operación
instrumentada. Los sanitizadores usan el flag ya existente
`MONSTER_TEST_INSTRUMENTED`, que conserva las comprobaciones geométricas y evita
confundir el tiempo instrumentado con el presupuesto de producción de 15 ms.
La compilación normal sigue comprobando ese presupuesto.

Resultado final: **PASS** en compilación completa forzada (`make -B all`),
suite completa normal, y suite completa con AddressSanitizer +
UndefinedBehaviorSanitizer y detección de fugas. No se reportaron errores de
memoria, comportamiento indefinido ni fugas. La compilación C no emite avisos;
Doxygen genera documentación con avisos de cobertura de comentarios.

La configuración instrumentada utilizada fue `-O1 -g -fsanitize=address,undefined
-fno-omit-frame-pointer -DMONSTER_TEST_INSTRUMENTED`, compilada forzosamente en un
directorio independiente, y `ASAN_OPTIONS=detect_leaks=1` durante la ejecución.

Evidencia visual y logs en `build/surface-validation/`:

- Adulto estático, marcha con semilla fija y edición de individuos durante marcha.
- Mandíbula animada, demo oral heredada, IK y demo de consola.
- Piel lisa, relieve cero, semillas distintas, escamas pequeñas/redondas y grandes/con quilla, rugosidades y nueve diagnósticos.
- Cámara cercana, media y lejana; inspección de normales, altura, regiones y LOD.
- Cinco edades, en MORPH y SETTLED, con 12 vistas por estado.
- Ciclo completo 0→1→0: 405 publicaciones, cero fallos, salto máximo 0,005002.
- Paridad CPU/GPU SDF conservada, error máximo observado 0,000002384.
- Benchmark de 180 fotogramas: cero `malloc/calloc/realloc`, cero `SDF_Build`, cero mallados y cero invalidaciones de huella durante animación.

Las capturas se convirtieron sin pérdida a PNG para reducir su tamaño.
Los logs finales son `final-build.log`, `final-tests.log` y `sanitizers.log`.

El ciclo completo precedió a las últimas correcciones del fallback genérico y
a la optimización aritmética del mapper; las capturas adicionales de cinco edades
verifican los mismos perfiles después de esa optimización. Las correcciones de
fallback no alteran el camino de lagarto ya habilitado.

## Límites y siguientes mejoras

1. El raymarcher GPU SDF conserva su sombreado previo. Reutilizará receta y evaluador cuando proporcione coordenadas de reposo equivalentes; no se introdujo un patrón mundial que nadase durante movimiento.
2. El mapper triplanar puede superponer patrones en cambios de proyección y estirar células donde cambian perfiles. Faltan cartas locales de miembros y orientación longitudinal más precisa.
3. Un cambio de morfología con remallado puede redistribuir células; estabilidad durante pose no implica correspondencia topológica entre criaturas arbitrarias.
4. Interpolar individuos de semillas distintas cambia identidad discretamente en el extremo final. Los presets del mismo individuo no tienen ese salto.
5. Piel neutra es el fallback intermedio de una transición genérica desde/hacia render heredado; no reproduce toda su antigua paleta por vértice.
6. Pelo, plumas y placas reservan tipos, pero aún usan fallback liso. Necesitarán backends que alteren silueta.
7. La iluminación no es PBR y no se ha demostrado ausencia de aliasing para toda combinación arbitraria de parámetros, cámaras y hardware. Las capturas revisadas no muestran el punteado inicial ni un cambio de silueta por escamas.
8. Para mejorar rendimiento: uploads separados de atributos fijos, deformación vectorizada/GPU y tiempos GPU por etapa. Para mejorar calidad: cartas anatómicas, mejores transiciones de proyección y BRDF GGX.

## Respuestas explícitas

| Pregunta | Resultado |
|---|---|
| ¿Cambiar apariencia de escamas reconstruye el SDF? | **NO** |
| ¿Cambiar pigmentación reconstruye el SDF? | **NO**, mediante la API de superficie |
| ¿La marcha cambia las coordenadas procedurales? | **NO** |
| ¿La marcha ejecuta Marching Cubes? | **NO** |
| ¿Los patrones son deterministas desde fenotipo/semilla? | **SÍ** |
