/**
 * @file MonsterVisualAsync.h
 * @brief Administrador de renderizado visual asíncrono para criaturas SDF.
 * Desacopla la reconstrucción del volumen de polígonos (Marching Cubes) del hilo de renderizado principal (60 FPS).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_VISUAL_ASYNC_H
#define MONSTER_VISUAL_ASYNC_H

#include "Monster.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "Mesh.h"
#include "RenderInterfaces.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum MonsterVisualQualityTier
 * @brief Niveles adaptativos de resolución para la generación de malla.
 */
typedef enum MonsterVisualQualityTier {
    MONSTER_VISUAL_QUALITY_INTERACTIVE = 0, /**< Malla rápida de menor resolución durante movimiento constante */
    MONSTER_VISUAL_QUALITY_SETTLED = 1      /**< Malla detallada de alta resolución cuando la postura se estabiliza */
} MonsterVisualQualityTier;

/**
 * @struct MonsterVisualAsyncConfig
 * @brief Configuración para el orquestador asíncrono de mallas SDF.
 */
typedef struct MonsterVisualAsyncConfig {
    SDFMesherConfig interactiveMesherConfig; /**< Configuración mesher para tier INTERACTIVE */
    SDFMesherConfig settledMesherConfig;     /**< Configuración mesher para tier SETTLED */
    MonsterSDFConfig sdfConfig;              /**< Configuración del campo SDF del monstruo */
    float settledDelaySec;                   /**< Tiempo en segundos sin cambios para escalar a SETTLED */
} MonsterVisualAsyncConfig;

/**
 * @struct MonsterVisualAsyncStats
 * @brief Diagnósticos de rendimiento y métricas de ejecución en segundo plano.
 */
typedef struct MonsterVisualAsyncStats {
    uint64_t requestCount;                  /**< Total de solicitudes enviadas al hilo worker */
    uint64_t completedBuildCount;           /**< Reconstrucciones completadas exitosamente */
    uint64_t coalescedCount;                /**< Solicitudes intermedias descartadas por coalescencia */
    float lastBuildDurationMs;              /**< Duración de la última reconstrucción en milisegundos */
    bool isWorkerBusy;                     /**< Indica si el worker está construyendo una malla activamente */
    MonsterVisualQualityTier activeQualityTier; /**< Tier de calidad usado en la malla mostrada */
} MonsterVisualAsyncStats;

typedef struct MonsterVisualEyeAsync {
    Mesh sclera;
    Mesh pupil;
} MonsterVisualEyeAsync;

/**
 * @struct MonsterVisualAsync
 * @brief Gestor asíncrono desacoplado con arquitectura de triple buffer y coalescencia de solicitudes.
 */
typedef struct MonsterVisualAsync {
    MonsterVisualAsyncConfig config;

    /* Front Buffer / Mallas expuestas al hilo principal de renderizado */
    Mesh displayMesh;
    MonsterVisualEyeAsync* displayEyes;
    size_t displayEyeCount;
    size_t displayEyeCapacity;
    uint64_t displayGeneration;
    uint64_t displayFingerprint;

    /* Sincronización y worker thread */
    pthread_t workerThread;
    pthread_mutex_t lock;
    pthread_cond_t cond;
    bool threadRunning;
    bool shouldQuit;

    /* Back Buffer / Snapshot pendiente solicitada (latest-request-wins) */
    bool hasPendingRequest;
    Monster pendingSnapshot;
    uint64_t pendingFingerprint;
    MonsterVisualQualityTier pendingTier;

    /* Ready Buffer / Mallas listas generadas por el worker */
    bool hasReadyMesh;
    Mesh readyMesh;
    MonsterVisualEyeAsync* readyEyes;
    size_t readyEyeCount;
    size_t readyEyeCapacity;
    uint64_t readyGeneration;
    uint64_t readyFingerprint;
    MonsterVisualQualityTier readyTier;

    /* Control de temporización de movimiento para cambio de tier */
    uint64_t lastObservedFingerprint;
    float timeSinceLastMotionSec;

    /* Métricas */
    MonsterVisualAsyncStats stats;
} MonsterVisualAsync;

/**
 * @brief Retorna una configuración asíncrona por defecto balanceada.
 */
MonsterVisualAsyncConfig MonsterVisualAsync_DefaultConfig(void);

/**
 * @brief Inicializa el gestor asíncrono y crea el hilo worker de reconstrucción.
 * @return Puntero asignado en heap a MonsterVisualAsync o NULL si falla.
 */
MonsterVisualAsync* MonsterVisualAsync_Create(MonsterVisualAsyncConfig config);

/**
 * @brief Detiene de forma segura el hilo worker y libera todas las mallas y memorias poseídas.
 */
void MonsterVisualAsync_Free(MonsterVisualAsync* asyncMgr);

/**
 * @brief Actualización llamada por el hilo principal (60 FPS, NUNCA bloquea).
 * Intercambia mallas listas y encola snapshots del estado del monstruo.
 * @param asyncMgr Gestor asíncrono.
 * @param monster Puntero al monstruo activo.
 * @param deltaTime Tiempo transcurrido en segundos desde el último frame.
 * @return true si la malla mostrada (displayMesh) fue actualizada en este fotograma.
 */
bool MonsterVisualAsync_Update(MonsterVisualAsync* asyncMgr, const Monster* monster, float deltaTime);

/**
 * @brief Obtiene la malla del cuerpo lista para ser renderizada.
 */
const Mesh* MonsterVisualAsync_GetDisplayMesh(const MonsterVisualAsync* asyncMgr);

/**
 * @brief Obtiene el número de ojos activos en la malla mostrada.
 */
size_t MonsterVisualAsync_GetDisplayEyeCount(const MonsterVisualAsync* asyncMgr);

/**
 * @brief Obtiene la esclerótica del ojo en el índice especificado.
 */
const Mesh* MonsterVisualAsync_GetDisplayEyeSclera(const MonsterVisualAsync* asyncMgr, size_t index);

/**
 * @brief Obtiene la pupila del ojo en el índice especificado.
 */
const Mesh* MonsterVisualAsync_GetDisplayEyePupil(const MonsterVisualAsync* asyncMgr, size_t index);

/**
 * @brief Renderiza todas las mallas visibles (cuerpo u ojos) utilizando la VTable de Renderer3D.
 */
bool MonsterVisualAsync_Render(const MonsterVisualAsync* asyncMgr, Renderer3D* renderer);

/**
 * @brief Retorna la generación actual de la malla en pantalla.
 */
uint64_t MonsterVisualAsync_GetDisplayGeneration(const MonsterVisualAsync* asyncMgr);

/**
 * @brief Retorna un puntero de sólo lectura a las estadísticas del gestor asíncrono.
 */
MonsterVisualAsyncStats MonsterVisualAsync_GetStats(const MonsterVisualAsync* asyncMgr);

/**
 * @brief Bloquea el hilo actual hasta que el worker procese las solicitudes pendientes.
 */
void MonsterVisualAsync_Flush(MonsterVisualAsync* asyncMgr);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_VISUAL_ASYNC_H
