/**
 * @file SDFSamplingPool.h
 * @brief Pool persistente y reutilizable de hilos POSIX para paralelizar el muestreo escalar de campos SDF.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_SDF_SAMPLING_POOL_H
#define MONSTER_SDF_SAMPLING_POOL_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SDF_SAMPLING_POOL_MAX_THREADS 16

/**
 * @brief Firma de la función de trabajo para cada partición de índice [startIndex, endIndex).
 */
typedef void (*SDFSamplingWorkFn)(void* context, int startIndex, int endIndex, int threadIndex);

typedef struct SDFSamplingPool SDFSamplingPool;

/**
 * @brief Crea un pool persistente de hilos para muestreo paralelo.
 * @param threadCount Cantidad total de hilos participantes (incluyendo el hilo llamador).
 *                    0 = auto (elige valor óptimo según cores disponibles).
 *                    1 = mono-hilo (forzado serial, sin hilos secundarios).
 * @return Puntero asignado en heap a SDFSamplingPool o NULL si falla.
 */
SDFSamplingPool* SDFSamplingPool_Create(int threadCount);

/**
 * @brief Retorna la cantidad total de hilos participantes configurados en el pool.
 */
int SDFSamplingPool_GetThreadCount(const SDFSamplingPool* pool);

/**
 * @brief Ejecuta una tarea en paralelo dividiendo [0, totalItems) entre los hilos del pool.
 * El hilo llamador participa ejecutando su propia porción como threadIndex 0.
 * La llamada es síncrona y retorna cuando todos los hilos han completado su rango.
 */
void SDFSamplingPool_ParallelFor(SDFSamplingPool* pool, int totalItems, SDFSamplingWorkFn workFn, void* context);

/**
 * @brief Libera los hilos y memoria del pool. Debe ser invocado únicamente por el propietario del pool.
 */
void SDFSamplingPool_Free(SDFSamplingPool* pool);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_SDF_SAMPLING_POOL_H
