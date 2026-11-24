# Brain Evolution Lab — Monster Engine

## 1. Propósito y separación de Evolution Arena

**Brain Evolution Lab** es un laboratorio de evolución neuro-conductual rápida diseñado para optimizar redes neuronales `recurrent_candidate_policy` (v2) sobre cualquier especie (`species/*.species.json`) y entorno (`environments/*.environment.json`) usando el mismo núcleo autoritativo `EcosystemWorld`.

- **Separación estricta:** la función de aptitud fisiológica (`EvolutionFitness`) y el algoritmo genético generacional (`BrainEvolutionLab`) existen **exclusivamente** en este laboratorio y nunca intervienen en la selección natural biológica de `EvolutionArena`.
- **Sin recompensas por comida ni por matar:** la aptitud evalúa únicamente supervivencia temporal, salud, energía, hidratación y penalizaciones por toxicidad/daño metabólico. Cero contribución positiva tras la muerte.
- **Validación sin sobreajuste:** semillas de entrenamiento, validación y test ciego son estrictamente disjuntas. El estado terminal informa honestamente `CONVERGED`, `TRAINING_BUDGET_EXHAUSTED`, `VALIDATION_THRESHOLD_NOT_REACHED`, `STAGNATED` o `MANUALLY_STOPPED`.

## 2. Ejecución Headless (`demos/demo_brain_evolution_lab_headless`)

### Ejemplo 1 — Evolución de Brumacerno desde fundadores aleatorios
```sh
./demos/demo_brain_evolution_lab_headless \
  --species=species/brumacerno.species.json \
  --environment=environments/brumacerno_mixed.environment.json \
  --seed=42 --population=64 --generations=20 \
  --training-seeds=3 --validation-seeds=8 --test-seeds=20 \
  --mutation-only --workers=auto \
  --output=build/brain-evolution/brumacerno_mixed --baselines --report
```

### Ejemplo 2 — Currículo progresivo completo de Velkar (carroñeo -> caza desde fundadores aleatorios)
```sh
./demos/demo_brain_evolution_lab_headless \
  --species=species/velkar.species.json \
  --curriculum=environments/velkar_scavenging.environment.json,environments/velkar_hunting.environment.json \
  --seed=42 --population=96 --generations=30 \
  --training-seeds=3 --validation-seeds=8 --test-seeds=20 \
  --workers=auto --output=build/brain-evolution/velkar_curriculum --report
```

### Ejemplo 3 — Reanudar desde checkpoint verificado por huellas
```sh
./demos/demo_brain_evolution_lab_headless \
  --species=species/brumacerno.species.json \
  --environment=environments/brumacerno_mixed.environment.json \
  --resume=build/brain-evolution/brumacerno_mixed/checkpoint.json \
  --generations=40 --output=build/brain-evolution/brumacerno_mixed
```

## 3. Aplicación gráfica (`demos/demo_brain_evolution_lab`)

```sh
./demos/demo_brain_evolution_lab \
  --species=species/velkar.species.json \
  --curriculum=environments/velkar_scavenging.environment.json,environments/velkar_hunting.environment.json \
  --population=64 --generations=30 --workers=0
```

La interfaz ejecuta el algoritmo genético en un hilo dedicado en segundo plano manteniendo 60 FPS fluidos e incluye:
- Panel de **Configuración del experimento**.
- Panel de **Estadísticas de entrenamiento en vivo** (generación, episodios, fitness, validación, diversidad, `eps/s`, `ticks/s`, bajas y cadáveres consumidos).
- **Gráficas históricas** de fitness (mejor, media, mediana, validación), tasa de supervivencia, diversidad neuronal y marcadores de cambio de etapa de currículo.
- **Inspector de población de candidatos** (`[1..5]`: Champion, Best Train, Best Val, Elite #2, Mediana).
- **Viewport 3D independiente de replay** (`[R]`) con anatomía procedural real, sin interferir nunca con la evaluación paralela.
- Controles interactivos y por teclado: `SPACE` (iniciar/pausar), `G` (avanzar 1 gen), `X` (detener), `V` (validar), `SAVE CKPT`, `LOAD CKPT`, `E` (exportar campeón), `R` (replay 3D).

## 4. Artefactos exportados por sesión

Cada directorio `--output=DIR` genera:
- `champion.brain.json`, `best_training.brain.json`, `best_validation.brain.json`
- `checkpoint.json` (guardado atómico `.tmp` + `rename` con verificación estricta de 6 huellas: moléculas, sustancias, perfil metabólico, especie, entorno y arquitectura)
- `training_history.csv`, `validation_results.json`, `experiment.json`, `report.json`

## 5. Escalado paralelo medido

En 144 episodios (`pop=48`, 3 semillas, `brumacerno_mixed`), con determinismo bit a bit verificado entre todos los conteos de hilos (`det=EXACT`):

| Trabajadores | Tiempo | Aceleración | Episodios/s | Ticks/s | Genomas/min |
| --- | --- | --- | --- | --- | --- |
| `1` | `2.665 s` | `1.00x` | `54.0` | `638,828` | `1,080.6` |
| `2` | `1.410 s` | `1.89x` | `102.2` | `1,207,868` | `2,043.2` |
| `4` | `0.743 s` | `3.59x` | `193.9` | `2,292,797` | `3,878.5` |
| `6 (auto)` | `0.601 s` | `4.44x` | `239.7` | `2,833,462` | `4,793.1` |
| `8` | `0.540 s` | `4.94x` | `266.7` | `3,153,741` | `5,334.9` |
