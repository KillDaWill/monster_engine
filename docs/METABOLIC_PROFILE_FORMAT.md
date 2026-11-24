# Formato de Perfiles Metabólicos — Monster Engine

> **Advertencia:** Los valores de absorción, costes digestivos, detoxificación y toxicidad son parámetros ficticios de simulación calibrados para generar nichos ecológicos diferenciados.

## 1. Estructura (`metabolism/profiles/*.metabolism.json`)

Cada especie referencia un perfil metabólico por identificador estable (`metabolism_profile`). Las reglas se indexan en tiempo de carga en un arreglo directo `O(1)` por `MoleculeHandle` sin comparaciones de cadenas en runtime.

```json
{
  "version": 1,
  "id": "brumacerno",
  "name": "Brumacerno Fibrous Fermenter",
  "digestive_capacity": 4.0,
  "max_ingestion_rate": 0.65,
  "waste_elimination_rate": 0.35,
  "detoxification_rate": 0.035,
  "toxicity_threshold": 0.30,
  "toxicity_damage_rate": 0.055,
  "nutrient_recovery_threshold": 0.35,
  "nutrient_recovery_bonus": 0.010,
  "reproduction_min_nutrients": 0.35,
  "default_unprocessable": {
    "passage_rate": 0.14,
    "energy_cost": 0.04,
    "discomfort": 0.16,
    "toxicity": 0.0
  },
  "molecule_rules": [
    {
      "molecule": "cellulose",
      "absorption_rate": 0.16,
      "energy_yield": 0.62,
      "hydration_yield": 0.0,
      "nutrient_yield": 0.18,
      "energy_cost": 0.08,
      "toxicity": 0.0,
      "discomfort": 0.0,
      "waste_fraction": 0.18
    }
  ]
}
```

## 2. Reglas de transformación molecular

Una regla en `molecule_rules` puede incluir un bloque opcional `outputs` para transformar una molécula en otra(s) dentro del tracto digestivo conservando la masa (`Σ(fraction) <= 1.0`, el remanente se absorbe o pasa a residuo).

## 3. Comportamiento ante moléculas no declaradas

Si un cuerpo ingiere una molécula registrada que no figura explícitamente en `molecule_rules` (por ejemplo, una molécula externa recién añadida), se aplica `default_unprocessable`: la masa atraviesa el tracto a `passage_rate`, consume `energy_cost`, aporta `discomfort` y se elimina como residuo sin fallos ni excepciones.

## 4. Perfiles incluidos

- `brumacerno` (`metabolism/profiles/brumacerno.metabolism.json`): fermentador fibroso con aprovechamiento alto de `cellulose` y `simple_carbohydrates`, digestión eficiente de `water` y balance energético negativo frente a `fresh_meat` y alto daño ante `decay_products`/`organic_toxins`.
- `velkar` (`metabolism/profiles/velkar.metabolism.json`): metabolizador proteico-lipídico con alto rendimiento sobre `proteins` y `lipids`, coste digestivo neto negativo en `cellulose`/`indigestible_fiber` (de modo que el pasto no sostiene energéticamente su supervivencia) y mayor tolerancia moderada a `decay_products`.
- `omnivore_test` (`metabolism/profiles/omnivore_test.metabolism.json`): perfil intermedio generalista para pruebas comparativas.
