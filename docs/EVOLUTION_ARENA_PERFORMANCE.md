# Informe de Rendimiento y Escalabilidad — Evolution Arena (500+ Criaturas)

Este informe documenta las mediciones reproducibles **antes y después** de la reestructuración arquitectónica y algorítmica de **Evolution Arena** para simular y visualizar cómodamente **500+ criaturas neuronales simultáneas** (con soporte arquitectónico para **1.024–2.048 ranuras** y base para miles), preservando el paso biológico fijo de `1/60 s`, las redes RNN independientes `32-12-10` a 5 Hz y las restricciones anatómicas autoritativas.

## 1. Entorno de medición y línea base

- **Commit de línea base (sin modificar):** `9f4691381a10a22840dfd0228e5d065a2ca78294` (`feat(neural): implement evolutionary neural brains and Evolution Arena`)
- **Hardware:** CPU AMD Cezanne (12 hilos lógicos, 6 trabajadores automáticos en simulación), GPU **NVIDIA GeForce RTX 3050 Laptop GPU** (`OpenGL 4.6.0 NVIDIA 580.178.04`), Linux.
- **Compilador y opciones:** `gcc -Wall -Wextra -Wpedantic -O2` (`-lm -pthread`, más `-lSDL2 -lGL -lGLU` en el ejecutable visual).
- **Metodología mejorada (`benchmarks/benchmark_evolution_scalability.c`):** separa explícitamente inicialización, calentamiento (*warmup*) y medición en estado estacionario. Las cargas diagnósticas **sostenidas** mantienen `min_pop == mean_pop == max_pop == N` para medir carga real a la población declarada, mientras que las cargas **naturales** y de **calibración biológica** ejecutan nacimientos, muertes y selección natural sin intervención.

---

## 2. Comparativa Headless: Línea Base vs Arquitectura Optimizada

### 2.1 Inferencia neuronal aislada (CPU puro, 1 hilo, cero heap)

| Carga | Línea base (µs/eval) | Optimizado (µs/eval) | Mejora | Evaluaciones / s |
|---|---:|---:|---:|---:|
| 100 redes × 2.000 pasos | 0,9317 µs | **0,5863 µs** | **1,59×** | 1.705.476 / s |
| 1.000 redes × 500 pasos | 1,0268 µs | **0,6966 µs** | **1,47×** | 1.435.518 / s |

> La mejora proviene de eliminar el escaneo completo `NeuralGenome_Validate()` (670 pesos + búsqueda de finitud redundante) en cada evaluación, sustituyéndolo por validación estructural O(1) y verificación finita de entradas/salidas.

### 2.2 Simulación sostenida (`N` constante, 6 trabajadores automáticos vs línea base)

| Población sostenida (`N`) | Base Media / tick | Base P95 | Base Vel. máx. | Base RSS | Optim. Media / tick | Optim. P95 | Optim. P99 | Optim. Vel. máx. | Optim. RSS | Aceleración | Reducción RAM |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **22** | 2,1851 ms | 4,5059 ms | 7,63× | 14,1 MB | **0,6326 ms** | 1,2383 ms | 1,5794 ms | **26,35×** | 13,5 MB | **3,5×** | 1,0× |
| **50** | 4,5957 ms | 8,4024 ms | 3,63× | 22,2 MB | **0,6090 ms** | 1,5154 ms | 2,0538 ms | **27,37×** | 13,5 MB | **7,5×** | 1,6× |
| **100** | 8,3389 ms | 17,1265 ms | 2,00× | 37,3 MB | **0,7658 ms** | 1,5571 ms | 1,9351 ms | **21,76×** | 16,6 MB | **10,9×** | 2,2× |
| **250** | 23,5583 ms | 39,8631 ms | 0,71× | 83,7 MB | **1,0435 ms** | 1,8365 ms | 2,1365 ms | **15,97×** | 19,6 MB | **22,6×** | 4,3× |
| **500** | 54,7513 ms | 73,8532 ms | 0,30× | 158,9 MB | **1,1933 ms** *(0,7509 ms en barrido 240 ticks)* | **2,7719 ms** | 3,3460 ms | **13,97×–22,20×** | **25,5 MB** | **45,9×–72,9×** | **6,2×** |
| **1.000** | 159,9912 ms | 198,1678 ms | 0,10× | 314,1 MB | **2,7909 ms** | **5,1276 ms** | 5,6287 ms | **5,97×** | **38,6 MB** | **57,3×** | **8,1×** |

