# Hito 2 — Hocico: pigmento localizado y respuesta de piel

## Alcance y arquitectura

Este incremento no añade crin, cola pilosa ni guías de pelo largo. Tampoco
modifica el SDF, los planos orales, los cutters nasales ni la silueta del hito 1.

`PigmentLayer.domain` es opcional en documentos v1. Contiene una estación
anatómica estable (`anchorNode`), centro y semiejes locales, suavidad,
irregularidad determinista, cobertura de pelo, rugosidad y peso de respuesta
cutánea. No se añade región `MUZZLE`, material SDF ni comprobación de especie.
Sin dominio, las capas anteriores conservan su alcance global/regional.

Recorrido:

```text
JSON v1 / controles de pintura
  → PigmentLayer + PigmentDomain
  → estación anatómica estable / envolvente resuelta
  → dominio de reposo de la superficie
  → bancos inmutables de pigmento durante morph
  → color, cobertura de conchas y ribbons, respuesta cutánea
```

Con ancla, centro y radios están expresados en semiejes de la envolvente de
esa estación, sobre los ejes XYZ de reposo. Sin ancla son coordenadas canónicas.
Los tres radios deben ser positivos para activar la localización; un radio cero
conserva la capa global, **no la desactiva**. Para retirar una capa se reduce
`strength` a cero. Un ancla inexistente retira color y material; nunca convierte
la máscara accidentalmente en pigmentación global.

La forma es elipsoidal, con borde cúbico continuo y ondulación sinusoidal
acotada por semilla. No depende de UV ni de índices de triángulos. Las máscaras
regionales existentes siguen multiplicando el dominio, sin frontera nueva entre
cabeza y cuello. `materialStrength=0` permite una marca de color sin modificar
el tegumento. Hay dos capas libres para otras marcas o variantes cromáticas.

La receta GPU pasa de 103 a 139 filas vec4: los índices anteriores no cambian.
Las 36 filas nuevas contienen tres bancos de cuatro dominios, sin arrays de
fibras ni asignaciones persistentes nuevas. Los archivos generados de esquema y
GLSL se regeneran exclusivamente mediante sus scripts.

## Crecimiento, morph y remallado

El descriptor local original permanece en el fenotipo; la apariencia resuelta
se convierte al marco del productor de superficie. La deformación de una pose
no recalcula las coordenadas materiales.

Se detectó y corrigió una diferencia importante: el worker asíncrono puede
transportar vértices juveniles al dominio adulto canónico. En ese caso el cuerpo
debe compilar también sus máscaras en el marco adulto, no usar el centro juvenil
actual. La receta de cuerpo conserva esa procedencia en la fila de metadatos;
las piezas cefálicas/orales que usan coordenadas actuales conservan su receta
actual. Una actualización cromática no solicita otra reconstrucción SDF.

Durante `CreatureMorph` se conservan descriptores anclados homólogos de ambos
extremos y se resuelven contra la anatomía interpolada. Los bancos mantienen sus
semillas y mezclan sus contribuciones, sin interpolar identidades discretas.

## Fenotipos y referencias

- Caballo: capa perinasal marrón grisácea oscura, no negra; labios más oscuros,
  cobertura baja de pelo y piel menos rugosa que el manto. El resto del rostro
  mantiene el color base. El interior nasal conserva su material y color oscuro.
- Asno: manto perinasal claro, con más pelo que el caballo; labios más oscuros.
  Se usa el mismo evaluador, cambiando sólo parámetros de receta.

Se consultaron y observaron las fotografías reales de Tanya Dewey:

