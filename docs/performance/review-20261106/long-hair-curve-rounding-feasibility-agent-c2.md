# C2 — Viabilidad de cotas numéricas locales para la crin

## Dictamen

**NO-GO para reducir los márgenes del helper C ni para certificar ahora el contrato CPU/GPU completo.** El `8192*FLT_EPSILON*M` del helper tiene una motivación de orden de magnitud, pero el comentario gamma existente no constituye todavía una demostración operación por operación para los dos compiladores/entornos. Además, ningún rango mundial de coordenadas del caballo se desprende del informe E ni de la receta estática. Reducir el multiplicador, `64*FLT_MIN`, el margen de extremos o `2*abs(lift)` sin reemplazarlos por una cota computada y redondeada hacia fuera no está justificado.

Sí hay una vía más estrecha y demostrable **para la auditoría CPU discreta original**, que no necesita cotas Bezier ni afirmaciones GPU: construir una caja puntual a partir del mismo resultado float de `LongHairGuide_Evaluate` y de las mismas operaciones CPU del hijo para cada `q=1..72`. Esa propuesta conserva los 72 puntos y la comparación existente; cambia solo el objeto espacial que se pasa al range query. Es un estudio de viabilidad, **no una autorización para modificar o integrar runtime**. Su utilidad depende de que el proveedor de intervalos acepte cajas puntuales/ULP y produzca rangos conservadores útiles; la evidencia E no probó esa variante.

## Alcance y fuentes consultadas

- HEAD de la rama C al iniciar el análisis: `c5403db35e4abc981c5e103c4ae981f70108aca5`.
- `src/LongHair.c`: `LongHairGuide_Evaluate` y el bucle original `q=1..72` de `LongHairSet_Build`.
- `shaders/long_hair.vert`: `Curve`, mezcla de curva hija y `normalize(mix(...))` de la normal.
- `src/HairCurveBounds.c` en HEAD C y en el snapshot citado por E (`c9757f85...`).
- Informe E `2123db945b6426878ee5a56c12883e2cba47d693`, en particular las tablas de padding/subdivisión y los límites corporales `-FLT_MAX`.
- Receta original de validación, solo lectura. Sus dimensiones/escalas son entradas de anatomía, no cotas de las coordenadas world-space producidas.

No compilé, ejecuté pruebas, medí tiempos ni reindexé.

## Por qué `8192*eps*M` no habilita reducirlo

Sea `u=2^-24` el unit roundoff binary32 (y `FLT_EPSILON=2u`), `M=max(abs(a..d))` en los cuatro controles Catmull que usa el segmento. Bajo operaciones IEEE-754 binary32 en round-to-nearest, sin overflow, un polinomio ejecutado como una secuencia conocida de operaciones puede acotarse con `gamma_n=nu/(1-nu)` multiplicado por la suma de magnitudes intermedias. Para `u∈[0,1]`, las sumas absolutas de coeficientes Catmull en forma de pesos son menores que 13; en la forma cúbica expandida del shader son menores que 24. El error de parámetro se puede separar: `x=fl(9t)` tiene error absoluto menor que `9u`; la resta de entero para formar `u` es exacta bajo Sterbenz en los segmentos interiores y exacta para el primer segmento. La derivada del polinomio Catmull tiene suma absoluta de coeficientes menor que 25, por lo que el término de parámetro es del orden de `225uM`. La conversión algebraica a Bézier tiene además errores acotables por las operaciones de resta, multiplicación por `fl(1/6)` y suma.

Esto permite diseñar una cota más precisa **si** se fijan el orden de operaciones, el conjunto de contracciones FMA permitidas, el formato y el entorno FP. No demuestra que sea seguro cambiar el código actual por un coeficiente menor:

