# Formato de Entornos de Evolución — Monster Engine

## 1. Propósito

Los archivos `environments/*.environment.json` describen escenarios deterministas reproducibles para episodios aislados de **Brain Evolution Lab** y validaciones controladas.

## 2. Esquema (`version: 1`)

```json
{
  "version": 1,
  "id": "velkar_hunting",
  "name": "Velkar Hunting-Dependent Survival",
  "description": "Scenario B: water, vegetation, and living prey with zero initial carcasses.",
  "arena_size": 32.0,
  "episode_seconds": 300.0,
  "focal_initial": {
    "energy_min": 0.42,
    "energy_max": 0.48,
    "hydration_min": 0.50,
    "hydration_max": 0.58,
    "nutrients": 0.50,
    "age": 240.0,
    "spawn_radius": 2.0
  },
  "obstacles": {
    "count": 2,
    "min_radius": 0.9,
    "max_radius": 1.15,
    "height": 2.2
  },
  "resources": [
    {
      "substance": "grass",
      "count": 5,
      "initial_mass": 4.5,
      "max_mass": 4.5,
      "radius": 0.80,
      "regen_policy": "linear",
      "regen_rate": 0.02,
      "decay_policy": "none",
      "min_dist": 3.5,
      "max_dist": 10.5
    }
  ],
  "background_creatures": [
    {
      "species": "species/brumacerno.species.json",
      "count": 3,
      "policy": "wander",
      "brain": null,
      "initial_energy": 0.90,
      "initial_hydration": 0.90,
      "initial_health": 0.35,
      "speed_scale": 0.72,
      "min_dist": 4.5,
      "max_dist": 9.5
    }
  ]
}
```

## 3. Entornos incluidos (`environments/`)

| Archivo | Propósito |
| --- | --- |
| `brumacerno_basic.environment.json` | Etapa básica de forrajeo e hidratación con `grass` y `clean_water`. |
| `brumacerno_mixed.environment.json` | Entorno mixto con sustancias beneficiosas (`grass`, `fruit`, `clean_water`), inútiles (`woody_bark`) y nocivas (`toxic_plant`, `contaminated_water`, `rotten_meat`). |
| `velkar_scavenging.environment.json` | Escenario A: carroñeo e hidratación con `fresh_meat`, `rotten_meat`, `grass`, `clean_water` y `contaminated_water`. |
| `velkar_hunting.environment.json` | Escenario B: **cero carne ni cadáveres iniciales**; agua, pasto y presas vivas (`Brumacerno`), obligando a descubrir la cadena causal ataque -> muerte -> cadáver -> consumo independiente. |
| `molecular_generalization.environment.json` | Entorno no visto durante entrenamiento con combinaciones variadas de sustancias, obstáculos y criaturas de fondo para evaluar generalización. |