- [Purasangres adultos, Michigan, octubre de 2006](https://animaldiversity.org/collections/contributors/tanya_dewey/thoroughbreds/).
  Permiten comparar contraste y extensión de la pigmentación, pero algunos
  individuos tienen marcas blancas y cabezadas; no se copian esas marcas al
  caballo marrón elegido.
- [Cría de asno siciliano, junio de 2001](https://animaldiversity.org/collections/contributors/tanya_dewey/burro/).
  Se observa pelo perinasal claro y borde distal oscuro; no se extrapolan sus
  proporciones juveniles a un adulto.
- [Descripción de Equus asinus, ADW](https://animaldiversity.org/accounts/Equus_asinus/):
  menciona explícitamente el hocico claro sobre manto gris.

Fotografías: Tanya Dewey, Animal Diversity Web, CC BY-NC-SA 3.0. Se inspeccionaron
fuera del repositorio; no se redistribuyen ni se presentan como capturas del
motor. Colores, semiejes y rugosidades del ejemplo son aproximaciones artísticas,
no mediciones veterinarias ni fotometría calibrada.

## Iteración visual

1. `before/`: recetas locales al inicio de este hito, conservando todo el trabajo
   anterior. Hocico y rostro casi uniformes.
2. `iteration-1/`: primer dominio funcional. Aparecieron dos defectos: el aclarado
   ventral lavaba el frente del hocico y la mezcla cutánea podía teñir el interior
   nasal. La respuesta de labios/piel no era continua en todas las piezas.
3. `after/`: los dominios localizados se componen después del campo ventral;
   las narinas y cavidades quedan protegidas y labios/piel comparten la máscara.
   Se añaden capturas del productor asíncrono y cuatro etapas por especie.

Comparaciones adultas en tres cuartos:

| | Apariencia | Pigmento | Máscara | Pelo | Rugosidad |
|---|---|---|---|---|---|
| Caballo | [Antes/después](caballo-appearance-comparison.png) | [Color](caballo-pigment-comparison.png) | [Dominio](caballo-mask-comparison.png) | [Cobertura](caballo-fur-comparison.png) | [Material](caballo-roughness-comparison.png) |
| Asno | [Antes/después](asno-appearance-comparison.png) | [Color](asno-pigment-comparison.png) | [Dominio](asno-mask-comparison.png) | [Cobertura](asno-fur-comparison.png) | [Material](asno-roughness-comparison.png) |

Los modos 29, 30 y 31 muestran respectivamente dominio geométrico sin patrón,
cobertura efectiva de pelo y rugosidad. La máscara no representa una nueva región
anatómica. Las vistas individuales laterales, frontales y tres cuartos están en
`after/`; `captures.json` registra comandos y hash del ejecutable.

## Reproducción

```sh
make test
make editor-test
make visual-equine
make visual-equine-muzzle
make visual-equine-muzzle-editor # Captura opcional; requiere Google Chrome.
make visual-morph-ownership
make visual-morph-gpu-regressions
```

`tools/validate_equine_muzzle.py --all-stages` llama al ejecutable C/OpenGL real.
Pillow sólo convierte PPM a PNG y monta comparaciones; no genera criaturas.
El editor usa geometría C en su preview web; la referencia del material y pelo
son las capturas OpenGL, no esa vista diagnóstica.

Comparación opcional con un snapshot anterior compilado, sin volver a capturar GL:

```sh
python3 tools/validate_equine_muzzle.py --geometry-only --baseline-root /ruta/al/snapshot
```

Se exige geometría idéntica y al menos un cambio de color visible en cada preview.
Esta comprobación detectó y corrigió un segundo recorrido que aún entregaba al
exportador el descriptor local sin resolver, en lugar de la apariencia resuelta.
Los controles se muestran en [editor-controls.png](editor-controls.png). Se abrió
la receta mediante la API pública, con revisión esperada y catálogo temporal
aislado; no se modificó una sesión del usuario. La captura web usa SwiftShader,
no sustituye las imágenes OpenGL/Radeon del material.

## Verificación y rendimiento

Los resultados definitivos, tiempos y tamaños se registran junto al informe en
los logs de pruebas, `after/*/capture.log` y `performance.json`.

| Verificación | Resultado |
|---|---|
| `make test` | PASS, incluidos anclaje, transición continua, poses y marco async |
| `make editor-test` | PASS, 10 tests HTTP/JSON/mallas; incluye color visible y lectura v1 sin dominio |
| `make visual-equine` | PASS, ocho casos de cuatro etapas |
| `make visual-equine-muzzle` | PASS, ocho casos GL, cinco modos y tres vistas; también productor async en etapas 0 y 3 |
| CPU/compute de dominios/material | 196608 muestras, error máximo `2.563e-6`, umbral `3e-5` |
| Paridad SDF compute/fragment/intervalos | PASS: caballo, asno, lagarto, perro, Brumacerno y Velkar |
| Morph ownership con retardo de worker de 3000 ms | PASS; referencia 42878 píxeles, extra 5.5%, faltante 5.1% |
| Morph GPU | PASS; comparación de material RGB entre productores: error 0 |
| Regresión de otras especies | 16 mallas con índices, posiciones, normales y colores idénticos; delta 0 con módulos reordenados |
| Regresión geométrica équida contra inicio de este hito | Ocho mallas con posiciones/normales/índices idénticos; cambia el pigmento, no la anatomía |

La referencia de las otras especies es el binario local anterior, adaptando sólo
el campo opcional `domain` para su lector JSON antiguo. Para los équidos se
recompiló el snapshot exacto guardado al comenzar este hito. No se relajaron las
tolerancias de las regresiones existentes. Se corrigió además una inicialización
incompleta de `PigmentLayer` en variación, que dejaba los campos nuevos sin
inicializar y rompía la huella determinista.

La paridad nueva compara máscara y material CPU/compute con 2048 muestras por
caso: cuatro estados de mezcla y los ocho patrones existentes, incluyendo
regiones mezcladas y semillas de 32 bits. El ruido superficial CPU reproduce el
hash GLSL sin modificar el ruido heredado de recetas/preview anteriores.

El benchmark de dibujo alterna seis pares con dominios activados/desactivados,
descarta el primer par y mide 60 fotogramas por configuración a 1000×900. Incluye
CPU, GPU sincronizada y presentación; **no es tiempo GPU aislado ni FPS de una
escena completa de juego**. La simulación de pelo largo aún no existe y no se
atribuyen costes de guías o fibras nuevas a este incremento.

Hardware: Radeon integrada Renoir, radeonsi/ACO, Mesa 25.2.8, OpenGL 4.6, MSAA 0.

| Caso | Sin dominios | Con dominios | Diferencia | CPU material/muestra |
|---|---:|---:|---:|---:|
| Caballo | 19.411 ms / 51.52 FPS | 19.776 ms / 50.57 FPS | +0.366 ms, +1.88% | 109.63 ns |
| Asno | 19.954 ms / 50.11 FPS | 20.060 ms / 49.85 FPS | +0.105 ms, +0.53% | 93.42 ns |

Son muestras de un encuadre cercano; diferencias tan pequeñas requieren más
repeticiones para caracterizar la variabilidad térmica. No se midió por separado
el coste de resolver el marco canónico durante cada actualización async.

Memoria, bytes antes → después: `SurfacePhenotype` 1792 → 2368,
`SurfaceRecipe` 1660 → 2236, `CreaturePhenotype` 8520 → 9096,
`CreatureRecipe` 37704 → 40008, `Monster` 181296 → 184752.
El incremento de 576 bytes por superficie corresponde a los tres bancos de
descriptores; no hay simulación ni buffers de pelo largo en este hito.

## Archivos del incremento

Lista exacta respecto al árbol local al iniciar este hito, no respecto a HEAD:
[changed-files.json](changed-files.json). El diff aislado está en
[session.patch](session.patch); `initial-status.txt` conserva el inventario de
cambios previos, que no se descartaron.
El inventario de capturas, logs y demás evidencias está en
[artifacts.json](artifacts.json).

- Creados: `shaders/pigment_domain.comp`, `tools/capture_equine_muzzle.c`,
  `tools/validate_equine_muzzle.py`, `tools/capture_equine_muzzle_editor.py` y
  este directorio de evidencias.
- API: `include/{OpenGLRenderer,PigmentPattern,Surface,SurfaceMapper}.h`.
- Núcleo: `src/{Creature,CreatureMorph,CreatureVariation,MonsterVisualAsync,OpenGLRenderer,PigmentPattern,Surface,SurfaceMapper}.c`.
- GLSL existente: `shaders/{fur_guard.vert,fur_guard.frag,monster_mesh.vert,monster_surface.frag,sdf_surface_material.glsl}`
  y `shaders/surface/{pigment,pigment_regions}.glsl`.
- Recetas: `recipes/{caballo,asno}.creature.json`.
- Generadores: `tools/generate_{equine_recipes,recipe_schema,sdf_shader,surface_shader}.py`.
- Editor: `tools/creature_editor/{app.js,test_editor.py}` y `tools/creature_editor_bridge.c`.
- Tests: `tests/{test_surface,test_equine_anatomy}.c`; construcción: `Makefile`.
- Regenerados: `src/CreatureRecipeSchema.inc`, `src/MonsterSDFCompute.generated.h`,
  `src/MonsterSDFShader.generated.h`, `src/MonsterSurfaceShader.generated.h`.

## Límites y trabajo restante

- La geometría heredada aún presenta un extremo del hocico demasiado plano,
  contornos orales angulosos y ojos simplificados en esta herramienta. No se han
  ocultado ni remodelado para hacer pasar la pigmentación por anatomía nueva.
- El dominio es elipsoidal sobre ejes de reposo, no una máscara pintada ni una
  curva facial arbitraria; no hay calibración física de rugosidad.
- La cobertura localizada atenúa conchas y ribbons existentes; no elimina sus
  raíces del buffer ni implementa todavía pelo largo.
- El crecimiento asíncrono se prueba en el marco adulto; las regresiones
  generales de morph no equivalen a aceptación visual de todos los cruces
  posibles con marcas faciales. Nuevos anfitriones sin estación homóloga retiran
  la capa de forma segura.
- Los hitos 3–6 siguen pendientes: guías anatómicas, hijos procedurales,
  dinámica, crin, colas diferenciadas, demostraciones no équidas y LOD de pelo.
  La aceptación anatómica del hito 1 continúa siendo parcial.