1. `LongHairGuide_Evaluate` calcula pesos Catmull en CPU; el shader calcula una cúbica expandida distinta en orden y evalúa `mix` de guías. La conversión Bezier también redondea sus controles. Una prueba válida debe acotar cada ruta frente a un mismo polinomio real y sumar esas cotas; “64 operaciones” y una suma de coeficientes global no sustituyen el inventario exacto de operaciones ni prueban los términos omitidos.
2. `M` del helper es el máximo de **los diez** controles de la guía, aunque cada segmento solo usa cuatro. Esto es seguro como escala conservadora, pero puede ser dominado por un control remoto que no participa en ese segmento. La primera mejora defendible sería el máximo de los cuatro controles `a,b,c,d` de cada segmento, no un factor menor aplicado a todo el guide.
3. El polinomio real es invariante a una traslación común; el resultado float no necesariamente lo es, porque el CPU suma cuatro productos y la suma efectiva de pesos puede desviarse de uno. El error de evaluación depende del **valor absoluto world-space** de los controles, no solo de la curvatura o la diagonal local. Restar un origen para estrechar la cota sin reponer el error de traslación cambiaría el bound del valor CPU realmente auditado.
4. Las sumas que construyen `roundingPadding` también son float. Para una cota formal, cada producto/suma del propio margen debe redondearse hacia arriba (por ejemplo, intervalos dirigidos o `nextafterf` tras cada acumulación), no solo expandir al final los extremos AABB. El `nextafterf` final protege los extremos, pero no convierte automáticamente en superior cada margen previo redondeado.
5. La normal de CPU usa `Unit(lerp, normalA)`; el shader usa `normalize(mix(...))`. Rechazar mezcla CPU de longitud `<=.01` evita el fallback CPU y un vector cero nominal, pero no prueba por sí mismo cómo el driver calcula `mix`, longitud y normalización. `2*abs(lift)` sí es una cota geométrica amplia entre dos normales exactamente unitarias; no debe quitarse por el hecho de que sea pequeña en el subconjunto E. Cualquier cota más estrecha necesitaría una cota de error para el `mix` GPU y una cota inferior demostrada de la longitud mezclada, válida en la precisión del shader objetivo.
6. No se ha fijado aquí una garantía normativa concreta de precisión del driver para cada operación GLSL 3.30 usada ni una política que impida transformaciones del shader. Sin esa garantía no se puede convertir una estimación de CPU/GCC en prueba universal de la ruta GPU. Una comprobación empírica en un driver solo probaría ese target, no todos.

Por tanto, el comentario matemático actual es una **estimación conservadora candidata**, no una certificación independiente de todos los perfiles de compilación. No se propone cambiar sus constantes en este trabajo.

## Dominio, conditioning y magnitudes del caballo

El helper C2 restringe coordenadas de entrada a `1e6`, controles Bezier a `5e6`, `weight` a `[0,1]`, `|lift|` a `1e6`, longitudes de normales a `[.99,1.01]`, y rechaza mezcla normal `<=.01`. Son límites de API, no límites derivados de la crin validada.

Con `eps=FLT_EPSILON`, la fórmula fuente produce por guía aproximadamente `8192*eps*M`; para el hijo suma ambas cotas guía y `8192*eps*(M_A+M_B)`. Ignorando los términos diminutos de subnormal y el margen AABB, eso equivale a `16384*eps*(M_A+M_B)`. En el extremo permitido `M_A=M_B=1e6`, el término no-lift llega a unos **3,906 unidades**; es mucho mayor que la crin. Es una cota de admisión muy amplia, no prueba de que tales coordenadas ocurran.

El informe E reporta para el subset adulto diagonal media de la caja de controles Bezier `0.1482`, máximo `0.1902`, padding restante no-lift medio `0.018817` (rango `0.016648–0.020927`) y `2*abs(lift)` medio `0.001091`, máximo `0.005382`. La diagonal no limita la traslación world-space. Si se atribuyera todo el padding restante a la fórmula anterior, esta correspondería algebraicamente a `M_A+M_B` del orden de 8.5–10.7; esto es **solo una inferencia inversa de las cifras redondeadas del informe**, no una medición independiente de `M`, y puede incluir el margen AABB. No debe usarse como límite contractual.

El informe documenta escalas de pelo/cuerpo por etapa (`.40,.62,.82,1.00`) y tamaños/diagonales locales, pero no el máximo absoluto de los puntos `LongHairGuide.points` en el marco usado por la auditoría, ni su distancia al origen tras todos los módulos/attachments. En consecuencia, un límite teórico mundial `M` del caballo **no es derivable** de las fuentes consultadas. Las dimensiones de receta no bastan para demostrarlo sin ejecutar/analizar toda la construcción anatómica. `M` debe calcularse por segmento del snapshot si se quiere una cota local.

## Ruta estrecha viable: certificar exactamente los puntos CPU auditados

La auditoría no comprueba una curva continua: llama `distance` solo en `q=1..72`, en ese orden, con `t=(float)q/72.f`; calcula dos `LongHairGuide_Evaluate`, luego `Vec3_Lerp`, `Vec3_Scale(normal,lift)` y `Vec3_Add`. Esto habilita una ruta de prueba distinta de la caja continua:

1. Para cada `q`, generar el punto CPU llamando a las mismas funciones, con los mismos argumentos y el mismo binario/flags que el fallback. Reusar ese punto tanto para `getCellRange` como para el fallback `distance`; no reconstruirlo vía Bézier.
2. Consultar el campo combinado exacto `min(body,jaw)` sobre la caja degenerada `[p,p]`. Si el proveedor requiere extensión positiva, usar los dos `nextafterf` adyacentes por eje y verificar desde su contrato que la caja incluye `p`.
3. Omitir la llamada exacta de distancia solo cuando el rango combinado sea conocido y su límite inferior alcance exactamente `.0005f*scale`; `UNKNOWN`, fallo, NaN o rango no concluyente ejecutan el `distance` original en ese mismo punto. Mantener los 72 q, el orden, el early exit y la comparación original.

La inclusión CPU es exacta por construcción: el punto float que consulta el intervalo es el mismo valor float que habría recibido `distance` en el bucle original. No se sustituye la curva, no se interpola una segunda representación y no se infiere 1-Lipschitz. La caja puntual certifica solo ese q, no el espacio entre q, que tampoco inspecciona la auditoría de referencia. Los q de frontera (incluidos los que caen exactamente en un límite entre segmentos y `q=72,t=1`) no se pierden porque se enumeran por su fórmula original, sin asignarlos mediante `floor(localT*2^depth)`.

Esta propuesta podría evitar que la incertidumbre de conversión Catmull→Bezier y el hull continuo ensanche cada caja. No demuestra que los intervalos puntuales sean rápidos o útiles: la rama de cutters puede seguir dando `lower=-FLT_MAX`, y el informe E ya observa 41.0% de cajas así a profundidad 4. **No hay cobertura, consultas ni ahorro medidos para cajas puntuales.**

Este es un certificado de la auditoría **CPU**, no una cota de cada posición producida por la GPU. Para la colisión/auditoría, la GPU no calcula el argumento de `distance`; por eso puede separarse conceptualmente sin cambiar la imagen ni el shader. Si el requisito de integración exige que el mismo bound encierre también cualquier implementación GPU, la solución por puntos CPU no satisface ese requisito y el dictamen global sigue siendo NO-GO hasta fijar y probar un modelo normativo de precisión del shader.

## Evidencia E: límite independiente del padding

La muestra adulta de E subdividida hasta profundidad 4 produce 863/20,736 q candidatos corporales (4.16%), con 35/2,016 proximales (1.74%); el 95.8% permanece fallback. El `lower=-FLT_MAX` del cuerpo aparece en 41.0% de las cajas de profundidad 4. B2 no añadió muestras certificadas respecto al cuerpo. E atribuye esa cota abierta a la rama de cutters/substracción que dispone solo de prueba superior.

Esto no cuantifica qué cobertura se obtendría con padding cero y no justifica reducirlo. Sí demuestra que una cota más estrecha de redondeo, por sí sola, no resuelve los `UNKNOWN` ni elimina el problema de cota inferior corporal. La comparación de esfuerzo/cobertura de intervalos puntuales debe mantenerse separada de cualquier claim de speedup; no se ejecutó aquí.

## Propuesta de gates futuros (requiere autorización separada)

1. **No tocar el helper aceptado en C2.** Si se autoriza una variante, conservarlo intacto y compararla en herramienta offline.
2. Empezar por el bound CPU por muestra `q`, en campo cuerpo+mandíbula idéntico. Registrar por separado rangos conocidos, clear, unknown, y fallback exacto; nunca contar una caja body-only como certificado.
3. Testear todos los `q=1..72`, todas las fronteras, extremos, cinco pesos/reintentos, lift firmado, controles repetidos y cancelación. Afirmar igualdad bit a bit entre el punto usado para rango y el punto del fallback, con los mismos flags FP.
4. Probar AABB degenerada/ULP, outward rounding de rangos y que `UNKNOWN` siempre vuelve a las 72 distancias originales. Validar jaw/body snapshot identity.
5. Para cualquier claim GPU separado, exigir documento de precisión GLSL/OpenGL aplicable al driver objetivo y un bound que incluya sus operadores; una captura o pruebas aleatorias no sustituyen ese contrato.
6. Solo tras revisión matemática y gates de exactitud, solicitar ranura exclusiva para medir coste de range query frente a distancia. No ejecutar benchmarks ni full tests en esta revisión C2.

## Handoff C2

- **Conclusión:** NO-GO para estrechar padding/márgenes del helper o afirmar una prueba universal CPU/GPU a partir del análisis actual.
- **Opción matemática:** bounds puntuales exactos para la lista discreta de auditoría CPU; candidato de estudio, no implementado ni medido.
- **Limitación clave:** no hay cota source-derived del máximo world-space de controles del caballo y no se fijó una garantía universal de precisión GPU.
- **Archivos modificados:** solo este informe; no se editó helper, tests, runtime ni Makefile.
- **Pruebas/tiempos:** ninguno ejecutado/medido.
