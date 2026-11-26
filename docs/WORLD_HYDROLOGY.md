# Hidrología, Lagos, Ríos y Acceso en Orilla — Monster Engine

## 1. Principio de diseño

La hidrología de los mundos procedurales (`include/WorldHydrology.h`, `src/WorldHydrology.c`) es **estática, determinista y derivada de la topografía real**: los lagos ocupan depresiones de drenaje con cota de superficie uniforme y los ríos siguen trayectorias monotónicas descendientes hacia depresiones o hacia el perímetro del dominio. El agua no es una capa decorativa independiente: la misma rejilla hidrológica rige el vadeo, el bloqueo por profundidad, los sitios de bebida en orilla, el balance de masa molecular y el renderizado GPU.

## 2. Pipeline de generación hidrológica

1. **Priority-Flood acíclico (Barnes et al.):**
   - Cola de prioridad (*min-heap*) iniciada en los bordes del terreno junto con una cola FIFO de rellenado plano (*pit queue*) para garantizar un campo de drenaje sin ciclos locales.
   - Determina para cada celda la elevación rellenada `filledHeight` y la dirección de drenaje descendente `flowDir` (8 vecinos).
2. **Acumulación de flujo:**
   - Recorrido topológico desde mayor a menor elevación acumulando el área drenada (`flowAccum`) por celda.
3. **Detección de cuencas de lagos (`WorldWaterBody` tipo `WATER_BODY_LAKE`):**
   - Las celdas con profundidad de depresión `filledHeight - groundHeight >= lake_min_depth` se agrupan por componentes conexos.
   - Se filtran por área mínima (`lake_min_area_cells`) y límite máximo (`lake_max_count`).
   - Cada lago recibe una cota de superficie de agua exacta (`surfaceY`) igual a su elevación de desbordamiento, garantizando una lámina horizontal físicamente coherente.
4. **Red fluvial y tallado de cauces (`WATER_BODY_RIVER`):**
   - Celdas con `flowAccum >= river_flow_threshold` inician canales que descienden siguiendo `flowDir` hasta desembocar en un lago o en el borde del terreno.
   - El cauce se ensancha suavemente en función de `sqrt(flowAccum)` y `river_width_scale`, tallando la altura del suelo (`river_carve_depth`) con cota de superficie monotónicamente no creciente a lo largo del río y asignando vector de flujo `(flowDirX, flowDirZ)` unitario.
5. **Validación post-tallado:**
   - Se garantiza en todas las celdas mojadas que `waterSurfaceY >= groundY + minDepth` y que las profundidades se mantengan finitas y acotadas.

## 3. Sitios de acceso en orilla (`WorldShoreSite`)

En arenas planas antiguas, el acceso al agua asumía estanques circulares con radio constante. En mundos procedurales con orillas irregulares:
- `WorldHydrology` extrae candidatos de orilla en la frontera entre tierra seca y cuerpos de agua (`waterDepth > 0`) cuya pendiente local cumple `slope <= shore_max_slope_deg` y que cuentan con espacio libre de obstáculos.
- Se aplica espaciado mínimo (`shore_min_spacing`) para distribuir puntos accesibles a lo largo de todo el perímetro de cada lago y río.
- Tras la generación geológica (`WorldGeology`), `WorldHydrology_RevalidateShoresAgainstRocks` descarta cualquier sitio cuya posición de apoyo o dirección de aproximación colisione con lóbulos de roca (`RockCollisionLobe`), garantizando que ninguna criatura reciba un objetivo de bebida inalcanzable.

## 4. Identidad compartida del cuerpo de agua y química molecular

- Cada cuerpo de agua (`WorldWaterBody`) corresponde a un recurso molecular compartido en `EcosystemWorld` (`ECO_WATER`, sustancia configurable vía `water_substance`, por defecto `"clean_water"`).
- Múltiples `WorldShoreSite` pertenecientes al mismo lago o río comparten el mismo saldo de masa molecular (`EcosystemResource`), preservando la conservación de masa y el consumo conjunto sin duplicar la química ni romper los contratos de `CreatureMetabolism`.
- `AIWorld` indexa los cuerpos de agua y selecciona dinámicamente el sitio de orilla válido más próximo y alcanzable para cada individuo (`AIWorld_InteractionTarget` / `AIWorld_CanDrink`), manteniendo estable la identidad del objetivo percibido por `NeuralControllerV2`.

## 5. Consultas en tiempo real

- `WorldHydrology_SampleWater(hydro, x, z, &sample)` y `World_SampleWater(staticWorld, x, z, &sample)` devuelven en tiempo $O(1)$:
  - `hasWater`: presencia de agua en `(x, z)`
  - `surfaceY`: elevación de la lámina libre
  - `depth`: profundidad exacta `max(0, surfaceY - groundY)`
  - `flowDir`: vector de corriente en plano XZ
  - `bodyIndex`: índice de `WorldWaterBody` (o `-1` en tierra seca)
