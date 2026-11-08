# Coordinación — programa de optimización de pelo largo y construcción procedural

## Reanudación autoritativa — 2026-11-06

Esta sección sustituye los estados de ejecución históricos de abajo; no implica aceptación de trabajo previo.

- `BASE_SHA` común: `0eb37d709433aa08ef6c3416cd76edf1a26108f0`; respecto a `bc83fd5` solo añade este ledger.
- Modelo exclusivo de agentes: `openai/gpt-6-luna#high`, resuelto mediante catálogo; `opencode auth list` confirma autenticación OpenAI almacenada. No se usan agentes OpenRouter.
- Integración: rama `agent/perf-20261106-integration`, worktree `/tmp/opencode/monster-perf-20261106/integration`. Ningún commit directo en main.
- Se conserva intacto el estado del checkout original: eliminación local del ledger y archivos sin seguimiento `PROMPTS.md` / `OPTIMIZACION_CRECIMIENTO_PELO.md`.
- Worktrees antiguos A/B contienen archivos sin seguimiento; D contiene `4a84de022e5b53e672908f81d55eb484403925a1`. Se consultan solo como referencia; no se sobrescriben ni se consideran aceptados.
- Faltan el INFORME solicitado y el README del paquete; el README raíz es del motor. Dependencias/ownership: fuente autoritativa `PROMPTS.md` original.
- Los checkouts completos agotaron temporalmente el disco; se retiraron exclusivamente los checkouts nuevos del coordinador y se recrearon dispersos sin `docs/validation`. Las referencias originales siguen intactas y se leen por ruta absoluta. Tras recuperación: raíz con 5,4 GB libres; `/mnt/storage` con 1,4 TB libres.

### Agentes activos y ownership exclusivo

Prefijo de worktree: `/tmp/opencode/monster-perf-20261106/`; prefijo de rama: `agent/perf-20261106-`.

| Agente | Sufijo rama/worktree | Sesión | Archivos propios | OUTPUT_SHA / estado |
|---|---|---|---|---|
| A | `a` | `ses_e4f7f9e5effemqGxjWS62ErwXE` | benchmark_long_hair_build.c, compare_long_hair_baseline.py, informe A, test_long_hair_regression_*, helper nuevo agent_a_build_benchmark.sh autorizado | `849e88fa145d51a03fb441b17adb8699181675c0` / baseline CPU revisado; slot liberado |
| B | `b` | `ses_e4f7f9db1ffeOHug1o4IOw53SQ` | LongHairFieldQuery.h/.c, test_long_hair_field_query.c | `d1e7d536cd96ab2b96337bada483612e19e7101f` / helper seguro revisado; cobertura pendiente |
| C | `c` | `ses_e4f7f9d69ffeS7HlZGySJx7c1G` | HairCurveBounds.h/.c, test_hair_curve_bounds.c | `c5403db35e4abc981c5e103c4ae981f70108aca5` / helper revisado con dominio explícito |
| D | `d` | `ses_e4f7f9d66ffefERHmGYfFdtYFJ` | LongHairScheduler.h/.c, test_long_hair_scheduler.c, nota propia y benchmark propio | `feadfb73fe520ae10d1443284e2cc963483a649f` / helper y benchmark revisados; sin integrar |
| J | `j` | `ses_e4f7f9d63ffeWf3sVU4IWi7j2a` | benchmark_creature_construction.c, informe J, benchmark_creatures.py opcional | `bd22d641b0b8a867f4e7e813716a0312c8135877` / revisión de esquema resuelta; medidas pendientes |

Los cinco recibieron instrucciones de leer su sección completa autocontenida de `PROMPTS.md`, respetar invariantes y devolver todos los campos del protocolo. No hay ownership compartido. Los archivos runtime compartidos y Makefile siguen bloqueados.

### Reserva secuencial de máquina

Durante desarrollo concurrente: solo compilaciones pequeñas y pruebas aisladas acotadas; sin benchmarks, capturas GPU ni suites pesadas. ACTUAL: A completó y liberó su ranura exclusiva (3 warmups + 5 muestras por etapa y ablación adulta). Artefactos en `/mnt/storage/monster-engine-agents/scratch/perf-20261106-a/controlled-baseline`. Sin full tests/capturas GPU en esa ranura. C/D/J permanecen sin autorización de timings; próximas ranuras deben coordinarse también con E.

Orden reservado, condicionado a que todos los demás trabajadores estén inactivos:

1. A: baseline cuerpo+mandíbula, cuatro etapas, calentamiento y repeticiones.
2. C: overhead de cotas/subdivisión.
3. D: scheduler sintético 1/2/4/8 hilos.
4. J: matriz de especies secuencial.
5. Coordinador: suite completa/editor/capturas oficiales secuencialmente y revisión de evidencia.

No basta un lock entre benchmarks: tampoco puede coexistir una medición oficial con compilaciones, sanitizadores o capturas pesadas. Las medidas en contención no se aceptan como baseline.

### Puertas vigentes

