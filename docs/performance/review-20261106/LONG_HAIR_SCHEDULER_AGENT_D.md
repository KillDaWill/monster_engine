# Scheduler determinista para pelo largo — Agente D

## Alcance

Este cambio añade un scheduler independiente para que Agent F pueda paralelizar fases de
`LongHairSet_Build`. No modifica ni integra `LongHair.c`, los visuales, el pool compartido,
ni el Makefile. Cada scheduler posee un `SDFSamplingPool` privado y persistente; por ello
no comparte `currentWorkFn/currentContext/jobGeneration` con los pools del mesher o morph.

## API y garantías

`include/LongHairScheduler.h` expone `LongHairScheduler_ParallelFor` para una fase y
`LongHairScheduler_RunPhases` para fases con barrera entre cada par. Las particiones son
contiguas y fijas; el índice de partición es estable y se entrega al callback. El callback
debe escribir únicamente su rango y no depender del tamaño de partición.

- A lo sumo un job por scheduler; una segunda llamada concurrente obtiene `BUSY`.
- Un callback que reingresa directamente al scheduler TLS actualmente activo obtiene
  `NESTED`. En la cadena A->B->A sólo B está en TLS al hacer la llamada interior; A ya tiene
  trabajo activo y la llamada devuelve `BUSY`, no `NESTED`.
- `Destroy` cierra la admisión, espera al job activo y después une/libera los workers, pero
  **no es una barrera de vida útil general**. Antes de llamarlo, el propietario debe impedir
  nuevos accesos y esperar/join a todos los submitters y lectores que ya pudieron entrar
  (`RunPhases`/`ParallelFor`, `IsRunning`, `GetThreadCount`, `GetStats`, `ResetStats`). La
  única llamada API permitida en vuelo es el job activo, que `Destroy` espera. Debe existir un
  solo `Destroy` coordinado por el propietario. No se soporta una destrucción arbitrariamente
  concurrente ni llamadas API que compitan por el mutex durante su destrucción. El test cubre
  el protocolo válido de job activo con lectores/submitters externos quiescentes.
- `Destroy` desde cualquier callback de cualquier scheduler se rechaza sin liberar el objeto.
- El retorno es síncrono: callback, contexto prestado y datos SDF/jaw que utilice el callback
  deben permanecer vivos hasta el retorno. Un callback fallido produce salida parcial que
  el llamador debe descartar.
- No se implementa cancelación entre fases ni rollback; el propietario mantiene la
  transacción de `LongHairSet_Build`.

Para Agent F, mantener roots/guides, colisión/certificados y folículos como fases distintas
de una sola llamada `RunPhases`. Así cada fase ve completos los resultados de la anterior.
El empaquetado GPU y toda llamada OpenGL siguen en el hilo gráfico.

## Presupuesto de hilos

El valor automático por defecto está limitado a 4 participantes (incluido el llamador);
el límite absoluto configurable es 8. La creación fallida del pool cae a un participante
serial salvo que `requireParallel` sea verdadero. El límite reduce, pero no elimina, la
contención: el scheduler no consulta ni coordina la ocupación del pool del mesher/morph ni
reserva CPUs. El integrador debe elegir explícitamente 2–4 participantes si esos workers
pueden coincidir; no debe asumir escalado lineal ni reintentar `BUSY` en bucle.
`requireParallel` sólo falla si el número de participantes resuelto es mayor que 1 y no se
puede crear un pool con al menos un worker; una solicitud resuelta a 1 (incluido
`threadCount=1`) se honra como serial aunque `requireParallel=true`.

## Verificación independiente

El test no requiere modificar el Makefile. Desde la raíz del repositorio:

```sh
gcc -Wall -Wextra -Wpedantic -O2 -DLONG_HAIR_SCHEDULER_STANDALONE \
  -Iinclude -Itests src/LongHairScheduler.c src/SDFSamplingPool.c \
  tests/test_long_hair_scheduler.c -lm -pthread -o /tmp/long_hair_scheduler_test
/tmp/long_hair_scheduler_test
```

La suite cubre rangos y determinismo 1/2/4/8, fases con barrera, fallos, anidamiento directo
y el retorno A->B->A, dos llamadores sin destrucción, el protocolo de destrucción con un job
activo y el resto de los llamadores quiescentes, ciclos de vida, persistencia de workers,
schedulers independientes y trabajo desbalanceado. En esta rama pasó tanto sin sanitizador
como con ASan/UBSan y TSan (este último requiere desactivar ASLR con `setarch -R` en el
entorno actual).

## Medición sintética y recuperación del coordinador — 2026-11-06

