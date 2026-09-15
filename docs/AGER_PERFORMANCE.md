# Rendimiento del Ager con superficies procedurales

> Mediciones históricas: el antiguo «lag 0,000» medía la escala del morph, no
> la edad geométrica. Para la implementación y validación actuales véase
> [Continuidad del Ager](AGER_CONTINUITY_REPORT.md).

## Pipeline efectivo

La edad visual y la edad geométrica son canales distintos. `MonsterAger` mantiene
el fenotipo continuo y un snapshot geométrico cuantizado (32 intervalos por
defecto). `MonsterVisualAsync` genera ese snapshot fuera del hilo de render,
conserva únicamente la última petición pendiente y publica una malla inmutable.
Entre keyframes, `LizardMorph` deforma la malla publicada sobre la anatomía
continua sin reconstruir SDF, índices ni coordenadas de superficie.

La apariencia se recompila como una receta pequeña y se envía como uniforms. Un
cambio de pigmento, cobertura, relieve o tamaño de escama no cambia la huella
geométrica ni solicita Marching Cubes.

## Residencia GPU

El renderer conserva una caché acotada de 32 mallas identificadas por identidad y
revisiones explícitas. Geometría, dominio superficial e índices viven en buffers
separados. Una malla estática sólo actualiza uniforms y dibuja. Durante morphing,
la deformación invalida únicamente posición/normal/color; no reenvía índices ni
el dominio superficial.

Las consultas `GL_TIME_ELAPSED` se recogen de forma diferida para evitar bloquear
la CPU. `OpenGLRenderer_GetMeshPerformanceStats` expone tiempo GPU, uploads,
aciertos de caché y bytes transferidos.

## Mapeo superficial

`SurfaceMapper` compila las conexiones anatómicas en un BVH determinista. Cada
nodo conserva una cota inferior basada en su AABB y el radio máximo, de modo que
la búsqueda descarta ramas sin perder la clasificación exacta de las dos regiones
más próximas. En las mallas medidas se evaluaron unas 26 conexiones por vértice
frente a 136 por fuerza bruta (4,6-5,3 veces menos pruebas).

## Mediciones reproducibles

Máquina de validación del 14-09-2026, build `-O2`:

| Escenario | Antes | Después |
|---|---:|---:|
| MORPH juvenil, generación completa | 2282 ms | 1135 ms |
| MORPH adulto, generación completa | 3491 ms | 1522 ms |
| 600 updates continuos, p95 CPU | 0,31 ms sin presentación coherente | 5,30 ms con deformación continua |
| 600 updates continuos, desfase de escala visual (no lag geométrico) | 0,953 | 0,000 |
| Captura estática, uploads/aciertos | 111 / 0 | 3 / 108 |
| Captura estática, bytes subidos | 217,2 MB | 10,6 MB |

El coste GPU adulto a 1024x768 fue 0,970 ms con escamas desactivadas y
2,327 ms con escamas activadas. La diferencia de 1,357 ms confirma que el shader
importa, pero no era el cuello de botella de segundos.

Comandos principales:

```sh
make benchmark-ager-realtime
DISPLAY=:0 ./demos/demo_ager_3d --mesh --morph --benchmark-mesh=120 1
DISPLAY=:0 ./demos/demo_ager_3d --mesh --morph --scales-off --benchmark-mesh=120 1
```

## Calidad y LOD de escamas

MORPH usa un presupuesto local reducido; SETTLED conserva la calidad anterior y
se solicita tras el debounce existente. El shader descarta tejidos sin cobertura
antes de buscar celdas, usa tres proyecciones en primer plano, dos a distancia
media, una más lejos y una aproximación lisa cuando la huella supera el píxel.

## Trabajo futuro

La deformación de topología estable ya es la opción práctica para este Ager: las
identidades anatómicas y los índices permanecen estables y las pruebas cubren el
ciclo y los 20 unguales. El siguiente salto es mover esa deformación a GPU para no
subir posiciones/normales durante el movimiento.

El render SDF directo es prometedor para transiciones entre morfologías cuya
topología no pueda corresponderse. Antes de convertirlo en ruta principal debe
integrar `Surface_Evaluate` sólo después del impacto del rayo; hoy no reproduce
las escamas procedurales del renderer de malla. No debe evaluar material dentro
de los pasos de raymarch.

Un caché de keyframes completos no se recomienda todavía: las mallas adaptativas
no garantizan correspondencia entre edades y retener varias mallas adultas tendría
un coste de memoria alto. El keyframe geométrico actual más deformación anatómica
ofrece la misma ventaja temporal con un único snapshot residente.
