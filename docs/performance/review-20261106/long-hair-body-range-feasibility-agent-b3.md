# Factibilidad de cotas inferiores corporales para pelo — Agent B3

## Decisión

**NO-GO para habilitar certificación de pelo en runtime.** El B2 jaw-far proof y el diagnóstico de Agent E eliminan la hipótesis de que el principal bloqueo sea simplemente “no hay rango de mandíbula”: para el caballo, el cuello de botella que queda es la **cota inferior corporal útil**. La fuente tiene una continuación acotada que vale la pena estudiar, pero esta lectura no demuestra que produzca suficientes cotas positivas después de incluir cabeza y cutters. La cobertura de cualquier propuesta B3 queda **UNKNOWN** hasta una prueba dirigida; no debe tratarse como positiva ni extrapolarse del subconjunto de E.

Este informe es análisis de fuente. No se cambió runtime, helper, tests ni Makefile; no se hicieron probes, compilaciones, benchmarks o capturas.

## Alcance y referencias revisadas

- Contrato original: `PROMPTS.md`, Agent B, especialmente líneas 120–128: composición del campo real `min(body,jaw)`, no suponer 1-Lipschitz, UNKNOWN conserva auditoría exacta y no aceptar límites por muestreo.
- Fuente revisada en este worktree: `src/MonsterSDF.c` (rango y evaluación escalar), `src/SDFOperations.c` (unión/resta suave), `src/MonsterSDFRayBounds.h` (bounds de cabeza), `include/MonsterSDF.h` (estado de sustracción).
- Diagnóstico E leído en `/tmp/opencode/monster-perf-20261106/e/docs/performance/long-hair-preflight-agent-e.md`. E identifica su snapshot de fuente como `c9757f85c10b06d34ae19ed7b0cde741a8f5b3eb`, receta congelada SHA-256 `241682fc8b63eebc601feb687ae52812cbcbd2ace6ece3e3d7f6291fd5ba2fb3`.
- Helper B2 revisado sólo en lectura, SHA reportado `301bb4a147f35795a176c32c9a57c3147a9c23aa`; no se integró ni se modificó.

## Camino exacto del campo y rango

### Gates y cobertura estructural

`MonsterSDF_GetCellRangeWrapper` (`src/MonsterSDF.c:2500–2508`) devuelve `false` si el snapshot es nulo, `bodyPartCount != 0`, no hay conectores ni al menos dos estaciones axiales, faltan nodos para conectores o alguna boca no tiene cabeza anatómica. La incompatibilidad de jerarquía no fuerza por sí sola UNKNOWN: selecciona la acumulación lineal (`:2507–2509`, `:2525–2532`).

En las cuatro etapas del caballo diagnosticado no se activa ninguno de esos gates: cabeza anatómica, `bodyPartCount=0`, 60 conectores, 119 nodos, 13 estaciones axiales y jerarquía compatible. E informa rango corporal finito/ordenado para 85,536/85,536 cajas por etapa. Por tanto, el bloqueo de ese caso no es ausencia del callback ni incompatibilidad de jerarquía; es la calidad/signo de sus cotas inferiores y la rama de cutters descrita abajo.

### Construcción del lower corporal

El evaluador escalar (`MonsterSDF_EvaluateDistanceInternal`, `src/MonsterSDF.c:1583–1642`) combina primero barrido axial, body parts y conectores mediante uniones suaves (`:1586–1619`), después añade el término de cabeza/bozal (`:1620–1628`) y finalmente aplica las restas de boca, órbitas, narinas y tímpanos (`:1629–1639`). El intervalo debe respetar ese mismo orden lógico: una cota de axial+conectores **antes** de la cabeza no es automáticamente una cota de todo el cuerpo.

`MonsterSDF_StationsRange` acota los tramos Hermite por sus cuatro controles Bézier (`src/MonsterSDF.c:2161–2192`). La jerarquía de conectores agrega `MonsterSDF_ConnectorRange` con `MonsterSDF_RangeSmoothMin` (`:2419–2455`); el recorrido lineal hace la unión equivalente (`:2525–2532`). La poda positiva por soportes de conectores sólo sirve cuando sus condiciones geométricas/cutoff se prueban (`:2460–2488`).

Para cabezas, el fast path exige `farHeads` (`:2510–2514`, `:2516–2522`, `:2548–2556`). La cota positiva se construye de `positiveHeadBounds`, compilada con `RayBounds_PackedMouth(m, headBodySmoothness, .001f)` (`:1098`; bounds en `src/MonsterSDFRayBounds.h:108–173`). El fast path que usa `RangeSmoothMin(lower, headBodySmoothness, headBodySmoothness)` (`:2519–2521`, `:2551–2556`) no debe copiarse como si el valor corporal pre-cabeza fuera el campo completo: su uso depende de la condición `farHeads` y de que la envolvente de subnivel de cabeza sea válida para el snapshot. Fuera de ese caso, la ruta completa transforma la caja a local y compone `MonsterSDF_UpperHeadRange` con `headBodySmoothness` (`:2558–2562`; implementación de cabeza `:2284–2356`). Cualquier límite B3 debe incluir este término smooth-min antes de certificar.

