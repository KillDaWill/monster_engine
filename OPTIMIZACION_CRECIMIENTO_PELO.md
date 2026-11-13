# Optimización de la reconstrucción del pelo durante el crecimiento

**Para:** agente optimizador de Monster Engine
**Ámbito:** crin del caballo, `LongHairSet_Build` y su reconstrucción por paso de crecimiento
**Estado:** corrección y aspecto validados; el rendimiento sigue siendo bloqueante
**Referencia funcional:** [`docs/validation/long-hair-styling/RESULTADOS_CRIN.md`](docs/validation/long-hair-styling/RESULTADOS_CRIN.md)

---

## 0. Resumen para el agente

La reconstrucción completa de la crin cuesta **~1,47 s por paso de crecimiento**. El crecimiento validado tiene **21 pasos**, así que la animación de edad completa tarda **~31 s**.

El coste es casi por completo **evaluaciones del SDF**, no recorridos de arrays ni búsquedas de vecinos:

| Medida (adulto, etapa 3) | Valor |
|---|---:|
| Actualización completa (`MonsterVisual_UpdateLongHair`) | 1,47–1,54 s |
| `MonsterSDF_Build` | 0,06–0,09 ms |
| `LongHairSet_Build` con folículos | 1,43–1,48 s |
| `LongHairSet_Build` con `rootFullness=0` | 164–171 ms |
| Pase de folículos (diferencia) | **1,27–1,30 s (~87 %)** |
| Llamadas SDF por reconstrucción | **857.381** |
| Coste medio por llamada SDF | **1,6–2,2 µs (típico 1,7–1,9)** |
| Pasos de crecimiento × coste | **21 × 1,47 s ≈ 31 s** |

**Objetivo primario:** ≤100 ms por paso (21 pasos ≤2,1 s).
**Objetivo deseable:** ≤50 ms por paso.
**Techo duro:** no superar 100 ms y no cambiar la salida visual ni debilitar las validaciones.

La conclusión operativa es una sola: **hay que reducir el número de llamadas SDF o repartirlas en paralelo**. A 1,68 µs por llamada, el presupuesto de 100 ms son **~59.500 llamadas**; el de 50 ms, **~29.800**. Hoy se hacen 857.381.

---

## 1. Qué se reconstruye y cuándo

`MonsterVisual_UpdateLongHair` ([`src/MonsterVisual.c:31`](src/MonsterVisual.c)) calcula una huella del pelo a partir de:

- los grupos de pelo (geometría, densidad, semillas, `rootFullness`),
- el grafo anatómico,
- la escala del fenotipo,
- el estado de animación, la cabeza y la boca.

Si la huella cambia:

1. construye un `MonsterSDF` (línea 47),
2. reconstruye el `LongHairSet` completo (línea 62),
3. si hay animación, aplica `LongHairSet_Pose` (línea 76).

Durante el crecimiento, [`tools/capture_long_hair.c:149-150`](tools/capture_long_hair.c) hace exactamente:

```c
MonsterAger_SetPerc(ager, frame/20.f);
MonsterVisual_UpdateLongHair(&morphHair, MonsterAger_GetResultConst(ager));
```

Cada paso cambia la anatomía, cambia la huella y dispara una reconstrucción **desde cero**. No se reutiliza nada del paso anterior.

El mismo camino se ejecuta en `MonsterVisualAsync_UpdateWithAppearance` ([`src/MonsterVisualAsync.c:1499`](src/MonsterVisualAsync.c)), así que un tirón de 1,5 s bloquea también el pipeline asíncrono y el scrubbing de edad del editor.

El adulto estático no es el problema: **2,85 ms de GPU** y **9,23 ms de pose CPU**. El problema es exclusivamente la reconstrucción.

---

## 2. Baseline medido

Receta validada: [`docs/validation/long-hair-styling/caballo-crin-foliculos.creature.json`](docs/validation/long-hair-styling/caballo-crin-foliculos.creature.json).
3 grupos × 96 guías × 32 hijos = **288 guías, 9.216 folículos**.

Medido en esta máquina (AMD Renoir, gcc `-O2`), mejor de 3 repeticiones, con el SDF corporal; la captura de producción añade la unión con la mandíbula y da totales dentro del 1–4 %:

