# Brumacerno y Velkar en el ecosistema

## Alcance y diagnóstico

Las recetas 3 y 4, su registro y un primer adaptador visual ya existían en el árbol de trabajo. Se corrigieron sobre esa implementación, conservando la simulación, los recursos, las poblaciones dinámicas, la selección, la cámara, la genética y los controles de tiempo.

La referencia visual de partida tenía cuatro problemas independientes: el animador no recibía desplazamiento ni giro; ojos y mandíbula permanecían en reposo; la escala dependía de la cola y del LOD; y la cadena oral analítica autorizaba puntos distintos del cuello dibujado. Su validador de partida indicó **1,1773 m** de error máximo desde un anclaje oral en reposo. La medida final usa el landmark oral deformado, por lo que no se presenta un factor de mejora numérico entre puntos de referencia distintos. Las variaciones de catálogo también alteraban demasiado la silueta y sustituían ambas paletas por los mismos colores.

## Recetas y anatomía

- **Brumacerno**: tronco profundo, cuello robusto, cabeza bovina ancha y roma, pabellones cortos, dos cuernos simétricos con crecimiento lateral y curvatura posterior, cola corta y dos dedos por pie que forman una pezuña hendida. La longitud vertical de cada miembro se calcula a partir de la altura de su cintura y de su suela; las crías nacen con las cuatro extremidades desarrolladas.
- **Velkar**: cintura estrecha, pecho profundo, extremidades digitígradas, cabeza corta, pabellones anchos y triangulares con cavidad y penachos, pupilas verticales y cola larga. Sus cuatro dedos, perfil felino y pelaje oscuro contrastan con el herbívoro.

La cola conserva detalle local también en LOD medio/lejano. La curva de reposo felina es suave: una curvatura excesiva volvía casi transversal el extremo respecto al barrido SDF longitudinal, adelgazándolo hasta producir islas. Una regresión comprueba por conectividad de triángulos las ocho estaciones caudales en los tres LOD, sin eliminar componentes.

Se reutilizan `AxialBodyUprightTetrapod`, `LimbMammal`, `HeadModule`, `Ornament`, `Tail`, `CreatureRig`, `AnatomyDeformer`, `MonsterAnimation`, `ProceduralAnimator`, `Locomotion` y `AnatomicalReach`. Los presets bovino y felino siguen compartiendo el resolutor cefálico mamífero existente. Se añadieron proporciones independientes de anchura/altura auricular, ranuras de ápice de pinna y dirección/arco lateral de ornamentos; no hay geometría especial en el renderizador por nombre de especie.

El pelaje tiene perfiles regionales de cara, cuello, pecho, tronco, extremidades y dedos. La variación genética conserva la paleta de cada especie. Las tres cubetas morfológicas cambian proporciones en aproximadamente un 3–8 %, en lugar de aplicar las mutaciones extremas del catálogo. `size` cambia escala, `pigment` cambia tono y semilla, `morphology` selecciona proporciones coherentes y `maturation` modifica la edad de madurez. La presentación no consume el PRNG ecológico.

## Desarrollo y geometría compartida

La edad visual se calcula como edad biológica / (`maturityAge` × `genome.maturation`) y alimenta `CreatureRecipe_EvaluateDevelopment`. Los estadios internos iniciales representan mamíferos recién nacidos; nunca semillas o larvas literales. Se conserva la topología de módulos, articulaciones y dedos.

El núcleo compila proporciones cada **0,5 segundos biológicos**, únicamente mientras crece el individuo. La escala corporal avanza con los ticks ecológicos. El render reutiliza una malla adulta por receta, cubeta y LOD: una paleta afín transporta centros y secciones del grafo hasta la anatomía actual. También escala los pivotes auxiliares sin volumen; omitirlos producía un cuello adulto estirado en las crías. No se remalla un animal al crecer o al avanzar su animación. Los ojos y la mandíbula comparten el marco cefálico y conservan el landmark oral durante el crecimiento.

Esta deformación es una aproximación por estaciones al SDF de cada edad; no equivale a remallar un SDF exacto de cada individuo. Las proporciones están muestreadas a 2 Hz biológicos y la escala es continua. Esta cuantización sigue siendo observable en una inspección numérica o con aceleraciones extremas del tiempo.