**Reanudación autorizada por usuario:** “continue with the work, the last agent halted, resume your previous work”. Se abre seguimiento acotado de pruebas/cobertura, NO bypass de puertas E/F. B2 lanzado modelo `openai/gpt-6-luna#high`, sesión `ses_e4e6d9d3effeosZGq38b7axqH5`, rama `agent/perf-20261106-b2`, worktree `/tmp/opencode/monster-perf-20261106/b2`, base 4352c25. Ownership exclusivo FieldQuery.h/.c, test_long_hair_field_query.c y nota propia nueva `docs/performance/long-hair-jaw-proof-agent-b2.md`. Objetivo: intervalo demostrado para rama far/AABB real de LongHairJawDistance, operación float por operación, UNKNOWN cerca de jaw/cavidades/no finito. Sin editar MonsterSDF/LongHair/MonsterVisual/Makefile. E reactivado misma sesión/rama, permiso adicional SOLO diagnóstico nuevo `tools/long_hair_interval_preflight.c` e informe `docs/performance/long-hair-preflight-agent-e.md`; verificar cobertura BODY real de caballo cuatro etapas y causas de UNKNOWN, sin confundir con certificado combinado. No runtime edits todavía, no benchmarks/capturas/suites pesadas; pruebas acotadas y conteos sin afirmaciones de tiempo. F/G/H/I siguen bloqueados hasta nueva aceptación E. J/D sin integración.

Preflight E entregado `c5eb624822214c7bba6d035a17073256c36f3615`, BASE c9757f85; solo diagnóstico/informe nuevos, sin runtime. Compilación O2 y cuatro etapas untimed informadas aprobadas; 288/9216, cero cajas de curva fuera del dominio. Reporte revisado por coordinador: snapshots bodyPartCount0,60 conectores,119 nodos,13 estaciones axiales, jerarquía compatible,cabeza anatómica. Cada etapa: BODY known 82944/82944 cajas de hijos, pero lower=-FLT_MAX para75.7–80.3%; body-only lower>=.0005*scale solo2225/13/30/43 cajas etapas0..3; segmento raíz cero. Combinado actual UNKNOWN85536/85536 cajas (guías+hijos). Es conteo real, no medida de tiempo ni certificado combinado; jaw solo no resuelve cuerpo insuficiente. Raw JSON ignorados en build de E; report y herramienta no incorporados aún. Coordinador informó B2 y reactivó E para seguimiento diagnóstico acotado: subset determinista adulto un hijo/guía, profundidad0/1/2/4 y presupuesto explícito; comparar cobertura por muestra q original y descomponer padding C2abs(lift) vs anchura geométrica/cota cutters. No cambiar helpers ni usar sensibilidad hipotética como certificado. Sigue NO-GO de producción hasta pruebas/cobertura/tiempo, F no autorizado.

B2 entrega `db0fa595a2b4ff8616b8599ef923f42c51ee9924`, base4352c25: archivos propios4, sin runtime; prueba intervalar operación float a operación float para far/AABB, metadata matchesExactField opt-in, UNKNOWN ante cerca/no finito/unsupported. Informa O2 y ASan/UBSan aprobados, sin benchmark ni caballo combinado; anticipa no-go con cobertura corporal previa. Coordinador revisó fuente/derivación y solicitó aclaración/restricción fenv: umbral escalar calculado en hilo query debe corresponder al callback exacto potencialmente en worker; preferir FE_TONEAREST comprobado y contrato homogéneo de rounding/gradual underflow en vida útil, UNKNOWN en otros modos, tests cambio de rounding/restauración. Resolver NULL lowerBound (rechazo o contrato explícito). B2 continúa misma sesión, helper no aceptado/integrado aún.

B2 revisión `301bb4a147f35795a176c32c9a57c3147a9c23aa`: exige FE_TONEAREST por fegetround, NULL lowerBound UNKNOWN; contrato de FP homogéneo en callback/workers explícito, metadata flag no verifica fenv. Agente informa tests/ASan/UBSan con shim fenv temporal (header local /usr/local/include/fenv.h incompatible). Coordinador comprobó header local SiFive/newlib y reprodujo suite C11 -Werror O2 con wrapper temporal `/tmp/opencode/b2-system-headers/fenv.h` que INCLUYE el header real `/usr/include/fenv.h` (sin declaraciones inventadas ni tocar host). Compila LongHairFieldQuery.c/Vector.c/AABB.c/MathUtils.c/test; ejecutable `/tmp/opencode/long_hair_jaw_b2_review`, aprobado. Helper matemático revisado condicionalmente al contrato, NO integrado/activado y sin coste medido. E autorizado a compilar diagnóstico con source/header B2 read-only y snapshot.matchesExactField confirmado, registrar SHAs/entorno y comparar cobertura COMBINADA/coarse/subdivisiones bajo presupuesto. Sin merge propio ni cambios runtime/helper, no timings autorizados todavía.

E seguimiento `2123db945b6426878ee5a56c12883e2cba47d693` sobre607adcff: mismos dos archivos diagnóstico/report; sin runtime. Compila con B2 read-only y snapshot homogéneo/FE_TONEAREST. Reporte revisado: B2 conocido adulto79494/82944 cajas hijo,3450UNKNOWN jaw,43clear; etapas0..3 clear1446/13/30/43. Subset adulto288fibras/20736q originales profundidad0/1/2/4: combined certified16/380/600/863; profundidad4 fallback19873 (784UNKNOWNjaw+19089knownno-clear). Queries a profundidad4=72/fibra; subset completo cuatro profundidades79282APIcalls, sin medir tiempo. No generalizar subset a todos los hijos. Padding restante C mean.01881703 (2abs(lift)mean.00109115): persistencia no se explica solo lift. NO-GO producción se mantiene, runtime omite0auditorías, B2 no incorporado. Nuevo BASE607adcff intermedio de diagnóstico y SHA final registrados, no cherry-pick aún.

Ranura exclusiva J concedida tras terminar E/B2: sesión `ses_e4f7f9d63ffeWf3sVU4IWi7j2a`, misma rama/worktree J bd22d641. Linked build y especies secuenciales horse/brumacerno/dog/lizard (+asno opcional), --runs6 preferido (cold+warmup descartado+5steady), voxel.18 fijo, timeout por especie documentado si patológico, outputs `/mnt/storage/monster-engine-agents/scratch/perf-20261106-j`. Sin full suites/capturas ni benchmarks de otros; coordinador no ejecuta carga pesada hasta liberar slot.