| Etapa | Actualización | `MonsterSDF_Build` | Pelo con folículos | Pelo sin folículos | Delta folículos |
|---:|---:|---:|---:|---:|---:|
| 0 | 1.496 ms | 0,067 ms | 1.463 ms | 164 ms | 1.298 ms |
| 1 | 1.513 ms | 0,063 ms | 1.470 ms | 170 ms | 1.300 ms |
| 2 | 1.485 ms | 0,061 ms | 1.443 ms | 168 ms | 1.275 ms |
| 3 | 1.498 ms | 0,061 ms | 1.463 ms | 171 ms | 1.292 ms |

«Sin folículos» significa la misma construcción con `rootFullness=0`: se conservan guías, colisión y certificado de holgura, pero no se ejecuta el reparto de raíces.

### 2.1 Contadores exactos (etapa 3)

Instrumentando una copia temporal de `LongHairSet_Build`:

| Concepto | Llamadas SDF |
|---|---:|
| Auditoría de 72 muestras por fibra | **705.205** |
| Lift durante los intentos | 36.183 |
| Proyección inicial de la raíz | 14.896 |
| **Subtotal folículos** | **756.284 (88,2 %)** |
| Colisión, pulido y certificado de guías | 101.097 |
| **Total por reconstrucción** | **857.381** |

- La auditoría de 72 muestras concentra el **93,2 %** de las llamadas del pase de folículos (705.205 de 756.284).
- Reintentos: 8.691 fibras aceptan al primer intento (94,3 %), 276 al segundo, 209 al tercero, 40 al cuarto, 0 al quinto. **Los reintentos no son el problema.**
- Búsqueda de vecinos: 288² ≈ 82.944 comparaciones aritméticas; despreciable frente a 857.381 evaluaciones SDF.
- `MonsterSDF_Build` tarda 0,06 ms: **no es el problema**.

### 2.2 Modelo de coste

```
coste ≈ llamadas_SDF × 1,68 µs
857.381 × 1,68 µs ≈ 1,44 s
```

Coincide con los 1,43–1,48 s medidos. Cualquier optimización debe medirse primero en **llamadas SDF por reconstrucción**, no sólo en milisegundos: es la métrica estable frente a frecuencia de CPU y carga de la máquina.

Presupuesto a 1,68 µs/llamada:

| Objetivo | Llamadas admitidas | Reducción necesaria |
|---|---:|---:|
| 200 ms | 119.000 | 7,2× |
| 100 ms | 59.500 | 14,4× |
| 50 ms | 29.800 | 28,8× |

Paralelizar multiplica el presupuesto por el número de hilos, pero no reduce el número de llamadas.

---

## 3. Algoritmo actual y camino caliente

`LongHairSet_Build` ([`src/LongHair.c:165`](src/LongHair.c)) hace, en orden:

1. **Raíces y curvas** a partir de las estaciones anatómicas.
2. **Colisión y certificado** ([`src/LongHair.c:244`](src/LongHair.c)): hasta 8 iteraciones PBD por guía, 3 iteraciones de pulido y un certificado de holgura por segmento. De aquí salen `clearance[p]` y `contactPenetration`.
3. **Reparto de folículos** (bloque ~`src/LongHair.c:246-292`).
4. **Empaquetado GPU** ([`src/LongHair.c:377`](src/LongHair.c)): 16 filas de curva + 64 filas de folículos por guía.

### 3.1 Reparto de folículos

Para cada guía visible de curva con `rootFullness > 0` y más de un hijo:

- busca las 4 guías más cercanas del mismo grupo con:
  - `dot(normal, normal_vecina) >= 0.9`,
  - `dot(flujo, flujo_vecina) >= 0.9`,
  - distancia `<= 0.35 × length`;
- por cada hijo elige una vecina y calcula:
  - peso `(.05 + .9·rand) · rootFullness`,
  - interpolación de raíz y normal entre las dos guías,
  - proyección de la raíz al SDF con margen `.002·scale` mediante hasta 12 iteraciones,
  - hasta 5 intentos reduciendo el peso a la mitad;
- **audita 72 muestras** de la curva Catmull interpolada exigiendo `d >= .0005·scale`;
- si la auditoría falla, reduce el peso o cae al comportamiento tubular anterior;
- guarda el descriptor `{vecina, peso, lift (elevación normal), válido}` sólo si es seguro.

