# Preflight de intervalos de crin — Agent E

## Decisión

**NO-GO para integrar certificación en runtime.** La matriz combinada de la primera revisión (helper anterior a B2) se conserva abajo como evidencia histórica: con esa versión y sin `jawRange`, todo combined era UNKNOWN. El helper B2 posterior reduce UNKNOWN mediante la prueba lejana de mandíbula, pero los intervalos positivos útiles siguen limitados por las cotas corporales, con cobertura adulta muy baja. No se integró ni se editó el helper B2.

Los conteos “body-only candidate” son un diagnóstico superior de cobertura: **no** certifican la unión real con la mandíbula, no son una medición de reducción de auditorías y no justifican una modificación del runtime.

## Comando reproducible

La receta y los datos de validación se leen directamente desde el repositorio original; no se copian ni modifican. Desde la raíz de este worktree:

Receta medida: `caballo-crin-foliculos.creature.json`, SHA-256 `241682fc8b63eebc601feb687ae52812cbcbd2ace6ece3e3d7f6291fd5ba2fb3`.

```sh
CORE_OBJS="$(make --no-print-directory --silent \
  --eval='print-agent-e-core:;@echo $(CORE_OBJS)' print-agent-e-core)"
make -j2 --no-print-directory $CORE_OBJS
B2=/tmp/opencode/monster-perf-20261106/b2
gcc -Wall -Wextra -Wpedantic -O2 \
  -I/tmp/opencode/b2-system-headers -I"$B2/include" -Iinclude \
  tools/long_hair_interval_preflight.c \
  src/HairCurveBounds.c "$B2/src/LongHairFieldQuery.c" $CORE_OBJS \
  -lm -pthread -o build/long_hair_interval_preflight_b2
RECIPE=/home/guillermo/Escritorio/monster_engine/docs/validation/long-hair-styling/caballo-crin-foliculos.creature.json
for stage in 0 1 2 3; do
  build/long_hair_interval_preflight_b2 "$RECIPE" "$stage" \
    > "build/long-hair-preflight-b2-stage-${stage}.json"
done
```

El ejecutable es una herramienta diagnóstica standalone, sin registro en Makefile. B2 se lee de forma independiente; no se copia, modifica ni integra. El `-I` al wrapper de `/usr/include/fenv.h` precede al include de B2 y al del worktree E. Los cuatro JSON reproducibles quedan en `build/` (ignorado/no versionado); cada uno contiene conteos y extremos `lower_min/max` y `upper_min/max` para guía e hijo, por segmento 0–8, además del resultado B2 y las condiciones FP observadas.

## Metodología y límites

- Para cada etapa se carga la receta congelada, se construye el `MonsterSDF` con `MonsterSDF_DefaultConfig()` y se fija la cabeza cerrada, igual que en el banco A y el camino de crin en reposo.
- Se replica el campo exacto de construcción `min(MonsterSDF_EvaluateDistance(body), LongHairJawDistance(rest snapshot))` con la misma transformación, marco, bounds AABB y early-out de mandíbula de `MonsterVisual.c`/el banco A. `LongHairSet_Build` genera una sola vez la crin completa; se consultan sus guías visibles y los descriptores de hijo válidos resultantes.
- Se toma cada uno de los 9 segmentos Catmull de cada curva guía con `HairCurveBounds_CatmullSegment` + `HairCurveBounds_BezierAABB`. Para cada hijo válido se usa su guía vecina, peso y lift guardados, normales de reposo y `HairCurveBounds_ChildSegment` + `HairCurveBounds_BezierAABB`. Son cajas conservadoras del segmento cúbico completo, no muestras puntuales ni subcajas subdivididas.
- En la ejecución inicial pre-B2 cada caja recibió dos consultas separadas: `MonsterSDF_GetField(&sdf).getCellRange` para diagnosticar el **cuerpo solamente**, y `LongHairFieldQuery_RangeStatus` configurado con el callback exacto cuerpo+mandíbula pero sin `jawRange`. Esa llamada registra únicamente el comportamiento del helper de esa revisión previa; la repetición B2 está descrita separadamente más abajo.
- El umbral reportado para “body-only candidate” es el umbral original `.0005 * hair.scale`. Si el límite inferior corporal conocido alcanza ese umbral, se cuenta como candidato de cobertura corporal, **no** como certificado combinado.
- Los extremos son estadísticos de los resultados por grupo de cajas; `lower_max`/`upper_min` no son límites universales sobre todos los segmentos. `-FLT_MAX` y `+FLT_MAX` son cotas abiertas amplias, no valores de distancia útiles. Los queries no están cronometrados y no se hacen afirmaciones de rendimiento.