Investigaciones fuente-only paralelas a J (sin compilar/ejecutar/reindexar): B3 en sesión B existente `ses_e4f7f9db1ffeOHug1o4IOw53SQ`, worktree b/rama b, único nuevo informe `docs/performance/long-hair-body-range-feasibility-agent-b3.md`; probar viabilidad algebraica de cota inferior conservadora alrededor de cutters/head smooth union, no cambios SDF/helper. C2 sesión C `ses_e4f7f9d69ffeS7HlZGySJx7c1G`, worktree c/rama c, único nuevo informe `docs/performance/long-hair-curve-rounding-feasibility-agent-c2.md`; derivar si es posible cota numérica rigurosa más local sin cambiar interpolación ni reducir multiplicadores sin prueba. Son propuestas no medidas; no autorización de modificar padding/certificados/campo. Ownership disjunto. F/G/H/I bloqueados.

B3 entrega `5338c9c9259be5a680af8cedc5c627f5ab46981d`, base d1e7d536: único informe nuevo, source-only sin compilación/ejecución/medidas, diff-check informado aprobado. Coordinador leyó informe. Monotonía de smooth-subtract conserva lower pre-cut válido pero no convierte lower<=0 en positivo; head smooth-min debe incluirse antes. Atajo positivo fijo.0002 por debajo de.0005*scale adulto; devolver lower de cuerpo sin cabeza sería inválido. Candidato diagnóstico NO autorizado runtime: comparar solo subcaso farHeads+sign-straddling que retorna-FLT_MAX con continuación existente full head+cutters; mantener caso demostrado interior no-clear. Cobertura/beneficio esperados UNKNOWN; prueba de float/head/cutters pendiente. No se incorpora ni altera MonsterSDF; NO-GO producción sigue. J mantiene ranura exclusiva.

C2 entrega `09470add6d7aac4f04977bba22bf6eaaf350a0f9`, base c5403db3: solo informe nuevo, sin builds/probes/medidas ni cambios helper. Coordinador leyó: racional gamma/coeficientes de C aún no es inventario completo operación-a-operación CPU/driver, el margen propio también requiere redondeo superior; no garantía universal GLSL normalize ni límite world-space derivado de receta. **Esta revisión supersede cualquier lectura anterior de C como certificado de producción**: helper sigue standalone experimental/diagnóstico, no habilitado, pruebas muestreadas no constituyen prueba formal universal. NO-GO para reducir padding o activar continuous skip.

J mediciones entregadas `ef75c8ef5fb6afbc7736aef790af8d480f9d3762` sobre bd22d641: cinco fuentes horse/brumacerno/asno/dog/lizard completadas exit0, voxel.18 fijo, cuatro etapas, runs6 (cold1+warmupdescartado+steady5); informe/csv/hash en scratch J. Coordinador revisó informe y verificó independientemente cinco CSV:1014filas/17columnas,rep0..5,etapas0..3,cero failed y SHA256 coinciden. Headless CPU meshing domina matriz de fases; SDF build máximo.124ms. Adult mesh steady p50 horse33.460,brumacerno63.263,asno24.640,dog28.164,lizard35.139ms; NO son tiempo frame ni crin. End-to-end continuo,crin,async,GPU no medidos; sum fases no end-to-end. Fuente/máquina/cargas/RSS/capacidades separados en informe. Slot J liberado completamente, sin cambios generales runtime autorizados. J todavía no incorporado en integración.

E draft discreto `af23cc1d421aea2e9b11540e6fdddeb5ddcddc03` source-only listo para compilar. Coordinador detectó fallbackQ++ por grupo no-clear (debe+=groupSize), función retornaba éxito pese mismatches. Se solicitó corregir y verificar covered+fallback=selected*72 y rootcovered+rootfallback=selected*7, errores bitwise/exact=>exitnozero, inválidos explícitos contabilizados, capacidad<=8; bitwise copy verifica solo reuso de buffer, no equivalencia de codegen producción. Tras liberar J se concede **slot exclusivo E** para corregir/compilar/ejecutar diagnóstico B2 discreto, guardar outputs/hashes en `/mnt/storage/monster-engine-agents/scratch/perf-20261106-e-discrete`; si correcto y factible, medición acotada sobre mismos buffers exactbaseline72 vs rangegroups+fallback con>=3warmups5samples, etiquetada subset descriptor final, NO build/retries/GPU. Si no hay modo comparable seguro, devolver no medido/no-go. Sin full suites/capturas ni agentes/cargas pesadas concurrentes. Producción permanece exacta, F/I bloqueados.

E autorizado a escribir SOLO extensión offline opt-in en sus mismos dos archivos para cajas de grupos de puntos CPU exactos q1..72 (grupos8/4/1 en subset adulto288fibras); min/max componentes float incluye cada punto por construcción, sin Bezier/cambiar padding ni reducir muestreo. Consulta combinado B2 y diferencial en los MISMOS puntos, solo certificado discreto/no continuo/GPU. Ninguna modificación runtime autorizada ni waiver de puertas E/F. **Durante ranura J: E escribe fuente/report únicamente, no compila/ejecuta/tests/probes ni benchmark; devolverá READY_TO_EXECUTE.** Ejecución/medida posterior requiere ranura explícita. Idea no implementada/verificada aún, cobertura/coste UNKNOWN.

