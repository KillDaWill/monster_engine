/**
 * @file EcosystemAnatomyCache.h
 * @brief Caché compartida e inmutable de plantillas anatómicas para simulación ecológica.
 *
 * Permite que cientos o miles de individuos con receta, revisión, cubeta morfológica
 * y muestra de desarrollo compatibles compartan una única plantilla compilada de rig,
 * grafo anatómico y perfil de alcance cervical, reduciendo ~200x la memoria por entidad
 * y evitando reconstrucciones redundantes de Monster durante el crecimiento juvenil.
 */
#ifndef ECOSYSTEM_ANATOMY_CACHE_H
#define ECOSYSTEM_ANATOMY_CACHE_H

#include "MonsterAnimation.h"
#include "CreaturePhenotype.h"
#include "AnatomicalReach.h"
#include "EvolutionProfiler.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ECOSYSTEM_ANATOMY_GROWTH_STEPS 16u
#define ECOSYSTEM_ANATOMY_CACHE_CAPACITY 256u

typedef struct EcosystemAnatomyTemplateKey {
    uint32_t recipeId;
    uint64_t recipeHash;
    uint32_t morphologyBucket;
    uint32_t growthStep; /**< 0..ECOSYSTEM_ANATOMY_GROWTH_STEPS (16 = adulto). */
} EcosystemAnatomyTemplateKey;

typedef struct EcosystemAnatomyTemplate {
    EcosystemAnatomyTemplateKey key;
    uint64_t topologyHash;
    uint32_t refCount;
    uint64_t lastUsedStamp;
    bool valid;

    Rig rig;
    AnatomyGraph graph;
    CreaturePhenotype phenotype;
    SkeletonPose rest;         /**< Pose de reposo base (sin standingHeight). */
    SkeletonPose standingRest; /**< Pose de reposo con standingHeight aplicado en raíces. */
    AnatomicalReachProfile profile;

    Vector3 center;
    float modelLength;
    float standingHeight;
    float normalizedDevelopment; /**< [0,1] usado al compilar esta plantilla. */

    /* Landmarks neutrales precomputados (con standingHeight, crouch=0) en espacio modelo. */
    Vector3 cervicalParentStandingPos;
    Quaternion cervicalParentStandingRot;
    Vector3 neutralMouthLocal;
    Vector3 neutralMouthDir;
    Vector3 neutralPivotLocal;
    Vector3 neutralElbowLocal;
    Vector3 restOralPointWorld; /**< rest.joints[headJoint].worldPosition + rig.oralPoint */
    int pivotChainIndex;        /**< Índice en profile.joints[] o -1 si está fuera de la cadena. */
    int elbowChainIndex;        /**< Índice en profile.joints[] o -1 si está fuera de la cadena. */
} EcosystemAnatomyTemplate;

typedef struct EcosystemAnatomyCacheStats {
    uint64_t hits;
    uint64_t misses;
    uint64_t evictions;
    unsigned activeEntries;
    unsigned referencedEntries;
} EcosystemAnatomyCacheStats;

/** @brief Cuantiza edad normalizada [0,1] al paso discreto de plantilla [0..16]. */
uint32_t EcosystemAnatomyCache_GrowthStep(double normalizedAge);

/** @brief Adquiere (con incremento de refCount) la plantilla compartida para los parámetros dados. */
const EcosystemAnatomyTemplate *EcosystemAnatomyCache_Acquire(uint32_t recipeId,
                                                              uint32_t morphologyBucket,
                                                              uint32_t growthStep,
                                                              EvolutionProfiler *profiler);

/** @brief Incrementa explícitamente el contador de referencias de una plantilla válida. */
const EcosystemAnatomyTemplate *EcosystemAnatomyCache_Retain(const EcosystemAnatomyTemplate *tmpl);

/** @brief Libera una referencia a una plantilla compartida (seguro con NULL). */
void EcosystemAnatomyCache_Release(const EcosystemAnatomyTemplate *tmpl);

/** @brief Invalida entradas de caché asociadas a una receta (cuando cambia su revisión). */
void EcosystemAnatomyCache_InvalidateRecipe(uint32_t recipeId);

/** @brief Libera todas las entradas sin referencias activas (p. ej. al reiniciar/cerrar). */
void EcosystemAnatomyCache_ClearUnused(void);

/** @brief Consulta métricas de aciertos, fallos y entradas activas. */
EcosystemAnatomyCacheStats EcosystemAnatomyCache_GetStats(void);

#ifdef __cplusplus
}
#endif

#endif /* ECOSYSTEM_ANATOMY_CACHE_H */
