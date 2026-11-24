# Formato de Moléculas y Sustancias — Monster Engine

> **Advertencia:** Las composiciones y firmas sensoriales son parámetros ficticios de simulación, no medidas bioquímicas reales.

## 1. Registro de moléculas (`metabolism/molecules.json`)

Schema versionado (`version: 1`, capacidad máxima `MOLECULE_MAX_COUNT = 64`). Cada entrada define una molécula o familia química junto a su firma sensorial observable en `[0, 1]`, **independiente de cualquier especie**:

```json
{
  "version": 1,
  "molecules": [
    {
      "id": "cellulose",
      "name": "Cellulose",
      "description": "Structural carbohydrate component",
      "sensory_signature": {
        "moisture": 0.0,
        "plant_aroma": 0.85,
        "organic_aroma": 0.10,
        "lipid_aroma": 0.0,
        "sweetness": 0.05,
        "decay_aroma": 0.0
      }
    }
  ]
}
```

### Validaciones estrictas
- `version == 1`.
- Identificadores `id` únicos, no vacíos, longitud `< 48` caracteres.
- Todos los canales de `sensory_signature` (`moisture`, `plant_aroma`, `organic_aroma`, `lipid_aroma`, `sweetness`, `decay_aroma`) deben ser finitos y en `[0.0, 1.0]`.
- Tras la carga, `MoleculeRegistry_Freeze()` bloquea mutaciones y calcula una huella FNV-1a de 64 bits usada por checkpoints y trazabilidad.

Catálogo inicial (`9` moléculas): `water`, `cellulose`, `simple_carbohydrates`, `proteins`, `lipids`, `minerals`, `indigestible_fiber`, `organic_toxins`, `decay_products`.

## 2. Registro de sustancias (`metabolism/substances/*.substance.json`)

Schema versionado (`version: 1`, capacidad máxima `SUBSTANCE_MAX_COUNT = 128`, hasta `16` componentes compilados por sustancia). Opcionalmente `metabolism/substances/catalog.txt` fija el orden determinista de carga; en su ausencia `MetabolismIO` descubre los archivos ordenados lexicográficamente.

```json
{
  "version": 1,
  "id": "grass",
  "name": "Grass",
  "composition": {
    "water": 0.62,
    "cellulose": 0.24,
    "indigestible_fiber": 0.08,
    "simple_carbohydrates": 0.03,
    "proteins": 0.02,
    "minerals": 0.01
  },
  "physical_properties": {
    "phase": "solid",
    "hardness": 0.18
  },
  "presentation": {
    "model": "vegetation",
    "color": [0.32, 0.62, 0.22]
  }
}
```

### Campos y validaciones
- `composition`: mapa `molecule_id -> fracción`. Todas las referencias deben existir en `MoleculeRegistry`, las fracciones deben ser finitas `>= 0` y su suma debe cumplir `|Σf - 1.0| <= 1e-3`.
- `physical_properties.phase`: `"solid"`, `"liquid"` o `"semi_solid"`; `hardness` en `[0, 1]`.
- `presentation.model`: `"vegetation"`, `"water"`, `"carcass"`, `"fruit"` o `"generic"`; `color`: RGB en `[0, 1]`.
- La firma sensorial de cada sustancia se calcula como media ponderada por fracción másica de las firmas de sus moléculas (con soporte para recálculo dinámico cuando un recurso se descompone).

## 3. Extensibilidad sin recompilar

Añadir una nueva molécula a `metabolism/molecules.json` y una nueva sustancia `*.substance.json` que la referencia **no requiere modificar enumeraciones C ni recompilar el motor**, ni añade neuronas de salida en `NeuralControllerV2`. Verificado en el Experimento 4 (`chitin` + `chitin_pod`).