### Por qué aparece `-FLT_MAX`

`MonsterSDF_CompileSubtractionBounds` agrega presupuesto y roundoff de cutters, crea una envolvente global de sus subniveles y guarda `subtractionBounds`, `subtractionLevel`, `subtractionBudget` y `subtractionRoundoff` (`src/MonsterSDF.c:1121–1163`; campos en `include/MonsterSDF.h:300–301`). En `MonsterSDF_GetCellRangeWrapper`, cuando se cumple `accumulated.hi < -subtractionLevel` **o** el caso `farHeads && accumulated.lo <= 0 && accumulated.hi >= 0`, y la caja queda fuera de la envolvente, la rama calcula sólo una cota superior:

```c
upper = max(accumulated.hi, -subtractionLevel)
      + subtractionBudget + subtractionRoundoff;
```

Si cumple el gate de `:2543`, devuelve `minimum=-FLT_MAX` (`src/MonsterSDF.c:2534–2545`). El comentario de `:2541–2542` declara el objetivo: una caja corporal incierta lejos de cabeza/cutters sigue incierta, no se resuelve a favor de free space. Esta es la única asignación deliberada de `-FLT_MAX` que E encontró para sus resultados. El conteo `-FLT_MAX` del diagnóstico E es por tanto una causa concreta de UNKNOWN, no “distancia negativa medida”.

La rama no produce el lower porque la prueba compilada de cutters es una prueba de subnivel/upper: basta para afirmar que el resultado sigue interior bajo sus condiciones, no para mostrar que toda la caja está libre. En especial, el caso de signo cruzado ya tiene `accumulated.lo <= 0`; conservar ese lower por monotonía no puede por sí solo certificar `.0005*scale`.

## Qué permite —y qué no— la monotonicidad de la resta

`SDF_SmoothSubtract(body,cutter,k)` (`src/SDFOperations.c:26–30`) es `max(body,-cutter)` suavizado: para aritmética real, su valor es mayor o igual que `body`; si `k <= .0001`, el código usa directamente `max` (`:27`). En secuencia, las restas no disminuyen el campo respecto del valor que reciben. Esto permite conservar una cota inferior pre-resta válida, pero **no convierte una cota inferior no positiva en una positiva**.

Para explotar una sustracción que sí aleja el campo de la superficie hace falta además una cota superior del cutter que permita acotar `-cutter`; el lower útil depende de ambos operandos y del `k` de cada operación. La implementación de intervalos ya tiene `MonsterSDF_SubtractRange` (`src/MonsterSDF.c:2232–2233`) y `MonsterSDF_HeadCuttersRange` (`:2363–2410`), que procesa slot/cavidad, órbitas, narinas y tímpanos. La ruta completa normal los llama después de incorporar `MonsterSDF_UpperHeadRange` (`:2558–2566`), pero el early return de sustracción los omite.

Así, existe una **oportunidad de factibilidad acotada**, no una prueba de cobertura: para el subcaso `farHeads && accumulated.lo <= 0 && accumulated.hi >= 0` que hoy devuelve `-FLT_MAX`, estudiar si continuar por la composición ya existente de cabeza+cutter obtiene un lower finito y positivo. El subcaso `accumulated.hi < -subtractionLevel` que la prueba acredita como interior no debe convertirse en free-space; puede permanecer UNKNOWN para hair. No se debe usar `accumulated.lo` sin cabeza, ni la monotonía de sustracción como sustituto de cotas de cutters.

La monotonía indicada es matemática. Para que el intervalo implementado contenga el resultado del evaluador **float**, una versión runtime tendría que establecer el redondeo exterior de las operaciones y la conversión de los extremos; el margen genérico existente al final (`:2568–2571`) no es, por sí solo, una derivación presentada aquí para un nuevo lower-only shortcut. La continuación candidata debe reutilizar la ruta de intervalo completa que ya existe o aportar un error float demostrado; si no puede probarlo, devolver UNKNOWN.

## Límites observados por E que no arregla un bound jaw

Los números de abajo son diagnósticos de E/B2, no nuevas mediciones de este informe:

| Etapa | `bodyPartCount` | Cajas hijo con rango body | Candidatas body-clear / 82,944 | `lower=-FLT_MAX` / 82,944 |
|---:|---:|---:|---:|---:|
| 0 | 0 | 82,944 | 2,225 | 62,789 (75.7%) |
| 1 | 0 | 82,944 | 13 | 64,034 (77.2%) |
| 2 | 0 | 82,944 | 30 | 65,714 (79.2%) |
| 3 | 0 | 82,944 | 43 | 66,640 (80.3%) |

