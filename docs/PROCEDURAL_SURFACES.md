# Superficies procedurales

La forma, la pose, la cobertura y el pigmento tienen autoridades independientes.
La primera cobertura implementada es piel lisa o escamas por relieve de normales.
No existen texturas obligatorias, UV dibujadas a mano ni triángulos por escama.

## Datos y responsabilidades

```
semilla individual / controles numéricos
 → LizardSurface_FromSeed
 → LizardPhenotype.surface / Monster.surface
 → SurfaceRecipe_Compile
 → Mesh.surfaceRecipe
 → Surface_Evaluate en módulos GLSL
 → SurfaceResponse → iluminación

AnatomyGraph + SurfaceMapping de la especie
 → SurfaceMapper_MapMesh en reposo
 → MeshVertex.surface (coordenadas, normal, regiones)
 → deformación de posición/normal geométricas
 → shader: dominio de material conservado
```

`SurfacePhenotype` contiene `PigmentPhenotype`, `IntegumentPhenotype` y perfiles
regionales. El pigmento define tres colores, contraste dorsal/ventral, intensidad,
frecuencia, mezcla de manchas/bandas y semilla. `ScalePhenotype` define tamaño,
aspecto, redondez, irregularidad, relieve, profundidad/anchura del surco, quilla,
rugosidad, microvariación y semilla. Tamaño y densidad son recíprocos: no hay dos
controles redundantes que puedan contradecirse.

`SurfaceRecipe` transporta 28 vectores de cuatro floats y dos semillas enteras.
Los valores se empaquetan explícitamente, sin reinterpretar padding de structs.
La compilación normaliza los rangos y sustituye valores no finitos. Los shaders
reciben sólo esa receta. Cada malla conserva su receta: varias criaturas pueden
usar el mismo renderer sin compartir accidentalmente apariencia.

`SDFMaterial` sigue identificando tejidos geométricos. Sólo `SKIN` recibe el
pigmento y las escamas. Boca, labios, órbita, narinas y tímpano conservan su color
anatómico; ojos separados conservan su ruta heredada. No se añadieron especies,
colores, pelo ni plumas al enum SDF.

## Edición sin reconstrucciones

```c
SurfacePhenotype surface = monster.surface;
surface.integument.scales.size = .14f;
surface.pigment.patternStrength = .4f;
Monster_SetSurface(&monster, &surface);
MonsterVisual_SetSurface(&visual, &monster.surface);
```

El setter normaliza y sincroniza el fenotipo de lagarto, cuando existe, sin
resolver anatomía ni reconfigurar el rig. `MonsterVisual_SetSurface` sólo copia
recetas. `MonsterVisual_Update` y `AnimatedVisual_Deform` también actualizan la
receta automáticamente. La ruta asíncrona actualiza apariencia sin encolar trabajo
cuando la geometría mostrada corresponde a la solicitada. Durante una transición
morfológica pendiente se conserva la receta del snapshot publicado.

Las huellas SDF no incluyen estos nuevos controles. La paleta heredada continúa
formando parte de las huellas existentes: editar esa paleta antigua no equivale a
editar `SurfacePhenotype.pigment`. Para las nuevas apariencias se usa el setter.

## Dominio estable y regiones

`SurfaceCoordinate.position` es la posición de reposo menos el origen del dominio,
dividida por `SurfaceMapping.unitScale`. Para el lagarto la unidad es `totalScale`:
las escamas crecen con la escala uniforme del animal. Las proporciones anatómicas
pueden variar por alometría; no se promete conservar cada célula tras un remallado
que cambia la morfología o la topología.

La normal de reposo determina los pesos de proyección triplanar y el gradiente
ventral. La normal de iluminación es la normal geométrica **actual**, deformada.
Separarlas evita que los pesos de proyección cambien al caminar o rotar. La
mandíbula y bisagra se mapean una vez en reposo cerrado y espacio de criatura;
los atributos se guardan en sus bases locales inmutables. La articulación copia
esos atributos y cambia únicamente posición y normal geométricas.