E discreto ejecutado `3927f8088fd2191a1b8fc68047897510b8aa2095` sobreaf23cc1d; same two files, sin runtime. Slot liberado. Reporte revisado:288fibrasadultas,20736q y2016rootq por tamaño, group8/4/1 cubiertos944/2720/3911 (4.55/13.12/18.86%), fallback19792/18016/16825;rootcovered28/72/79. Identidades completas q/root/grupos/queries aprobadas, inválidos/rechazados/exactmismatch/bitwisefail=0, exactchecks=covered. Bitwise solo buffercopy, NO prueba de codegen producción. Medición acotada mismo buffer3warmups5muestras ruta: baseline mediana33.683/33.771/33.566ms vs range+fallback36.173/36.260/52.886ms; **+7.39/+7.37/+57.56% más lento**. No earlyexit; incluye minmax+range+exactfallback, excluye generación puntos/retries/wholebuild/GPU. Todos candidatos rechazados NO-GO por coste neto/cobertura, no hay velocidad de producto mejorada. Artefactos `/mnt/storage/monster-engine-agents/scratch/perf-20261106-e-discrete`, B2read-only301bb4a, fenv homogéneo. Ninguna técnica habilitada, F/G/H/I siguen bloqueados por E.

Siguiente ranura D concedida al liberar E: sesión D existente, rama d4907fab, medición sintética del scheduler1/2/4/8, dispatch y trabajo readonly mismo volumen/digest determinista, >=3warmups9muestras preferidas; autorizado nuevo archivo propio benchmarks/benchmark_long_hair_scheduler.c además nota propia, NO Makefile/runtime/scheduler impl salvo corrección revisada. Scratch `/mnt/storage/monster-engine-agents/scratch/perf-20261106-d`. Comparación es sintética y no habilita F ni implica speedup real de crin; default budget4/callerincluido/contrato lifetime intactos. Coordinador/otros agentes sin cargas pesadas hasta liberar slot.

**Recuperación D tras cuota, solicitada por usuario:** sesión Luna high falló `subscription_sharing_usage_limit_exceeded`. No se cambió modelo/proveedor/auth ni se lanzaron agentes alternativos. Coordinador encontró CSV completo21muestras agregadas/workload+threads y benchmark sin seguimiento; preservó esos dos artefactos. Primera corrida guardada readonly_compute p50/p95/max µs1h18626.885/19004.531/19170.621,2h9572.356/9826.652/9834.026,4h4869.840/5010.462/5015.923,8h2591.468/2616.435/2681.335. Sin raw samples; recompilación no byte-idéntica al binario previo: no se afirma identidad demostrada de aquella fuente/binario.

Coordinador recuperó SOLO benchmark/notapropios D: añadió SAMPLE por medición, paridad por muestra fuera del timer, etiquetaFNV64 y metadata serial0; scheduler/header/pool intactos. Nueva ranura controlada exclusiva antes de fulltest,3warmups+21muestras por caso1/2/4/8,168samples. GCC13.3 C11-Werror-O2 Ryzen5600H, load1before1.489/after1.530. Readonly_compute p50/p95/max µs1h19499.196/19690.314/19712.154;2h9787.328/9910.249/10031.486;4h5013.899/5523.963/5745.849;8h2677.739/3345.067/3429.826. Dispatchcopy µs1h.130/.140/.170;2h10.550/11.592/26.039;4h16.220/28.994/32.211;8h18.094/23.814/24.556. Coordinador verificó168paridades,efectivos1/2/4/8,cuantilesnearest-rank desde rawCSV. Estimación8MiBstack/worker es reserva nominal NO RSS. Syntheticonly, no SDF/no pelo/sin contention inyectada; no predicción de velocidad producto. Sourcehash ac9d01e31f18c77b920c97a94524e448ba9585d05538429d96ee2508bdbc74dd,CSV43af0f0ee956d850aadb700b3ff06cc1e20972f5b048a61927df254e56a4d689,binary1fb1d5aba2995c8a81c2ed936e35eaea5ed06bbe74b912305252991dd54ebbe6. Scratch `/mnt/storage/monster-engine-agents/scratch/perf-20261106-d/coordinator-recovery` con manifest y verificationJSON. Commit D `feadfb73fe520ae10d1443284e2cc963483a649f`,BASE4907fab; únicamente benchmark nuevo+nota. Diffcached-check aprobado. Scheduler sigue SIN incorporar; no waiver E/F.

Benchmark D completado/slot de timing liberado. Después se hidrató sparse D con fixtures no gráficos y se ejecutó make -j2 test secuencial sin benchmark concurrente: shell job `sh_1b2ab5c45001ZTqk8ThYPSHTsN`, **exit0 aprobado**; log `coordinator-recovery/make-test.log`, código `make-test.exit`. El scheduler/benchmark standalone no está registrado en Makefile: este full-suite pass acredita suites existentes, no integración real de scheduler/pelo. Suite propia scheduler12grupos y168samples benchmark constan por separado. Producción sin optimización, meta<=100ms no alcanzada. Coordinador pidió autorización expresa al usuario para cambiar dependencia E-aceptada antes de F y probar paralelización exacta preservando todas las auditorías; **decisión pendiente**, no se lanza F automáticamente. Cuota OpenAI Luna sigue bloqueando nuevos agentes hasta reset; no cambiar auth/modelo sin autorización.