## Evidencia del snapshot y del wrapper

| Etapa | Escala pelo/cuerpo | Tipo de cabeza | `bodyPartCount` | Conectores | Nodos jerárquicos | Estaciones axiales | Jerarquía compatible | Guías visibles | Hijos válidos |
|---:|---:|---|---:|---:|---:|---:|---|---:|---:|
| 0 | 0.40 | anatómica | 0 | 60 | 119 | 13 | sí | 288/288 | 9,216/9,216 |
| 1 | 0.62 | anatómica | 0 | 60 | 119 | 13 | sí | 288/288 | 9,216/9,216 |
| 2 | 0.82 | anatómica | 0 | 60 | 119 | 13 | sí | 288/288 | 9,216/9,216 |
| 3 | 1.00 | anatómica | 0 | 60 | 119 | 13 | sí | 288/288 | 9,216/9,216 |

Las cuatro capturas tienen una boca y `anatomicalHead=true`; los 13 barridos axiales hacen compatible la selección axial de la jerarquía. La raíz del árbol tiene `[begin,end)=[0,60)`, `hasEligible=true`, hijos `[1,60]`. El término mandíbula está activo en las cuatro etapas. Ningún hard gate inicial del wrapper rechaza el snapshot: `bodyPartCount==0`, existen primitivas corporales, hay nodos para todos los conectores y todas las bocas tienen cabeza anatómica.

El wrapper fuente (`src/MonsterSDF.c:2500–2571`) condiciona la consulta a: snapshot válido, `bodyPartCount==0`, conectores o al menos dos estaciones axiales, nodos si hay conectores y cabezas anatómicas para cada boca. La jerarquía incompatible no fuerza un UNKNOWN por sí misma; el código toma la ruta de acumulación sin jerarquía compatible. En estos snapshots la jerarquía es compatible. Para las **85,536** cajas por etapa (2,592 de guías + 82,944 de hijos), el callback corporal devolvió intervalo finito y ordenado: no se observó UNKNOWN corporal ni fallo por esos gates.

## Cobertura de cajas por segmento (registro pre-B2)

`body known` incluye intervalos cuyo límite inferior es `-FLT_MAX`; por tanto, no equivale a cobertura certificable. `body-only candidates` exige `lower >= .0005 * scale`. En esta ejecución histórica, el helper anterior a B2 devolvió UNKNOWN combinado en el 100% de las cajas porque el término es `LONG_HAIR_JAW_FIELD` y no había `jawRange` coincidente ni exclusión jaw demostrada.

| Etapa | Guía: conocidas / cajas | Guía: candidatos corporales | Hijo: conocidas / cajas | Hijo: candidatos corporales | Cajas de hijo con `lower=-FLT_MAX` | Combinado **pre-B2** UNKNOWN: guía + hijo |
|---:|---:|---:|---:|---:|---:|---:|
| 0 | 2,592 / 2,592 | 336 (12.963%) | 82,944 / 82,944 | 2,225 (2.683%) | 62,789 (75.7%) | 2,592 + 82,944 |
| 1 | 2,592 / 2,592 | 50 (1.929%) | 82,944 / 82,944 | 13 (0.0157%) | 64,034 (77.2%) | 2,592 + 82,944 |
| 2 | 2,592 / 2,592 | 65 (2.508%) | 82,944 / 82,944 | 30 (0.0362%) | 65,714 (79.2%) | 2,592 + 82,944 |
| 3 | 2,592 / 2,592 | 63 (2.431%) | 82,944 / 82,944 | 43 (0.0518%) | 66,640 (80.3%) | 2,592 + 82,944 |

