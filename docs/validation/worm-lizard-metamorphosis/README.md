# Metamorfosis Procedural: Gusano Blanco a Lagarto Alfa

Este documento describe la arquitectura, la parametrizacion morfogenetica y las correcciones implementadas en **Monster Engine** para resolver la metamorfosis continua de un gusano vermiforme a un lagarto adulto completamente desarrollado en la demo interactiva 3D (`demos/demo_ager_3d`).

---

## 1. Problemas Abordados

1. **Estadio Inicial Prematuro ("Cabeza ya formada")**:
   - Inicialmente, la larva mostraba una esfera abultada desproporcionada en el extremo anterior y mandibulas prematuras emergiendo desde el inicio.
   - El cuerpo presentaba una longitud excesiva y ensanchamientos posteriores (abultamiento conico en la cola).
2. **"Cabeza Rota" / Discontinuidades y Agujeros**:
   - Durante la transicion de crecimiento, la cabeza sufria fracturas geometricas ortogonales, planos de corte y normales numericamente inestables que producian franjas negras y agujeros visuales.
3. **Ausencia de Emergencia Organica**:
   - La cabeza no se comportaba como una extension axial en desarrollo, sino como un elemento rigido desconectado de la ontogenia corporal.

---

## 2. Soluciones e Implementacion

### A. Fenotipo y Grafo Anatomico Larvario (`src/Lizard.c` e `include/Lizard.h`)

- **Cilindro Vermiforme Homogeneo**:
  - Se homogeneizaron los anchos de las estaciones axiales (`neckWidth = 0.90f`, `shoulderWidth = 0.92f`, `thoraxWidth = 0.92f`, `abdomenWidth = 0.92f`, `pelvicWidth = 0.90f`, `bodyFlattening = 1.0f`).
  - Se acorto la longitud del tronco a `2.40f` y la escala total a `0.38f`.
  - La base de la cola se calibro (`tailBaseWidth = 0.44f`) para coincidir con el radio pelvico (`0.45f`), y su longitud inicial se redujo a `0.50f`, eliminando cualquier saliente o discontinuidad posterior.
- **Relajacion de Cuello Dependiente de la Ontogenia**:
  - En `LizardPhenotype_Normalize`, el estrechamiento del cuello respecto a la cintura escapular se modula mediante interpolacion suave con `cephalicDevelopment`:
    $$\text{neckWidth} \le \text{shoulderWidth} \times \text{Lerp}(1.0, 0.80, \text{cephalicDevelopment})$$
    A edad 0, permite un diametro continuo de gusano sin constriccion cervical.
- **Crecimiento de la Cabeza como Extension Proyectada**:
  - El nodo `ANATOMY_ID_HEAD` y el elipsoide de base cefalica inician colineales con el extremo anterior del cuello (`headZ = neckZ + 0.45 * neckLength * s`), proyectandose suavemente en $+Z$ a medida que `cephalicDevelopment` avanza hacia la madurez adulta.
  - El desarrollo facial (rostro, fosas nasales, crestas oseas, orbitas y fosa temporal) esta ponderado continuamente por `cephalicDevelopment`.

### B. Eliminacion de Discontinuidades en el Campo SDF (`src/MonsterSDF.c` y `shaders/monster_sdf_trace.glsl`)

- **Causa Raiz Identificada**:
  - El evaluador de distancias utilizaba recortes booleanos duros sobre cajas delimitadoras (`containsBox` en GLSL y `AABB_ContainsPoint` en C) para condicionar la inclusion de la boveda craneal, la cavidad oral y la mandibula inferior.
  - Al cruzar los limites de las cajas, el campo de distancia experimentaba saltos discontinuos ($C^{-1}$), produciendo gradientes infinitos que corrompian el calculo de normales por diferencias finitas:
    $$\mathbf{n} = \nabla f(p) \approx \frac{f(p + \epsilon) - f(p - \epsilon)}{2\epsilon}$$
    Esto generaba planos de corte negros ("cabeza rota") y agujeros en el trazado de rayos.
- **Correccion Continua**:
  - Se eliminaron las condiciones duras `containsBox` y `AABB_ContainsPoint` en las evaluaciones globales de cabeza y boca.
  - Las uniones y sustracciones suaves (`SDF_SmoothSubtract` y `joinField`) amortiguan la influencia de las cavidades y rasgos a distancia de forma matematicamente continua sin producir artefactos de recorte.
  - La mandibula articulada escala su campo segun `cephalicDevelopment`, suprimiendose por completo en el gusano sin alterar la continuidad espacial.

---

## 3. Registro de Fases de Desarrollo

| Etapa | Edad ($t$) | Caracteristicas Morfologicas |
| :--- | :---: | :--- |
| **Larva Vermiforme** | $0.00$ | Gusano cilindrico blanco, corto y uniforme. Extremo anterior redondeado colineal. Sin extremidades ni mandibulas visibles. |
| **Brote Ontogenetico** | $0.25$ | Proyeccion cefalica en $+Z$. Aparicion sutil de ojos y pliegue labial cerrado. Yemas de extremidades emergiendo en los flancos. Alargamiento de la cola. |
| **Lagarto Juvenil** | $0.50$ | Aplanamiento dorsoventral del cuerpo. 4 extremidades articuladas con sus 20 digitos completamente visibles. Craneo continuo y sin cortes planos. |
| **Lagarto Alfa** | $1.00$ | Craneo escamoso robusto con rebordes orbitarios, fosas nasales, mandibula articulada con apertura oral, musculatura temporal y pigmentacion madura. |

---

## 4. Estructura de Archivos Modificados

- `src/Lizard.c` / `include/Lizard.h`: Definicion de fenotipos larvario y adulto, normalizacion dependiente de la edad y resolucion del grafo anatomico.
- `src/MonsterSDF.c` / `include/MonsterSDF.h`: Campo SDF continuo, evaluacion suave de rasgos craneales y escalado morfogenetico de mandibula.
- `shaders/monster_sdf_trace.glsl`: Shader GLSL para raymarching de campo directo en OpenGL sin artefactos de recorte planar.
- `src/MonsterSDFShader.generated.h`: Representacion empaquetada autogenerada del shader compilado.
- `demos/demo_ager_3d.c`: Demo interactiva 3D con soporte para control de edad en tiempo real, cambio de vistas e inspeccion anatomica.

---

## 5. Instrucciones de Compilacion y Verificacion

```bash
# Compilar y ejecutar la suite completa de pruebas unitarias (100% verde)
make test

# Compilar los ejecutables de demostracion interactiva
make demos

# Ejecutar la demo interactiva 3D
./demos/demo_ager_3d
```

### Controles de la Demo:
- **Flecha DERECHA / ARRIBA**: Avanzar edad de metamorfosis (+).
- **Flecha IZQUIERDA / ABAJO**: Retroceder edad (-).
- **Barra Espaciadora**: Pausar / reanudar animacion automatica continua.
- **Teclas 0 a 4**: Saltar a 0%, 25%, 50%, 75% o 100% de desarrollo.
- **H / F1-F4**: Inspeccion anatomica de la cabeza en distintas orientaciones.
- **Rueda del raton**: Zoom de camara.
