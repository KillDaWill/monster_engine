# Locomoción, Navegación y Animación en Terreno — Monster Engine

## 1. Perfiles de travesía declarativos (`TraversalProfile`)

Las capacidades de desplazamiento en terreno no están codificadas a mano por especie en el motor, sino declaradas en `species/*.species.json` y almacenadas en `CreatureMotor` / `WorldCollision`:

```json
"traversal": {
  "max_walkable_slope_deg": 36.0,
  "comfortable_slope_deg": 18.0,
  "max_step_height": 0.45,
  "max_wade_depth": 0.55,
  "preferred_wade_depth": 0.25,
  "uphill_speed_penalty": 0.55,
  "downhill_speed_penalty": 0.20,
  "wade_speed_penalty": 0.50,
  "uphill_energy_multiplier": 1.60,
  "wade_energy_multiplier": 1.35,
  "rock_clearance_margin": 0.12
}
```

- **Brumacerno:** perfil pesado de herbívoro (`max_walkable_slope_deg: 34°`, `max_wade_depth: 0.65`, mayor penalización cuesta arriba).
- **Velkar:** perfil ágil de depredador (`max_walkable_slope_deg: 42°`, `max_wade_depth: 0.45`, menor penalización en pendiente moderada).

## 2. Física de locomoción y contacto en `EcosystemWorld`

Cada paso fijo (`1/60 s`):
1. **Propuesta y validación de segmento (`World_CheckTraversalSegment`):** el desplazamiento horizontal propuesto se muestrea a lo largo del segmento comprobando límites del dominio, pendiente máxima, salto de altura (`max_step_height`), profundidad máxima de vadeo (`max_wade_depth`) y proxies de colisión de rocas (`RockCollisionLobe[3]` vía rejilla CSR).
2. **Modulación de velocidad y gasto energético:** las pendientes ascendentes y el vadeo reducen la velocidad efectiva (`uphill_speed_penalty`, `wade_speed_penalty`) e incrementan el costo metabólico (`uphill_energy_multiplier`, `wade_energy_multiplier`) sin alterar el esquema de `NeuralControllerV2`.
3. **Resolución de deslizamiento y bloqueo:** si un paso choca contra una roca o acantilado, se proyecta tangencialmente evitando que las criaturas atraviesen rocas o caigan en agua profunda no vadeable.
4. **Posición vertical y marco corporal 3D (`World_BuildBodyFrame`):**
   - La coordenada `position.y` se asienta exactamente sobre el triángulo baricéntrico del `TerrainHeightfield`.
   - En mundos procedurales, `body.orientation` (cuaternión) alinea suavemente el eje vertical del cuerpo con la normal del terreno suavizada (*pitch/roll* acotados para evitar vuelcos irreales) manteniendo el rumbo `body.yaw`.
   - En mundos planos se preserva la ruta rápida exacta de rumbo horizontal (`YawFlat`) para mantener reproducibilidad bit a bit en escenarios legados.

## 3. Navegación A* consciente del relieve (`CreatureNavigation`)

- `WorldStaticData` precalcula una rejilla navegable base (`WorldNavBaseGrid`, hasta `512×512`) con elevación, pendiente, profundidad de agua y máscara de bloqueo por rocas.
- `CreatureNavigation_Plan` extiende el planificador A* optimizado con *binary heap* y búferes *thread-local*:
  - **Conectividad de 8 vecinos** en mundos procedurales (diagonales con corte de esquinas prohibido para evitar rozar rocas o acantilados).
  - **Coste de arista físico:** penaliza pendientes ascendentes/descendentes y vadeo somero (`wadeCost`), y bloquea celdas con pendiente superior a `max_walkable_slope_deg`, profundidad mayor a `max_wade_depth` o intersección con roca.
  - **Elevación en waypoints:** cada punto del camino incluye la altura real del terreno, permitiendo visualizar rutas A* conformes al relieve (`D`).
  - **Compatibilidad plana:** en arenas planas mantiene la heurística Manhattan y el trazado original para 100 % de paridad con las pruebas de regresión.

## 4. Percepción y línea de visión 3D (`AIWorld` / `World_ClearSightSegment`)

- `World_ClearSightSegment` evalúa la visibilidad desde la posición real de los ojos del observador (`eyePos.y = groundY + eyeHeight`) hasta el objetivo usando `Terrain_RaycastSegment` (2D DDA acelerado por bloques min/max `8×8` + intersección exacta de triángulos) y pruebas de intersección con los lóbulos de roca (`WorldGeology`).
- Las colinas, crestas y formaciones rocosas ocluyen realmente presas, depredadores y recursos: los animales deben rodear obstáculos o ascender para detectarlos.

## 5. Animación procedural y contacto de patas (`TerrainAnimationAdapter` / `EcosystemVisual`)

- En el motor de animación, `EcosystemVisual` evalúa la locomoción e IK de patas en el marco local del organismo para reutilizar mallas cacheadas y skinning sin remallado SDF por frame.
- `TerrainAnimationAdapter` implementa el contrato `World` (`World_SampleGround`) traduciendo las consultas locales de cada pie a coordenadas de mundo mediante el marco 3D (`World_LocalToWorld`), muestreando la altura/normal exacta del `TerrainHeightfield` y proyectando el impacto de vuelta al espacio local (`World_WorldToLocal`).
- Esto mantiene las patas plantadas en pendientes y orillas en nivel `NEAR` y evita que el cuerpo flote o se hunda en cambios de inclinación.
- El alcance cervical (`EcosystemAnatomy`) transforma las posiciones de la boca y el objetivo a través del marco 3D completo, garantizando que el contacto de alimentación y bebida en orillas inclinadas siga siendo anatómicamente válido.

## Acceso al agua en v3

La extracción v3 puede probar tres distancias de aproximación desde el contorno firmado. Comprueba la pendiente y el desnivel a lo largo de la última aproximación, la profundidad de vadeo y la separación de rocas. La separación de estaciones permite que dos bancos enfrentados tengan candidatos distintos. El sitio es una aproximación geométrica que el motor y el alcance anatómico vuelven a evaluar durante la interacción con cada criatura.

La fracción transitable del grid base es un indicador estático. Los contadores agregados de fallo de navegación y acción incluyen situaciones de control, recursos y congestión. Las simulaciones de comparación no permiten asignar todo el diferencial de fallos al terreno.