En el **segmento 0 (raíz)** hubo 288/288 consultas de guía y 9,216/9,216 de hijo por etapa; todos los límites inferiores corporales quedaron por debajo del umbral. En etapa adulta, el mayor `lower` de las cajas de raíz fue `-0.016070703` para guías y `-0.044332605` para hijos. Es especialmente concluyente para la inserción: no hay certificado potencial en la primera sección de ninguna guía/hijo.

Para etapa 3, los conteos candidatos del hijo por segmento (0–8) fueron `[0,0,15,24,0,0,3,0,1]`, total 43/82,944. Los `lower_max` por segmento fueron `[-0.044332605,-0.016835554,0.002237117,0.006421110,0.000289999996,0.000451218,0.001420697,0.000401919,0.001301596]`; los `upper_min` correspondientes fueron `[0.102885589,0.118385576,0.112924568,0.081889734,0.058341395,0.049430739,0.049522973,0.044271473,0.042015448]`. El resto de etapas y los intervalos de guía están en los JSON reproducibles.

## Diagnóstico de subdivisión y padding (solo CPU, solo adulto)

Para separar la amplitud geométrica del margen de C, se midieron las 9 cajas cúbicas de cada una de **288 fibras representativas**: para cada guía visible se escogió determinísticamente el primer descriptor de hijo válido en orden de slot. No se extrapola esta muestra al universo de 9,216 hijos. En el subset se evalúa cada muestra exacta del auditor original `q=1..72`: `HairCurveBounds_AuditSample` determina el segmento y parámetro local; la muestra cuenta como **candidata corporal** sólo si pertenece a una subcaja de de Casteljau del mismo segmento cuyo `getCellRange` devuelve `lower >= .0005 * scale`. No se cuentan skips combinados (mandíbula UNKNOWN), ni se altera/acepta ningún descriptor.

El diagnóstico consulta una vez cada subcaja que contiene al menos una muestra q; no consulta subcajas sin muestras. Presupuesto máximo documentado: `288 * 72 * 4 = 82,944` consultas de cuerpo (profundidades 0, 1, 2 y 4). Uso real: **38,880**. Cada nivel cubre las mismas 20,736 muestras originales. Las consultas son sin cronómetro. El comparador de pertenencia usa el parámetro local de la auditoría y `floor(localT * 2^depth)`, limitado al último hijo en `localT=1`; por ello sólo se consideran muestras cuyas curvas completas están dentro de la subcaja conservadora correspondiente.

| Profundidad | Cajas consultadas | Cajas clear corporales | Muestras q candidatas corporales / 20,736 | Muestras proximales q=1..7 / 2,016 | Caja lower `-FLT_MAX` |
|---:|---:|---:|---:|---:|---:|
| 0 (segmento completo) | 2,592 (9/fibra) | 2 | 16 (0.077%) | 0 | 2,059 (79.5%) |
| 1 | 5,184 (18/fibra) | 94 | 380 (1.83%) | 16 (0.79%) | 3,692 (71.2%) |
| 2 | 10,368 (36/fibra) | 296 | 600 (2.89%) | 28 (1.39%) | 6,207 (59.9%) |
| 4 | 20,736 (72/fibra) | 863 | 863 (4.16%) | 35 (1.74%) | 8,505 (41.0%) |

Subdividir **sí mejora** la cobertura candidata en este subset: profundidad 4 da unas 54× más muestras q candidatas que la caja completa (863 vs 16). Aun así, 95.8% de las muestras siguen sin candidato corporal; sólo 1.74% de las siete muestras originales del tramo proximal lo son. Profundidad 4 usa ocho veces las consultas de cuerpo de profundidad 0 y llega a 72 consultas por fibra para cubrir 72 q; no se midió el coste relativo de `getCellRange` frente a `distance()`, por lo que no se atribuye ahorro ni speedup. En esta primera ejecución los candidatos eran de cuerpo solamente y no se contaron skips combinados; la repetición con B2 queda separada en la sección siguiente.

### Padding C y lifts observados

`HairCurveBounds_ChildSegment` incorpora `2*abs(lift)` en `roundingPadding`; el AABB lo expande por cada lado, además del redondeo exterior. En 2,592 segmentos de las 288 fibras adultas del subset:

| Magnitud | Mínimo | Media | Máximo |
|---|---:|---:|---:|
| Término C `2*abs(lift)` | 0.00000883 | 0.00109115 | 0.00538230 |
| Padding restante (no lift) | 0.01664785 | 0.01881703 | 0.02092689 |
| Diagonal de AABB de controles Bezier, sin padding | 0.05100202 | 0.14821214 | 0.19015611 |
| Diagonal del AABB conservador con padding | 0.11878433 | 0.20892570 | 0.25341652 |

En este caballo, el término `2*abs(lift)` no supera la diagonal geométrica en ninguno de los 2,592 segmentos; su media es aproximadamente 17 veces menor que el padding restante. El margen restante se mantiene durante la subdivisión, junto con márgenes acumulados de de Casteljau. Aun en profundidad 4, 41.0% de las cajas consultadas conserva `lower=-FLT_MAX` y el mínimo global del límite inferior sigue siendo `-FLT_MAX`. Esto indica que **el `2*abs(lift)` no es el dominante observado**; reducirlo no está justificado y violaría el contrato de C. La limitación persistente combina el resto del padding geométrico/conservador y, de forma más directa para la cobertura del lower, la rama corporal de cutters/substracción que sólo entrega una cota superior. No se ensayó padding hipotéticamente menor.

Los rangos reales de lift de todos los hijos válidos por grupo fueron:

| Etapa | Grupo 1 | Grupo 2 | Grupo 3 | Rango conjunto |
|---:|---:|---:|---:|---:|
| 0 | [-0.001746, 0.002199] | [-0.001448, 0.001984] | [-0.001440, 0.002563] | [-0.001746, 0.002563] |
| 1 | [-0.001717, 0.003429] | [-0.001828, 0.003464] | [-0.001506, 0.004496] | [-0.001828, 0.004496] |
| 2 | [-0.003104, 0.005483] | [-0.002181, 0.005573] | [-0.002512, 0.004197] | [-0.003104, 0.005573] |
| 3 | [-0.004084, 0.007496] | [-0.002304, 0.007869] | [-0.003615, 0.006249] | [-0.004084, 0.007869] |

Cada grupo contiene 3,072 descriptores válidos por etapa. El máximo `2*abs(lift)` observado en el subset adulto es 0.00538230, menor que el máximo entre grupos de `abs(lift)` (0.00786878; los máximos no necesariamente pertenecen al subset representativo).

## Repetición con B2 revisado (helper read-only)

Esta matriz es una ejecución nueva, distinta de la matriz histórica **pre-B2** de arriba. Se compiló el header/source de B2 desde `/tmp/opencode/monster-perf-20261106/b2` (SHA `301bb4a147f35795a176c32c9a57c3147a9c23aa`) contra el diagnóstico/hair bounds de E y el core del worktree de integración. Los sources core `MonsterSDF.c`, `LongHair.c`, y `HairCurveBounds.c` provienen de `c9757f85c10b06d34ae19ed7b0cde741a8f5b3eb`; la revisión previa del harness/documento de E era `607adcffb67d83612443330cd6c8ced92a67b13a`. La receta tiene el SHA-256 anotado arriba. No se fusionó ni editó la rama B2.

El campo exacto se construyó de la misma `MonsterSDF` que alimenta las consultas. El contexto del callback y `LongHairJawInfluence` usan el mismo `sdf.mouths[0]`, `MonsterSDF_GetJawField(...).getBounds`, centro, ejes de `Mouth.rotation`, pivote y escalas de ese snapshot. Por esa correspondencia concreta —no por una asunción sobre identidad— se fijó `matchesExactField=true`. No se pasa `jawRange`; B2 intenta su prueba de rama lejana y compone con el rango de cuerpo.

El diagnóstico fija `FE_TONEAREST` antes de construir el snapshot y conserva el mismo hilo sin workers; la sonda de gradual underflow resultó verdadera. Los cuatro JSON indican `fegetround=0`, `fast_math_enabled=false`, `iec559_compile_macro=true`, `jaw_matches_exact_field=true`. El helper B2 ejecuta sus propios gates de modo FP/formato/subnormal en cada prueba. Compilación: GCC `-Wall -Wextra -Wpedantic -O2`, sin `-ffast-math`, con `-I/tmp/opencode/b2-system-headers -I<B2>/include -Iinclude`; el wrapper incluye `/usr/include/fenv.h`. No se inyectaron flags de prueba ni se forzó una respuesta positiva.

