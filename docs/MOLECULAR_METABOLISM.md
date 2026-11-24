# Metabolismo Molecular — Monster Engine

> **Nota sobre fidelidad científica:** Las composiciones moleculares, tasas de digestión, eficiencias de absorción, coeficientes de toxicidad y costes metabólicos definidos en `metabolism/` son **parámetros ficticios de simulación** diseñados para producir dinámicas evolutivas claras y balanceadas, no un simulador bioquímico científicamente completo.

## 1. Arquitectura y flujo de datos

El sistema de metabolismo molecular desacopla la identidad química de las sustancias ambientales de las capacidades digestivas de cada cuerpo y de las decisiones neuronales:

```
MoleculeRegistry (metabolism/molecules.json)
        │ handles compactos (uint16_t) + firma sensorial observable (6 canales)
        ▼
SubstanceRegistry (metabolism/substances/*.substance.json)
        │ mezclas compiladas + fase física + dureza + presentación visual
        ▼
EcosystemResource (composición dinámica, regeneración/descomposición, metadatos)
        │ ingestión física limitada sólo por alcance y capacidad digestiva
        ▼
MetabolicState (tracto digestivo acotado por individuo, 0 asignaciones en runtime)
        │ procesado por paso fijo según MetabolicProfile del cuerpo
        ▼
CreatureMetabolism_Step() -> CreaturePhysiology (energía, hidratación, nutrientes,
                             malestar digestivo, toxicidad, daño y DEATH_TOXICITY)
        │
        ▼
Retroalimentación desagregada hacia NeuralControllerV2 (sin conocimiento dietético)
```

El núcleo metabólico (`MoleculeRegistry`, `SubstanceRegistry`, `MetabolicProfile`, `MetabolicState`, `CreatureMetabolism`, `MetabolismIO`) pertenece a `CORE_SRCS` y es completamente independiente de SDL, OpenGL, mallado y algoritmos de entrenamiento.

## 2. Separación estricta de responsabilidades

1. **Composición química vs. propiedades físicas vs. ciclo de vida vs. presentación:**
   - Una sustancia con alta fracción de `water` (ej. `fruit`) sigue siendo sólida y no se convierte en estanque.
   - `contaminated_water` conserva la fase líquida y geometría de `clean_water`, pero añade `organic_toxins`.
   - Los cadáveres (`brumacerno_tissue`, `velkar_tissue`) aplican política `RESOURCE_DECAY_DECOMPOSE` que conserva la masa convirtiendo progresivamente proteínas/lípidos en `decay_products` y actualizando perezosamente la firma sensorial observable (`decay_aroma`).
2. **Ingestión física independiente de compatibilidad metabólica:**
   - `EcosystemWorld_Consume()` en modo molecular no consulta `dietMask`. Cualquier criatura puede intentar ingerir sustancias inútiles o tóxicas; sólo la mecánica física (alcance, accesibilidad, masa disponible y capacidad digestiva) restringe la ingestión.
   - La ingestión **no** otorga energía ni hidratación inmediata: los efectos aparecen de forma retardada durante `CreatureMetabolism_Step()`.
3. **Ataque y consumo independientes:**
   - `EcosystemWorld_Attack()` en modo molecular no filtra por `dietMask` ni `preyMask` y otorga **0.0** de energía o hidratación directa al golpear o matar.
   - Al morir una criatura, `EcosystemWorld` genera un cadáver molecular usando `species.tissueSubstance` y `species.tissueMass`. El consumo del cadáver requiere una acción independiente `CONSUME_TARGET`.
4. **Muerte por toxicidad (`DEATH_TOXICITY`):**
   - La acumulación de toxinas por encima de `toxicity_threshold` produce daño metabólico continuo y puede causar muerte registrada explícitamente como `DEATH_TOXICITY`.

## 3. Verificación experimental

Resultados medidos (`./benchmarks/verify_molecular_experiments`, dosis `1.0`, `20 s`):

| Sustancia | Perfil metabólico | Energía neta | Hidratación neta | Nutrientes | Daño | Toxicidad máx. |
| --- | --- | --- | --- | --- | --- | --- |
| `grass` | `brumacerno` | `+0.1891` | `+0.3239` | `0.0701` | `0.0000` | `0.000` |
| `grass` | `velkar` | `-0.0265` | `+0.3239` | `0.0184` | `0.0000` | `0.000` |
| `grass` | `omnivore_test` | `+0.0775` | `+0.3239` | `0.0350` | `0.0000` | `0.000` |
| `fresh_meat` | `brumacerno` | `-0.0204` | `+0.2926` | `0.0339` | `0.0000` | `0.000` |
| `fresh_meat` | `velkar` | `+0.3454` | `+0.2926` | `0.1832` | `0.0000` | `0.000` |
| `fresh_meat` | `omnivore_test` | `+0.2107` | `+0.2926` | `0.1268` | `0.0000` | `0.000` |
| `contaminated_water` (dosis `2.0`) | `brumacerno` | `-0.0472` | `+0.9196` | `0.0000` | `0.2241` | `0.533` |
| `toxic_plant` (dosis `2.0`) | `brumacerno` | `+0.1816` | `+0.6061` | `0.0680` | `0.3681` | `0.640` |

Ingestión sostenida de `contaminated_water` produjo hidratación simultánea (`+4.584`) junto con daño metabólico (`1.000`) y muerte confirmada por `DEATH_TOXICITY` en el tick 329.

## 4. Documentación relacionada

- [Formato de Moléculas y Sustancias](MOLECULE_AND_SUBSTANCE_FORMAT.md)
- [Formato de Perfiles Metabólicos](METABOLIC_PROFILE_FORMAT.md)
- [Controlador Neuronal V2](NEURAL_CONTROLLER_V2.md)
- [Brain Evolution Lab](BRAIN_EVOLUTION_LAB.md)
