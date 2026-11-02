# Fundación anatómica: auditoría y dependencias

> Informe del primer bloque. La continuación equina se documenta en
> [../equine-anatomy/README.md](../equine-anatomy/README.md); el inventario
> siguiente describe la base original, no el estado posterior a esa continuación.

## Alcance real

Rama: `feature/universal-anatomy-foundation`.
Base: `cbf5ef201da6ba9932f9135eb70a7cd452df4596`.

Se implementó el bloque independiente **A2 de dependencias** y su integración
con geometría, rig, JSON v1 y editor. **No está completado todo el hito 1**:
faltan los contratos de nuevas familias semánticas. No se implementaron todavía
los animales de los hitos 2–6. Las cuatro recetas `reordered-*.creature.json`
son fixtures de compatibilidad de criaturas existentes, no especies nuevas.

## Inventario contrastado con la implementación

| Área | Implementado en la base | Pendiente |
|---|---|---|
| Despachador axial (`src/AxialBody.c`) | Tetrápodo extendido y erecto | Serpentino, aviano y custom; peces ni siquiera tienen enumerado |
| Despachador de miembros (`src/Limb.c`) | Reptil, mamífero y ungulado de pezuña hendida | Aviano, ala, aleta y custom |
| Cabeza (`src/Head.c`) | Lagarto, cánido, aviano y bóvido | Arquitecturas felina, equina, cérvida y proboscídea dedicadas |
| Plan corporal | Axial, cabeza, miembro, cola y ornamento | Aleta, caparazón, apéndice blando, sensorial y estructura superficial |
| Cola (`src/Tail.c`) | Perfiles afilado, látigo, pesado, prensil y con aleta | La cola prensil existente no equivale a una trompa muscular animada |
| Ornamentos (`src/Ornament.c`) | Cuerno curvo, espina, lámina, placa y cresta | Ramificación de astas con contrato propio; custom no tiene resolutor |
| Marchas (`src/Creature.c`) | Sin marcha, cuadrúpedo extendido/erecto, felina y bovina | Vuelo, natación, ondulación y salto |
| Serialización | JSON v1, esquema generado, cuatro etapas y editor C | Migración v2 cuando haya nuevos datos; no necesaria en este bloque |

Los tests de metamorfosis con nombres como «quadruped_to_snake» no demuestran
que exista un resolutor axial serpentino. Tampoco un enumerado de ala demuestra
que existan membranas o plumas de vuelo anatómicas.

## Arquitectura implementada

- `CreatureRecipe_ResolutionOrder`: orden topológico sin asignaciones dinámicas.
  Dependencias explícitas: anfitrión de ranura **o ambos extremos reales de ruta**,
  más el soporte anterior de una lámina. Rechaza identidades repetidas, referencias
  ausentes, autodependencias y ciclos. Los empates se resuelven por familia e ID,
  conservando la precedencia histórica de las recetas incorporadas.
- `Creature_ResolveAnatomy`: sustituye los dos pases por ese orden. Comprueba que
  el nodo raíz realmente exista antes de publicar un resultado válido.
- `AttachmentPath_ResolveNodes`: contrato reutilizable para rutas sobre cualquier
  módulo que ya publique estaciones. Mantiene la sección elíptica histórica,
  rechaza centros no finitos, radios inválidos, tramos colapsados y normales
  degeneradas/paralelas. No modifica la ranura destino al fallar.
- `CreatureRig_Build`: usa el mismo orden para cadenas caudales y miembros;
  reorganizar JSON no debe permutar los índices locomotores.
- No hay cambios de layout, enumerados, primitivas SDF, shaders ni material.
  No se añadieron dependencias SDL/GL al núcleo. No se editaron salidas generadas.

Las rutas pueden conectar estaciones axiales, cefálicas, de miembros, colas u
ornamentos; los módulos futuros podrán usar el mismo contrato cuando publiquen
nodos. Esto **no** afirma que los resolutores actuales de cabeza/miembros admitan
cualquier rotación de todo su volumen: algunos conservan convenciones globales.
Tampoco se publican ranuras nuevas automáticamente por cada nodo.

## Memoria y rendimiento

Capacidades sin aumentar: 16 miembros, 4 colas, 64 ornamentos, 64 módulos,
1024 nodos y 1024 conexiones. En esta plataforma de 64 bits:

| Tipo | Bytes antes y después |
|---|---:|
| `CreatureRecipe` | 36264 |
| `CreaturePhenotype` | 8160 |
| `AnatomyGraph` | 121880 |
| `Monster` | 178904 |

El orden necesita 512 bytes para máscaras y 512 para índices; la validación y
el resolutor mantienen buffers temporales pequeños en pila, no en cada criatura.
Coste de ordenación O(M²), acotado por M=64. Se probó el límite exacto y el exceso.

Mediana de siete ejecuciones intercaladas, 1000 resoluciones adultas por receta,
tiempo CPU con `clock()` (sin render/meshing; no es una medición de FPS):

| Receta | Base µs | Actual µs |
|---|---:|---:|
| Lagarto | 48.317 | 49.173 |
| Perro | 32.695 | 33.351 |
| Brumacerno | 32.387 | 34.226 |
| Velkar | 41.950 | 43.292 |