### Consulta gruesa en todos los descriptores reales

Por etapa se consulta cada AABB completa de los 9 segmentos de las 288 guías y de los 9 segmentos de los 9,216 descriptores válidos. Para cada caja se ejecutan por separado el rango corporal directo y `LongHairFieldQuery_RangeStatus` B2. “Clear B2” cuenta cajas de **segmento completo** cuyo límite inferior de la unión es al menos `.0005 * scale`; no confundir con los conteos q/72 del subset que sigue.

| Etapa | B2 guía: conocidas / UNKNOWN mandíbula / clear | B2 hijo: conocidas / UNKNOWN mandíbula / clear | UNKNOWN cuerpo / UNKNOWN composición tras prueba jaw |
|---:|---:|---:|---:|
| 0 | 2,461 / 131 / 294 | 78,468 / 4,476 / 1,446 | 0 / 0 |
| 1 | 2,470 / 122 / 50 | 78,698 / 4,246 / 13 | 0 / 0 |
| 2 | 2,480 / 112 / 65 | 79,054 / 3,890 / 30 | 0 / 0 |
| 3 | 2,492 / 100 / 63 | 79,494 / 3,450 / 43 | 0 / 0 |

Cada fila tiene 2,592 cajas de guía y 82,944 de hijo. Todo UNKNOWN combinado se clasificó como **prueba de exclusión jaw no establecida**; el helper no ofrece un subcódigo para distinguir cercanía/intersección, no finitud u otras causas internas. La prueba adicional sobre las cajas UNKNOWN también devolvió false. No hubo cajas con body UNKNOWN ni casos donde la prueba jaw pasara pero fallara la composición. B2 no aumenta las cajas positivas sobre el conteo body-only: en etapas 1–3 coinciden y en etapa 0 la composición reduce guía 336→294 e hijo 2,225→1,446. En estos datos el límite corporal sigue limitando el clear; mandíbula puede reducirlo, no rescatar las cotas corporales negativas. En adulto, 43/82,944 cajas de hijo completas (0.0518%) y 63/2,592 de guía superan el umbral.

Contadores gruesos acumulados en las cuatro etapas: 342,144 rangos de cuerpo directos, 342,144 llamadas `RangeStatus` B2, y 16,527 probes jaw repetidos sólo para clasificar UNKNOWN; cada `RangeStatus` B2 hace internamente una consulta corporal. Esto suma 700,815 llamadas de API del diagnóstico y 684,288 invocaciones subyacentes al proveedor de intervalos corporales. Son conteos, no tiempos.

### Muestras de auditoría originales con subdivisión y B2

La misma muestra adulta determinista (primer hijo válido por guía, 288 fibras) y los mismos niveles 0/1/2/4 del apartado anterior. Cada muestra original `q=1..72` se asigna a su subcaja por el parámetro exacto calculado por `HairCurveBounds_AuditSample`. **Combined B2 certified q** cuenta únicamente muestras cuya subcaja devuelve rango combinado conocido con lower sobre el umbral. Cada otro q queda en fallback, separado por causa. Las consultas de cuerpo, B2 y probes están contadas en el presupuesto de 248,832 llamadas.

| Prof. | Consultas body | `RangeStatus` B2 | Probes jaw | Cajas B2 UNKNOWN jaw | Combined B2 certified q / 20,736 | Fallback q: jaw UNKNOWN | Fallback q: known pero no clear | Root certified q=1..7 / 2,016 |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 0 | 2,592 | 2,592 | 117 | 117 | 16 (0.077%) | 921 | 19,799 | 0 |
| 1 | 5,184 | 5,184 | 212 | 212 | 380 (1.83%) | 833 | 19,523 | 16 (0.79%) |
| 2 | 10,368 | 10,368 | 409 | 409 | 600 (2.89%) | 803 | 19,333 | 28 (1.39%) |
| 4 | 20,736 | 20,736 | 784 | 784 | 863 (4.16%) | 784 | 19,089 | 35 (1.74%) |

