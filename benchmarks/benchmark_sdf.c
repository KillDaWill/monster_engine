/**
 * @file benchmark_sdf.c
 * @brief Benchmark de rendimiento para el pipeline de generación de malla SDF de monstruos.
 * @author Monster Engine Team
 * @date 2026
 */

#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <time.h>
#include <math.h>

#include "Monster.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "Mesh.h"
#include "ColorPalette.h"

static double GetTimeMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1000000.0;
}

static Monster BuildYoungLizard(void) {
    Monster lizard = Monster_Create();
    Monster_Init(&lizard);
    lizard.colorPalette = ColorPalette_CreateGradient(Color_FromRGB(40, 200, 80), Color_FromRGB(100, 230, 120), 4);

    BodyPart* head = Monster_GetHead(&lizard);
    if (head) {
        head->width = 1.0f; head->height = 0.8f; head->length = 1.2f;
    }
    BodyPart chest = BodyPart_Create(0.0f, 0.0f, -1.3f, 1.2f, 1.4f, 0.9f, 0.0f);
    BodyPart tail  = BodyPart_Create(0.0f, 0.0f, -2.8f, 0.6f, 1.5f, 0.5f, 0.0f);
    Monster_AddBodyPart(&lizard, chest);
    Monster_AddBodyPart(&lizard, tail);

    Mouth mouth = Mouth_Create(0, Vec3_Create(0.0f, -0.15f, 0.48f), Vec3_Create(0.6f, 0.3f, 0.5f), Color_FromRGB(100, 10, 10), Color_FromRGB(150, 40, 40));
    mouth.openFactor = 0.3f;
    Monster_AddMouth(&lizard, mouth);

    Eye leftEye = Eye_Create(0, Vec3_Create(-0.28f, 0.12f, 0.45f), Vec3_Create(0.16f, 0.15f, 0.10f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    leftEye.pupilScale = 0.4f;
    Monster_AddEye(&lizard, leftEye);

    Eye rightEye = Eye_Create(0, Vec3_Create(0.28f, 0.12f, 0.45f), Vec3_Create(0.16f, 0.15f, 0.10f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    rightEye.pupilScale = 0.4f;
    Monster_AddEye(&lizard, rightEye);

    return lizard;
}

static Monster BuildAdultLizard(float scaleFactor) {
    Monster lizard = Monster_Create();
    Monster_Init(&lizard);
    lizard.colorPalette = ColorPalette_CreateGradient(Color_FromRGB(220, 40, 40), Color_FromRGB(240, 180, 30), 4);

    BodyPart* head = Monster_GetHead(&lizard);
    if (head) {
        head->width = 2.5f * scaleFactor;
        head->height = 1.8f * scaleFactor;
        head->length = 2.8f * scaleFactor;
        head->position = Vec3_Scale(head->position, scaleFactor);
        head->positionRender = Vec3_Scale(head->positionRender, scaleFactor);
    }
    BodyPart chest   = BodyPart_Create(0.0f, 0.0f, -3.0f * scaleFactor, 3.2f * scaleFactor, 3.5f * scaleFactor, 2.2f * scaleFactor, 0.0f);
    BodyPart abdomen = BodyPart_Create(0.0f, 0.0f, -6.6f * scaleFactor, 2.8f * scaleFactor, 3.2f * scaleFactor, 1.9f * scaleFactor, 0.0f);
    BodyPart tail1   = BodyPart_Create(0.0f, 0.0f, -10.0f * scaleFactor, 1.8f * scaleFactor, 3.0f * scaleFactor, 1.4f * scaleFactor, 0.0f);
    BodyPart tail2   = BodyPart_Create(0.0f, 0.0f, -13.1f * scaleFactor, 0.9f * scaleFactor, 2.5f * scaleFactor, 0.8f * scaleFactor, 0.0f);

    Monster_AddBodyPart(&lizard, chest);
    Monster_AddBodyPart(&lizard, abdomen);
    Monster_AddBodyPart(&lizard, tail1);
    Monster_AddBodyPart(&lizard, tail2);

    Mouth mouth = Mouth_Create(0, Vec3_Create(0.0f, -0.3f * scaleFactor, 1.2f * scaleFactor), Vec3_Create(1.6f * scaleFactor, 0.9f * scaleFactor, 1.2f * scaleFactor), Color_FromRGB(60, 0, 0), Color_FromRGB(200, 30, 30));
    mouth.openFactor = 0.95f;
    Monster_AddMouth(&lizard, mouth);

    Eye leftEye = Eye_Create(0, Vec3_Create(-0.75f * scaleFactor, 0.35f * scaleFactor, 1.1f * scaleFactor), Vec3_Create(0.42f * scaleFactor, 0.4f * scaleFactor, 0.25f * scaleFactor), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    leftEye.pupilScale = 0.45f;
    Monster_AddEye(&lizard, leftEye);

    Eye rightEye = Eye_Create(0, Vec3_Create(0.75f * scaleFactor, 0.35f * scaleFactor, 1.1f * scaleFactor), Vec3_Create(0.42f * scaleFactor, 0.4f * scaleFactor, 0.25f * scaleFactor), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    rightEye.pupilScale = 0.45f;
    Monster_AddEye(&lizard, rightEye);

    return lizard;
}

static void BenchmarkMonster(const char* name, Monster* monster, float requestedVoxelSize, int iterations) {
    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = requestedVoxelSize;
    cfg.maxResolution = 128;
    cfg.maxCells = 500000;

    SDFMesher mesher = SDFMesher_Create(cfg);
    MonsterSDF sdf = MonsterSDF_Create();
    Mesh mesh = Mesh_Create();

    /* Warm-up run */
    MonsterSDF_Build(&sdf, monster, MonsterSDF_DefaultConfig());
    SDFField field = MonsterSDF_GetField(&sdf);
    SDFMesher_GenerateMesh(&mesher, &field, &mesh);

    double totalBuildTime = 0.0;
    double totalMeshingTime = 0.0;

    for (int i = 0; i < iterations; ++i) {
        double t0 = GetTimeMs();
        MonsterSDF_Build(&sdf, monster, MonsterSDF_DefaultConfig());
        double t1 = GetTimeMs();
        field = MonsterSDF_GetField(&sdf);
        SDFMesher_GenerateMesh(&mesher, &field, &mesh);
        double t2 = GetTimeMs();

        totalBuildTime += (t1 - t0);
        totalMeshingTime += (t2 - t1);
    }

    double avgBuildTime = totalBuildTime / (double)iterations;
    double avgMeshingTime = totalMeshingTime / (double)iterations;
    double avgTotalTime = avgBuildTime + avgMeshingTime;

    const SDFMesherStats* stats = SDFMesher_GetLastStats(&mesher);

    AABB3D bounds = field.getBounds(field.context);

    printf("======================================================================\n");
    printf(" BENCHMARK CATEGORY: %s\n", name);
    printf("======================================================================\n");
    printf(" Bounds                 : [%.2f, %.2f, %.2f] -> [%.2f, %.2f, %.2f]\n",
        bounds.start.x, bounds.start.y, bounds.start.z,
        bounds.end.x, bounds.end.y, bounds.end.z);
    printf(" Requested Voxel Size   : %.3f\n", stats->requestedVoxelSize);
    printf(" Effective Voxel Size   : %.3f (Budget Adjusted: %s)\n",
        stats->effectiveVoxelSize, stats->cellBudgetAdjusted ? "YES" : "NO");
    printf(" Effective Resolution   : %d x %d x %d\n",
        stats->resolutionX, stats->resolutionY, stats->resolutionZ);
    printf(" Total Cell Count       : %zu\n", stats->cellCount);
    printf(" Grid Point Count       : %zu\n", stats->gridPointCount);
    printf(" Field Evaluations      : %zu\n", stats->fieldEvaluationCount);
    printf(" Generated Vertices     : %zu\n", stats->generatedVertexCount);
    printf(" Generated Triangles    : %zu\n", stats->generatedTriangleCount);
    printf("----------------------------------------------------------------------\n");
    printf(" SDF Build Time (avg)   : %.3f ms\n", avgBuildTime);
    printf(" SDF Meshing Time (avg) : %.3f ms\n", avgMeshingTime);
    printf(" Total Pipeline Time    : %.3f ms (%.1f FPS equiv)\n",
        avgTotalTime, (avgTotalTime > 0.0) ? 1000.0 / avgTotalTime : 0.0);
    printf("======================================================================\n\n");

    Mesh_Free(&mesh);
    MonsterSDF_Free(&sdf);
    SDFMesher_Free(&mesher);
}

int main(void) {
    printf("\n>>> RUNNING SDF CREATURE PIPELINE BENCHMARK <<<\n\n");

    const float TARGET_VOXEL_SIZE = 0.13f;
    const int BENCH_ITERATIONS = 5;

    Monster young = BuildYoungLizard();
    BenchmarkMonster("SMALL (Young Lizard 1.0x)", &young, TARGET_VOXEL_SIZE, BENCH_ITERATIONS);
    Monster_Free(&young);

    Monster medium = BuildAdultLizard(1.0f);
    BenchmarkMonster("MEDIUM (Adult Lizard 1.0x)", &medium, TARGET_VOXEL_SIZE, BENCH_ITERATIONS);
    Monster_Free(&medium);

    Monster large = BuildAdultLizard(2.0f);
    BenchmarkMonster("LARGE (Adult Lizard 2.0x)", &large, TARGET_VOXEL_SIZE, BENCH_ITERATIONS);
    Monster_Free(&large);

    Monster xlarge = BuildAdultLizard(3.0f);
    BenchmarkMonster("VERY LARGE (Adult Lizard 3.0x)", &xlarge, TARGET_VOXEL_SIZE, BENCH_ITERATIONS);
    Monster_Free(&xlarge);

    printf(">>> BENCHMARK COMPLETE <<<\n");
    return 0;
}
