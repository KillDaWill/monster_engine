/** @file EcosystemVisual.h
 * @brief Orquestador visual compartido y escalable (1024+ ranuras) para criaturas de un ecosistema.
 */
#ifndef ECOSYSTEM_VISUAL_H
#define ECOSYSTEM_VISUAL_H

#include "AnatomyDeformer.h"
#include "EcosystemWorld.h"
#include "EvolutionSimulationRunner.h"
#include "EvolutionProfiler.h"
#include "MonsterVisual.h"
#include "VisualVariation.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define ECOSYSTEM_VISUAL_MAX_SLOTS 2048u
#define ECOSYSTEM_VISUAL_CACHE_CAPACITY 96u
#define ECOSYSTEM_VISUAL_QUEUE_CAPACITY 64u

/** @brief Clave estructural de una malla compartida. */
typedef struct EcosystemVisualCacheKey {
    uint32_t recipeId;
    uint32_t stage;
    uint32_t morphologyBucket;
    VisualTier tier;
} EcosystemVisualCacheKey;

/** @brief Datos de cámara que alimentan la selección automática de LOD. */
typedef struct EcosystemVisualProjection {
    float projectedPixels;
    bool visible;
} EcosystemVisualProjection;

/** @brief Estadísticas visuales del último fotograma para diagnóstico/HUD. */
typedef struct EcosystemVisualStats {
    size_t activeSlots;
    size_t slotCapacity;
    size_t visibleCount;
    size_t animatedCount;
    size_t nearCount;
    size_t midCount;
    size_t farCount;
    size_t culledCount;
    size_t monsterInstances;
    size_t cacheEntries;
    size_t cacheBytes;
    size_t pendingQueue;
    size_t buildCount;
    size_t cacheHits;
} EcosystemVisualStats;

/** @brief Paquete inmutable que el adaptador gráfico consume durante un frame. */
typedef struct EcosystemVisualDrawPacket {
    uint32_t slotIndex;
    AIObjectId entityId;
    uint32_t recipeId;
    uint32_t morphologyBucket;
    bool ready;
    bool instancedEligible; /**< true para instancias FAR ligeras aptas para instancing GPU. */
    VisualTier tier;
    const Mesh* bodyMesh;
    const Mesh* headMesh;
    const MonsterVisualEye* eyes;
    size_t eyeCount;
    const MonsterVisualMouth* mouths;
    size_t mouthCount;
    const AnatomyDeformer* deformer;
    const AABB3D* bodyJointBounds; /**< Cotas de reposo por influencia, compartidas con la malla. */
    const Skeleton* skeleton;
    const SkeletonPose* pose;
    const SkeletonPose* previousPose;
    const SkeletonPose* restPose;
    int headJoint, jawJoint;
    Vector3 oralPoint;
    const AnatomyGraph* targetGraph;
    Vector3 headScale;
    Vector3 jawScale;
    float eyeScale;
    Vector3 renderedMouth;
    float mouthError;
    float footError; /**< Máximo residuo de IK de apoyo en unidades mundiales. */
    float stanceFootError; /**< Residuo real de los pies que permanecen plantados. */
    Vector3 presentedCenter;
    Vector3 supportPoints[RIG_MAX_LIMBS]; /**< Centro de suela aproximado en el marco mundial de presentación. */
    unsigned supportCount;
    unsigned stanceMask;
    float poseAlpha;
    Vector3 position;
    Vector3 groundNormal;
    Quaternion orientation;
    float yaw;
    float scale;
    float bobY;       /**< Oscilación vertical analítica para instancias FAR. */
    float leanPitch;  /**< Inclinación axial analítica para instancias FAR. */
    Color tintColor;  /**< Pigmentación individual para lotes instanciados. */
    float meshRadius;
    float measuredMeshRadius;
    float meshLength;
    float calibrationLength;
    float axialTotalScale;
    float calibrationFactor;
    float gaitCadence;
    float postureLean;
    float headScanRate;
    SurfaceRecipe surfaceRecipe;
} EcosystemVisualDrawPacket;

/** @brief Estado opaco; no se copia por valor. */
typedef struct EcosystemVisual {
    void* state;
} EcosystemVisual;