**Puertas ABC retry aprobadas:** make -j2 test exit0; make editor-test exit0; make -j2 build/capture_long_hair exit0; validador rooted-mane/all-stages/caballo-curve exit0, secuencial. Coordenador revisó performance.json: cuatro etapas 288/288 guías, 9216/9216 folículos, parity=1 (error máximo adulto 1.44656428e-6), 230400 muestras de hijos por etapa con penetración observada 0. Campo/jaw-root específico tiene 0 muestras (no confundir con prueba de contacto jaw de raíces adjuntas). Morph adulto informado 21 pasos por captura, sin afirmación de objetivo de tiempo. Comparación independiente PIL frente a referencia absoluta resultado: 20/20 PNG exactos por etapa, **80/80 imágenes pixel-idénticas**, cuatro roots.json iguales como objetos JSON. Evidencia `/mnt/storage/monster-engine-agents/scratch/perf-20261106-coordinator-abc-retry/visual/coordinator-image-comparison.json`. Son gates del runtime SIN optimización; no prueban futura certificación/pooling ni speedup.

**ESTADO ACTUAL: NO-GO E; cadena de producción detenida.** E entregó BASE_SHA=OUTPUT_SHA=`c9757f85c10b06d34ae19ed7b0cde741a8f5b3eb`, sin archivos modificados ni solicitudes compartidas; worktree limpio/diff-check aprobado. Prueba de flujo de fuente (no medición): mandíbula activa en cuatro etapas (baseline con evaluaciones jaw), LongHairFieldQuery_RangeStatus exige jawRange conservador para JAW_FIELD, no existe proveedor de producción y exclusión automática siempre UNKNOWN. Por tanto, sin ese proveedor, toda caja del campo combinado queda UNKNOWN: 0% certificable por el adaptador actual, sin necesidad de muestreo. No confundir con medición espacial del rango corporal. No se implementó BuildEx, toggle ni bypass; comportamiento exacto intacto. TESTS_AND_RESULTS: inspección y diff-check únicamente, sin full suite propia. DIFFERENTIAL_RESULT: ningún cambio runtime. MEASURED_PERFORMANCE: no medido/A-B inexistente. LIMITATIONS: falta primitiva jaw conservadora, sin GPU/visual de optimización. READY_FOR_NEXT_AGENT=false. F/G/H/I no autorizados; D permanece sin integrar aunque su helper pasó revisión.

Plan acotado propuesto (todavía sin lanzar): proveedor de intervalo conservador para la MISMA LongHairJawDistance de reposo, con prueba del mapa float recíproco/multiplicación y rama AABB, dominio/generación/vida útil explícitos y UNKNOWN fuera de soporte; no cambiar salida del campo, umbrales ni introducir Lipschitz. Validar adversarialmente y repetir preflight combinado de cuatro etapas antes de reabrir E. No aceptar menos auditoría ni cuerpo-solo como sustituto. Requiere nueva asignación/ownership y revisión, no continuación automática de F.

Integración A+B+C incorporada en rama del coordinador hasta `c9757f8` mediante cherry-picks en orden A (74779e7, 6ff4386, b3f09d0), B (cfe12dc, 14afd3e), C (b0d9b1f, c9757f8). Son harness/helpers sin runtime/Makefile; D y J NO incorporados. E lanzado desde c9757f8 en `/tmp/opencode/monster-perf-20261106/e`, rama `agent/perf-20261106-e`, sesión `ses_e4f60a8a3ffem1RpPgYWFkYYMe`, modelo OpenAI Luna high. Recibió sección E completa y preflight obligatorio: sin jawRange conservador real no hay certificación combinada; ante cobertura nula debe detenerse con no-go, no inventar poda. Único escritor compartido E en su worktree, permiso de glue tests/main_test.c y guards de tests B/C; sin permiso de cambiar primitivas SDF/jaw. Default exacto intacto. Timings/full suites/capturas de E pendientes de ranura.

Coordinador ejecutando puertas ABC secuenciales: make -j2 test, make editor-test, build captura y validador rooted-mane cuatro etapas caballo-curve, baseline absoluto resultado congelado. Shell job `sh_1b09fbcad001fTcpFPGjPal5Oo`; logs y salida `/mnt/storage/monster-engine-agents/scratch/perf-20261106-coordinator-abc`. Ningún timing concurrente autorizado a agentes. Resultados pendientes; no afirmar aprobación visual/GPU hasta revisar logs e imágenes.

Resultado primer intento ABC: make test exit2 en tests/test_long_hair.c:173, `CreatureRecipeIO_LoadFile` de `docs/validation/long-hair-foundation/recipes/caballo-curve.creature.json` omitida por sparse checkout. Fallo de fixture ausente confirmado por ruta del assert; no es prueba de regresión de geometría. Editor/build captura/validador no ejecutados por parada secuencial. Se amplió SOLO configuración sparse local del coordinador para incluir docs/validation excepto imágenes/vídeos grandes; sin editar tests ni código ni referencias. Retry secuencial iniciado con fixture comprobada existente: job `sh_1b0a2cf27001EIngfrnZMk0tZc`, logs `/mnt/storage/monster-engine-agents/scratch/perf-20261106-coordinator-abc-retry`. Resultados del retry pendientes. No benchmarks de agentes durante gates. NO-GO E independiente de este fallo de entorno.

Baseline A `849e88fa145d51a03fb441b17adb8699181675c0` (solo informe sobre ea78dd): Ryzen 5 5600H, GCC13.3 O2. Etapas 0/1/2/3 p50 1442.405/1433.593/1425.038/1420.968 ms; p95 interpolado n5 1445.968/1434.946/1427.216/1430.420 ms. Callbacks combinados 865208/861136/857918/857381; jaw snapshot 32592/29435/24855/21046. Todas 288 guías visibles/9216 descriptores válidos, digest estable en cinco repeticiones. Coordinador verificó independientemente SHA256 de cuatro JSON (coinciden informe), formas, IDs únicos, slots válidos, digest estable, contador body igual callback y certification_effective=false. Adulto total componentes=878427 (857381 body+21046 jaw); el histórico 857381 era contador body/callback, no total de componentes. Ablación limpia rootFullness intacto: p50 176.907 ms/p95 178.306, 105963 callbacks, no evidencia de calidad. Carga gráfica/fondo activa, load1 1.39–1.86; exclusividad del programa, no aislamiento físico. Fases internas null; sin A/B ni visual/GPU. A+B+C aprobados para incorporación como harness/helpers, no como optimización habilitada. D se retiene hasta E.