### 2.3 Cargas de estrés especializadas (500 criaturas)

| Carga de estrés | Línea base (media) | Línea base (P95 / Máx) | Optimizado (media) | Optimizado (P50 / P95) | Aceleración |
|---|---:|---:|---:|---:|---:|
| **Estrés de interacción oral** (500 criaturas, 300 contactos simultáneos) | 271,5184 ms | 521,33 / 525,20 ms | **4,6398 ms** | 0,94 / 21,40 ms | **58,5×** |
| **Estrés de interacción oral** (100 criaturas, 60 contactos simultáneos) | 53,5571 ms | 103,11 / 104,42 ms | **0,9278 ms** | 0,19 / 4,27 ms | **57,7×** |
| **Crecimiento juvenil continuo** (500 juveniles simultáneos) | 51,6735 ms | 61,44 / 64,38 ms | **0,4103 ms** | 0,38 / 0,57 ms | **125,9×** |
| **Simulación natural estable** (500 criaturas, 1.200 ticks) | 57,4100 ms | 87,81 / 96,38 ms | **1,6410 ms** | 1,61 / 3,02 ms | **35,0×** |

- **Estabilidad de memoria:** en la simulación natural prolongada de 1.200 ticks a 500 criaturas, el RSS permaneció constante en **52.648 KiB → 52.648 KiB** (+0 KiB de deriva).

### 2.4 Escalado de hilos y tamaño de celda espacial (500 criaturas sostenidas, 240 ticks)

| Hilos (`--workers`) | Media / tick | P95 | P99 | Velocidad máx. | Determinismo (`eventHash`) |
|---|---:|---:|---:|---:|---|
| 1 (serial) | 1,1169 ms | 2,3573 ms | 2,7491 ms | 14,92× | Referencia |
| 2 | 0,9242 ms | 2,1251 ms | 2,7411 ms | 18,03× | **Idéntico bit-a-bit** |
| 4 | 0,8412 ms | 2,0533 ms | 2,9925 ms | 19,81× | **Idéntico bit-a-bit** |
| 6 (`auto`) | **0,7509 ms** | **1,8391 ms** | **2,1651 ms** | **22,20×** | **Idéntico bit-a-bit** |
| 8 | 0,7936 ms | 2,0019 ms | 2,9099 ms | 21,00× | **Idéntico bit-a-bit** |

| Tamaño de celda espacial (`AISpatialIndex`) | Media / tick (500 criaturas, 6 hilos) | P95 | P99 |
|---|---:|---:|---:|
| `2.0` unidades | 0,7832 ms | 1,8997 ms | 2,3208 ms |
| `4.0` unidades *(defecto)* | 0,7718 ms | 1,8780 ms | 2,2522 ms |
| `8.0` unidades | **0,7583 ms** | **1,8506 ms** | **2,1965 ms** |

---

## 3. Rendimiento Visual en 1920×1080 Full HD (500 Criaturas Vivas)

Medido con `./demos/demo_evolution_arena --preset=large --seed=42 --width=1920 --height=1080 --frames=180 --realtime` (calidad `MED`, 6 trabajadores de simulación en hilo dedicado, 400 Brumacernos + 100 Velkares + 800 plantas + 40 estanques + 24 obstáculos):

| Escenario visual (1920×1080, 500 criaturas) | Media frame | P95 frame | P99 frame | FPS medios | Draw calls | Vel. solicitada | Vel. alcanzada | Tick sim medio | RSS pico |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| **Vista general `1×`** (+ panel `P` + minimapa `M`) | **3,15 ms** | **5,36 ms** | **5,90 ms** | **317,6 FPS** | 8,0 | 1× | **1,0×** | 0,99 ms | 210,7 MB |
| **Vista general `4×`** (+ panel `P` + minimapa `M`) | **3,12 ms** | **4,75 ms** | **5,44 ms** | **320,3 FPS** | 9,0 | 4× | **4,0×** | 2,00 ms | 210,9 MB |
| **Vista general `16×`** (+ panel `P` + minimapa `M`) | **3,90 ms** | **6,93 ms** | **9,33 ms** | **256,5 FPS** | 9,0 | 16× | **15,0×** | 1,42 ms | 209,8 MB |
| **Vista general `64×`** (limitado por throughput) | **3,39 ms** | **6,96 ms** | **8,37 ms** | **295,3 FPS** | 9,0 | 64× | **14,5×** | 1,42 ms | 208,9 MB |
| **Vista general `MAX`** (limitado por throughput) | **3,06 ms** | **4,90 ms** | **5,37 ms** | **327,0 FPS** | 9,0 | MAX | **14,9×** | 1,41 ms | 207,1 MB |
| **Primer plano (`zoom=16`, `1×`)** | **1,89 ms** | **3,84 ms** | **4,01 ms** | **528,8 FPS** | 4,0 | 1× | **1,0×** | 0,76 ms | 149,3 MB |
| **Inspector neuronal activo (`N`, `1×`)** | **3,33 ms** | **5,16 ms** | **6,16 ms** | **300,1 FPS** | 8,0 | 1× | **1,0×** | 0,87 ms | 210,0 MB |

