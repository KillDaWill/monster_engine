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
