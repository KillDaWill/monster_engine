/**
 * @file EvolutionJobSystem.h
 * @brief Pool persistente de trabajadores CPU determinista para fases paralelas de Evolution Arena.
 *
 * Reutiliza la infraestructura POSIX `SDFSamplingPool` del motor evitando sobre-suscripción
 * respecto al hilo gráfico y al trabajador de mallado.
 */
#ifndef EVOLUTION_JOB_SYSTEM_H
#define EVOLUTION_JOB_SYSTEM_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EvolutionJobSystem EvolutionJobSystem;
typedef void (*EvolutionJobRangeFn)(void *context, unsigned start, unsigned end, unsigned workerIndex);

/** @brief Crea un pool persistente. `workerCount == 0` selecciona automáticamente un número seguro. */
EvolutionJobSystem *EvolutionJobSystem_Create(unsigned workerCount);

/** @brief Libera el pool y detiene ordenadamente sus hilos trabajadores. */
void EvolutionJobSystem_Destroy(EvolutionJobSystem *sys);

/** @brief Devuelve el número total de hilos participantes (incluido el hilo llamador). */
unsigned EvolutionJobSystem_WorkerCount(const EvolutionJobSystem *sys);

/** @brief Selección automática recomendada según CPUs disponibles, reservando margen para render/mallas. */
unsigned EvolutionJobSystem_RecommendedWorkers(void);

/**
 * @brief Ejecuta `fn(context, start, end, workerIndex)` sobre particiones disjuntas de `[0, itemCount)`.
 *
 * Si `sys == NULL`, `workerCount <= 1`, o `itemCount < minParallelItems`, ejecuta en el hilo llamador.
 * Retorna sólo tras completarse todas las particiones (barrera determinista).
 */
void EvolutionJobSystem_ParallelFor(EvolutionJobSystem *sys, unsigned itemCount,
                                    unsigned minParallelItems,
                                    EvolutionJobRangeFn fn, void *context);

#ifdef __cplusplus
}
#endif

#endif /* EVOLUTION_JOB_SYSTEM_H */
