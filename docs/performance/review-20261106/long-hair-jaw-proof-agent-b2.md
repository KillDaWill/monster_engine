# B2: prueba acotada de exclusión de mandíbula

## Resultado

`LongHairFieldQuery_JawExclusionLowerBound` ya no usa la heurística anterior
`32*FLT_EPSILON`: devuelve un límite sólo cuando demuestra que **cada punto de
la caja** ejecuta la rama lejana exacta de `LongHairJawDistance` y que el valor
redondeado de esa rama es estrictamente mayor que `.4f * bodyScale`. Además,
el dueño debe activar `matchesExactField` sólo después de verificar que el
snapshot copiado coincide con la mandíbula usada por `exactDistance` en esa
misma generación. Si no puede establecer esa correspondencia, la exclusión
geométrica devuelve UNKNOWN. En los
demás casos responde UNKNOWN (`false`, `-FLT_MAX`). No evalúa ni aproxima la
distancia de la cavidad, no cambia el callback exacto ni sus resultados.

La prueba geométrica produce un límite `jLo` para el término de mandíbula. Si
`LongHairFieldQuery_RangeStatus` ya tiene un intervalo corporal válido y
coherente con la generación/contexto, puede componer `[jLo, FLT_MAX]` con él.
El extremo superior es válido porque el callback exacto en la rama certificada
devuelve una distancia float finita; si el campo corporal no tiene intervalo,
el resultado sigue UNKNOWN. La composición es `min([bLo,bHi],[jLo,jHi]) =
[min(bLo,jLo), min(bHi,jHi)]`, ensanchada hacia fuera. Un callback `jawRange`
conservador válido tiene prioridad.

## Derivación numérica

Para una caja de consulta `Q`, se propagan intervalos cerrados outward por el
árbol real de operaciones de `LongHairJawDistance` y `Vector.c`:

1. `rel = p - center` (por componente).
2. Cada `Vec3_Dot(rel, axis)`: productos binarios float y suma left-associative
   `((x*x + y*y) + z*z)`, con cada intervalo de resultado expandido al float
   representable anterior/siguiente.
3. `r = 1.0f / scale` como reciprocidad float; después `local = pivot +
   (local - pivot) * r`, de nuevo encerrando resta, producto y suma separados.
4. Para cada eje, `delta = max(bounds.start-local,
   max(0,local-bounds.end))`. Se usa monotonicidad de resta/max para encerrar
   todos los valores de la caja; `delta` se recorta a no negativo, como en el
   código exacto.
5. `Vec3_Length`: `((dx*dx + dy*dy) + dz*dz)`, luego `sqrtf`, luego
   multiplicación por `scale`. Se propagan los extremos inferiores/superiores
   para cada operación. `scaled.lo` es menor o igual que cualquier `lower`
   calculado por el código exacto para cualquier `p` de `Q`.
6. Sólo se acepta si `scaled.lo > (.4f * bodyScale)`, donde el lado derecho es
   calculado con la misma operación float y comparación estricta. Así la
   condición del `if` exacto se satisface para la caja completa y el resultado
   devuelto por esa rama queda acotado inferiormente por `scaled.lo`.

Cada operación float finita se ensancha por `nextafterf` en ambos sentidos.
Esto encierra el valor real exacto para IEEE binary32 en `FE_TONEAREST`, con
`sqrtf` IEEE correctamente redondeado; no se usa una tolerancia constante ni un margen
epsilon empírico. El helper falla cerrado ante formato distinto, caja/metadata
no válidos, resultado no finito/desbordamiento, compilación con
`__FAST_MATH__` o sin macro IEC 60559, falta de gradual underflow (sonda de subnormal) o predicado
lejano no demostrado. El ensanchamiento también cubre underflow gradual. Las
operaciones de la implementación se mantienen separadas y sin `-ffast-math`;
si se cambia el árbol de `LongHairJawDistance`, la derivación/tests deben
actualizarse antes de reutilizar este certificado.

La comprobación `fegetround()` ocurre en el hilo que calcula la cota. El
callback exacto y cualquier worker que lo ejecute deben conservar
`FE_TONEAREST`, gradual underflow y las mismas suposiciones de compilador/FP
durante toda la vida del certificado. Un cambio de modo en un hilo consumidor
puede alterar la evaluación del predicado estricto `.4f * bodyScale`; el
planificador futuro debe fijar/verificar este contrato y no trasladar una cota
entre entornos incompatibles. `matchesExactField` sólo afirma correspondencia
de metadata/generación: **no** verifica modo de redondeo, FTZ/DAZ ni flags de
compilación.

`lowerBound` es obligatorio: NULL devuelve false/UNKNOWN, igual que los demás
fallos; con puntero no nulo la salida se inicializa a `-FLT_MAX` antes de
intentar probar. La detección portable de FTZ/DAZ se limita a la sonda de
subnormal del helper; no se dispone de una API C portable para activar/togglear
FTZ en el test, por lo que esa variante se documenta como no probada.

## Alcance y cobertura observada