El agente completó una primera medición antes de detenerse por cuota de autenticación.
Su CSV y ejecutable se conservaron intactos en
`/mnt/storage/monster-engine-agents/scratch/perf-20261106-d/`. Ese CSV sólo emite
agregados; no contiene muestras individuales. El coordinador revisó el benchmark y añadió
salida de muestras y comprobación de paridad después de cada medición, fuera del timer.
No modificó `LongHairScheduler.c`, su header ni `SDFSamplingPool.c`.

La nueva corrida controlada está en `coordinator-recovery/` bajo ese mismo directorio.
Máquina: AMD Ryzen 5 5600H, 12 CPUs lógicas; GCC 13.3.0, flags
`-std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude`, link `-lm -pthread`.
Carga 1/5/15 minutos antes: 1.489/1.031/1.012; después: 1.530/1.047/1.018.
No había otro benchmark, compilación o captura del programa durante las muestras; la carga
ambiental del escritorio no se eliminó. No se fijó afinidad ni frecuencia CPU.

Se crean pools privados persistentes antes de medir, uno por configuración; el llamador
participa. Ambas cargas procesan los mismos 288 elementos en 1/2/4/8 participantes efectivos.
`dispatch_copy` copia los datos; `readonly_compute` ejecuta 6.000 iteraciones dependientes
de sinf/cosf por elemento sobre entrada de sólo lectura y escribe únicamente su salida.
No se evalúa un SDF ni se construye pelo. Cada configuración usa 3 warmups y 21 muestras,
con p50/p95 por nearest-rank (rangos 11/20 de 21). La comprobación bit a bit por muestra,
hash final y checksum coincidieron con la referencia serial en los ocho casos. El
coordinador verificó las 168 muestras y recalculó los cuantiles desde el CSV.

| Participantes efectivos | Compute p50 / p95 / max (ms) | Dispatch-copy p50 / p95 / max (µs) |
|---:|---:|---:|
| 1 | 19.499196 / 19.690314 / 19.712154 | 0.130 / 0.140 / 0.170 |
| 2 | 9.787328 / 9.910249 / 10.031486 | 10.550 / 11.592 / 26.039 |
| 4 | 5.013899 / 5.523963 / 5.745849 | 16.220 / 28.994 / 32.211 |
| 8 | 2.677739 / 3.345067 / 3.429826 | 18.094 / 23.814 / 24.556 |

La escalabilidad del compute sintético no demuestra aceleración del constructor real.
El overhead del dispatch hace claramente inadecuado paralelizar esta copia diminuta.
No se inyectó contención con mesher/morph ni dos criaturas simultáneas. El número de
threads del proceso tras crear cada pool fue 1/2/4/8; el presupuesto conservador por
defecto del scheduler sigue siendo 4, no 8.

La estimación de memoria del CSV refleja un layout espejo de metadata de pool (696 B
con pool, 0 en serial) más 8.388.608 B de reserva nominal de stack por worker adicional:
8/24/56 MiB para 2/4/8 participantes. No es RSS ni memoria física comprometida; omite
guard pages, objeto scheduler y otros recursos de pthread. No se midió RSS en esta corrida.

Reproducción desde la raíz del worktree D, con macros de identidad explícitas:

```sh
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude \
  '-DLONG_HAIR_BENCH_GIT_SHA="4907fab2ea1c7b1c02d8170efdebad0dfa56eb72+benchmark-recovery"' \
  '-DLONG_HAIR_BENCH_CFLAGS="-std=c11 -Wall -Wextra -Wpedantic -Werror -O2 -Iinclude"' \
  benchmarks/benchmark_long_hair_scheduler.c src/LongHairScheduler.c \
  src/SDFSamplingPool.c -lm -pthread -o /tmp/opencode/benchmark_long_hair_scheduler
/tmp/opencode/benchmark_long_hair_scheduler
```

`manifest.json` registra comando, hashes de fuentes/binario/CSV y entorno de la corrida;
`coordinator-verification.json` registra las verificaciones independientes.
SHA-256 benchmark fuente: `ac9d01e31f18c77b920c97a94524e448ba9585d05538429d96ee2508bdbc74dd`;
CSV: `43af0f0ee956d850aadb700b3ff06cc1e20972f5b048a61927df254e56a4d689`;
binario: `1fb1d5aba2995c8a81c2ed936e35eaea5ed06bbe74b912305252991dd54ebbe6`.

**Handoff:** helper aún sin integrar; no se ha ejecutado con SDF real ni demostrado paridad
de descriptores de pelo integrados. La certificación E sigue NO-GO y F permanece bloqueado
por el grafo original. Estos resultados sintéticos no satisfacen la meta de 100 ms de crin
ni autorizan una excepción de dependencia para paralelizar la auditoría exacta.
