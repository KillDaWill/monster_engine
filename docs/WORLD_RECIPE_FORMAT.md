# Formato de Recetas de Mundo (`*.world.json`) — Monster Engine

## 1. Propósito y reglas de validación estricta

Los archivos `worlds/*.world.json` definen de forma declarativa y versionada la geometría, relieve, hidrología, geología y biomas de un territorio reproducible. El cargador [WorldIO.c](../src/WorldIO.c) aplica validación estricta:
- Rechaza claves desconocidas, versiones incompatibles (`version != 1`, `generator_version != 1`), valores `NaN`/`Inf`, dimensiones fuera de rango y resoluciones que no sean impares (`resolution % 2 == 1`) o excedan el presupuesto de memoria (`max_memory_mb`).
- Calcula una huella canónica FNV-1a (`WorldRecipe_ComputeHash`) independiente del formato textual para cachés y validación de checkpoints.
- Deriva sub-semillas independientes y deterministas mediante SplitMix64 (`WorldSeed_Derive(seed, salt)`), garantizando que el relieve, la hidrología, las rocas, el hábitat y la población no interfieran estadísticamente entre sí.

## 2. Esquema JSON (`version: 1`)

```json
{
  "version": 1,
  "generator_version": 1,
  "id": "river_valley",
  "name": "River Valley Territory",
  "mode": "procedural",
  "size": 256.0,
  "resolution": 513,
  "height_scale": 14.0,
  "base_seed": 42,
  "max_memory_mb": 96,
  "terrain": {
    "relief_amplitude": 1.0,
    "macro_frequency": 0.0085,
    "ridge_weight": 0.22,
    "domain_warp": 14.0,
    "valley_strength": 0.52,
    "basin_depth": 0.26,
    "border_falloff": 0.18,
    "erosion_iterations": 18,
    "talus_angle_deg": 38.0
  },
  "hydrology": {
    "enabled": true,
    "water_substance": "clean_water",
    "lake_min_area_cells": 24,
    "lake_min_depth": 0.35,
    "lake_max_count": 20,
    "river_flow_threshold": 900,
    "river_width_scale": 1.25,
    "river_carve_depth": 1.15,
    "shore_max_slope_deg": 32.0,
    "shore_min_spacing": 3.5
  },
  "geology": {
    "enabled": true,
    "rock_density": 0.24,
    "cluster_probability": 0.35,
    "min_scale": 0.9,
    "max_scale": 2.8,
    "shore_clearance": 2.4,
    "min_spacing": 4.0,
    "max_instances": 320
  },
  "biome": {
    "moisture_decay_distance": 32.0,
    "base_moisture": 0.32,
    "fertility_elevation_penalty": 0.35,
    "fertility_slope_penalty": 0.65,
    "rock_slope_deg": 34.0
  }
}
```

### Campos principales

| Campo | Tipo | Restricciones / Semántica |
| --- | --- | --- |
| `version` | entero | Debe ser `1`. |
| `generator_version` | entero | Debe ser `1`. Se incluye en la huella final de `WorldStaticData`. |
| `mode` | cadena | `"flat"` (terreno plano compatible) o `"procedural"`. |
| `size` | flotante | Lado del dominio cuadrado `[-size/2, +size/2]` en unidades de mundo (`16.0` a `1024.0`). |
| `resolution` | entero | Número de vértices por eje; debe ser impar `>= 17` y `<= 1025` (p. ej. `17`, `129`, `385`, `513`, `1025`) para divisiones limpias en chunks `32×32` y 4 niveles LOD. |
| `height_scale` | flotante | Amplitud vertical máxima del relieve en unidades de mundo (`0.0` en modo plano). |
| `water_substance` | cadena | Identificador molecular del registro `SubstanceRegistry` (p. ej. `"clean_water"`). |
| `max_memory_mb` | entero | Límite duro de memoria estática permitida durante la generación (`WorldGenerator_Generate` falla explícitamente si la estimación lo supera). |

## 3. Política de semillas y huellas

1. **Separación de semillas:** la semilla del terreno (`worldSeed`) puede fijarse o derivarse de la semilla del episodio (`EvolutionEnvironment`) independientemente de la semilla poblacional (`--seed`).
2. **Huellas de identidad:**
   - `recipeHash`: hash canónico de los parámetros de `WorldRecipe`.
   - `heightfieldHash`: hash FNV-1a de la rejilla de elevaciones resultante.
   - `dataFingerprint`: hash combinado de `recipeHash`, `generator_version`, `worldSeed`, `heightfieldHash`, hidrología y geología. Garantiza la invalidez automática de cachés GPU (`EnvironmentRenderer`) y la verificación estricta de checkpoints (`EvolutionCheckpointIO`).

## 4. Recetas incluidas (`worlds/`)

| Archivo | Resolución | Tamaño | Uso principal | Perfil medido (`seed=42`) |
| --- | ---: | ---: | --- | --- |
| `worlds/flat.world.json` | `17×17` | `64 u.m.` | Compatibilidad y regresión plana | `0,90 ms`, `0,09 MB`, `100%` transitable |
| `worlds/lab_valley_small.world.json` | `129×129` | `64 u.m.` | Episodios rápidos de Brain Evolution Lab | `18,44 ms`, `1,41 MB`, 5 lagos, 1 río, 59 orillas, 2 rocas, `63,4%` transitable |
| `worlds/river_valley.world.json` | `513×513` | `256 u.m.` | Territorio principal Evolution Arena (500+ criaturas) | `255,06 ms`, `17,43 MB`, 17 lagos, 1 río, 210 orillas, 65 rocas, `75,0%` transitable |
| `worlds/mountain_lakes.world.json` | `513×513` | `256 u.m.` | Relieve alpino con lagos encajonados y pasos estrechos | `268,52 ms`, `17,43 MB`, 16 lagos, 1 río, 187 orillas, 137 rocas, `29,2%` transitable |
| `worlds/rocky_highlands.world.json` | `513×513` | `256 u.m.` | Altiplano rocoso de alta oclusión y densidad geológica | `276,60 ms`, `17,43 MB`, 6 lagos, 1 río, 150 orillas, 212 rocas, `25,2%` transitable |
| `worlds/wetlands.world.json` | `385×385` | `192 u.m.` | Humedales extensos con vados y orillas amplias | `135,32 ms`, `9,96 MB`, 10 lagos, 1 río, 179 orillas, 8 rocas, `81,4%` transitable |