/** @brief Inicializa caché, fallback y el trabajador de mallado vinculado a un EcosystemWorld. */
bool EcosystemVisual_Init(EcosystemVisual*, EcosystemWorld*);
/** @brief Inicializa en modo desacoplado por snapshots inmutables (sin acceder a EcosystemWorld*). */
bool EcosystemVisual_InitForSnapshots(EcosystemVisual*, unsigned initialCapacity);
/** @brief Cambia el mundo vinculado y destruye todas las ranuras anteriores. */
bool EcosystemVisual_BindWorld(EcosystemVisual*, EcosystemWorld*);
/** @brief Configura presupuestos de LOD detallado (NEAR/MID) y factor de calidad/distancia. */
void EcosystemVisual_SetBudget(EcosystemVisual*, unsigned maxNear, unsigned maxMid, float lodBias);
/** @brief Asocia un perfilador opcional para registrar tiempos y contadores visuales. */
void EcosystemVisual_SetProfiler(EcosystemVisual*, EvolutionProfiler*);
/** @brief Sincroniza desde EcosystemWorld por valor; nunca conserva EcosystemEntity*. */
bool EcosystemVisual_Sync(EcosystemVisual*);
/** @brief Reconciliación incremental desde un snapshot inmutable de simulación. */
bool EcosystemVisual_SyncSnapshot(EcosystemVisual*, const EvolutionRenderSnapshot*);
/** @brief Avanza reconciliación, animación, publicación y como máximo una cola (modo clásico). */
bool EcosystemVisual_Update(EcosystemVisual*, float deltaSeconds);
/** @brief Avanza LOD, animación acotada y publicación desde un snapshot inmutable. */
bool EcosystemVisual_UpdateFromSnapshot(EcosystemVisual*, const EvolutionRenderSnapshot*, float realDeltaSeconds);
/** @brief Libera worker, caché, deformadores, mallas auxiliares y ranuras. */
void EcosystemVisual_Shutdown(EcosystemVisual*);

/** @brief Alimenta el tamaño proyectado calculado por la cámara. */
bool EcosystemVisual_SetProjection(EcosystemVisual*, AIObjectId, EcosystemVisualProjection);
/** @brief Solicita explícitamente un LOD; NEAR queda permitido aunque N sea grande. */
bool EcosystemVisual_RequestTier(EcosystemVisual*, AIObjectId, VisualTier);
/** @brief Borra la selección explícita y devuelve el control a la cámara. */
bool EcosystemVisual_ClearTierRequest(EcosystemVisual*, AIObjectId);
/** @brief Solicita claves visibles antes de comenzar el bucle interactivo. */
size_t EcosystemVisual_PrewarmVisible(EcosystemVisual*);

/** @brief Solicita una malla; ante miss devuelve el fallback sin bloquear. */
const Mesh* EcosystemVisual_RequestMesh(EcosystemVisual*, EcosystemVisualCacheKey);
/** @brief Retorna el fallback siempre residente. */
const Mesh* EcosystemVisual_FallbackMesh(const EcosystemVisual*);
/** @brief Copia paquetes de draw sin transferir propiedad al consumidor. */
size_t EcosystemVisual_GetDrawPackets(const EcosystemVisual*, EcosystemVisualDrawPacket*, size_t);
/** @brief Ruta CPU equivalente al skinning de pose y crecimiento, sin remallado. */
bool EcosystemVisual_DeformBody(const EcosystemVisualDrawPacket*, Mesh* output);

/** @brief Selecciona la superficie dibujada más próxima a un rayo mundial. */
AIObjectId EcosystemVisual_PickRay(const EcosystemVisualDrawPacket* packets, size_t count,
    Vector3 origin, Vector3 direction, float nearDistance, float farDistance);

/** @brief Comprueba que cada entidad viva tenga una única ranura válida (diagnóstico explícito). */
bool EcosystemVisual_Validate(const EcosystemVisual*);
/** @brief Consulta el hash determinista de una clave estructural. */
uint64_t EcosystemVisual_CacheKeyHash(EcosystemVisualCacheKey);
/** @brief Estadísticas detalladas de ranuras, LOD y caché de mallas. */
EcosystemVisualStats EcosystemVisual_GetStats(const EcosystemVisual*);
size_t EcosystemVisual_SlotCount(const EcosystemVisual*);
size_t EcosystemVisual_CacheCount(const EcosystemVisual*);
size_t EcosystemVisual_BuildCount(const EcosystemVisual*);
size_t EcosystemVisual_CacheHitCount(const EcosystemVisual*);

#endif