No body-UNKNOWN samples or composition-UNKNOWN-after-jaw-proof samples occurred in this subset. The sums `certified + fallback` equal all 20,736 original q samples at each level. Direct body candidates and combined B2 certificates match exactly (16/380/600/863); B2 adds no clear samples beyond body because its successful far-jaw lower bounds do not repair the weak body lower. The full subset consumed **79,282** budgeted calls: 38,880 direct body queries + 38,880 B2 `RangeStatus` calls + 1,522 jaw reason probes. The combined-helper calls also invoke the body range internally. No exact-distance q was skipped in runtime; these are conservative diagnostic certificates only.

**Lectura:** B2 fixes the pre-B2 all-UNKNOWN state for most boxes (adult known 95.84% of child segment AABBs), but this does not yield a useful positive certificate rate. Bounded subdivision raises the representative subset to 4.16% combined q candidates only at 72 interval queries/fiber, with 1.74% of proximal q samples; 95.84% still require the original audit. This supports **NO-GO**: body interval quality, not the jaw proof, limits positive coverage. Keep exact runtime unchanged; do not widen the SDF or lower C padding. B2 remains independently reviewed and may be kept as a future helper, but is not an end-to-end optimization result.

## Causa del bajo límite inferior y siguiente paso acotado

No es un hard gate de configuración corporal. En el wrapper, el único sitio que devuelve deliberadamente `minimum=-FLT_MAX` (`src/MonsterSDF.c:2534–2544`) es la rama conservadora de cutters/substracción, que sólo dispone allí de una prueba del lado superior. Esa rama representa entre 75.7% y 80.3% de las cajas de hijos en estas etapas. Adicionalmente, aunque el intervalo sea válido, las cajas completas son amplias y sólo una fracción minúscula tiene límite inferior sobre el umbral; la sección de raíz no lo consigue nunca.

**Siguiente paso acotado, no autorizado como cambio de runtime en este trabajo:** B2 ya fue revisado condicionalmente y la repetición confirma que elimina UNKNOWN jaw para la mayoría de cajas, pero no mejora la cobertura positiva corporal. Si se continúa, instrumentar offline cuál subcaso de cutters/substracción de la receta produce `-FLT_MAX`, y estudiar una cota inferior sólo para un caso donde pueda probarse que la operación no reduce el campo dentro de la caja. Mantener UNKNOWN en todo otro caso, componer con redondeo exterior y repetir la matriz/dense-check como regresión (no como sustituto de la prueba), con especial atención al primer segmento. No inferir 1-Lipschitz, no certificar por muestras, no usar body-only como campo combinado y no iniciar un rewrite general de SDF.

**Resultado operativo actualizado:** la subdivisión acotada mejora la cobertura candidata de una muestra representativa, por lo que no se debe concluir que ninguna subdivisión sirve. Pero incluso a profundidad 4 sólo 863/20,736 muestras q del subset adulto tienen una cota corporal sobre el umbral, con 35/2,016 proximales; la mayoría de los queries siguen con lower no útil. La repetición B2 prueba el jaw far-arm en las cajas que lo permiten, pero no mejora los positivos respecto del cuerpo y la mayoría de q sigue en fallback. El costo relativo de estos queries no se midió. **Se mantiene NO-GO para integrar runtime**: no relajar `roundingPadding`, no generalizar la cobertura del subset y no ampliar el SDF sin prueba acotada.

## Experimento opt-in de grupos de puntos CPU discretos

Esta sección es una propuesta de diagnóstico separada de todas las matrices de cajas continuas Catmull/Bezier anteriores. Sigue C2 (`09470add` en `/tmp/opencode/monster-perf-20261106/c`): no se estrecha ni modifica el padding C y no se afirma una prueba universal CPU/GPU. Solo se estudian los puntos de auditoría CPU originales `q=1..72` del subset adulto determinista (primer hijo válido por guía visible).

