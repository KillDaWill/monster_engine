# Monster Engine (C Core)

[![Language: C](https://img.shields.io/badge/Language-C99%20%2F%20C11-00599C.svg?logo=c)](https://en.wikipedia.org/wiki/C_(programming_language))
[![Compiler: GCC / Clang](https://img.shields.io/badge/Compiler-GCC%20%7C%20Clang-orange.svg)](Makefile)
[![Flags: -Wall -Wextra -Wpedantic -O2](https://img.shields.io/badge/Flags--Wall%20-Wextra%20-Wpedantic%20-O2-blue.svg)](Makefile)
[![Dependencies: Pure C / Zero external](https://img.shields.io/badge/Dependencies-Zero%20(Pure%20C%20%2B%20libm)-brightgreen.svg)](AGENTS.md)
[![Platform: Linux](https://img.shields.io/badge/Platform-Linux-FCC624.svg?logo=linux&logoColor=black)](README.md)
[![Graphics: OpenGL 4.6 + GLSL Compute / SDL2](https://img.shields.io/badge/Graphics-OpenGL%204.6%20%2B%20GLSL%20Compute%20%2F%20SDL2-5586A4.svg?logo=opengl)](shaders/)
[![Tests: 100% Passing](https://img.shields.io/badge/Tests-100%25%20Passing-success.svg)](tests/)

Motor de simulación procedural de monstruos desacoplado, modular y **agnóstico de cualquier librería de renderizado**, desarrollado en C idiomático con aceleración por Compute Shaders en GPU, metamorfosis ontogénica, inteligencia artificial animal continua y simulación de ecosistemas con genética heredable.

## 🚀 Arquitectura General

El motor está compuesto por los siguientes módulos principales:

- **`MathUtils` (`include/MathUtils.h`)**: Utilidades matemáticas escalares genéricas (clamp, lerp, grados/radianes) y cálculo seguro de capacidades para arreglos dinámicos.
- **`Vector` (`include/Vector.h`)**: Álgebra vectorial para dimensiones fijas (`Vector2`, `Vector3`, `Vector4`) y N-dimensionales (`VectorND`).
- **`Transform3D` (`include/Transform3D.h`)**: Transformaciones 3D (posición, rotación Euler y escala) con conversión entre espacio local y mundo.
- **`AABB` (`include/AABB.h`)**: Bounding Boxes 3D alineadas a los ejes (AABB) y utilidades de expansión/margen.
- **`Color` & `ColorPalette` (`include/Color.h`, `include/ColorPalette.h`)**: Gestión de espacio de colores RGBA/HSV, gradientes dinámicos y muestreo continuo.
- **`BodyPart` (`include/BodyPart.h`)**: Definición anatómica de segmentos corporales, cajas de colisión y lista dinámica de traits.
- **`Head` (`include/Head.h`)**: Fenotipo semántico, resolver de landmarks y receta superficial compartida por cabezas de lagarto, cánido y ave. `Mouth` es su subsistema oral; `Eye` continúa siendo geometría poseable separada.
- **`Eye` & `Mouth` (`include/Eye.h`, `include/Mouth.h`)**: Puente visual de ojos y subsistema oral articulado, respectivamente.
- **`Trait`, `VisualTrait` & `MonsterBehavior` (`include/Trait.h`, `include/VisualTrait.h`, `include/MonsterBehavior.h`)**: Rasgos polimórficos (comportamiento, combate y visuales) y controladores de comportamiento.
- **`Monster` (`include/Monster.h`)**: Entidad principal que engloba paleta cromática, partes corporales, ojos, bocas, traits polimórficos y física.
- **`MonsterQueries` (`include/MonsterQueries.h`)**: Funciones de consulta pura (no mutadoras) sobre la entidad: centros de masa, direcciones, anchos/altos interpolados y caja envolvente.
- **`MonsterAger` (`include/MonsterAger.h`)**: Módulo de interpolación continua entre fases ontogénicas (envejecimiento/evolución de un monstruo).
- **`WorldInterface` (`include/WorldInterface.h`)**: Interfaz agnóstica de consulta del mundo/entorno (altura del terreno).

### 🧬 Arquitectura Fenotípica y Criaturas Modulares

- **`Creature` & `CreatureRecipes` (`include/Creature.h`, `include/CreatureRecipes.h`, `include/CreaturePhenotype.h`)**: Definición fenotípica modular de criaturas y arquetipos (cánidos, reptiles cuadrúpedos, ápodos, etc.), grafos anatómicos procedurales y recetas ontogénicas.
- **`Anatomy` & `AnatomyDeformer` (`include/Anatomy.h`, `include/AnatomyDeformer.h`)**: Grafo anatómico con relaciones jerárquicas, conectores, anclaje continuo de vértices y deformación esquelética asistida por SDF.
- **`RigBuilder` (`include/RigBuilder.h`)**: Construcción procedural de esqueletos y cinemática para miembros axiales, locomoción y apéndices articulados.

### 🔄 Metamorfosis Dinámica y Transferencia Geométrica (Morph Pipeline)

- **`CreatureMorph` (`include/CreatureMorph.h`)**: Coordinador de metamorfosis continua entre recetas y arquetipos biológicamente dispares.
- **`MorphHandoff` (`include/MorphHandoff.h`)**: Continuidad geométrica y transferencia suave de apéndices/ramas con conservación volumétrica y uniones asintóticas.
- **`MorphSpatialCache` (`include/MorphSpatialCache.h`)**: Estructura espacial dispersa de bloques 3D (*sparse brick cache*) con seguimiento de AABBs sucias para actualización incremental del SDF.
- **`MorphChunkManager` & `MeshPatch` (`include/MorphChunkManager.h`, `include/MeshPatch.h`)**: Remallado adaptativo y localizado por sub-mallas/chunks independientes, minimizando el impacto de reconstrucción global.
- **`MorphSurfaceTracker` & `MorphSurfacePatcher` (`include/MorphSurfaceTracker.h`, `include/MorphSurfacePatcher.h`)**: Proyección, persistencia y deformación de campos superficiales (escamas, queratina y pigmentos) sobre mallas en transformación.

### 🧊 Pipeline SDF (Mallado Implícito)

La geometría de los monstruos se define mediante **Signed Distance Fields (SDF)** y se convierte en malla poligonal a través del siguiente pipeline:

1. **`SDFPrimitives` (`include/SDFPrimitives.h`)**: Primitivas geométricas continuas (esfera, elipsoide, cápsula, cápsula cónica o elíptica ahusada y caja 3D).
2. **`SDFOperations` (`include/SDFOperations.h`)**: Operaciones CSG booleanas y mezclas suaves (Smooth Union / Subtract) con materiales.
3. **`SDFInterval` (`include/SDFInterval.h`)**: Aritmética de intervalos analítica sobre campos SDF para poda jerárquica de espacio vacío/sólido.
4. **`SDFSampling` (`include/SDFSampling.h`)**: Muestreo del campo SDF y estimación de normales por diferencias finitas.
5. **`MonsterSDF` (`include/MonsterSDF.h`)**: Representación geométrica implícita compilada de un monstruo (snapshot desacoplado e independiente tras `Build`).
6. **`MarchingCubes` (`include/MarchingCubes.h`, `include/MarchingCubesTables.h`)**: Intérprete de topología: máscara de aristas cortadas y filas canónicas de triángulos (256 casos).
7. **`SDFMesher` & `SDFAdaptiveMesher` (`include/SDFMesher.h`, `include/SDFAdaptiveMesher.h`)**: Orquestadores de triangulación: convierten el campo SDF en malla poligonal con caché de aristas y resolución adaptable.
8. **`Mesh` & `PrimitiveMesh` (`include/Mesh.h`, `include/PrimitiveMesh.h`)**: Estructura de datos genérica de mallas 3D indexadas (vértices + índices) y generador de primitivas.
9. **`MonsterVisual` & `MonsterVisualAsync` (`include/MonsterVisual.h`, `include/MonsterVisualAsync.h`)**: Síntesis visual y canal desacoplado con workers multihilo en segundo plano (niveles de detalle interactivo vs estabilizado).

### 🎨 Superficies Procedurales y Pelaje

- **`Surface` & `SurfaceMapper` (`include/Surface.h`, `include/SurfaceMapper.h`)**: Síntesis procedural de escamas, pigmentación y rugosidad desacoplada de la topología geométrica subyacente.
- **`Fur` (`include/Fur.h`)**: Densidad, orientación y capas de pelaje procedural (guard hairs, underfur) con shaders dedicados.

### 🖥️ Renderizado y Aceleración por GPU

- **`RenderInterfaces` & `OpenGLRenderer` (`include/RenderInterfaces.h`, `include/OpenGLRenderer.h`)**: Arquitectura agnóstica de dibujado (`MonsterRenderer` VTable y `ICamera`) implementada actualmente mediante OpenGL y SDL2 (renderizado atómico de triángulos, modo wireframe y shaders de superficie).
- **`GpuSDFSurfaceCache` (`include/GpuSDFSurfaceCache.h`)**: Evaluación por compute shaders (`GLSL`) de campos SDF dispersos y extracción acelerada de superficies.

### 🧠 Inteligencia Artificial Modular, Navegación y Conducta Animal

- **`CreatureMind` (`include/CreatureMind.h`)**: Mente modular desacoplada de la anatomía física; coordina necesidades biológicas, percepción sensorial, memoria, selección de metas y control motor continuo.
- **`BrainProfile` (`include/BrainProfile.h`)**: Perfiles de personalidad inmutables y reutilizables (`balanced`, `social`, `aggressive`, `solitary`, `waterless`).
- **`CreatureNeeds` (`include/CreatureNeeds.h`)**: Integración dinámica de urgencias fisiológicas (sed, hambre, energía, compañía) con curvas de satisfacción.
- **`AIWorld` (`include/AIWorld.h`)**: Registro espacial agnóstico de entidades, visibilidad geométrica, recursos (estanque de agua, comida) y contención de interacción.
- **`CreaturePerception` (`include/CreaturePerception.h`)**: Campo de visión cónico y memoria individual de recursos conocidos con tiempo de expiración.
- **`CreatureDecision` (`include/CreatureDecision.h`)**: Selección de metas por función de utilidad con compromiso mínimo para suprimir la oscilación de decisiones.
- **`CreatureNavigation` (`include/CreatureNavigation.h`)**: Navegación en plano XZ mediante búsqueda A*, reutilización de trayectorias, llegada suave y detección de estancamiento.
- **`CreatureMotor` (`include/CreatureMotor.h`)**: Conversión continua de intenciones a rumbo angular, aceleración y maniobras de marcha atrás.
- **`CreatureActions` (`include/CreatureActions.h`)**: Máquina de estados persistente para búsqueda, aproximación, bebida en orilla, descanso y evasión.
- **`AnatomicalReach` (`include/AnatomicalReach.h`)**: Cinemática inversa cervical (CCD acotado) que orienta el landmark oral exacto al recurso sin desplazar los apoyos plantares ni el tórax.
- **`CreatureBoundary` (`include/CreatureBoundary.h`)**: Contención y colisión volumétrica continua contra los límites del escenario y obstáculos.

### 🌿 Simulación de Ecosistemas, Genética y Fisiología

- **`EcosystemWorld` (`include/EcosystemWorld.h`)**: Gestor global del ecosistema desacoplado de renderizado; administra ciclos de vida, múltiples especies (herbívoros, carnívoros, carroñeros), natalidad, mortalidad, biomasa y dinámica poblacional.
- **`CreatureGenome` (`include/CreatureGenome.h`)**: Genoma cuantitativo con 7 rasgos fenotípicos heredables (tamaño, velocidad, eficiencia metabólica, fertilidad, agresividad, percepción, longevidad), recombinación sexual biparental y mutaciones estocásticas acotadas.
- **`CreaturePhysiology` (`include/CreaturePhysiology.h`)**: Estado metabólico dinámico (reservas de energía y agua, fatiga, salud, tasa metabólica basal) y transiciones ontogénicas (infancia, madurez, senescencia, muerte por inanición/edad).
- **`CreatureReproduction` (`include/CreatureReproduction.h`)**: Mecánica de cortejo, evaluación de receptividad, compatibilidad genética, gestación interna y parto con herencia alélica.
- **`EcosystemResources` (`include/EcosystemResources.h`)**: Fuentes dinámicas de nutrientes: vegetación regenerativa, carcasas de animales muertos con decadencia temporal y fuentes hídricas permanentes.
- **`EcosystemScenario` (`include/EcosystemScenario.h`)**: Presets reproducibles de condiciones iniciales y balances ecológicos (A–E: equilibrio biológico, depredador-presa, sequía, sobrepoblación, convivencia).
- **`SimulationClock` (`include/SimulationClock.h`)**: Control temporal autoritativo con ticks discretos de física/biología (1/60s), acumulación de tiempo real y ciclo diurno/nocturno.


---

## 🛠️ Compilación y Uso con `Makefile`

El proyecto incluye un `Makefile` listo para construir el motor, las ejecuciones de prueba, los visores 3D y la documentación.

```bash
# Compilar todo (pruebas, ejecutable principal, demos y documentación Doxygen)
make all

# Compilar y ejecutar la suite de pruebas unitarias
make test

# Compilar únicamente las demos de la carpeta demos/
make demos

# Generar la documentación HTML en doc/html
make docs

# Limpiar objetos y binarios generados
make clean
```

---

## 💻 Visor Principal y Demos

1. **Visor 3D Interactivo (`lizard_viewer`, `src/main_lizard_viewer.c`)**:
   Visor SDL2 + OpenGL del pipeline SDF que renderiza el lagarto con ojos sintetizados (mallado por voxel size).
   ```bash
   ./lizard_viewer
   ```

2. **Prueba Lógica e Impresión de Datos (`demos/demo_lizard_console`)**:
   Construye un lagarto y muestra su anatomía, paleta y centros por consola.
   ```bash
   ./demos/demo_lizard_console
   ```

3. **Visor 3D Interactivo de Envejecimiento (`demos/demo_ager_3d`)**:
   Muestra la animación en tiempo real del lagarto evolucionando de juvenil a adulto. Ambos extremos comparten una cadena axial coherente y resuelven cráneo, rostrum, órbitas, narinas y mandíbula desde `HeadPhenotype`.
   - **Flecha DERECHA / ARRIBA**: Avanzar porcentaje de edad.
   - **Flecha IZQUIERDA / ABAJO**: Retroceder porcentaje de edad.
   La ruta predeterminada muestra las escamas procedurales mientras la cámara gira y la criatura crece. `--sdf` selecciona el raymarcher anterior; `--validate-gpu` y `--benchmark=N` también seleccionan esa ruta.
   - **R**: Pausar/reanudar el giro de cámara.
   - **ESPACIO**: Activar/Desactivar oscilación automática.
   ```bash
   ./demos/demo_ager_3d
   ```

4. **Animación anatómica de mandíbula (`demos/demo_mouth_animation`)**:
   Presenta un estudio cercano de cabeza, cuello y hombros de lagarto con ojos embebidos, rostrum largo, narinas sustractivas, mandíbula articulada y transición de tejido blando.
   `ESPACIO` alterna la animación, las flechas ajustan `openFactor` y `R` reinicia.

5. **Metamorfosis Extrema Inter-Especies (`demos/demo_morph_dog_lizard`)**:
   Prueba el `MonsterAger` al extremo conectando dos especies biológicamente dispares: un Perro digitígrado con pelaje (`CreatureRecipes_Dog()`) y un Lagarto de andadura baja con escamas (`CreatureRecipes_Lizard()`).
   El arranque normal y `--sdf` usan `sdf-raymarch`: trazado GPU por píxel del campo actual, sin extracción de una rejilla volumétrica. `--surface-mode=exact-sdf` activa el trazado de referencia para diagnóstico. `gpu-sdf-dense`, `gpu-sdf-sparse` y `legacy-handoff` requieren selección explícita con `--surface-mode=...`.
   - El pelaje canino se acorta y reduce su densidad hasta desaparecer en la primera mitad (0% a 45%).
   - Se atraviesa una fase de piel desnuda intermedia (45% a 55%) donde se repliegan las orejas, el tórax se aplana y las patas se abren hacia los lados (*sprawl*).
   - El Ager interpola anatomía resuelta mediante IDs semánticos; las ramas ausentes se contraen hacia su ancestro compartido. El SDF interactivo utiliza la edad actual; `--legacy-lag` conserva el control experimental por desfase de malla.
   - En la segunda mitad (55% a 100%), brotan las escamas reptilianas ganando relieve y quilla junto con la paleta de pigmentos esmeralda y amarilla.
   - **ESPACIO**: Alternar metamorfosis automática (oscilación fluida de ida y vuelta).
   - **Flechas DERECHA / IZQUIERDA**: Control manual fino del porcentaje de metamorfosis.
   - **0, 1, 2, 3, 4**: Atajos a etapas clave (0%: Perro con pelo, 25%: Adelgazamiento, 50%: Piel desnuda, 75%: Escamas incipientes, 100%: Lagarto).
   - **F1..F5**: Vistas de cámara (Lateral, Frontal, 3/4, Primer plano cabeza, Cola).
   - **R**: Pausar / reanudar giro de cámara.
   - **A**: Alternar overlay de esqueleto anatómico.
   ```bash
   ./demos/demo_morph_dog_lizard
   # Captura estable y ciclo instrumentado.
   ./demos/demo_morph_dog_lizard --age=0.5 --view=3 --capture-prefix=/tmp/morph-mitad
   ./demos/demo_morph_dog_lizard --validate-cycle=/tmp/morph-ciclo
   ```
   El [informe de metamorfosis](docs/AGER_MORPH_REPORT.md) describe la implementación, validación y límites de correspondencia.

6. **Estudio Isométrico con IA y Navegación (`demos/demo_game_scene`)**:
   Simulación 3D continua donde un Cánido (`CreatureRecipes_Dog`) y un Lagarto (`CreatureRecipes_Lizard`) conviven en una plataforma con un estanque de agua central, gobernados por el núcleo de inteligencia artificial animal desacoplado:
   - **Ciclo Fisiológico y Sed**: Las criaturas exploran el entorno, perciben visualmente el estanque y, al incrementarse su urgencia de hidratación, planifican su trayectoria y se desplazan hacia la orilla.
   - **Biomecánica Oral Cervical**: Al alcanzar la orilla, la cabeza desciende mediante cinemática inversa anatómica (`AnatomicalReach`), alineando el morro al agua sin desplazar los apoyos plantares ni fracturar el tórax, finalizando con un retroceso de despeje.
   - **Metamorfosis Ontogénica en Tiempo Real (Tecla T)**: El lagarto muta dinámicamente entre cualquiera de sus 10 variantes fenotípicas mediante `MonsterAger`, actualizando en caliente campos SDF, escamas y pigmentos.
   - **Controles Interactivos**:
     - **ESPACIO**: Pausar / reanudar simulación.
     - **T**: Mutación aleatoria del lagarto a otra variante anatómica (`MonsterAger`).
     - **F**: Activar / desactivar pelaje procedural del cánido.
     - **B**: Alternar visualización de cajas envolventes de colisión (`CreatureBoundary`).
     - **1, 2, 3, 4**: Orientaciones axiales de cámara isométrica (45°, 135°, 225°, 315°).
     - **Flechas IZQUIERDA / DERECHA**: Rotación continua orbital de la cámara.
     - **Flechas ARRIBA / ABAJO (+ / -)**: Zoom ortogonal (acercar / alejar).
     - **R**: Reiniciar posiciones de las criaturas y resincronizar mentes.
   ```bash
   ./demos/demo_game_scene
   # Simulación con perfiles específicos de personalidad:
   ./demos/demo_game_scene --dog-brain=social --lizard-brain=waterless
   # Validación headless determinista:
   ./demos/demo_game_scene --headless --validate-ai --seed=42 --fixed-dt=.016666667 --frames=1800 --cycles=0
   ```

7. **Simulación de Ecosistema Completo y Vida Artificial (`demos/demo_ecosystem`)**:
   Simulador de vida artificial continuo donde decenas de criaturas cohabitan, buscan alimento, se reproducen, transmiten su genoma y mueren por causas fisiológicas:
   - **Dinámica Poblacional y Genética**: Selección de presas/depredadores, descomposición de biomasa, linajes evolutivos y herencia de rasgos cuantitativos (`CreatureGenome`).
   - **Controles Interactivos**:
     - **ESPACIO**: Pausar / reanudar la simulación del ecosistema.
     - **1, 2, 3, 4**: Velocidad del tiempo ecológico (1x, 2x, 4x, 16x).
     - **TAB**: Ciclar e inspeccionar criaturas vivas.
     - **F**: Activar / desactivar modo seguimiento de cámara sobre la criatura seleccionada.
     - **W, A, S, D**: Desplazar el centro de la cámara por el terreno.
     - **Flechas IZQUIERDA / DERECHA**: Rotación orbital de la cámara.
     - **Flechas ARRIBA / ABAJO (+ / -)**: Zoom ortogonal (acercar / alejar).
     - **B**: Alternar líneas de depuración (visión cónica, objetivos y cinemática oral).
     - **R**: Reiniciar escenario ecológico con sus condiciones iniciales.
   ```bash
   # Visor 3D interactivo con OpenGL y HUD:
   ./demos/demo_ecosystem
   # Ejecución con escenario específico (A..E) y velocidad inicial:
   ./demos/demo_ecosystem --scenario=B --speed=4
   # Modo headless de máximo rendimiento para experimentos:
   ./demos/demo_ecosystem_headless --scenario=B --ticks=3600
   # Validación automatizada de estabilidad biológica:
   python3 tools/validate_ecosystem.py
   ```

---

## ⚡ Benchmarks de Rendimiento

El motor cuenta con bancos de pruebas y herramientas de perfilado sin dependencias gráficas para medir el rendimiento de los subsistemas críticos:

```bash
# Benchmark de evaluación implícita y mallado SDF (CPU)
make benchmark

# Benchmark de inteligencia animal pura (percepción, utilidad, A* y motricidad)
make benchmark-ai

# Benchmark de alcance anatómico cervical y bebida orillera
make benchmark-interactions

# Benchmark de rendimiento y dinámica poblacional de ecosistema masivo
make benchmarks/benchmark_ecosystem
./benchmarks/benchmark_ecosystem 100 3600 10000

# Benchmark de memoria y coste de animación procedural
make benchmark-animation

# Benchmark de coherencia temporal y deformación en tiempo real (MonsterAger)
make benchmark-ager-realtime
```

---

## 🧪 Pruebas Unitarias

La suite de pruebas unitarias (`tests/test_*.c`) cubre colores, álgebra vectorial, partes del cuerpo, entidad monstruo, envejecimiento, pipeline SDF, anatomía oral, cinemática inversa (IK), esqueletos, navegación A*, perfiles de cerebro, necesidades, percepción, acciones persistentes, shaders, tablas de Marching Cubes y el pipeline completo (SDF → Mesh → Visual). Se ejecuta sin requerir contexto OpenGL ni librerías de ventana:

```bash
make test
```

---

## 📁 Estructura del Repositorio

```text
monster_engine/
├── include/       # Cabeceras públicas (API en C puro, anotaciones Doxygen completas)
├── src/           # Núcleo procedimental puro desacoplado (solo OpenGLRenderer.c enlaza GL)
├── shaders/       # GLSL Shaders (Compute .comp, Vertex .vert, Fragment .frag) para SDF y pelaje
├── demos/         # Demostraciones interactivas 3D y por consola (demo_game_scene, morph, etc.)
├── tests/         # Suite exhaustiva de pruebas unitarias (100% C puro, sin GL, solo -lm)
├── benchmarks/    # Benchmarks de rendimiento en CPU y GPU (AI, SDF, Animación, Cinemática)
├── tools/         # Generadores de shaders (.generated.h), scripts de validación y profiling
└── docs/          # Informes técnicos exhaustivos, arquitectura e investigaciones empíricas
```

---

## 📖 Documentación Doxygen para Desarrolladores

Toda la base de código utiliza anotaciones en formato Doxygen (`@file`, `@brief`, `@param`, `@return`, `@struct`).

Para actualizar o consultar la documentación HTML:
1. Ejecuta `make docs` o `doxygen Doxyfile`.
2. Abre `doc/html/index.html` en el navegador.

---

## 📚 Documentación Técnica e Informes de Arquitectura

El directorio `docs/` incluye informes exhaustivos de diseño, optimización y validación del motor:

- **Inteligencia Artificial y Conducta Animal**:
  - [Arquitectura de IA Modular](docs/CREATURE_AI_ARCHITECTURE.md): Personalidades (`BrainProfile`), estados de mente (`CreatureMind`), necesidades y máquina de estados.
  - [Interacciones Orales y Límites Anatómicos](docs/ANATOMICAL_INTERACTIONS.md): Cinemática cervical asistida (`AnatomicalReach`), aproximación y bebida funcional en orilla.
  - [Contención y Navegación de Escena](docs/GAME_SCENE_BOUNDARIES.md): Volúmenes de contención (`CreatureBoundary`), evitación de obstáculos y navegación A*.
  - [Frecuencia de Ager y Navegación](docs/GAME_SCENE_AGER_PACING.md): Pacing temporal, presupuesto por frame y desacoplamiento de locomoción.
  - [Mutación Continua y Rendimiento](docs/GAME_SCENE_MUTATION_PERFORMANCE.md): Rendimiento de deformación en tiempo real durante transiciones de variantes.
  - [Crecimiento de Accesorios y Continuidad](docs/GAME_SCENE_GROWTH_CONTINUITY.md): Conservación de rasgos fenotípicos y continuidad de pigmentos.
  - [Marcha y Giros Procedurales](docs/GAME_SCENE_PROCEDURAL_ANIMATION.md): Cinemática inversa de patas (`IK`), apoyo cuadrúpedo y sincronización de zancada.
  - [Estrategia de Optimización de Game Scene](docs/GAME_SCENE_AGER_OPTIMIZATION_ANALYSIS_2026-10-08.md): Auditoría de contacto y estructuración persistente de aceleración.

- **Superficies y Apariencia Procedural**:
  - [Arquitectura de Superficies Procedurales](docs/PROCEDURAL_SURFACES.md): Controles, API de materiales, coordenadas en reposo y shaders.
  - [Informe de Implementación de Superficies](docs/SURFACE_IMPLEMENTATION_REPORT.md): Medidas de rendimiento, filtrado y limitaciones.
  - [Campo Disperso de Superficies](docs/AGER_SPARSE_SURFACE_FIELD_IMPLEMENTATION.md): Evaluación localizada de propiedades superficiales.

- **Metamorfosis Dinámica y Transferencia Anatómica**:
  - [Informe de Metamorfosis Inter-Especies](docs/AGER_MORPH_REPORT.md): Metodología de correspondencia anatómica y ontogenia canino-reptiliana.
  - [Arquitectura de Remallado por Chunks](docs/AGER_CHUNKED_MORPH_IMPLEMENTATION.md): Gestión de parches locales (`MeshPatch`) y bloques espaciales (`MorphSpatialCache`).
  - [Reparación de Costuras Topológicas en Chunks](docs/AGER_CHUNKED_MORPH_REPAIR.md): Eliminación de discontinuidades y costuras en parches.
  - [Corrección de Transferencia y Cesión Geométrica](docs/AGER_VISUAL_HANDOFF_REPAIR.md): Uniones asintóticas y conservación de volumen en apéndices.
  - [Auditoría de Seguridad y Memoria en Morphing](docs/AGER_MORPH_SAFETY_AUDIT.md): Invariantes topológicos, límites de recursos y robustez matemática.
  - [Optimizaciones de Rendimiento en Morphing](docs/AGER_MORPH_OPTIMIZATION_REPORT.md): Benchmarks, SIMD/hilos y aceleración temporal.
  - [Diagnóstico de Arquitectura 2026-10-06](docs/AGER_DIAGNOSTIC_2026-10-06.md): Auditoría de trazado GPU, cesión de malla y primera raíz propietaria.
  - [Reparación del Diagnóstico 2026-10-06](docs/AGER_DIAGNOSTIC_REPAIR_2026-10-06.md): Poda conservadora, cola de raíces y caché densa estática.
  - [Reparación Interactiva 2026-10-06](docs/AGER_INTERACTIVE_REPAIR_2026-10-06.md): Restauración de raymarch interactivo y solución de regresiones de FPS.

- **Aceleración por GPU y Rendimiento**:
  - [Extracción de Superficie SDF en GPU](docs/AGER_GPU_SDF_SURFACE.md): Compute shaders GLSL, buffers SSBO y caché dispersa en GPU.
  - [Informe de Rendimiento del Ager](docs/AGER_PERFORMANCE.md): Perfilado por etapas, residencia GPU y deformación continua.

- **Ecosistemas, Genética y Vida Artificial**:
  - [Arquitectura de Simulación de Ecosistemas](docs/ECOSYSTEM_SIMULATION.md): Auditoría, modelo fisiológico, genoma hereditario, reproducción y escenarios reproducibles.