| Caso | Resultado esperado/observado |
|---|---|
| Caja en/intersectando el bounds de mandíbula, cavidad o cerca del umbral | UNKNOWN si la rama lejana no queda probada |
| Caja lejana, transformación identity | Prueba positiva en test independiente y malla de regresión |
| Rotación, escala activa pequeña, pivote no cero | Intervalos completos; sólo se acepta si pasa el mismo predicado |
| Traslación grande/cancelación, subnormal/overflow, AABB vacío, NaN/Inf | Falla cerrado cuando no se puede conservar una cota finita |
| Body range UNKNOWN/no finito | Unión UNKNOWN, aunque la mandíbula esté lejos |
| Cambio de generación | Se refleja en `LongHairFieldQuery_MatchesGeneration`; el propietario debe recrear la vista |
| B2 aplicado a caballo / cobertura total de la crin | **No medido**: el preflight E previo al glue reporta combinado UNKNOWN en el 100% de cajas; B2 no se ejecutó contra esas cajas |

Las mallas densas de puntos en las pruebas son regresiones para detectar errores,
no constituyen la prueba. La prueba es la propagación intervalar descrita arriba.
No hay aún evidencia de que una fracción útil de las cajas de `caballo-curve`
quede certificada, ni de que el callback de bounds de producción coincida con
la metadata de mandíbula al construir `LongHairFieldQuery`.

### Incorporación del preflight E (c5eb624822214c7bba6d035a17073256c36f3615)

El diagnóstico de E sí permite precisar el límite de utilidad sin afirmar que
la mandíbula ya esté cubierta. En las cuatro etapas el wrapper corporal devolvió
intervalo ordenado para las 85,536 cajas consultadas; sin embargo, el límite
inferior de 75.7–80.3% de las 82,944 cajas de hijo fue `-FLT_MAX`. Sólo
2,225 / 13 / 30 / 43 cajas de hijo en etapas 0 / 1 / 2 / 3 alcanzaron el
umbral corporal `.0005 * scale`. El primer segmento de raíz tuvo cero
candidatos en las cuatro etapas; en adulto el máximo inferior allí fue
`-0.044332605` para hijos. El término combinado cuerpo+mandíbula permanece
UNKNOWN para el 100% de las cajas con la API previa al glue.

Una cota inferior de mandíbula **por sí sola no arregla** una caja cuyo límite
inferior corporal es `-FLT_MAX`: para la unión `min(body,jaw)`, el límite
inferior compuesto sigue siendo `min(-FLT_MAX,jawLo) == -FLT_MAX`. Por tanto,
incluso en el mejor caso ideal donde B2 prueba la rama lejana en todas las cajas,
los conteos corporales citados son un techo absoluto para candidatos combinados
sin una mejora independiente de los bounds corporales. Los conteos son sólo
diagnóstico de cobertura, no muestras evitadas ni rendimiento medido.

El coste neto también es desconocido. La propagación hace múltiples
operaciones float, `nextafterf` y `sqrtf` por consulta; E no cronometró las
consultas y B2 no ejecutó benchmark. Es posible que un número bajo de positivos
no pague ese coste frente a la auditoría exacta. **Expectativa operativa: NO-GO
para activar/integrar como optimización** salvo que una repetición de la matriz
de E, usando este proveedor con metadata coincidente, demuestre candidatos
combinados suficientes (incluida cobertura proximal) y un benchmark mida
reducción neta de tiempo. No se propone expandir el scope a `MonsterSDF.c` ni
al cuerpo; todo UNKNOWN conserva exact fallback.

## Propiedad e integración propuesta

`LongHairFieldQuery`, `SDFField.context`, el contexto del callback exacto y el
contexto de `jawRange` siguen siendo prestados: deben permanecer vivos e
inmutables mientras se use la consulta. El propietario incrementa `generation`
al reconstruir cualquier geometría, eje, pivote, escala, bounds, callback o
contexto. No se añade ownership ni se retiene memoria.

`matchesExactField` es una afirmación explícita del propietario, no una
verificación introspectiva del callback opaco. Si los metadatos no coinciden o
la generación cambia, se debe recrear la vista con el flag falso/actualizado;
el helper no certifica a partir de una coincidencia inferida o de una caja
desconectada del callback.

La integración futura puede inicializar `LongHairFieldQuery` con el `SDFField`
corporal, el **callback exacto existente** (la unión body+jaw de producción),
la metadata copiada del mismo `LongHairJawField`, y generación de la misma
construcción; luego pasar `LongHairFieldQuery_Distance` como callback exacto y
`LongHairFieldQuery_Range` como proveedor opcional. No cambiar `LongHairJawDistance`
ni conectar una caja de mandíbula de otra generación. Se solicita que el dueño
de `MonsterVisual.c` haga ese glue y mida cobertura antes de habilitarlo; esta
rama no edita archivos de runtime.

No se usó la exclusión por AABB como intervalo completo aislado: es válida
únicamente para la rama lejana exacta. La zona cercana/cavidad permanece
UNKNOWN salvo que un callback independiente demuestre un intervalo verdadero.
