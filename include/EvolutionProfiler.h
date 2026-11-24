/**
 * @file EvolutionProfiler.h
 * @brief Instrumentación de rendimiento opcional y de bajo coste para Evolution Arena.
 *
 * Mide tiempos (media, mediana, P95, P99, máximo e invocaciones) por subsistema y
 * contadores operativos tanto de simulación como de presentación. Cuando `enabled` es
 * false o el puntero es NULL, las macros/inline no realizan llamadas al reloj.
 */
#ifndef EVOLUTION_PROFILER_H
#define EVOLUTION_PROFILER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVO_PROF_SAMPLE_CAPACITY 512u

typedef enum EvolutionProfileSection {
    EVO_PROF_SIM_TICK = 0,
    EVO_PROF_PHYSIOLOGY,
    EVO_PROF_METABOLISM,
    EVO_PROF_RESOURCES,
    EVO_PROF_ENTITY_LOOKUP,
    EVO_PROF_OBS_PUBLISH,
    EVO_PROF_SPATIAL_INDEX,
    EVO_PROF_PERCEPTION,
    EVO_PROF_NEURAL_INFERENCE,
    EVO_PROF_ACTIONS,
    EVO_PROF_NAVIGATION,
    EVO_PROF_AVOIDANCE,
    EVO_PROF_MOVEMENT,
    EVO_PROF_ANATOMY_CONFIG,
    EVO_PROF_ANATOMY_REACH,
    EVO_PROF_REPRODUCTION,
    EVO_PROF_BIRTH_DEATH,
    EVO_PROF_STATISTICS,
    EVO_PROF_SNAPSHOT_PUB,
    EVO_PROF_VIS_RECONCILE,
    EVO_PROF_VIS_ANIMATION,
    EVO_PROF_MESH_GEN,
    EVO_PROF_MESH_PUB,
    EVO_PROF_GPU_UPLOAD,
    EVO_PROF_CPU_SKINNING,
    EVO_PROF_GPU_SKINNING,
    EVO_PROF_DRAW_SUBMIT,
    EVO_PROF_HUD_NEURAL,
    EVO_PROF_GPU_RENDER,
    EVO_PROF_SECTION_COUNT
} EvolutionProfileSection;

typedef struct EvolutionProfileBucket {
    double totalMs;
    double maxMs;
    uint64_t invocations;
    float samples[EVO_PROF_SAMPLE_CAPACITY];
    unsigned sampleCount;
    unsigned sampleHead;
} EvolutionProfileBucket;

typedef struct EvolutionProfileSummary {
    double meanMs;
    double medianMs;
    double p95Ms;
    double p99Ms;
    double maxMs;
    double totalMs;
    uint64_t invocations;
} EvolutionProfileSummary;

typedef struct EvolutionProfilerCounters {
    uint64_t anatomicalSolverInvocations;
    uint64_t anatomicalSolverIterations;
    uint64_t skeletonTransformUpdates;
    uint64_t anatomicalTemplateHits;
    uint64_t anatomicalTemplateMisses;
    uint64_t monsterConstructions;
    uint64_t navigationRequests;
    uint64_t astarExpandedNodes;
    uint64_t spatialQueryCandidates;
    uint64_t exactCollisionTests;
    uint64_t activeVisualSlots;
    uint64_t animatedVisualSlots;
    uint64_t visibleCreatures;
    uint64_t renderedNear;
    uint64_t renderedMid;
    uint64_t renderedFar;
    uint64_t drawCalls;
    uint64_t generatedMeshes;
    uint64_t gpuUploads;
    uint64_t cpuDeformationFallbacks;
    uint64_t allocations;
    uint64_t peakRssKib;
} EvolutionProfilerCounters;

typedef struct EvolutionProfiler {
    bool enabled;
    EvolutionProfileBucket sections[EVO_PROF_SECTION_COUNT];
    EvolutionProfilerCounters counters;
} EvolutionProfiler;

/** @brief Inicializa el perfilador (activado o desactivado). */
void EvolutionProfiler_Init(EvolutionProfiler *p, bool enabled);

/** @brief Reinicia acumuladores, ventanas de muestras y contadores sin cambiar `enabled`. */
void EvolutionProfiler_Reset(EvolutionProfiler *p);

/** @brief Reloj monótono en milisegundos. */
double EvolutionProfiler_NowMs(void);

/** @brief Registra una duración en milisegundos para la sección dada. */
void EvolutionProfiler_RecordMs(EvolutionProfiler *p, EvolutionProfileSection section, double ms);

/** @brief Actualiza `counters.peakRssKib` consultando `getrusage(RUSAGE_SELF)`. */
void EvolutionProfiler_SampleMemory(EvolutionProfiler *p);

/** @brief Calcula media, mediana, P95, P99 y máximo sobre la ventana de muestras. */
EvolutionProfileSummary EvolutionProfiler_Summarize(const EvolutionProfiler *p,
                                                    EvolutionProfileSection section);

/** @brief Nombre legible de la sección para JSON, CLI y panel diagnóstico. */
const char *EvolutionProfiler_SectionName(EvolutionProfileSection section);

/** @brief Exporta el informe completo del perfilador en formato JSON. */
bool EvolutionProfiler_WriteJSON(const EvolutionProfiler *p, FILE *out);

static inline double EvolutionProfiler_Begin(const EvolutionProfiler *p) {
    return (p && p->enabled) ? EvolutionProfiler_NowMs() : 0.0;
}

static inline void EvolutionProfiler_End(EvolutionProfiler *p, EvolutionProfileSection section,
                                         double startMs) {
    if (p && p->enabled) {
        double elapsed = EvolutionProfiler_NowMs() - startMs;
        if (elapsed < 0.0) elapsed = 0.0;
        EvolutionProfiler_RecordMs(p, section, elapsed);
    }
}

static inline void EvolutionProfiler_AddAtomic(uint64_t *counter, uint64_t delta) {
    if (counter && delta) {
        __atomic_fetch_add(counter, delta, __ATOMIC_RELAXED);
    }
}

#ifdef __cplusplus
}
#endif

#endif /* EVOLUTION_PROFILER_H */
