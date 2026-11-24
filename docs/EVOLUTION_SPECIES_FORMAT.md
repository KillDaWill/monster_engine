# Formato de Especies para Evolución — Monster Engine

## 1. Propósito

Los archivos `species/*.species.json` describen de forma declarativa una especie jugable/entrenable para **Brain Evolution Lab** y futuras arenas genéricas, vinculando:
- Receta anatómica procedural (`recipe_id` en `recipes/`).
- Perfil metabólico externo (`metabolism_profile` en `metabolism/profiles/`).
- Sustancia y masa del cadáver generado al morir (`tissue_substance`, `tissue_mass`).
- Capacidades físicas y fisiológicas (`physical`, `physiology`, `reproduction`).

Esto elimina el acoplamiento duro a dos especies fijas y permite entrenar cerebros para cualquier receta anatómica y perfil metabólico válidos.

## 2. Ejemplo (`species/brumacerno.species.json`)

```json
{
  "version": 1,
  "id": "brumacerno",
  "name": "Brumacerno",
  "recipe_id": 3,
  "metabolism_profile": "brumacerno",
  "tissue_substance": "brumacerno_tissue",
  "tissue_mass": 14.0,
  "physical": {
    "radius": 0.95,
    "walk_speed": 2.4,
    "run_speed": 5.2,
    "turn_rate": 2.1,
    "vision_range": 18.0,
    "vision_fov_deg": 210.0,
    "attack_damage": 0.08,
    "attack_range": 0.75,
    "attack_cooldown": 1.4,
    "attack_energy_cost": 0.035,
    "water_interaction": true
  },
  "physiology": {
    "energy_drain": 0.0022,
    "hydration_drain": 0.0026,
    "move_energy_cost": 0.0014,
    "health_recovery": 0.006,
    "maturity_age": 120.0,
    "max_age": 3600.0
  },
  "reproduction": {
    "min_energy": 0.55,
    "min_hydration": 0.45,
    "min_health": 0.70,
    "energy_cost": 0.16,
    "gestation": 120.0,
    "cooldown": 180.0
  }
}
```

## 3. Especies incluidas

- `species/brumacerno.species.json` (`recipe_id: 3`, perfil `brumacerno`, tejido `brumacerno_tissue`).
- `species/velkar.species.json` (`recipe_id: 4`, perfil `velkar`, tejido `velkar_tissue`).
- `species/omnivore_test.species.json` (`recipe_id: 3`, perfil `omnivore_test`, tejido `brumacerno_tissue`).

## 4. Validación y huellas

`EvolutionSpeciesConfig_LoadFile()` valida versión, identificadores, rangos físicos/fisiológicos positivos y calcula una huella FNV-1a de 64 bits verificada por los checkpoints de Brain Evolution Lab. `EvolutionSpeciesConfig_BuildEcosystemSpecies()` resuelve los manejadores contra `MetabolismDatabase`.
