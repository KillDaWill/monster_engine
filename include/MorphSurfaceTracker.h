/** @file MorphSurfaceTracker.h
 * @brief Superficie persistente con predicción anatómica y corrección SDF selectiva.
 */
#ifndef MORPH_SURFACE_TRACKER_H
#define MORPH_SURFACE_TRACKER_H

#include "AnatomyDeformer.h"
#include "HeadMorph.h"
#include "Monster.h"
#include "SDFSampling.h"
#include "SDFSamplingPool.h"

/** Métricas de una corrección contra el campo exacto. */
typedef struct MorphSurfaceTrackerStats {
    size_t distanceEvaluations, gradientEvaluations, projectedVertices, rejectedProjections;
    size_t correctedNormals;
    size_t connectorCandidates,connectorExact,connectorPruned,processedBricks;
    size_t thinTriangles, opposedTriangles, qualityBricks;
    float meanResidual, p95Residual, maximumResidual;
    size_t maximumResidualVertex;
    float deformationMs, projectionMs;
    float brickUpdateMs,candidateCompileMs,normalCorrectionMs,qualityScanMs;
    bool valid, watertight;
} MorphSurfaceTrackerStats;

/** Conserva dominio, índices y vínculos; sólo desplaza posiciones y normales. */
typedef struct MorphSurfaceTracker {
    Vector3* corrections;
    Vector3* normalCorrections;
    Vector3* rawPositions;
    Vector3* rawNormals;
    float* edgeLength;
    float* residuals;
    size_t* triangleOffsets;
    uint32_t* incidentTriangles;
    uint8_t* normalDirty;
    uint8_t* problemTriangles; /**< Triángulos que requieren corrección topológica local. */
    uint8_t* referenceThinTriangles;
    size_t vertexCount;
    size_t triangleCount;
    float referenceAge;
    float correctedAge;
    SDFSamplingPool* pool; /**< Pool prestado para bloques independientes de vértices. */
    struct MorphSurfaceBrick* bricks;
    size_t brickCount,brickCapacity;
    size_t* brickTable;
    size_t brickTableCapacity;
    uint32_t* vertexOrder;
    uint32_t* vertexBrick;
    MorphSurfaceTrackerStats stats;
} MorphSurfaceTracker;

/** @brief Reserva estado por vértice a partir de una malla de referencia exacta. */
bool MorphSurfaceTracker_Bind(MorphSurfaceTracker* tracker,const Mesh* reference,float age);
/** @brief Asocia un pool prestado para la fase de corrección. */
void MorphSurfaceTracker_SetPool(MorphSurfaceTracker* tracker,SDFSamplingPool* pool);
/** @brief Libera las reservas del rastreador. */
void MorphSurfaceTracker_Free(MorphSurfaceTracker* tracker);
/** @brief Predice la superficie sin consultas SDF; conserva su topología y dominio. */
bool MorphSurfaceTracker_Predict(MorphSurfaceTracker* tracker,const AnatomyDeformer* anatomy,
    const HeadMorph* head,const Monster* target,Mesh* mesh);
/** @brief Predice regiones estables sin mover vértices de módulos congelados. */
bool MorphSurfaceTracker_PredictMasked(MorphSurfaceTracker* tracker,
    const AnatomyDeformer* anatomy,const HeadMorph* head,const Monster* target,
    Mesh* mesh,const uint8_t* frozenVertices);
/** @brief Proyecta sólo los vértices cuyo error supera la tolerancia local. */
bool MorphSurfaceTracker_Correct(MorphSurfaceTracker* tracker,const SDFField* field,
    Mesh* mesh,float age);
/** @brief Recuenta las caras plegadas tras sustituir un parche y reanclar. */
void MorphSurfaceTracker_AuditQuality(MorphSurfaceTracker* tracker,const Mesh* mesh);
/** @brief Transfiere correcciones entre instancias de la misma referencia. */
bool MorphSurfaceTracker_CopyCorrections(MorphSurfaceTracker* dst,const MorphSurfaceTracker* src);

#endif