`SurfaceMapping` contiene etiquetas por ID anatómico. La especie decide qué ID
es cabeza, cuello, dorso, miembro anterior/posterior, cola o dedo. El mapper
no conoce IDs de lagarto. Compila segmentos y radios una vez por operación,
clasifica cada vértice por distancia normalizada a segmentos y mezcla las dos
regiones próximas en una banda. Los dedos usan también el tipo semántico de
conexión. Los valores de los perfiles se mezclan en el vertex shader antes de
interpolarlos: nunca se interpola un entero regional y se usa como índice.
La transición dorso/vientre usa el gradiente ventral continuo de reposo.

Este primer mapper usa ejes cartesianos de reposo, con proyección triplanar y
anisotropía regional. No requiere UV ni tangentes, y funciona sobre cualquier
malla. Todavía no construye cartas cilíndricas orientadas individualmente a cada
miembro; las zonas de mezcla triplanar pueden superponer patrones. Las etiquetas
quedan fijas durante deformación y se vuelven a resolver al remallar.

## Escamas, relieve y filtrado

Cada proyección busca las dos células más próximas en nueve candidatos de una
retícula alternada con jitter acotado. Hashes enteros de 32 bits producen
identidad, perturbación y variación reproducibles. La célula no se usa como ruido
crudo: tiene interior redondeado, perfil asimétrico, surco que llega a altura
común en el borde y quilla longitudinal. Redondez, aspecto e irregularidad
modifican el perfil; no modifican la geometría ni la silueta.

Una `ScaleSample` conserva altura, borde, variación, LOD y gradiente. El gradiente
se obtiene mediante diferencias centradas del perfil local reutilizando las dos
células encontradas; **no se repite la búsqueda Worley** para normal, pigmento o
rugosidad. Esto evita el punteado de las diferencias directas de altura entre
bloques de fragmentos 2×2. Las proyecciones mezclan gradientes en espacio de
material; las derivadas de posición y coordenadas llevan el gradiente a la
superficie actual. Se corrige la escala uniforme del dominio y se acotan
pendientes extremas en triángulos vistos casi de canto. No hacen falta tangentes
UV. El gradiente ignora la derivada del cambio de pesos triplanares y perfiles,
para no convertir una transición de material en una costura de relieve.

Las derivadas de coordenadas estiman la huella en células por píxel. El surco se
filtra según esa huella. Entre 0,18 y 0,65 células por píxel se atenúan relieve y
microvariación; por encima de 0,65 se omite la búsqueda de células y queda la
respuesta media. La microvariación se apaga antes que el relieve. La piel lisa
omite todas las búsquedas celulares. Manchas y bandas se atenúan también cuando
su frecuencia macroscópica resulta subpíxel. No se mantiene ruido de alta
frecuencia a distancia arbitraria.

`SurfaceResponse` separa albedo, normal, rugosidad y especular de la iluminación.
La respuesta inicial combina ambiente, difuso, luz de relleno y un especular
moderado dependiente de rugosidad. No pretende ser un BRDF PBR calibrado.

## Individuos y crecimiento

No existe un `CreatureGenome` general maduro en este repositorio. Se añadió
`LizardSurface_FromSeed(seed, maturity)` como resolver compatible, sin reescribir
la genética ni la morfología. Un futuro genoma puede suministrar directamente
los mismos controles y semillas. Los rangos aleatorios producen verdes/olivas/
ocres, vientres cálidos y escalas de tamaño acotado; no RGB arbitrario.

Los presets juvenil y adulto comparten semillas y varían cobertura, contraste y
relieve. La larva empieza sin cobertura de escamas y con pigmento claro.
`LizardPhenotype_Interpolate` interpola los controles de superficie junto a la
maduración; piel↔escamas usa una cobertura continua. Las semillas no se interpolan
como floats. Si se interpolan **individuos de semillas diferentes**, se mantiene
la semilla inicial hasta el extremo final, donde cambia a la del destino. Ese
cambio discreto no aparece al envejecer los presets del mismo individuo. En el
ager genérico se preservan activación y mapeo exactos de ambos extremos; origen
y unidad del dominio se interpolan. Las etiquetas discretas se seleccionan del
extremo predominante. Un extremo sin superficie aporta piel neutra para los
estados intermedios: no se intenta reconstruir su antigua paleta por vértice.