## Contacto y animación

`EcosystemAnatomy` mantiene un rig, grafo y estado de alcance por individuo, sin mallas ni OpenGL. El mismo cuerpo se utiliza en visual y headless. Su solucionador cervical devuelve la boca real y estados de contacto, bloqueo o imposibilidad. La alimentación y el agua conservan la validación de contacto, orientación, estabilidad y transacción de recursos. El espacio para un nacimiento utiliza el radio neonatal, no el adulto; la comprobación inicial y la huella publicada comparten la misma escala de desarrollo. Los ataques de los cuerpos anatómicos requieren además alcance real hasta la presa; siguen sin conceder nutrición al matar.

El adaptador aplica la postura axial autoritativa **antes de la IK de extremidades**, mediante un punto de extensión genérico de `ProceduralAnimator`. Cambiarla después desplazaba pies que ya estaban resueltos. El desplazamiento real se transforma al marco local; el animador conserva apoyos durante el giro y recentra sus contactos sin integrar un segundo desplazamiento mundial.

El rango articular mamífero es un control fenotípico reutilizable: el felino solicita 1,5 veces la flexión base de hombros y bisagras para agacharse sin levantar los pies. El perfil bovino se registra aparte del cuadrúpedo erecto genérico, conservando la configuración previa de otras recetas.

La marcha pesada y la carrera diagonal del herbívoro contrastan con el acecho y la carrera por pares del felino. La cadencia utiliza tanto velocidad lineal como velocidad de los pies durante el giro. Los pasos se liberan cuando se agota la reserva de alcance derivada de la cadena; sus objetivos también quedan limitados por esa reserva. Las velocidades angulares máximas declarativas son 1 rad/s para el herbívoro y 2 rad/s para el depredador. Se mezclan marcha/carrera, apertura mandibular al comer/beber/atacar, descenso de descanso, respiración y contracurvatura de cola.

La medida oral compara el landmark de la malla compartida transformado por la misma escala/pose cefálica del dibujo con `EcosystemEntity.body.mouth`. Se mide también el residuo **posterior** a todos los cambios de pose, separando los pies plantados de los que vuelan. No se proyectan objetivos imposibles para declararlos alcanzados.

## Caché, render y vida de entidades

La clave de geometría contiene receta, plantilla adulta, cubeta y LOD. La caché tiene 48 entradas y un trabajador de mallado. Los pesos y buffers de geometría pertenecen a la plantilla compartida; por individuo se actualizan la paleta de pose y la superficie. La generación de skinning ya no depende del ID del animal, evitando volver a subir los mismos pesos en cada draw.

Hay tres resoluciones con histéresis y prioridad cercana para selección/captura. Mientras se prepara otro LOD se conserva una plantilla ya residente de la misma anatomía. El arranque muestra una carga explícita hasta tener geometría visible. La ruta CPU de deformación utiliza la misma paleta que compute y sirve de fallback cuando no hay skinning GPU.

Las ranuras se reconcilian por ID estable, semilla y receta. Muertes, reutilización de ranuras y reinicios liberan sus cuerpos privados y conservan sólo las plantillas compartidas. El núcleo nunca posee recursos GL. La velocidad lineal también queda limitada por la curvatura necesaria para llegar al waypoint sin sobrepasarlo. El escenario E conserva su ventaja de escape con velocidades angulares de 1,5 y 1,7 rad/s.

Los radios ecológicos conservadores del escenario son 0,95 y 0,80 unidades, respectivamente; se declaran en datos de especie, separados de la referencia axial visual.

## Archivos principales