Sobreprecio observado: aproximadamente 0.66–1.84 µs por resolución. Datos crudos
en `performance.json`. Las huellas anatómicas adultas coinciden con la base.
La base se compiló sustituyendo únicamente `Creature.c`, `CreatureRecipe.c` y
`AttachmentPath.c` por sus fuentes de la revisión indicada; no hay cambios de
layout entre ambas versiones. No es una compilación independiente de todo el
repositorio histórico.

## Evidencia visual y compatibilidad

`results.json` registra 16 casos: cuatro criaturas × cuatro etapas, tres vistas
por caso. El puente C produce las mallas; Pillow/NumPy únicamente las proyectan.
Se compararon metadatos, materiales, índices, estaciones por ID y vértices:

- Base → actual: diferencia máxima de vértice **0**.
- Actual → módulos invertidos: diferencia máxima de vértice **0**.
- Geometría finita en todos los casos.

Capturas diagnósticas, **sin shader de pelaje ni animación OpenGL**:

- [Lagarto, cuatro etapas y tres vistas](recipe-1.png)
- [Perro, cuatro etapas y tres vistas](recipe-2.png)
- [Brumacerno, cuatro etapas y tres vistas](recipe-3.png)
- [Velkar, cuatro etapas y tres vistas](recipe-4.png)
- [Captura solicitada mediante API pública del editor](agent-brumacerno.png)
- [Captura OpenGL: SDF exacto frente a remallado diferido](morph-opengl.png)

Estas imágenes documentan preservación, no una mejora anatómica ni comparación
con referencias biológicas. Se conservan defectos visibles anteriores, incluyendo
la baja resolución de malla y transiciones juveniles poco naturales. No se
inventaron cocientes biológicos ni se validó todavía un caballo real.

## Verificación reproducible

```bash
make test
make editor-test
make benchmark-anatomy
make visual-anatomy-foundation
python3 tools/validate_anatomy_dependencies.py --output build/anatomy-validation
# Opcional: --baseline-bridge /ruta/al/puente/de/referencia
python3 tools/creature_editor/agent.py capabilities
python3 tools/creature_editor/agent.py schema
python3 tools/creature_editor/agent.py inspect
make visual-morph-ownership
make build/validate_recipe_gpu
build/validate_recipe_gpu recipes/brumacerno.creature.json 3
```

La prueba nueva de C cubre orden inverso, determinismo, estaciones estables,
rutas sobre miembros/cola/cabeza, extremos ausentes, raíz ausente, ciclos,
normales inválidas, capacidad exacta y pipeline con rig/SDF/superficies en cuatro
etapas. La prueba nueva HTTP verifica construcción real en C, materiales,
rechazo transaccional de ciclos y preservación de undo/redo.

Resultados: la suite base `make test` pasó antes de editar; `make editor-test`
pasó antes (7 pruebas) y después (8 pruebas). La regresión OpenGL
`make visual-morph-ownership` pasó: «Silueta sin cuerpo obsoleto visible», con
latencia de worker de 3000 ms. La validación de 16 casos de malla pasó.
La ejecución final de `make test`, incluyendo los cambios de rig y la suite
nueva de dependencias, también pasó completa. `git diff --check` no detectó
errores de espacio en blanco; los módulos modificados compilaron sin warnings.

**Limitación GPU preexistente reproducida:** Radeon Renoir, Mesa 25.2.8,
Brumacerno adulto, 5204 muestras. Compute: `0.00014102459` m (bajo tolerancia
`0.0002`); fragmento: `2.19592166` m (**FAIL**); intervalos de pinna:
`0.000178694725` m. La base produce exactamente los mismos tres errores.
No se corrigió ni se amplió la tolerancia para ocultarlo.

## Archivos de implementación modificados/creados

- `.gitignore`, `Makefile`.
- `include/CreatureRecipe.h`, `include/AttachmentPath.h`.
- `src/CreatureRecipe.c`, `src/Creature.c`, `src/AttachmentPath.c`, `src/CreatureRig.c`.
- `tests/test_module_dependencies.c`, `tests/main_test.c`.
- `tools/creature_editor/test_editor.py`.
- `tools/validate_anatomy_dependencies.py`.
- `benchmarks/benchmark_anatomy.c`.
- Este directorio: `README.md`, `results.json`, `performance.json`,
  `reordered-1.creature.json`, `reordered-2.creature.json`,
  `reordered-3.creature.json`, `reordered-4.creature.json`, `recipe-1.png`,
  `recipe-2.png`, `recipe-3.png`, `recipe-4.png`, `agent-brumacerno.png`,
  `morph-opengl.png`.

## Trabajo restante, por orden

1. Completar hito 1: contratos semánticos nuevos y capacidad/propiedad geométrica
   y material end-to-end, sin habilitar enumerados que carezcan de resolutor.
2. Hito 2: cráneo equino independiente, autopodio monodáctilo sustentante,
   receta de caballo y segundo animal que reutilice ambos, editor/JSON y
   validación multivista con referencias reales.
3. Hito 3: representación axial curva de estaciones variables, serpiente real,
   caparazón estructural y ciclo anfibio con eventos topológicos.
4. Hito 4: aves/murciélagos/peces/delfines con alas, membranas, aletas y
   controladores propios, más paridad CPU/GPU y deformación superficial.
5. Hito 5: instanciación segmentada, artrópodos, brazos blandos y moluscos.
6. Hito 6: generador por plantillas, biblioteca ampliada e integración completa
   de desarrollo/morfología. Las transiciones caballo/ave/rana/caparazón/astas
   solicitadas aún no están implementadas ni verificadas.
