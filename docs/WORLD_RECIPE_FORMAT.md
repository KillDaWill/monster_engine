# Formato de Recetas de Mundo (`*.world.json`) — Monster Engine

## 1. Propósito y reglas de validación estricta

Los archivos `worlds/*.world.json` definen de forma declarativa y versionada la geometría, relieve, hidrología, geología y biomas de un territorio reproducible. El cargador [WorldIO.c](../src/WorldIO.c) aplica validación estricta:
- Rechaza claves desconocidas, versiones incompatibles (`version != 1`, `generator_version ∉ {1, 2, 3, 4}`), valores `NaN`/`Inf`, dimensiones fuera de rango y resoluciones que no sean impares (`resolution % 2 == 1`) o excedan el presupuesto de memoria (`max_memory_mb`).
- Calcula una huella canónica FNV-1a (`WorldRecipe_Fingerprint`) independiente del formato textual para cachés y validación de checkpoints.
- Deriva sub-semillas independientes y deterministas mediante SplitMix64 (`WorldSeed_Derive(seed, salt)`), garantizando que el relieve, la hidrología, las rocas, el hábitat y la población no interfieran estadísticamente entre sí.

## 2. Esquema JSON (`version: 1`)

```json
{
  "version": 1,
  "generator_version": 4,
  "id": "river_valley",
  "name": "River Valley",
  "seed": 42,
  "size": 256.0,
  "flat": false,
  "terrain": {
    "resolution": 513,
    "height_scale": 24.0,
    "base_frequency": 0.011,
    "octaves": 5,
    "persistence": 0.5,
    "lacunarity": 2.0,
    "ridge_weight": 0.22,
    "domain_warp_strength": 0.25,
    "valley_shaping": 0.38
  },
  "erosion": {
    "thermal_iterations": 26,
    "talus_angle_degrees": 32.0,
    "thermal_rate": 0.35
  },
  "hydrology": {
    "enabled": true,
    "outlet_policy": "open_boundary",
    "river_density": 0.45,
    "lake_density": 0.3,
    "river_width_scale": 1.0,
    "river_depth_scale": 1.0,
    "water_substance": "clean_water",
    "reservoir_policy": "infinite_external",
    "finite_volume": 500.0
  },
  "geology": {
    "rock_density": 0.35,
    "rock_variants": 16,
    "min_rock_scale": 0.7,
    "max_rock_scale": 3.0
  },
  "vegetation": {
    "density": 0.72,
    "fertility_bias": 0.05,
    "moisture_bias": 0.05
  }
}
```

### Campos principales

| Campo | Tipo | Restricciones / Semántica |
| --- | --- | --- |
| `version` | entero | Debe ser `1`. |
| `generator_version` | entero | Admite `1`, `2`, `3` o `4`. Se incluye en la huella final de `WorldStaticData`. |
| `flat` | booleano | Selecciona el mundo plano compatible. |
| `size` | flotante | Lado del dominio cuadrado `[-size/2, +size/2]` en unidades de mundo (`12.0` a `1024.0`). |
| `terrain.resolution` | entero | Número de vértices por eje; debe ser impar `>= 17` y `<= 1025` (p. ej. `17`, `129`, `385`, `513`, `1025`) para divisiones limpias en chunks `32×32` y 4 niveles LOD. |
| `terrain.height_scale` | flotante | Amplitud vertical máxima del relieve en unidades de mundo (`0.0` en modo plano). |
| `hydrology.water_substance` | cadena | Identificador molecular del registro `SubstanceRegistry` (p. ej. `"clean_water"`). |

## 3. Política de semillas y huellas

1. **Separación de semillas:** la semilla del terreno (`worldSeed`) puede fijarse o derivarse de la semilla del episodio (`EvolutionEnvironment`) independientemente de la semilla poblacional (`--seed`).
2. **Huellas de identidad:**
   - `recipeHash`: hash canónico de los parámetros de `WorldRecipe`.
   - `heightfieldHash`: hash FNV-1a de la rejilla de elevaciones resultante.
   - `dataFingerprint`: hash combinado de `recipeHash`, `generator_version`, `worldSeed`, `heightfieldHash`, hidrología y geología. Garantiza la invalidez automática de cachés GPU (`EnvironmentRenderer`) y la verificación estricta de checkpoints (`EvolutionCheckpointIO`).

## 4. Recetas incluidas (`worlds/`)


Las métricas de esta tabla corresponden a la generación v1 y se conservan como referencia histórica.

| Archivo | Resolución | Tamaño | Uso principal | Perfil medido (`seed=42`) |
| --- | ---: | ---: | --- | --- |
| `worlds/flat.world.json` | `17×17` | `64 u.m.` | Compatibilidad y regresión plana | `0,90 ms`, `0,09 MB`, `100%` transitable |
| `worlds/lab_valley_small.world.json` | `129×129` | `64 u.m.` | Episodios rápidos de Brain Evolution Lab | `18,44 ms`, `1,41 MB`, 5 lagos, 1 río, 59 orillas, 2 rocas, `63,4%` transitable |
| `worlds/river_valley.world.json` | `513×513` | `256 u.m.` | Territorio principal Evolution Arena (500+ criaturas) | `255,06 ms`, `17,43 MB`, 17 lagos, 1 río, 210 orillas, 65 rocas, `75,0%` transitable |
| `worlds/mountain_lakes.world.json` | `513×513` | `256 u.m.` | Relieve alpino con lagos encajonados y pasos estrechos | `268,52 ms`, `17,43 MB`, 16 lagos, 1 río, 187 orillas, 137 rocas, `29,2%` transitable |
| `worlds/rocky_highlands.world.json` | `513×513` | `256 u.m.` | Altiplano rocoso de alta oclusión y densidad geológica | `276,60 ms`, `17,43 MB`, 6 lagos, 1 río, 150 orillas, 212 rocas, `25,2%` transitable |
| `worlds/wetlands.world.json` | `385×385` | `192 u.m.` | Humedales extensos con vados y orillas amplias | `135,32 ms`, `9,96 MB`, 10 lagos, 1 río, 179 orillas, 8 rocas, `81,4%` transitable |

Las seis recetas incluidas usan el generador v4 y los identificadores sin sufijo. La semilla, el identificador, los parámetros y la versión del generador forman parte de la identidad física. Los checkpoints que referencian recetas antiguas necesitan sus definiciones originales.
