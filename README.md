# Monster Engine (C Core)

Motor de simulación procedural de monstruos desacoplado, modular y **agnóstico de cualquier librería de renderizado**, desarrollado en C idiomático.

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
   - El pelaje canino se acorta y reduce su densidad hasta desaparecer en la primera mitad (0% a 45%).
   - Se atraviesa una fase de piel desnuda intermedia (45% a 55%) donde se repliegan las orejas, el tórax se aplana y las patas se abren hacia los lados (*sprawl*).
   - El Ager interpola anatomía resuelta mediante IDs semánticos; las ramas ausentes se contraen hacia su ancestro compartido. La reproducción adapta su velocidad al desfase de la malla publicada, conservando la resolución.
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

---

## 🧪 Pruebas Unitarias

La suite de pruebas unitarias (`tests/test_*.c`) cubre colores, vectores, partes del cuerpo, monstruo, envejecimiento, pipeline SDF, anatomía oral, materiales, auditoría topológica, tablas de Marching Cubes, mallas primitivas y el pipeline completo (SDF → Mesh → Visual). Se ejecuta con:

```bash
make test
```

---

## 📖 Documentación Doxygen para Futuros Agentes

Toda la base de código utiliza anotaciones en formato Doxygen (`@file`, `@brief`, `@param`, `@return`, `@struct`).

Para actualizar o consultar la documentación HTML:
1. Ejecuta `make docs` o `doxygen Doxyfile`.
2. Abre `doc/html/index.html` en el navegador.

## 📚 Documentación Técnica e Informes de Arquitectura

El directorio `docs/` incluye informes exhaustivos de diseño, optimización y validación del motor:

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

- **Aceleración por GPU y Rendimiento**:
  - [Extracción de Superficie SDF en GPU](docs/AGER_GPU_SDF_SURFACE.md): Compute shaders GLSL, buffers SSBO y caché dispersa en GPU.
  - [Informe de Rendimiento del Ager](docs/AGER_PERFORMANCE.md): Perfilado por etapas, residencia GPU y deformación continua.