Primeras entregas A/B/C: ownership conforme, sin cambios compartidos. Ninguna aceptada ni integrada; todos reportan rendimiento no medido, sin diferencial/visuales de caballo de producción. A informa build/CLI/sintaxis Python/comparador sintético aprobados; fases internas null e interruptor certificación no consumido. B informa C11 -Werror y ASan/UBSan aprobados, pruebas sintéticas, cobertura caballo desconocida. C informa suite standalone y ASan/UBSan aprobadas, comparaciones CPU/shader y mapeo de 72 muestras; sin coste de helpers medido. Revisiones solicitadas por el coordinador:

- A: contador mezcla callback combinado y subevaluaciones mandíbula; separar semánticas. Comparador debe rechazar NaN/Inf, metadatos/formas ausentes, tolerancias inválidas y divergencia digest entre runs; corregir percentil con índices iguales y error truncado al retornar temprano. Evitar PASS de certificación off/on cuando el toggle no tiene efecto, distinguir SHA de identidad del binario.
- B: cota jaw basada en 32 eps no demuestra los errores del mapa float real (recíproco/multiplicación y cancelación amplificada por escala); exigir prueba o UNKNOWN seguro. Documentar correspondencia de campos/generación y invalidar o definir Init fallido.
- C: derivar padding numérico CPU/GPU y propagar error de subdivisión; resolver dominios de normales/lift y cancelación. Rechazar casos sin demostración para fallback exacto. Pruebas aleatorias por sí solas no constituyen prueba.

Los tres agentes continúan en sus mismas ramas/sesiones con pruebas acotadas y sin ranura de rendimiento. E sigue bloqueado.

Actualización B `d1e7d536cd96ab2b96337bada483612e19e7101f` (revisión sobre `891399d`): elimina toda exclusión automática jaw sin prueba; devuelve UNKNOWN salvo callback jawRange conservador suministrado por propietario. Documenta correspondencia de componentes con callback exacto/generación; Init fallido limpia estado y distancia inválida devuelve -FLT_MAX. Coordinador revisó fuente y reprodujo standalone C11 -Werror -O2 con LongHairFieldQuery.c, Vector.c, AABB.c, MathUtils.c y test_long_hair_field_query.c; pasó. ASan/UBSan informado por B, no repetido por coordinador. Helper conforme como fallback seguro, no como mejora de rendimiento; cobertura de caballo y diferencial producción pendientes. Integración retenida por A/C. Nota de integración: test B tiene main standalone y requiere adaptar registro/guard para suite integrada.

Actualización C `c5403db35e4abc981c5e103c4ae981f70108aca5` sobre `76fef77`: documenta margen gamma/coeficientes para binary32 round-to-nearest, limita controles guía a 1e6, Bezier a 5e6, weight [0,1], |lift|<=1e6, normales [0.99,1.01] y mezcla >0.01; entradas fuera de dominio devuelven false y exigen auditoría exacta. Acumula margen por subdivisión, máximo 16 niveles, extremos nextafterf hacia fuera. Padding incluye 2*|lift| para diferencias de orientación de normales: puede empeorar cobertura y debe medirse, no reducir sin nueva prueba. Coordinador revisó fuente/header y compiló suite standalone contra LongHair.c real con GCC C11 -Werror -O2 -ffunction-sections -fdata-sections -Wl,--gc-sections (Vector.c/MathUtils.c); ejecutable `/tmp/opencode/hair_curve_bounds_review` aprobado. Helper conforme bajo dominio/precondiciones; no prueba driver GLSL ni visuales reales, no overhead medido. Integración E aún bloqueada por A; test C tiene main standalone y necesitará glue/guard de suite.

Entrega J `454c6e93b353a4c197399c8c3ff5a97f1019aa2f`: tres archivos propios, sin runtime; sintaxis C/Python/CLI/parser CORE_SRCS informados aprobados; sin linked build ni medidas. Coordinador solicitó revisión: WriteRow genera columna CSV extra; separar counters desconocidos de cero; renombrar suma de fases discontiguas que no es tiempo end-to-end real; fallar ante fur/pose failed en vez de agregado medido/exit0; definir warmups y selección de muestras frías/calientes. J continúa en misma rama, sin ranura concedida. Recomendaciones generales siguen condicionales, sin agentes nuevos de runtime autorizados.

Revisión A `ea78dd059d40dc378b0705bf7972c548dc0d4859`: separa contador combinado de body/jaw/earlyouts, exporta FNV del ejecutable (no criptográfico), exige confirmación efectiva de modo por build para aceptación A/B y rechaza toggle no consumido. Comparador valida metadatos/formas/finitud/tolerancia/digest entre repeticiones y corrige percentiles/errores truncados. Agente informa compilación/CLI/sintaxis/regresiones sintéticas aprobadas; coordinador revisó fuente. Harness conforme para baseline CPU etiquetado sin control de certificación efectivo; comparación off/on sigue bloqueada hasta runtime con control real. Se concede primera ranura A; aceptación integral A/B/C aún pendiente de resultados.

