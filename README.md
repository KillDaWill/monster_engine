# Monster Engine (C Core)

Motor de simulación procedural de monstruos desacoplado, modular y **agnóstico de cualquier librería de renderizado**, desarrollado en C idiomático.

## 🚀 Arquitectura General

El motor está compuesto por los siguientes módulos principales:

- **`Color` & `ColorPalette` (`include/Color.h`, `include/ColorPalette.h`)**: Gestión de espacio de colores RGBA/HSV, gradientes dinámicos y muestreo continuo.
- **`Vector` (`include/Vector.h`)**: Álgebra vectorial para dimensiones fijas (`Vector2`, `Vector3`, `Vector4`) y N-dimensionales (`VectorND`).
- **`BodyPart` (`include/BodyPart.h`)**: Definición anatómica de segmentos corporales, cajas de colisión y lista dinámica de traits.
- **`Monster` (`include/Monster.h`)**: Entidad principal que engloba paleta cromática, partes corporales, traits polimórficos y física.
- **`MonsterAger` (`include/MonsterAger.h`)**: Módulo de interpolación continua entre fases (envejecimiento/evolución de un monstruo).
- **`RenderInterfaces` & `OpenGLRenderer` (`include/RenderInterfaces.h`, `include/OpenGLRenderer.h`)**: Arquitectura agnóstica de dibujado (`MonsterRenderer` VTable y `ICamera`) implementada actualmente mediante OpenGL y SDL2.

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

## 💻 Demos Disponibles (`demos/`)

1. **Prueba Lógica e Impresión de Datos (`demos/demo_lizard_console`)**:
   Construye un lagarto y muestra su anatomía, paleta y centros por consola.
   ```bash
   ./demos/demo_lizard_console
   ```

2. **Visor 3D Interactivo de Envejecimiento (`demos/demo_ager_3d`)**:
   Muestra la animación en tiempo real con OpenGL del lagarto evolucionando de joven a adulto alfa.
   - **Flecha DERECHA / ARRIBA**: Avanzar porcentaje de edad.
   - **Flecha IZQUIERDA / ABAJO**: Retroceder porcentaje de edad.
   - **ESPACIO**: Activar/Desactivar oscilación automática.
   ```bash
   ./demos/demo_ager_3d
   ```

---

## 📖 Documentación Doxygen para Futuros Agentes

Toda la base de código utiliza anotaciones en formato Doxygen (`@file`, `@brief`, `@param`, `@return`, `@struct`).

Para actualizar o consultar la documentación HTML:
1. Ejecuta `make docs` o `doxygen Doxyfile`.
2. Abre `doc/html/index.html` en el navegador.