La auditoría es el camino caliente: 72 muestras × 9.216 fibras = 663.552 llamadas mínimas, más reintentos = 705.205.

### 3.2 GPU

`LongHairSet_Pack` copia `follicles[child]` a las filas 16..79. `shaders/long_hair.vert` ([`shaders/long_hair.vert:49-81`](shaders/long_hair.vert)) lee el descriptor del hijo, interpola curva y normal, conserva la elevación normal durante toda la fibra y suprime el engrosamiento proximal. Si la vecina es invisible, descarta la fibra.

No se propone cambiar este contrato: el objetivo es reconstruirlo antes y con menos llamadas SDF, no alterar cómo se dibuja.

---

## 4. Restricciones que la optimización no puede romper

La receta y la salida visual actuales están validadas en cuatro etapas:

- **288/288 raíces visibles** y **9.216/9.216 hijos con folículo propio**.
- **288/288 guías caen hacia +X** (un solo lado de la cabeza).
- **0 penetraciones** en 230.400 muestras GPU por etapa (9.216 fibras × 25 vértices).
- Paridad CPU/GPU de curvas con error máximo **1,45e-6**.
- Error de proyección cutánea máximo **3,44e-6** (adulto); separación mínima entre raíces **4,09e-4** en el adulto (crece con la edad).
- Identidades estables entre etapas; construcción determinista.
- `LongHairSet_Build` es transaccional: si falla, debe conservarse el runtime anterior.

Debe seguir pasando:

```sh
make test
make editor-test
git diff --check
```

Y la validación específica:

```sh
python3 tools/validate_long_hair.py --rooted-mane --all-stages \
  --case caballo-curve \
  --output /tmp/long-hair-validation
```

Validar en `/tmp` evita pisar la referencia de `docs/validation/long-hair-styling/resultado/`; comparar después las imágenes y métricas contra ella.

**Prohibido para ganar rendimiento:**

- reducir la densidad o esconder raíces,
- debilitar el umbral `.0005·scale` o el certificado de colisión,
- sustituir la auditoría de 72 muestras por un submuestreo ingenuo,
- desactivar la colisión o el fallback tubular,
- cambiar la receta o el aspecto sin revisar y documentar el cambio.

Si una optimización cambia la decisión de qué fibras reciben folículo, **cambia la imagen**. Debe revisarse visualmente y volver a validarse; no basta con que los tests pasen.

---

## 5. Estrategias, ordenadas por relación impacto/riesgo

### 5.1 Certificar con la holgura ya calculada (primera opción, coste cero de SDF)

`LongHairSet_Collide` ya calcula `clearance[p]`: un radio libre conservador alrededor de cada segmento de la guía, que descuenta el sobre de la guía, `.003·scale` y un margen de velocidad. El tubo certificado **no penetra**.

Idea:

- para un hijo con peso `w`, elevación `lift` y vecina `B`, su curva es
  `p(t) = lerp(A(t), B(t), w) + n(t)·lift`;
- la distancia de `p(t)` a la guía `A` está acotada por
  `w·|B(t)-A(t)| + |lift| + desviación de normal`;
- si esa cota es menor que `clearance[A](t)` en todo el recorrido, la fibra entera está dentro del tubo certificado y **no hace falta auditar 72 muestras**;
- si la cota no alcanza, se ejecuta la auditoría exacta como hasta ahora.

La comprobación debe cubrir el recorrido entre nodos, no sólo los 10 puntos de control: usar el margen de velocidad que ya emplea el certificado o subdividir cada tramo de forma conservadora. Certificar sólo los nodos dejaría huecos entre ellos.

Ventajas:

- no añade ninguna llamada SDF: usa datos que ya existen;
- es una contención geométrica, no un supuesto sobre derivadas del SDF;
- si sólo certifica fibras que la auditoría exacta también aceptaría, la salida es idéntica.

Qué medir antes de celebrar: **porcentaje de muestras certificadas sin evaluar**. Si se evita el 80–90 % de las 705k auditorías, quedan 70–140k llamadas; sumando el coste base y los lifts, el paso queda en **~370–500 ms**. Si se certifica poco, es porque `clearance` es demasiado conservador (incluye el sobre de la guía y `rootFullness`); en ese caso pasar a 5.2.