## Backend y extensibilidad

Los módulos GLSL viven en `shaders/surface/`; un generador Python estándar resuelve
sus includes y genera `MonsterSurfaceShader.generated.h`. El Makefile regenera el
header al editar cualquier módulo. No hay rutas de shaders ni imágenes que
resolver al ejecutar un binario desde otro directorio.

La ruta programable usa GLSL 330 compatibility y buffers OpenGL para transmitir
atributos e índices. Convive con el render fijo de geometría sin `hasSurface`.
La demo exige compilación GLSL correcta y comprueba errores GL; los consumidores
heredados pueden usar fallback con diagnóstico si el programa no está disponible.
SDL/GL continúa confinado a `src/OpenGLRenderer.c`.

La primera integración visual es la **malla animada**. El raymarcher GPU SDF
conserva su sombreado anterior; no se le añadió una evaluación en coordenadas
mundiales que nadase sobre el animal. `SurfaceRecipe` y los módulos de evaluación
no dependen de la malla y podrán reutilizarse cuando el ray hit proporcione un
dominio de reposo equivalente. `demo_ager_3d` usa por defecto la malla con escamas durante el crecimiento
y gira la cámara automáticamente; `R` pausa el giro. `--mesh` sigue siendo válido
y `--sdf` permite seleccionar el raymarcher anterior. Las vistas de cabeza y
las capturas anatómicas permanecen fijas. Para capturar doce ángulos del giro:
`demo_ager_3d 1 --turntable --capture-prefix=/tmp/ager`.

`FUR`, `FEATHERS` y `PLATES` reservan tipos de cobertura; todavía usan fallback
liso. Pelo largo y plumas necesitarán un backend de strands/cards/instancias que
modifique silueta. No se intentará representarlos sólo mediante normales. La
altura de escala queda disponible para un futuro backend de desplazamiento.

## Demo y validación reproducible

```
make demos
./demos/demo_lizard_surface
./demos/demo_lizard_surface --mode=jaw --seed=5 --size=.18 --keel=.7
./demos/demo_lizard_surface --smooth --mode=static
python3 tools/validate_surfaces.py
```

Controles: ←/→ seleccionan parámetro; ↑/↓ o +/− cambian su valor; R cambia
individuo; C cambia sólo pigmentación; S alterna piel/escamas; Tab cambia
visualización; espacio pausa; 1..4 cambian vista; D muestra el rig. El terminal
indica parámetro y valor. Opciones adicionales: `--roundness`, `--aspect`,
`--relief`, `--roughness`, `--age`, `--zoom`, `--frames`, `--capture`,
`--capture-prefix`, `--no-grid`, `--edit-sweep` y `--legacy` para comparación.

Diagnósticos 1..9: perfiles/regiones, coordenadas, identidad celular, altura,
surcos, macro pigmento, normal geométrica, normal perturbada y LOD. El modo de
regiones representa modificadores mezclados, no una paleta categórica de IDs.
El hash de atributos del demo abarca cuerpo, cabeza, mandíbula y bisagra; verifica
que no cambian durante marcha ni edición de apariencia.

`tests/test_surface.c` cubre determinismo, parámetros no finitos, maduración,
regiones y orden del grafo, marcha real del rig con dominio inmutable, snapshots,
y edición síncrona/asíncrona sin reconstrucción. El script de validación captura
adulto, marcha, mandíbula, juvenil, semillas, tamaños, quilla, redondez,
rugosidad, piel lisa, cámaras y modos de diagnóstico. Los tiempos de dibujo
incluyen envío, rasterizado y espera GPU; no deben confundirse con coste puro del
fragment shader. La deformación se mide por separado.

La prueba heredada de selección de calidad asíncrona usa ahora `Flush` y comprueba
la huella publicada en lugar de imponer un timeout de 30 segundos. Su propósito
es validar el tier, no el rendimiento de una máquina bajo sanitizadores.
