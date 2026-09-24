#include "Creature.h"
#include "Limb.h"
/**
 * @file benchmark_ager_realtime.c
 * @brief Benchmark automatizado de coherencia temporal, lag de edad y FPS para demo_ager_3d.
 * Simula un ciclo completo 0 -> 1 -> 0 a 60 Hz y audita percentiles de retardo y anatomía.
 * @author Monster Engine Team
 * @date 2026
 */

#include "Monster.h"
#include "Creature.h"
#include "MonsterAger.h"
#include "MonsterVisualAsync.h"
#include "MathUtils.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

static double Bench_GetTimeMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}

static int CompareFloats(const void* a, const void* b) {
    float fa = *(const float*)a;
    float fb = *(const float*)b;
    return (fa > fb) - (fa < fb);
}

static unsigned Bench_VisibleDigits(const Mesh* mesh, float scale) {
    float lo = 0, hi = 1;
    for (unsigned i = 0; i < 24; ++i) {
        float t = (lo + hi) * 0.5f;
        CreaturePhenotype p = CreaturePhenotype_Interpolate(&CreatureRecipes_Lizard()->juvenile,&CreatureRecipes_Lizard()->adult, t);
        if (p.axial.totalScale < scale) lo = t; else hi = t;
    }
    CreaturePhenotype p = CreaturePhenotype_Interpolate(&CreatureRecipes_Lizard()->juvenile,&CreatureRecipes_Lizard()->adult, (lo + hi) * 0.5f);
    AnatomyGraph graph;
    if (!Creature_ResolveAnatomy(CreatureRecipes_Lizard(),&p, &graph)) return 0;
    const unsigned formula[2][5] = {{2, 3, 4, 5, 3}, {2, 3, 4, 5, 4}};
    unsigned visible = 0;
    for (unsigned limb = 0; limb < 4; ++limb) {
        for (unsigned digit = 0; digit < 5; ++digit) {
            const AnatomyNode* n = AnatomyGraph_FindNode(&graph,
                Anatomy_MakeId(10+(limb),Limb_DigitLocalId( digit, formula[limb / 2][digit] + 1)));
            if (!n) continue;
            float nearest = 1e6f;
            for (size_t v = 0; v < mesh->vertexCount; ++v) {
                nearest = fminf(nearest, Vec3_Distance(n->center, mesh->vertices[v].position));
            }
            visible += nearest <= n->widthRadius * 1.8f;
        }
    }
    return visible;
}