B2 hizo conocidas 79,494/82,944 cajas adultas y dejó 3,450 UNKNOWN por jaw, pero clear siguió en 43; no elevó el positivo sobre body-only. En el subset adulto representativo de 288 primeros hijos por guía, las 20,736 muestras q originales dieron 16/380/600/863 candidatas con profundidad 0/1/2/4; a profundidad 4 siguieron 19,873/20,736 en fallback y sólo 35/2,016 muestras proximales fueron candidatas. Son candidatos de intervalo, no muestras runtime omitidas ni promesa de speedup.

La cota positiva fija `.0002f` de `:2514` también puede quedar por debajo de la condición del auditor `.0005f*scale`: para scales 0.62/0.82/1.0 los umbrales son aproximadamente .00031/.00041/.0005. En stage 0, scale .40 hace que ambos sean aproximadamente iguales, no deja margen por sí mismo. Por tanto esa salida no sirve como certificado general del caballo adulto.

La caja geométrica importa además del rango: E reporta para 2,592 segmentos adultos representativos diagonal de los controles Bézier media 0.1482 frente a AABB padded media 0.2089; el padding restante medio 0.01882 excede aproximadamente 17 veces la media del término `2*abs(lift)` (0.001091). En esa muestra el lift no domina. No hay base para reducir el padding C; una subdivisión puede estrechar la caja, pero E observó sólo 4.16% de q candidatas a profundidad 4, con 72 niveles/caja por fibra y sin medir coste de rango.

## Propuesta acotada y gates

1. **No integrar ni habilitar el skip ahora.** Conservar el exact audit y UNKNOWN para toda caja sin lower combinado probado.
2. Si el coordinador autoriza una fase de factibilidad, aislar únicamente el early return de `farHeads && accumulated.lo<=0 && accumulated.hi>=0` (`MonsterSDF.c:2534–2545`). Comparar, sólo como diagnóstico, su intervalo actual con la continuación por `MonsterSDF_LocalCell` → `MonsterSDF_UpperHeadRange` → `MonsterSDF_HeadCuttersRange` (`:2558–2566`). No tocar el campo exacto, umbral, padding, jaw callback ni límites de Bézier. Mantener el atajo de caja demostrablemente interior como no-clear/UNKNOWN para pelo.
3. La continuación debe componer el lower pre-resta con la cabeza usando el `headBodySmoothness` real y después aplicar cada cutter/rim/suavizado en el orden de la evaluación escalar. Los extremos convertidos a `float` deben redondearse hacia fuera con error justificado; si cualquier pieza, operación o contexto no está certificado, salida UNKNOWN. La caja de cuerpo sola no puede etiquetarse como `min(body,jaw)`.
4. **Cobertura esperada: UNKNOWN.** La hipótesis de que se rescaten cajas del `-FLT_MAX` branch requiere medirlo; monotonicidad sola no rescata intervalos con lower no positivo. Rechazar si los nuevos lowers no exceden `.0005*scale + safety_margin` en una fracción útil o si la consulta completa cuesta más que el fallback.

### Configuraciones y verificaciones requeridas antes de integrar

- Para la prueba de factibilidad: receta caballo congelada, cabeza cerrada en reposo, etapas 0–3, caja de guía/hijo Catmull completa con el padding actual. Reportar por separado branch original, branch de fallback, body lower, lower después de smooth-union de cabeza, lower final tras todos los cutters y clasificaciones positive/UNKNOWN. No extrapolar los 288 primeros hijos a los 9,216.
- Porque `MonsterSDF_GetCellRangeWrapper` es compartido, cualquier cambio genérico también requiere todas las configuraciones que pasan sus gates: especies con `bodyPartCount==0`, axial/no axial y jerarquía compatible/incompatible, una o varias bocas anatómicas, cutters/morphs con variación de cabeza y llamadas de meshing. Para body parts, cabeza no anatómica, ausencia de jerarquía necesaria, bounds no finitos o unsupported head, la respuesta continúa siendo `false`.
- Unit tests de interval containment en cajas por debajo, encima y cruzando cavidades/slot, órbitas, narinas y tímpanos; composiciones smooth-union head/body; varias escalas/etapas; subcaso interior que conserva UNKNOWN; NaN/Inf y redondeo. Denso muestreo es regresión solamente, nunca la prueba.
- Sólo después de prueba estática y test: la matriz autorizada de E con cuatro etapas, auditoría original de 72 muestras, exact field body+jaw, mismo digest/raíces y capturas. Medir intervalo y coste total en slot exclusivo; ningún resultado de este informe es timing.

**Conclusión operativa:** el branch de cutters es un lugar concreto para una investigación acotada, pero la única propiedad de monotonía identificada no basta para obtener certificados positivos cuando el pre-cut lower cruza cero. La continuación full-head/full-cutter existente es una hipótesis comprobable, no evidencia de cobertura. Hasta pasar esas gates: **NO-GO, exact fallback intacto, sin speedup atribuido**.