Riesgo: usar `clearance` como si fuera la distancia exacta. No lo es: es un radio conservador por segmento. Hay que respetar el segmento al que pertenece cada `t` y no interpolar `clearance` como si fuera suave.

Aviso importante para la raíz: `clearance` descuenta `.003·scale` más el sobre de la guía. Cerca de la piel el radio puede quedar en cero aunque la fibra sea segura, así que es probable que 5.1 no certifique los primeros tramos. Medir la certificación **por tramo**: si la parte distal se certifica y la proximal no, auditar sólo la proximal (por ejemplo, las primeras 12 muestras) y certificar el resto. Si la certificación proximal es mala, pasar a 5.2, que usa distancias exactas y no descuenta el sobre.

### 5.2 Precalcular distancias exactas por guía

Si 5.1 certifica poco:

- evaluar el SDF en 72 parámetros de cada una de las 288 guías: **20.736 llamadas (~35 ms)**;
- para cada fibra, acotar su distancia a las dos guías con
  `|p(t)-A(t)| <= w·|B(t)-A(t)| + |lift| + ...`
  y, si el campo se comporta como distancia 1-Lipschitz,
  `d(p) >= d(A) - |p-A|`;
- aceptar sin auditoría cuando la cota supere `.0005·scale` con margen;
- auditar exactamente sólo las fibras ambiguas.

Coste esperado: 20.736 + 10–20 % de auditorías (~70–140k) + 51k de lifts/proyecciones + 101k de base ≈ **240–310k llamadas → 400–530 ms**.

Advertencia importante: `src/MonsterSDF.c` documenta explícitamente que los intervalos **no suponen derivadas acotadas de una distancia aproximada**. No se puede afirmar 1-Lipschitz sin justificarlo. Opciones:

- usar `SDFInterval` donde exista una cota del campo,
- o validar empíricamente la cota contra la auditoría exacta en miles de fibras y aplicar un margen de seguridad,
- y mantener siempre la auditoría exacta como fallback para las fibras dudosas.

Nunca aceptar una fibra sólo con una cota no verificada.

### 5.3 Paralelizar el pase de guías y folículos

Cada guía escribe únicamente en su propio `follicles[64][4]`; la búsqueda de vecinos sólo lee; la evaluación del SDF y de la mandíbula es de sólo lectura. El proyecto ya tiene `SDFSamplingPool` ([`include/SDFSamplingPool.h`](include/SDFSamplingPool.h)), usado por el mesher y el pipeline de morph.

- partición fija por índice de guía (no work-stealing) para conservar determinismo;
- pool persistente, no crear/destruir por paso;
- cuidado con pools anidados: `MonsterVisualAsync` ya tiene uno;
- verificar la vida útil del `context` prestado (SDF y campo de mandíbula).

Rendimiento esperado en Renoir (8 hilos): **4–8×**. Solo, 1,47 s → 190–370 ms. Combinado con 5.1 o 5.2, puede bajar de 100 ms.

### 5.4 Reutilizar entre pasos de crecimiento

Los 21 pasos reconstruyen las mismas 288 guías; el orden y las semillas son estables y la anatomía cambia suavemente.

- cachear `{vecina, peso, lift}` por `groupIndex + guideIndex + stableRootId`;
- revalidar el descriptor cacheado con 5.1/5.2;
- volver a buscar sólo las guías cuyo descriptor falla.

No vale reutilizar sin revalidar: una fibra que era segura en el paso anterior puede penetrar en el siguiente. Además, la selección de vecina puede cambiar aunque la identidad de la guía se conserve. Cualquier reutilización que altere la vecina elegida cambia la imagen y debe revisarse.

El límite inferior de esta vía es el coste base de guías (~170 ms) si se sigue reconstruyendo todo. Para bajar de 100 ms hay que reducir también ese coste base o paralelizarlo.

### 5.5 Reducir el coste por evaluación SDF

Cada llamada cuesta 1,6–2,2 µs (típico 1,7–1,9). Líneas posibles:

- revisar la unión con la mandíbula y sus early-outs de AABB,
- evitar subexpresiones repetidas en `MonsterSDF_EvaluateDistance`,
- culling por volúmenes conservadores antes de evaluar,
- evaluar en lote o reutilizar resultados por proximidad.

Es la vía de menor prioridad: cambiar el campo puede cambiar la imagen y la ganancia por llamada es limitada. Primero reducir el número de llamadas.

