# Primer mamífero: perro digitígrado

El adulto usa `CreatureRecipes_Dog()` (ID persistente 2), la misma resolución de
anatomía, rig, SDF y malla que las demás criaturas. No hay geometría exclusiva del
visor. El trabajo posterior a la incorporación del pelo se centró en la morfología;
se conserva el sistema de pelaje existente.

## Anatomía y arquitectura

- `AxialBodyUprightTetrapod` compone seis estaciones semánticas compartidas: cuello,
  cintura pectoral, tórax anterior/posterior, abdomen y pelvis. El dorso común y las
  secciones ventrales diferentes producen pecho profundo y retracción abdominal.
- `LimbMammal` conserva los cuatro IDs articulares: hombro/codo/carpo/pata y
  cadera/rodilla/corvejón/pata. Cuatro dedos cortos por extremidad, sin abanico
  reptiliano. El descriptor genérico reside en `LimbRig.h`; el rig estático es válido.
- Las conexiones anatómicas admiten plenitud transversal y dorsoventral cuadrática
  y un eje transversal anatómico. Esto proporciona volumen muscular y metapodios
  estrechos, sin añadir mallas ni nodos de animación específicos del perro.
  Los perfiles participan en la huella del grafo, límites, poda, intervalos y GPU.
- La validación de recetas consulta las capacidades de cabeza, cuerpo axial y
  miembros. No contiene una excepción basada en el ID del perro.
- La cabeza canina utiliza el cráneo barrido compartido, hocico ahusado, trufa
  diferenciada, narinas sobre el volumen nasal y mandíbula coherente con el perfil
  facial. `eyelidCoverage` separa la abertura del párpado del tamaño del globo.
- `SDF_TaperedPinna` describe pabellones orientados con base ancha, punta estrecha,
  espesor decreciente y concavidad. Los límites son conservadores y el intervalo
  usa la misma primitiva. El GLSL se genera desde las fuentes CPU.
- Las cuatro etapas conservan topología; por ahora son escalas compatibles, no
  una simulación del desarrollo de un cachorro. La receta no activa locomoción.

## Medidas del adulto

Unidades internas; H es la altura nominal a la cruz.

| Magnitud | Valor |
|---|---:|
| H | 4,000 |
| Longitud corporal de referencia / H | 1,000 |
| Longitud cefálica / H | 0,396 |
| Hocico / longitud cefálica | 0,485 |
| Tórax: anchura × profundidad | 1,40 × 1,92 |
| Abdomen: anchura × profundidad | 0,92 × 1,12 |
| Pelvis: anchura × profundidad | 1,12 × 1,44 |
| Altura del carpo / corvejón | 0,449 / 0,860 |
| Altura de la suela, cuatro patas | 0,000 |
| Pinna: anchura basal × altura × espesor basal | 0,3374 × 0,5684 × 0,0896 |
| Dirección de pinna izquierda (x,y,z) | (0,1015; 0,9663; 0,2367) |

La longitud corporal de referencia usa tronco más semianchura torácica; no es la
longitud de la AABB completa, que incluye hocico y cola.

## Reproducción

Desde la raíz del repositorio:

```sh
make -B -j4 test demos
./demos/demo_dog --neutral
./demos/demo_dog
./demos/demo_dog --rotate
mkdir -p /tmp/dog-neutral /tmp/dog-coat /tmp/dog-gpu
./demos/demo_dog --neutral --capture-dir=/tmp/dog-neutral
./demos/demo_dog --capture-dir=/tmp/dog-coat
./demos/demo_dog --neutral --sdf --capture-dir=/tmp/dog-gpu
```

Teclas 1–6: lateral, frontal, tres cuartos, superior, cabeza y cola. A: grafo.
F/S: pelo/piel; espacio: giro. `--neutral` emplea material uniforme sin pelo y
sin cambiar la anatomía. Las capturas se convierten de PPM a PNG sin retoque.

## Evidencia

La verificación final `make -B -j4 test demos` terminó con código 0: todas las
suites pasaron, incluidas perro, pelo y regresiones reptilianas, sin advertencias
de compilación. [Registro completo](build-test.log), [malla](mesh.log),
[GPU](gpu.log) y [baseline independiente](baseline-suites.log).

- [Tres cuartos neutro](neutral/tres-cuartos.png), [lateral](neutral/lateral.png),
  [frontal](neutral/frontal.png), [superior](neutral/superior.png),
  [cabeza](neutral/primer-plano-cabeza.png), [cola](neutral/primer-plano-cola.png).
- [Pelaje existente](coat/tres-cuartos.png) y [primer plano](coat/primer-plano-cabeza.png).
- [SDF directo](gpu/tres-cuartos-sdf.png): diagnóstico del campo; no dibuja las
  mallas oculares separadas y por ello muestra las cavidades vacías.
- Malla: 109 nodos anatómicos, 81.663 vértices, una componente; segundo componente
  de tamaño cero. No hay piezas corporales, orejas o dedos desconectados.
- Pruebas nuevas: cuatro etapas, validación/rig/topología, apoyo, proporciones,
  zigzag sagital, menor abducción que el reptil, pinnae, malla y materiales.
  Además, 2.700 cajas musculares contrastan intervalos y poda con evaluación sin
  poda; se comprueba que los nuevos perfiles invalidan la huella geométrica.
- Paridad CPU/GPU: 796 puntos alrededor de orejas y los doce segmentos musculares;
  error máximo 0,00000024 en Radeon integrada/Mesa.

El estado inicial fallaba en `TestSandBurrowerReferenceShape` (conectividad) y,
mediante ejecución independiente de suites, también en
`test_lizard_local_head_field_quality` (muestreo de narina). Se registraron antes
de los cambios. El repositorio contenía y recibió otros cambios durante este
trabajo; los resultados finales describen el estado integrado, sin atribuir todas
sus reparaciones a la implementación mamífera.

## Límites y siguiente hito

La forma es una base canina procedural estilizada, no una reproducción fotorrealista
ni una raza exacta. La pose es simétrica y estática. La concavidad auricular es una
aproximación geométrica, sin cartílago detallado; faltan labios y comisuras más
ricos, uñas/cojinetes diferenciados y pequeñas asimetrías de reposo. El pelaje
actual conserva su aspecto granular; no se ha rediseñado en esta tarea.

El siguiente hito lógico es locomoción digitígrada con apoyo plantar e IK sobre
este rig, manteniendo inmutable la anatomía de reposo y sin remallar por fotograma.
