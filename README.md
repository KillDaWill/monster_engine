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
- **`Eye` & `Mouth` (`include/Eye.h`, `include/Mouth.h`)**: Órganos faciales de las criaturas: ojos individuales y boca/mandíbula.
- **`Trait`, `VisualTrait` & `MonsterBehavior` (`include/Trait.h`, `include/VisualTrait.h`, `include/MonsterBehavior.h`)**: Rasgos polimórficos (comportamiento, combate y visuales) y controladores de comportamiento.
- **`Monster` (`include/Monster.h`)**: Entidad principal que engloba paleta cromática, partes corporales, ojos, bocas, traits polimórficos y física.
- **`MonsterQueries` (`include/MonsterQueries.h`)**: Funciones de consulta pura (no mutadoras) sobre la entidad: centros de masa, direcciones, anchos/altos interpolados y caja envolvente.
- **`MonsterAger` (`include/MonsterAger.h`)**: Módulo de interpolación continua entre fases (envejecimiento/evolución de un monstruo).
- **`WorldInterface` (`include/WorldInterface.h`)**: Interfaz agnóstica de consulta del mundo/entorno (altura del terreno).

### 🧊 Pipeline SDF (Mallado Implícito)

La geometría de los monstruos se define mediante **Signed Distance Fields (SDF)** y se convierte en malla poligonal a través del siguiente pipeline:

1. **`SDFPrimitives` (`include/SDFPrimitives.h`)**: Primitivas geométricas continuas (esfera, elipsoide, cápsula, cápsula cónica, caja 3D).
2. **`SDFOperations` (`include/SDFOperations.h`)**: Operaciones CSG booleanas y mezclas suaves (Smooth Union / Subtract) con materiales.
3. **`SDFSampling` (`include/SDFSampling.h`)**: Muestreo del campo SDF y estimación de normales por diferencias finitas.
4. **`MonsterSDF` (`include/MonsterSDF.h`)**: Representación geométrica implícita compilada de un monstruo (snapshot desacoplado e independiente tras `Build`).
5. **`MarchingCubes` (`include/MarchingCubes.h`, `include/MarchingCubesTables.h`)**: Interprete de topología: máscara de aristas cortadas y filas canónicas de triángulos (256 casos).
6. **`SDFMesher` (`include/SDFMesher.h`)**: Orquestador de triangulación: convierte el campo SDF en malla poligonal con caché de aristas.
7. **`Mesh` & `PrimitiveMesh` (`include/Mesh.h`, `include/PrimitiveMesh.h`)**: Estructura de datos genérica de mallas 3D indexadas (vértices + índices) y generador de mallas primitivas (esferas UV).
8. **`MonsterVisual` (`include/MonsterVisual.h`)**: Coordinador desacoplado que sintetiza la representación visual (SDF → Mesh) de un monstruo, incluyendo los ojos.

### 🖥️ Renderizado

- **`RenderInterfaces` & `OpenGLRenderer` (`include/RenderInterfaces.h`, `include/OpenGLRenderer.h`)**: Arquitectura agnóstica de dibujado (`MonsterRenderer` VTable y `ICamera`) implementada actualmente mediante OpenGL y SDL2 (renderizado atómico de triángulos y modo wireframe).

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
   Muestra la animación en tiempo real con OpenGL del lagarto evolucionando de joven a adulto alfa.
   - **Flecha DERECHA / ARRIBA**: Avanzar porcentaje de edad.
   - **Flecha IZQUIERDA / ABAJO**: Retroceder porcentaje de edad.
   - **ESPACIO**: Activar/Desactivar oscilación automática.
   ```bash
   ./demos/demo_ager_3d
   ```

---

## 🧪 Pruebas Unitarias

La suite de pruebas unitarias (`tests/test_*.c`) cubre colores, vectores, partes del cuerpo, monstruo, envejecimiento, pipeline SDF, tablas de Marching Cubes, validación de mallas, mallas primitivas y el pipeline completo (SDF → Mesh → Visual). Se ejecuta con:

```bash
make test
```

---

## 📖 Documentación Doxygen para Futuros Agentes

Toda la base de código utiliza anotaciones en formato Doxygen (`@file`, `@brief`, `@param`, `@return`, `@struct`).

Para actualizar o consultar la documentación HTML:
1. Ejecuta `make docs` o `doxygen Doxyfile`.
2. Abre `doc/html/index.html` en el navegador.