La opción `--discrete-q` construye una vez los 72 `Vector3` float originales por hijo, en orden q, con `t=(float)q/72.f`, `LongHairGuide_Evaluate` de ambas guías, `Vec3_Lerp`, normal `Unit(Vec3_Lerp(posedNormalA,posedNormalB,weight), posedNormalA)` con el fallback/umbral de `LongHair.c`, `Vec3_Scale(normal,lift)` y `Vec3_Add`, sin pasar por Bézier. Agrupa índices originales contiguos con tamaños 8, 4 y 1 (no resegmenta ni omite fronteras q). La AABB componente a componente se forma directamente del mismo buffer float. Solo un rango B2 conocido, finito y válido con `lower >= .0005f*scale` hace provisionalmente cubiertos todos los q del grupo; UNKNOWN, lower cercano o inválido cuenta como fallback. Para grupos cubiertos, la comprobación diferencial pasa exactamente `points[q]` al callback combinado exacto y marca cualquier no finito o distancia menor al umbral como mismatch. Los checks bitwise verifican únicamente reutilización del buffer entre la copia observada al construir la AABB y los elementos consumidos por el diferencial; **no** demuestran equivalencia del codegen del diagnóstico con el codegen de producción. La expresión fuente se revisa por separado contra `LongHairSet_Build`. Valores de punto no finitos rechazan el grupo explícitamente y se cuentan, nunca se omiten silenciosamente.

La validez requiere por tamaño de grupo `covered_q + fallback_q == selected_fibers*72`, `root_covered_q1_q7 + root_fallback_q1_q7 == selected_fibers*7`, contabilización completa de grupos/consultas, cero grupos/puntos rechazados, cero bitwise failures y cero exact mismatches. Si falla una identidad, hay un valor inválido o aparece un mismatch, el JSON marca `status=INVALID`, se escribe el fallo de invariant a stderr y el proceso termina con código no cero; nunca se reporta PASS silenciosamente.

`--measure-discrete-q` añade una comparación opcional sobre **los mismos buffers precomputados**: baseline de las 72 llamadas exactas originales por descriptor (q en orden, early-exit intacto) frente a cada tamaño de grupo con min/max por componente, `RangeStatus` combinado B2 y callback exacto para q de fallback (orden y early-exit intactos). Hace tres warmups y cinco muestras monotónicas por ruta/tamaño; el temporizador incluye el minmax y consultas, excluye la generación de puntos y el escaneo preexistente de cobertura del harness. Solo mide el subset de descriptores finales aceptados: no incluye intentos/retries, proyección de raíz, el `LongHairSet_Build` completo ni GPU. Estos tiempos son feasibility local del query path, no speedup de runtime ni resultado de producción.

El JSON separa por tamaño de grupo: grupos/consultas/conocidos/UNKNOWN/clear, q cubiertos y fallback, q raíz `1..7`, puntos inválidos/rechazos y comprobaciones/mismatches exactos. La identidad es el mismo snapshot de `MonsterSDF` y callback combinado B2 descrito arriba; `matchesExactField` sigue establecido solo con ese snapshot. El diagnóstico no altera los reintentos ni el early-reject del constructor y no cambia el runtime: no salta ninguna distancia de producción. Tampoco certifica puntos intermedios, una curva continua, otros hijos ni GPU. El AABB contiene por construcción los puntos CPU finitos agrupados; la comprobación exacta offline validó los puntos provisionalmente cubiertos.

### Resultado de cobertura y exactitud

Ejecución adulta, primer hijo válido por cada una de **288** guías visibles, 20,736 puntos `q=1..72` por tamaño. Las identidades de q total y q raíz `1..7` pasaron para 8/4/1, así como contabilización de grupos/rangos, `exact_checks == covered_q`, bitwise failures = 0 y exact mismatches = 0. No hubo puntos inválidos ni grupos rechazados. Condiciones: `FE_TONEAREST` (`fegetround=0`), sonda gradual-underflow true, fast-math false, `__STDC_IEC_559__` presente; jaw metadata matched el mismo snapshot exacto.