int main(int argc,char** argv) {
    bool paced=argc<2 || strcmp(argv[1],"--unpaced")!=0;
    printf("=================================================================\n");
    printf(" BENCHMARK TIEMPO REAL: Coherencia Temporal y Morphing en Ager 3D \n");
    printf("=================================================================\n");

    Monster young = Monster_Create();
    Monster adult = Monster_Create();
    CreaturePhenotype juvP = CreatureRecipes_Lizard()->juvenile;
    CreaturePhenotype adultP = CreatureRecipes_Lizard()->adult;
    if (!Creature_BuildMonster(&young,CreatureRecipes_Lizard(), &juvP) || !Creature_BuildMonster(&adult,CreatureRecipes_Lizard(), &adultP)) {
        fprintf(stderr, "[ERROR] Fallo al construir fenotipos base\n");
        return 1;
    }

    float ageFactor = 0.0f;
    MonsterAger ager = MonsterAger_Create(&young, &adult, ageFactor);

    MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
    MonsterVisualAsync* visual = MonsterVisualAsync_Create(cfg);
    if (!visual) {
        fprintf(stderr, "[ERROR] Fallo al crear MonsterVisualAsync\n");
        return 1;
    }

    MonsterVisualAsync_SetContinuousMotion(visual, true);
    MonsterVisualAsync_SetMorphMode(visual, true);

    /* Generación inicial */
    MonsterVisualAsync_UpdateWithAppearance(visual,MonsterAger_GetGeometryResultConst(&ager),
        MonsterAger_GetResultConst(&ager),0.0f);
    MonsterVisualAsync_Flush(visual);

    const size_t MAX_FRAMES = 600; /* 10 segundos @ 60 FPS */
    float* ageLags = (float*)malloc(MAX_FRAMES * sizeof(float));
    float* frameDurationsMs = (float*)malloc(MAX_FRAMES * sizeof(float));

    float growthDir = 1.0f;
    float ageSpeed = 0.20f; /* 5s ida, 5s vuelta = 10s total */
    float dt = 1.0f / 60.0f; /* 16.666 ms */

    size_t frameCount = 0;
    unsigned minDigits = 20;
    double benchStart = Bench_GetTimeMs();

    while (frameCount < MAX_FRAMES) {
        double fStart = Bench_GetTimeMs();

        /* Avanzar edad objetivo */
        ageFactor += growthDir * ageSpeed * dt;
        if (ageFactor >= 1.0f) {
            ageFactor = 1.0f;
            growthDir = -1.0f;
        } else if (ageFactor <= 0.0f) {
            ageFactor = 0.0f;
            growthDir = 1.0f;
        }
        MonsterAger_SetPerc(&ager, ageFactor);

        const Monster* current = MonsterAger_GetResultConst(&ager);
        MonsterVisualAsync_UpdateWithAppearance(visual,MonsterAger_GetGeometryResultConst(&ager),current,dt);

        MonsterVisualAsyncStats stats = MonsterVisualAsync_GetStats(visual);
        const Mesh* bodyMesh = MonsterVisualAsync_GetDisplayMesh(visual);

        float presentedScale=stats.presentedScale>0?stats.presentedScale:stats.displayedScale;
        float lag = stats.geometryLag;
        ageLags[frameCount] = lag;

        /* Verificar dígitos cada 30 fotogramas para no ralentizar el benchmark */
        if (frameCount % 30 == 0 && bodyMesh && bodyMesh->vertexCount > 0) {
            unsigned vis = Bench_VisibleDigits(bodyMesh,presentedScale);
            if (vis < minDigits) minDigits = vis;
        }

        double fEnd = Bench_GetTimeMs();
        frameDurationsMs[frameCount] = (float)(fEnd - fStart);
        frameCount++;
        /* Reloj absoluto de presentación: 600 frames representan diez segundos
         * reales. --unpaced mide exclusivamente throughput de actualización. */
        if(paced) {
            double deadlineMs=benchStart+frameCount*(1000.0/60.0);
            struct timespec deadline={.tv_sec=(time_t)(deadlineMs/1000.0),
                .tv_nsec=(long)(fmod(deadlineMs,1000.0)*1000000.0)};
            while(clock_nanosleep(CLOCK_MONOTONIC,TIMER_ABSTIME,&deadline,NULL)==EINTR){}
        }
    }

    double totalBenchMs = Bench_GetTimeMs() - benchStart;

    /* Ordenar para cálculo de percentiles */
    qsort(ageLags, frameCount, sizeof(float), CompareFloats);
    qsort(frameDurationsMs, frameCount, sizeof(float), CompareFloats);

    float p50Lag = ageLags[(size_t)(frameCount * 0.50f)];
    float p95Lag = ageLags[(size_t)(frameCount * 0.95f)];
    float p99Lag = ageLags[(size_t)(frameCount * 0.99f)];
    float maxLag = ageLags[frameCount - 1];

    float p50FrameMs = frameDurationsMs[(size_t)(frameCount * 0.50f)];
    float p95FrameMs = frameDurationsMs[(size_t)(frameCount * 0.95f)];
    float maxFrameMs = frameDurationsMs[frameCount - 1];

    float effectiveFps = (float)frameCount / (float)(totalBenchMs / 1000.0);

    printf("\n--- RESULTADOS DEL BENCHMARK (%zu fotogramas simulados) ---\n", frameCount);
    printf(" Duración total simulación: %.2f s | FPS promedio de actualización: %.1f\n",
           totalBenchMs / 1000.0, effectiveFps);
    printf(" Tiempo por frame (ms): p50 = %.3f ms | p95 = %.3f ms | max = %.3f ms\n",
           p50FrameMs, p95FrameMs, maxFrameMs);
    printf(" Desfase de edad geométrica (lag): p50 = %.4f | p95 = %.4f | p99 = %.4f | max = %.4f\n",
           p50Lag, p95Lag, p99Lag, maxLag);
    printf(" Extremos de dígitos mínimos observados: %u / 20\n", minDigits);

    bool passP95 = p95Lag <= 0.02f;
    bool passMax = maxLag <= 0.04f;
    bool passFps = p95FrameMs < 33.33f; /* > 30 FPS */
    bool passDigits = minDigits == 20;

    printf("\n--- CRITERIOS DE ACEPTACIÓN ---\n");
    printf(" [ %s ] p95 lag <= 0.02 (obtenido: %.4f)\n", passP95 ? "PASS" : "FAIL", p95Lag);
    printf(" [ %s ] max lag <= 0.04 (obtenido: %.4f)\n", passMax ? "PASS" : "FAIL", maxLag);
    printf(" [ %s ] render/update >= 30 FPS (p95 = %.2f ms, equivalente a %.1f FPS)\n",
           passFps ? "PASS" : "FAIL", p95FrameMs, 1000.0f / fmaxf(p95FrameMs, 0.001f));
    printf(" [ %s ] 20 dígitos/unguales preservados intactos (%u/20)\n",
           passDigits ? "PASS" : "FAIL", minDigits);

    MonsterVisualAsyncStats finalStats=MonsterVisualAsync_GetStats(visual);
    printf(" Worker: completados=%llu cancelados=%llu obsoletos_descartados=%llu coalescidos=%llu\n",
        (unsigned long long)finalStats.completedBuildCount,(unsigned long long)finalStats.cancelledBuildCount,
        (unsigned long long)finalStats.staleBuildDiscardedCount,(unsigned long long)finalStats.coalescedCount);
    free(ageLags);
    free(frameDurationsMs);
    MonsterVisualAsync_Free(visual);
    MonsterAger_Free(&ager);
    Monster_Free(&young);
    Monster_Free(&adult);

    if (passP95 && passMax && passFps && passDigits) {
        printf("\n=========================================================\n");
        printf(" ¡BENCHMARK DE COHERENCIA TEMPORAL SUPERADO CON ÉXITO!   \n");
        printf("=========================================================\n");
        return 0;
    } else {
        printf("\n[ERROR] Uno o más criterios de coherencia no se cumplieron.\n");
        return 1;
    }
}