- `WorldHydrology_FindNearestShore` acelera la resolución de bebida para `CreatureActions` y `AIWorld`.

## Generadores continuos v2 y v3

v2 recorta el agua con su dominio firmado en el triángulo autoritativo. v3 conserva esa representación y emplea un relieve de valle con frecuencias separadas. Priority-Flood se ejecuta sobre una copia filtrada con el borde anclado. Los bajos rechazados quedan en la superficie física. El drenaje potencial puede atravesar un bajo seco que no tiene un cauce materializado.

v3 ajusta las cotas del grafo fluvial mediante difusión longitudinal en longitudes físicas y una proyección en orden de drenaje. La rasante típica se limita hasta agotar el presupuesto de rebaje, por lo que un salto residual puede quedar en tramos montañosos. El cauce se une a las cotas lacustres al desembocar. El ancho de banco crece con la excavación y tiene un máximo en unidades de mundo. El segmento más próximo selecciona el dominio. Las aportaciones compatibles del mismo cuerpo se mezclan en una vecindad corta. Los niveles de cuerpos independientes conservan sus identidades.

El litoral se relaja en cuatro pasadas con doble búfer. Se atenúa la corrección hacia el exterior, se conserva el signo de profundidad y se preservan crestas de vertedero, desniveles fuertes y límites entre cuerpos. La geometría final alimenta biomas, colisión, colocación y navegación. Las estaciones de agua se vuelven a evaluar contra la geología. La validación distingue la monotonía del grafo del gradiente inferido en las muestras ya mezcladas.

- [Priority-Flood, Barnes et al.](https://github.com/r-barnes/Barnes2013-Depressions)
- [Mediciones v3 y volcado de etapas](WORLD_MORPHOLOGY_LIGHT_TRANSPORT_V3.md)

### Reparación de taludes excavados en las uniones v3

El banco fluvial queda limitado por la cota del terreno existente o de la lámina, la mayor de las dos. Así se evita que el perfil analítico levante una berma sobre una llanura ya situada cerca del agua. Tras tallar, se compara el desnivel de cada arista mojada que cruza un dominio o una orilla con el desnivel previo a la excavación. La corrección reparte el exceso por un corredor local de hasta 2,2 unidades, redondeado a la rejilla. El desnivel previo sirve de referencia para respetar los escarpes naturales. El ajuste conserva la cota de las láminas, la identidad de sus cuerpos y el signo de profundidad durante esta fase. Se ejecuta sólo al generar v3.

En River Valley v3, semilla 42, el desnivel entre (37, 20) y (37,5, 20) pasó de 0,75 a 0,18 unidades. La pareja de muestras sigue perteneciendo a dos cuerpos mojados distintos. Las capturas de este punto muestran una línea residual entre láminas con cotas diferentes. El recorte de las láminas sigue siendo el autoritativo. Se verificaron las cinco recetas v3 con las semillas 42, 108, 317 y 707. Los mundos v1 y v2 conservan sus algoritmos. Como cambia el terreno v3, los checkpoints que almacenaron la huella física anterior requieren esa revisión para continuar la misma simulación.

## Orillas v4

El excavador v1 aplicaba el corte máximo de secciones circulares superpuestas y excluía las muestras lacustres. En River Valley con semilla 42, esa interrupción convirtió 5 cm de desnivel natural en un escalón de 85 cm en la unión cerca de (15, 34,5). También dejó depresiones secas en la zona alcanzada por el tallado lateral.

Las recetas incluidas conservan el relieve base de las recetas originales y usan secciones fluviales continuas. El generador limita la excavación longitudinal y el relleno, conserva la divisoria junto a lagos más altos y reparte los saltos de altura añadidos a lo largo de todo el corredor. La relajación local toma como referencia el relieve anterior a la excavación y conserva el lado mojado o seco de cada muestra. El terreno final alimenta las consultas de colisión, navegación y contacto con la orilla.

Para abrir el valle corregido en Arena con semilla 42, ejecutar `./demos/demo_evolution_arena --world=worlds/river_valley.world.json --world-seed=42`. El entorno de Brain Evolution Lab es `environments/brumacerno_river_valley.environment.json`. Para reproducir un mundo de una versión anterior, usa la receta de su revisión original.

El diagnóstico opcional de generación guarda el tallado previo a esta reparación en `river_ground_f32` y el terreno posterior en `carved_ground_f32`. La comparación y las capturas de la revisión están en `build/shore-repair/`.