Revisión J `bd22d641b0b8a867f4e7e813716a0312c8135877`: coordinador revisó diff; CSV ahora 17 columnas incluyendo sample_class, counters vacíos fuera de fases instrumentadas, suma renombrada `sum_of_profiled_cpu_phases`, end-to-end real no medido. Fur/pose failed abortan nonzero. Rep0 cold, una pasada completa descartada, demás steady; SDF/mesh reuso etiquetado separado. Agente informa sintaxis/parser/driver CSV temporal aprobados, sin linked build ni medidas. Esquema conforme para siguiente revisión de ejecución; J espera ranura posterior, no autoriza mejoras runtime.

Entrega D: ownership verificado por el coordinador (cuatro archivos nuevos, sin runtime/Makefile). Agente informa suite aislada O2/C11, ASan/UBSan y TSan con `setarch -R` aprobadas; sin este último TSan falla al inicializar por mapping. Evidencia todavía no reproducida por el coordinador. Revisión de fuente detecta contrato de Destroy insuficientemente explícito respecto a otros lectores/submitters ya dentro de una API: `activeCalls` solo sigue el job admitido. Se solicita aclarar quiescencia externa de todas las APIs y excepción de job activo, anidamiento A->B->A, rechazo Destroy desde cualquier callback y semántica requireParallel/1 hilo. D vuelve a revisión en su rama; no se integra todavía. Diferencial: implementación inicial reutiliza archivos previos sin cambios; rendimiento no medido; SDF/pelo real y visuales no verificados; READY solo para revisión del helper.

Revisión D resuelta en `4907fab2ea1c7b1c02d8170efdebad0dfa56eb72`: header/nota requieren explícitamente quiescencia externa de todos los lectores/submitters antes de Destroy, con única excepción del job admitido activo; destrucciones concurrentes no soportadas. Documenta A->B->A como BUSY, Destroy rechazado desde cualquier callback y solicitud serial explícita incluso con requireParallel. Coordinador revisó diff y reprodujo GCC C11 `-Wall -Wextra -Wpedantic -O2 -DLONG_HAIR_SCHEDULER_STANDALONE`: 12 grupos de pruebas aprobados, sin warnings. Comando: compilar src/LongHairScheduler.c + src/SDFSamplingPool.c + tests/test_long_hair_scheduler.c en worktree D con -Iinclude -Itests -lm -pthread; ejecutable `/tmp/opencode/long_hair_scheduler_review`. Sanitizadores siguen siendo evidencia reportada por D, no reproducida por coordinador. Revisión de helper conforme bajo este contrato; integración retenida hasta E y puertas correspondientes. Rendimiento sintético no medido; determinismo integrado/SDF real/visuales no verificados. READY_FOR_NEXT_AGENT: solo helper revisado, no habilita F.

- A/B/C: revisión pendiente; E no lanzado hasta aceptación y pruebas de contratos.
- D: revisión/concurrencia pendiente; F bloqueado por D y E con puertas visuales completas.
- G/H/I bloqueados en orden por sus dependencias. J no autoriza cambios generales SDF.
- Pruebas integradas y visuales: todavía no ejecutadas. Rendimiento nuevo: no medido. Objetivo <=100 ms p95: no demostrado.

---

## Registro histórico anterior (no es estado actual)

**Coordinador:** agente lead de OpenCode
**Repositorio:** `KillDaWill/monster_engine`
**Baseline revisado:** `bc83fd586d7b1b8edae881c65ee25e96536ac08f`
**HEAD de `main` al arrancar:** `bc83fd5` (coincide con el baseline revisado)
**Estado:** ola 1 en ejecución

## Documentos de referencia

- `AGENTS.md` — contratos del repositorio.
- `OPTIMIZACION_CRECIMIENTO_PELO.md` — baseline medido y estrategias.
- `PROMPTS.md` — misión autocontenida de cada agente.
- `README.md` — contexto general del motor.
- `INFORME_OPTIMIZACION_MONSTER_ENGINE_SDF_PELO_2026-11-05.md`: **no existe en este checkout**. El coordinador trabaja con `AGENTS.md`, `OPTIMIZACION_CRECIMIENTO_PELO.md` y `PROMPTS.md`; se deja constancia para que ningún agente invente su contenido.

## Baseline no negociable

- 288 guías visibles, 9.216 hijos con raíz independiente.
- 857.381 llamadas SDF por reconstrucción adulta (705.205 auditoría, 51.079 lift/raíz, 101.097 guía/colisión).
- ~1,47 s por paso de crecimiento; 21 pasos ~31 s.
- Campo real de construcción: unión `min(bodySDF, jawRestDistance)`.
- `MonsterSDF_GetCellRangeWrapper` no cubre necesariamente todas las configuraciones; el SDF aproximado no es 1-Lipschitz demostrado.
- `range == UNKNOWN/false` ⇒ auditoría exacta original, nunca espacio libre.
- Objetivo: ≤100 ms p95 por rebuild (aspiracional ≤50 ms), sin perder 288/9.216 raíces, IDs estables, semillas deterministas, transaccionalidad, colisión de mandíbula, cero penetración GPU observada ni aspecto visual.
- Prohibido: tocar shader/receta/densidad/margen de colisión/capturas de referencia para hacer pasar el benchmark; submuestrear la auditoría; desactivar fallback tubular; editar fuentes generadas a mano.

## Aislamiento de trabajo

Cada agente trabaja en un worktree disperso propio (sin los PPM grandes de `docs/validation`):