| Grupo contiguo | Rangos B2 | Clear groups | Covered q / 20,736 | Fallback q / 20,736 | Root covered q=1..7 / 2,016 | Exact checks / mismatches | Bitwise checks / failures | Invalid / rejected |
|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 8 | 2,592 | 118 | 944 (4.55%) | 19,792 (95.45%) | 28 (1.39%) | 944 / 0 | 944 / 0 | 0 / 0 |
| 4 | 5,184 | 680 | 2,720 (13.12%) | 18,016 (86.88%) | 72 (3.57%) | 2,720 / 0 | 2,720 / 0 | 0 / 0 |
| 1 | 20,736 | 3,911 | 3,911 (18.86%) | 16,825 (81.14%) | 79 (3.92%) | 3,911 / 0 | 3,911 / 0 | 0 / 0 |

### Feasibilidad de costo del query path

Se hicieron 3 warmups y 5 muestras por baseline/candidato/grupo, usando los mismos buffers CPU precomputados de los 288 descriptores finales aceptados. Baseline mide solo las 72 llamadas exactas combinadas en orden q con early-exit. Candidato mide min/max componente a componente + `RangeStatus` body+jaw combinado + llamadas exactas para grupos fallback, conservando orden q y early-exit; el temporizador excluye generación de puntos y el escaneo grueso/subdivisión ajenos a la comparación. No hubo early exits en ninguna ruta. Los valores son medianas `CLOCK_MONOTONIC` de esta máquina:

| Grupo | Baseline exacto (ms) | Candidato range+fallback (ms) | Diferencia local | Range queries | Exact fallback calls |
|---:|---:|---:|---:|---:|---:|
| 8 | 33.683 | 36.173 | +7.39% | 2,592 | 19,792 |
| 4 | 33.771 | 36.260 | +7.37% | 5,184 | 18,016 |
| 1 | 33.566 | 52.886 | +57.56% | 20,736 | 16,825 |

**NO-GO para integrar/investigar este candidato como optimización de runtime:** incluso la mejor cobertura (grupo 1) deja 81.14% de q en fallback y 96.08% de los q raíz, y los tres query paths medidos fueron más lentos que el baseline en este entorno. El costo incluye el minmax/range necesario, no la construcción de puntos. Esto es una comparación acotada del query path del subset, no una predicción de `LongHairSet_Build`, no un speedup del producto y no una medición CPU/GPU universal. La evaluación no cambió umbrales ni bounds.

### Comandos reproducibles

```sh
CORE_OBJS="$(make --no-print-directory --silent \
  --eval='print-agent-e-core:;@echo $(CORE_OBJS)' print-agent-e-core)"
B2=/tmp/opencode/monster-perf-20261106/b2
gcc -Wall -Wextra -Wpedantic -O2 \
  -I/tmp/opencode/b2-system-headers -I"$B2/include" -Iinclude \
  tools/long_hair_interval_preflight.c \
  src/HairCurveBounds.c "$B2/src/LongHairFieldQuery.c" $CORE_OBJS \
  -lm -pthread -o build/long_hair_interval_preflight_b2
RECIPE=/home/guillermo/Escritorio/monster_engine/docs/validation/long-hair-styling/caballo-crin-foliculos.creature.json
build/long_hair_interval_preflight_b2 "$RECIPE" 3 --discrete-q \
  > build/long-hair-preflight-discrete-q-adult.json
build/long_hair_interval_preflight_b2 "$RECIPE" 3 --measure-discrete-q \
  > build/long-hair-preflight-discrete-q-timing-adult.json
```

La ejecución registrada usó B2 read-only SHA `301bb4a147f35795a176c32c9a57c3147a9c23aa` y guardó JSON, stderr, códigos de salida, logs de build, resúmenes y hashes SHA-256 en `/mnt/storage/monster-engine-agents/scratch/perf-20261106-e-discrete/`. El recipe SHA-256 es `241682fc8b63eebc601feb687ae52812cbcbd2ace6ece3e3d7f6291fd5ba2fb3`; el wrapper `/tmp/opencode/b2-system-headers/fenv.h` SHA-256 es `77fe1facf706487f4f33663f23c375fb5e86a6d91e2e27982bc0c2e4f08ef2de`. El manifiesto `identity.txt` conserva las identidades completas de fuente, binario, compilador, kernel y CPU; `artifact-sha256.txt` hashea las salidas crudas.