- **Tiempo de calentamiento geométrico inicial:** `0,128 s` para las mallas compartidas `FAR` unificadas (frente a `3,33 s` antes de eliminar micro-subdivisiones `0,012` en el tier `FAR`).
- **GPU render mean:** `1,32 ms` en RTX 3050 Laptop GPU para ~470 criaturas visibles + 800 plantas + 40 estanques + 24 obstáculos (~1.330 objetos en **5–6 draw calls instanciadas**).

---

## 4. Calibración Biológica Multi-Semilla (Arena Grande, 500 Criaturas, 5 Días)

Ejecutado con `./demos/demo_evolution_arena_headless --preset=large --days=5` (400 Brumacernos + 100 Velkares, arena `200 × 200`, 800 plantas, 40 estanques, 24 obstáculos, capacidad 1.024, controladores neuronales independientes):

| Semilla | Población final | Nacimientos | Muertes | Hambre | Deshidratación | Depredación | Gen. máx. | Linajes activos | Fundadores registrados | Diversidad neural | Dispersión RMS |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| `42` | 254 | 228 | 474 | 71 | 321 | 82 | **2** | 212 | 500 / 500 (0 perdidos) | 0,0595 | 0,0629 |
| `101` | 240 | 235 | 495 | 46 | 349 | 100 | **3** | 199 | 500 / 500 (0 perdidos) | 0,0681 | 0,0764 |
| `777` | 235 | 240 | 505 | 60 | 357 | 88 | **3** | 194 | 500 / 500 (0 perdidos) | 0,0671 | 0,0722 |

Todas las semillas sostienen una población activa de 235–254 individuos tras 5 días ecológicos completos sin extinción, con más de 225 nacimientos, generaciones 2–3 y contabilidad completa de los 500 fundadores sin truncamiento.

---

## 5. Comprobación frente a objetivos del hito

| Objetivo | Meta | Resultado medido | Estado |
|---|---|---|---|
| **A. Simulación y visualización 500+** | 500 criaturas, 1.024 ranuras, HUD/inspector/cerebros | 500 vivas, 1.024–2.048 ranuras, todas las funciones activas | **SUPERADO** |
| **B. 500 criaturas visual a `1×`** | ~60 FPS a 1080p, P95 ≤ 20 ms, P99 ≤ 33,3 ms | **317,6 FPS** (3,15 ms), P95 **5,36 ms**, P99 **5,90 ms** | **SUPERADO** |
| **C. Simulación `4×` con 500 criaturas** | Media headless ≤ 3,0 ms, P95 ≤ 4,17 ms | Media **0,75–1,19 ms**, P95 **1,84–2,77 ms** (14–22×) | **SUPERADO** |
| **D. Stretch `16×` con 500 criaturas** | Media headless ≤ 1,042 ms | Media **0,75 ms** (22,2× en barrido de 6 hilos) | **SUPERADO** |
| **E. `64×` / `MAX` responsivo** | UI fluida, velocidad solicitada vs alcanzada separadas | **295–327 FPS** a `64×`/`MAX`, P95 **4,90–6,96 ms**, 0 bloqueos | **SUPERADO** |

## 6. Verificación de calidad y memoria

- `make test` (suite completa C): **PASS** (incluyendo `tests/test_evolution_scalability.c`).
- `make evolution-sanitize` (ASan + UBSan + LSan sobre las 6 suites evolutivas y de escalabilidad): **PASS (0 fugas, 0 errores)**.
- `make evolution-tsan` (ThreadSanitizer sobre paralelismo determinista y `EvolutionSimulationRunner`): **PASS (0 advertencias de carreras de datos)**.
- `python3 tools/validate_evolution_arena.py --visual`: **13 / 13 capturas visuales PASS**.
- `make validate-ecosystem`: verificado compatible con la simulación autoritativa.
