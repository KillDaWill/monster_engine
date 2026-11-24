/**
 * @file EvolutionProfiler.c
 * @brief Implementación del perfilador opcional por subsistemas para Evolution Arena.
 */
#include "EvolutionProfiler.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <time.h>

static int cmpFloat(const void *a, const void *b) {
    float x = *(const float *)a;
    float y = *(const float *)b;
    return (x > y) - (x < y);
}

void EvolutionProfiler_Init(EvolutionProfiler *p, bool enabled) {
    if (!p) return;
    memset(p, 0, sizeof(*p));
    p->enabled = enabled;
}

void EvolutionProfiler_Reset(EvolutionProfiler *p) {
    if (!p) return;
    bool enabled = p->enabled;
    memset(p, 0, sizeof(*p));
    p->enabled = enabled;
}

double EvolutionProfiler_NowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

void EvolutionProfiler_RecordMs(EvolutionProfiler *p, EvolutionProfileSection section, double ms) {
    if (!p || !p->enabled || section < 0 || section >= EVO_PROF_SECTION_COUNT) return;
    if (!isfinite(ms) || ms < 0.0) ms = 0.0;
    EvolutionProfileBucket *b = &p->sections[section];
    b->totalMs += ms;
    if (ms > b->maxMs) b->maxMs = ms;
    ++b->invocations;
    b->samples[b->sampleHead] = (float)ms;
    b->sampleHead = (b->sampleHead + 1u) % EVO_PROF_SAMPLE_CAPACITY;
    if (b->sampleCount < EVO_PROF_SAMPLE_CAPACITY) ++b->sampleCount;
}

void EvolutionProfiler_SampleMemory(EvolutionProfiler *p) {
    if (!p) return;
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0 && ru.ru_maxrss > 0) {
        p->counters.peakRssKib = (uint64_t)ru.ru_maxrss;
    }
}

EvolutionProfileSummary EvolutionProfiler_Summarize(const EvolutionProfiler *p,
                                                    EvolutionProfileSection section) {
    EvolutionProfileSummary out = {0};
    if (!p || section < 0 || section >= EVO_PROF_SECTION_COUNT) return out;
    const EvolutionProfileBucket *b = &p->sections[section];
    out.totalMs = b->totalMs;
    out.maxMs = b->maxMs;
    out.invocations = b->invocations;
    out.meanMs = b->invocations ? (b->totalMs / (double)b->invocations) : 0.0;
    if (b->sampleCount == 0u) return out;

    float sorted[EVO_PROF_SAMPLE_CAPACITY];
    memcpy(sorted, b->samples, b->sampleCount * sizeof(float));
    qsort(sorted, b->sampleCount, sizeof(float), cmpFloat);
    unsigned n = b->sampleCount;
    out.medianMs = (double)sorted[n / 2u];
    out.p95Ms = (double)sorted[((n - 1u) * 95u) / 100u];
    out.p99Ms = (double)sorted[((n - 1u) * 99u) / 100u];
    return out;
}

const char *EvolutionProfiler_SectionName(EvolutionProfileSection section) {
    static const char *names[EVO_PROF_SECTION_COUNT] = {
        "sim_tick",
        "physiology",
        "metabolism",
        "resources",
        "entity_lookup",
        "obs_publish",
        "spatial_index",
        "perception",
        "neural_inference",
        "actions",
        "navigation",
        "avoidance",
        "movement",
        "anatomy_config",
        "anatomy_reach",
        "reproduction",
        "birth_death",
        "statistics",
        "snapshot_pub",
        "vis_reconcile",
        "vis_animation",
        "mesh_gen",
        "mesh_pub",
        "gpu_upload",
        "cpu_skinning",
        "gpu_skinning",
        "draw_submit",
        "hud_neural",
        "gpu_render"
    };
    return (section >= 0 && section < EVO_PROF_SECTION_COUNT) ? names[section] : "unknown";
}

bool EvolutionProfiler_WriteJSON(const EvolutionProfiler *p, FILE *out) {
    if (!p || !out) return false;
    fprintf(out, "{\n  \"sections\": {\n");
    for (int i = 0; i < EVO_PROF_SECTION_COUNT; ++i) {
        EvolutionProfileSummary s = EvolutionProfiler_Summarize(p, (EvolutionProfileSection)i);
        fprintf(out,
                "    \"%s\": {\"invocations\": %llu, \"total_ms\": %.4f, \"mean_ms\": %.5f, "
                "\"median_ms\": %.5f, \"p95_ms\": %.5f, \"p99_ms\": %.5f, \"max_ms\": %.5f}%s\n",
                EvolutionProfiler_SectionName((EvolutionProfileSection)i),
                (unsigned long long)s.invocations, s.totalMs, s.meanMs,
                s.medianMs, s.p95Ms, s.p99Ms, s.maxMs,
                (i + 1 < EVO_PROF_SECTION_COUNT) ? "," : "");
    }
    const EvolutionProfilerCounters *c = &p->counters;
    fprintf(out,
            "  },\n  \"counters\": {\n"
            "    \"anatomical_solver_invocations\": %llu,\n"
            "    \"anatomical_solver_iterations\": %llu,\n"
            "    \"skeleton_transform_updates\": %llu,\n"
            "    \"anatomical_template_hits\": %llu,\n"
            "    \"anatomical_template_misses\": %llu,\n"
            "    \"monster_constructions\": %llu,\n"
            "    \"navigation_requests\": %llu,\n"
            "    \"astar_expanded_nodes\": %llu,\n"
            "    \"spatial_query_candidates\": %llu,\n"
            "    \"exact_collision_tests\": %llu,\n"
            "    \"active_visual_slots\": %llu,\n"
            "    \"animated_visual_slots\": %llu,\n"
            "    \"visible_creatures\": %llu,\n"
            "    \"rendered_near\": %llu,\n"
            "    \"rendered_mid\": %llu,\n"
            "    \"rendered_far\": %llu,\n"
            "    \"draw_calls\": %llu,\n"
            "    \"generated_meshes\": %llu,\n"
            "    \"gpu_uploads\": %llu,\n"
            "    \"cpu_deformation_fallbacks\": %llu,\n"
            "    \"allocations\": %llu,\n"
            "    \"peak_rss_kib\": %llu\n"
            "  }\n}\n",
            (unsigned long long)c->anatomicalSolverInvocations,
            (unsigned long long)c->anatomicalSolverIterations,
            (unsigned long long)c->skeletonTransformUpdates,
            (unsigned long long)c->anatomicalTemplateHits,
            (unsigned long long)c->anatomicalTemplateMisses,
            (unsigned long long)c->monsterConstructions,
            (unsigned long long)c->navigationRequests,
            (unsigned long long)c->astarExpandedNodes,
            (unsigned long long)c->spatialQueryCandidates,
            (unsigned long long)c->exactCollisionTests,
            (unsigned long long)c->activeVisualSlots,
            (unsigned long long)c->animatedVisualSlots,
            (unsigned long long)c->visibleCreatures,
            (unsigned long long)c->renderedNear,
            (unsigned long long)c->renderedMid,
            (unsigned long long)c->renderedFar,
            (unsigned long long)c->drawCalls,
            (unsigned long long)c->generatedMeshes,
            (unsigned long long)c->gpuUploads,
            (unsigned long long)c->cpuDeformationFallbacks,
            (unsigned long long)c->allocations,
            (unsigned long long)c->peakRssKib);
    return !ferror(out);
}