### 5.6 Lo que no funciona

- **Submuestrear la auditoría** (por ejemplo, 8 puntos en vez de 72): puede dejar pasar penetraciones entre muestras. La auditoría actual tampoco es una prueba continua, pero reducida es peor.
- **Eliminar los reintentos**: sólo afectan al 5,7 % de las fibras; el ahorro es pequeño y se pierde el seguro para raíces cerca de la piel.
- **Relajar los filtros de normal/flujo**: aparecen fibras cruzadas y el problema visual vuelve.
- **Optimizar la búsqueda de vecinos**: es aritmética barata; no mueve la aguja.
- **Tocar `MonsterSDF_Build`**: 0,06 ms; no es el cuello de botella.

---

## 6. Plan de ataque recomendado

1. **Reproducir el baseline** con el desglose de la sección 7 y registrar llamadas SDF, no sólo ms. Guardar etapa 0–3.
2. **Instrumentar** temporalmente contadores de: auditorías, lifts, proyecciones iniciales, reintentos, llamadas de colisión y µs/llamada. No dejar logging verboso en la versión final.
3. **Implementar 5.1** (contención en el tubo certificado). Medir el porcentaje de muestras certificadas y las llamadas restantes. Es la opción con mejor relación impacto/riesgo.
4. Si no alcanza 100 ms, **implementar 5.2** (distancias por guía) y **5.3** (pool de hilos).
5. Después, **5.4** (caché entre pasos) si el crecimiento sigue siendo el objetivo principal.
6. **5.5** sólo si el número de llamadas ya es bajo y el coste por llamada vuelve a dominar.
7. Validar después de cada paso; no acumular cambios sin medir.

Cada iteración debe responder: **¿cuántas llamadas SDF quedan por reconstrucción y cuántas se certifican sin auditar?**

---

## 7. Cómo reproducir las medidas

La captura oficial imprime el desglose en `capture.log`:

```sh
make build/capture_long_hair
build/capture_long_hair \
  docs/validation/long-hair-styling/caballo-crin-foliculos.creature.json \
  /tmp/crin-stage-3 3
```

Líneas relevantes:

```text
roots 288 visible 288 root_build_ms 1522.108887 ...
follicles 9216 max_skin_offset_error 3.44077125e-06 ...
collision_children samples 230400 max_sdf_penetration 0
guide_gpu_parity 1 max_error 1.44656428e-06
morph_cpu_ms 0.097796 morph_hair_update_ms 1473.963645 steps 21
```

Para separar SDF, pelo y folículos, el banco temporal hace:

```c
MonsterSDF sdf = MonsterSDF_Create();
MonsterSDF_Build(&sdf, m, MonsterSDF_DefaultConfig());

LongHairSet hair = {0};
double t0 = Now();
LongHairSet_Build(&hair, m->surface.longHair, m->surface.longHairCount,
                  &m->anatomyGraph, scale,
                  MonsterSDF_EvaluateDistanceWrapper, &sdf);
double hairMs = Now() - t0;

/* Repetir con una copia de los grupos y rootFullness = 0
   para aislar el coste base (guías + colisión + certificado). */
```

Para contar llamadas, envolver la función de distancia:

```c
static unsigned long EvalCalls;
static float CountingDistance(const void* ctx, Vector3 p) {
    ++EvalCalls;
    return MonsterSDF_EvaluateDistanceWrapper(ctx, p);
}
```

y pasar `CountingDistance` a `LongHairSet_Build`. El contador se reinicia antes de cada construcción.

Puntos exactos donde instrumentar dentro del bloque de folículos:

- proyección inicial de la raíz (bucle de 12 iteraciones),
- lift dentro de cada intento,
- bucle `q = 1..72` de la auditoría,
- reintentos por fibra (histograma 0..4).

Comparar siempre **mejor de 3 repeticiones** y reportar también el número de llamadas. La captura tarda lo suyo por las 300 poses; el banco aísla la reconstrucción.

---

## 8. Criterios de aceptación