| Área | Archivos |
|---|---|
| Recetas y presets | `src/CreatureRecipeBrumacerno.c`, `src/CreatureRecipeVelkar.c`, `src/Head.c`, `src/SurfacePresets.c` |
| Ensamblado y registro | `src/Creature.c`, `src/CreatureRecipes.c`, `include/CreatureRecipes.h`, `include/CreatureRecipe.h`, `include/SurfacePresets.h` |
| Extensiones anatómicas | `include/Head.h`, `include/HeadModule.h`, `src/HeadModule.c`, `include/CreaturePhenotype.h`, `src/CreaturePhenotype.c`, `src/Ornament.c`, `src/LimbMammal.c`, `include/LimbRig.h` |
| Cuerpo oral sin gráficos | `include/EcosystemAnatomy.h`, `src/EcosystemAnatomy.c`, `src/EcosystemWorld.c`, `src/EcosystemScenario.c`, `include/EcosystemWorld.h`, `src/CreatureActions.c` |
| Presentación y genética visible | `include/EcosystemVisual.h`, `src/EcosystemVisual.c`, `include/VisualVariation.h`, `src/VisualVariation.c`, `demos/demo_ecosystem.c` |
| Marcha y orden de IK | `src/Locomotion.c`, `src/GaitPresets.c`, `include/GaitPresets.h`, `src/ProceduralAnimator.c`, `include/ProceduralAnimator.h`, `src/MonsterAnimation.c` |
| CPU/GPU | `src/AnatomyDeformer.c`, `include/AnatomyDeformer.h`, `src/OpenGLRenderer.c`, `include/OpenGLRenderer.h`, `shaders/mesh_skin.comp` |
| Huellas y variantes del catálogo | `src/CreaturePhenotype.c`, `src/CreatureVariation.c` |
| Registro y validación | `Makefile`, registro de recetas, `tests/main_test.c`, `tests/test_ecosystem_creatures.c`, `tests/test_creature_visual_variation.c`, `tools/capture_ecosystem_creatures.py` |
| Documentación | Este informe, `docs/ECOSYSTEM_SIMULATION.md` y evidencias en `docs/validation/ecosystem-creatures/` |

`src/MonsterSurfaceShader.generated.h` se regenera desde el shader fuente mediante el generador del Makefile. Las normales usan la inversa transpuesta de la deformación afín, tanto en CPU como en GPU. Se corrigieron inicializadores de ornamentos del catálogo y la huella fenotípica para evitar bytes de padding y datos indeterminados.

## Reproducción de la validación

```sh
make -B -j6 test demos benchmarks/benchmark_ecosystem
python3 tools/validate_ecosystem.py --output=build/ecosystem-final-validation
make ecosystem-sanitize
./benchmarks/benchmark_ecosystem 100 3600 10000
python3 tools/capture_ecosystem_creatures.py --actions
./demos/demo_ecosystem --frames=900 --validate-mouth=900 \
  --profile=build/ecosystem-reference.json --capture=build/ecosystem-reference.ppm
```

Las capturas usan el ecosistema real y estados realmente observados. `--initial-age` configura edades iniciales para revisar desarrollo; `--capture-species` filtra una especie sin depender de un ID concreto. La captura espera además una malla cercana residente del individuo seleccionado; no acepta un fallback recién solicitado. Beber/comer exige `REACH_REACHED` para disparar la captura; atacar también exige contacto anatómico. `--zoom`, vistas relativas al individuo y `--no-hud` permiten revisar la silueta sin ocultarla con paneles. `captures.json` guarda comandos y resultados; un estado no observado produce un fallo explícito.

La búsqueda de un ID nulo devuelve ausencia, nunca una ranura libre: antes de corregirlo, una captura por especie podía apuntar a una posición vacía en vez del sujeto. La cámara de captura usa también su orientación real.

Las pruebas cubren registro/variación, vida de ranuras, caché y LOD, CPU sin GL, crecimiento neonatal sin picos cervicales, apoyo de crías, alcance al suelo, rechazo de objetivos imposibles y paridad de PRNG, eventos, posiciones y bocas con/sin presentación. La espera asíncrona utiliza un plazo real en vez de contar llamadas a `sched_yield`.

Los resultados medidos y las capturas se encuentran en [validation/ecosystem-creatures](validation/ecosystem-creatures/). La máquina de validación utiliza un AMD Ryzen 5 5600H y una NVIDIA GeForce RTX 3050 Laptop GPU, OpenGL 4.6, a 1280×800. Los perfiles de capturas de un solo frame incluyen compilación/subida inicial y no representan rendimiento estable. Las cifras de población 100 y la referencia se informan por separado. `--validate-skinning` compara mediante readback todos los vértices GPU de cada individuo con su paleta CPU de pose/crecimiento; `--cpu-skinning` permite revisar el fallback completo.