| Agente | Rama | Worktree | Ola |
|---|---|---|---|
| A | `agent/a` | `/mnt/storage/monster-engine-agents/agent-a` | 1 |
| B | `agent/b` | `/mnt/storage/monster-engine-agents/agent-b` | 1 |
| C | `agent/c` | `/mnt/storage/monster-engine-agents/agent-c` | 1 |
| D | `agent/d` | `/mnt/storage/monster-engine-agents/agent-d` | 1 |
| J | `agent/j` | `/mnt/storage/monster-engine-agents/agent-j` | 1 |
| E | `agent/e` | pendiente (se crea desde main tras A+B+C) | 2 |
| F | `agent/f` | pendiente (tras D+E) | 3 |
| G | `agent/g` | pendiente (tras F) | 4 |
| H | `agent/h` | pendiente (tras G) | 5 |
| I | `agent/i` | pendiente (tras H) | 6 |

Reglas:

- Ningún agente hace commit en `main`, push, reset ni force-push.
- Solo se editan archivos del ownership asignado. Los archivos compartidos (`src/LongHair.c`, `src/MonsterVisual.c`, `src/MonsterVisualAsync.c`, `src/MonsterSDF.c`, `src/SDFSamplingPool.c`, `Makefile`) quedan bloqueados hasta que el coordinador asigne al integrador de la ola.
- Los worktrees son dispersos: si falta un documento de referencia, se lee del checkout principal por ruta absoluta; no se copia ni se commitea.
- `PROMPTS.md` y `OPTIMIZACION_CRECIMIENTO_PELO.md` están sin seguimiento en `main`; no deben commitearse desde las ramas de agente.
- No se toca `docs/validation/long-hair-styling/resultado/` (referencia congelada). Las capturas van a `/mnt/storage/monster-engine-agents/scratch/<agente>/`.

## Ventanas de medición

La máquina tiene 12 CPUs, 14 GB de RAM, GPU AMD Renoir / Mesa 25.2.8 y DISPLAY `:0`.

- Compilación funcional concurrente: `make -j2` como máximo por agente (5 agentes ≈ 10 hilos).
- Cualquier benchmark, captura OpenGL o medición que se vaya a reportar debe ejecutarse dentro de una ranura exclusiva:
  ```sh
  /mnt/storage/monster-engine-agents/run-bench.sh <comando>
  ```
  El wrapper usa `flock` sobre `/mnt/storage/monster-engine-agents/bench.lock` y registra la carga antes/después.
- No se comparan tiempos tomados con cargas distintas. Registrar `uptime`, versión de gcc, flags y SHA.
- `make test`/`make editor-test` funcionales pueden correr fuera de la ranura, pero no se usan como benchmark.
- El disco raíz está casi lleno: todo artefacto temporal va a `/mnt/storage/monster-engine-agents/scratch/` o al `build/` del worktree. Evitar `/tmp` del sistema salvo archivos pequeños.

## Grafo de dependencias

1. **Ola 1:** A, B, C, D, J en paralelo. A/B/C/D/J solo añaden archivos nuevos y prueban de forma aislada; no editan archivos compartidos ni `Makefile`.
2. **Ola 2 (E):** tras aceptar A+B+C. E es el único escritor de `LongHair.c`/`MonsterVisual.c`; integra certificación conservadora con fallback exacto.
3. **Ola 3 (F):** tras aceptar D y E. F es el único escritor del camino caliente e integra el scheduler de D.
4. **Ola 4 (G):** tras F medido y sin regresiones. Optimiza el cuello real restante, no una caché especulativa.
5. **Ola 5 (H):** tras G. Reutilización incremental con invalidación demostrada contra el campo completo cuerpo+mandíbula.
6. **Ola 6 (I):** tras H. Edad/scrubbing no bloqueante con publicación coherente cuerpo+pelo de la misma generación.
7. **J** entrega su matriz por especie por separado; sus hallazgos no se mezclan en el PR de pelo sin autorización y medición.

## Puertas del coordinador tras cada integración

- Revisar archivos cambiados, ownership y API pública.
- `make test`, `make editor-test`, `git diff --check`.
- `python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /mnt/storage/monster-engine-agents/scratch/validation-<ola>`.
- Comparar 4 etapas: 288/288 guías, 9.216/9.216 raíces propias, IDs estables, digest de raíces/descriptores, campo cuerpo+mandíbula, cero penetración GPU, paridad visual side/front/rear/root y crecimiento.
- Comparar p50/p95/max, llamadas SDF exactas, intervalos, fallback, hilos y memoria. Una reducción de llamadas que aumente el tiempo total se rechaza o queda como modo experimental.
- TSan/ASan cuando sea viable. Si no se puede verificar una afirmación visual/GPU, se marca `unverified` y no se acepta como completa.

## Plantilla de reporte de agente

```
BASE_SHA:
OUTPUT_SHA:
CHANGED_FILES:
REQUESTED_SHARED_EDITS:
TESTS_AND_RESULTS:
DIFFERENTIAL_RESULT:
MEASURED_PERFORMANCE_OR_NOT_MEASURED:
LIMITATIONS:
READY_FOR_NEXT_AGENT:
```

## Registro de integraciones

| Ola | Agente | BASE_SHA | OUTPUT_SHA | Estado | Notas |
|---|---|---|---|---|---|
| 1 | A | `bc83fd5` | — | en ejecución | harness de baseline |
| 1 | B | `bc83fd5` | — | en ejecución | intervalos conservadores |
| 1 | C | `bc83fd5` | — | en ejecución | cotas Bézier/AABB |
| 1 | D | `bc83fd5` | — | en ejecución | scheduler determinista |
| 1 | J | `bc83fd5` | — | en ejecución | perfil por especie |
| 2 | E | — | — | bloqueado | espera A+B+C |
| 3 | F | — | — | bloqueado | espera D+E |
| 4 | G | — | — | bloqueado | espera F |
| 5 | H | — | — | bloqueado | espera G |
| 6 | I | — | — | bloqueado | espera H |