| Criterio | Objetivo | Baseline actual |
|---|---:|---:|
| `root_build_ms` (etapas 0–3) | ≤100 ms | 1.506–1.543 ms |
| `morph_hair_update_ms` | ≤100 ms | 1.474 ms (etapa 3) |
| 21 pasos de crecimiento | ≤2,1 s | ~31 s |
| `follicles` | 9.216 | 9.216 |
| `guides_falling_positive_x` | 288/288 | 288/288 |
| `collision_children` penetración | 0 | 0 |
| `guide_gpu_parity` | 1, error <2e-5·scale | 1, 1,45e-6 |
| `max_skin_offset_error` | ≤ baseline por etapa | 8,65e-7–3,44e-6 |
| `min_parent_separation` | ≥ baseline por etapa | 1,85e-4–4,09e-4 |
| Identidades entre etapas | estables | estables |
| `make test` | pasa | pasa en ejecución aislada |
| `make editor-test` | 10/10 | 10/10 |
| Imágenes side/front/rear/root | idénticas o mejora revisada | referencia |

Objetivo deseable: ≤50 ms por paso, 21 pasos ≤1,05 s.

Reportar además `runtime_bytes` y `gpu_bytes`. El baseline es 476.292 B de runtime y 368.640 B de buffer GPU; `LongHairGuide` ya pesa 1.600 B y no conviene añadir otra caché de 64 elementos por guía sin justificar memoria.

---

## 9. Trampas conocidas

- **La auditoría de 72 muestras es un muestreo, no una prueba continua.** Una optimización no debe convertirla silenciosamente en un muestreo más pobre. Si se añade una cota, debe ser conservadora y verificable.
- **`clearance[p]` no es `d(p)`.** Es un radio libre conservador por segmento, con el sobre de la guía ya descontado. No interpolarlo como una distancia suave.
- **`rootFullness` tiene otros significados** en dominios que no son curva (apertura/volumen). No cambiar su semántica global para optimizar la crin.
- **Los filtros de normal y flujo** son los que evitan fibras cruzadas; relajarlos para ganar velocidad reabre el bug visual.
- **El fallback tubular** es obligatorio para vecinas inválidas y dominios no-curva. No eliminarlo.
- **El certificado de colisión** ya usa un margen de velocidad; no sustituirlo por una comprobación sólo en los nodos.
- **La huella incluye animación, cabeza y boca.** Una caché indexada sólo por anatomía se queda obsoleta.
- **`LongHairSet_Build` es transaccional.** Si la nueva construcción falla, el runtime anterior debe seguir siendo válido.
- **Paralelizar no puede cambiar el orden de las semillas.** Cada hijo debe consumir su RNG de forma independiente del scheduling.
- **La captura usa la unión con la mandíbula**; el banco corporal puede dar cifras algo menores. Comparar siempre la misma configuración.
- **El primer `make test` concurrente con capturas falló una vez `AppendageMeshContinuity`** por espera de malla LOD. Ejecutar los tests en aislado y repetir antes de atribuir el fallo a una optimización.

---

## 10. Apéndice: por qué el coste base también importa

El pase de folículos son ~756k llamadas, pero el coste base de guías + colisión + certificado son **101.097 llamadas (~170 ms)**. Aunque se eliminara por completo la auditoría de folículos, el paso seguiría por encima de 100 ms.

Por eso el objetivo de 100 ms exige dos cosas a la vez:

1. reducir las llamadas del pase de folículos (5.1/5.2),
2. reducir o paralelizar el coste base (5.3/5.4).

Presupuesto combinado a 1,68 µs/llamada:

| Escenario | Llamadas | 1 hilo | 4 hilos | 8 hilos |
|---|---:|---:|---:|---:|
| Actual | 857k | 1,44 s | 360 ms | 180 ms |
| Auditoría −90 % | 223k | 375 ms | 94 ms | 47 ms |
| Auditoría −90 % + base −50 % | 172k | 289 ms | 72 ms | 36 ms |
| Auditoría −95 % + base −50 % | 137k | 230 ms | 58 ms | 29 ms |

La fila «Auditoría −90 %» es realista si 5.1 certifica la mayoría de fibras. La paralelización (4–8 hilos) es la que cruza el umbral de 100 ms; con 8 hilos hay margen. Sin paralelizar, hay que reducir aún más las llamadas.

**Métrica de éxito final:** `morph_hair_update_ms ≤ 100` en la captura de las 4 etapas, con las mismas 288 guías, 9.216 folículos, 0 penetraciones y paridad CPU/GPU.
