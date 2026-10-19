/** @file EcosystemVisual.h
 * @brief Orquestador visual compartido para criaturas de un ecosistema.
 */
#ifndef ECOSYSTEM_VISUAL_H
#define ECOSYSTEM_VISUAL_H

#include "AnatomyDeformer.h"
#include "EcosystemWorld.h"
#include "MonsterVisual.h"
#include "VisualVariation.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#define ECOSYSTEM_VISUAL_MAX_SLOTS 256u
#define ECOSYSTEM_VISUAL_CACHE_CAPACITY 48u
#define ECOSYSTEM_VISUAL_QUEUE_CAPACITY 32u

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

/** @brief Paquete inmutable que el adaptador gráfico consume durante un frame. */
typedef struct EcosystemVisualDrawPacket {
    uint32_t slotIndex;
    AIObjectId entityId;
    bool ready;
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
    float poseAlpha;
    Vector3 position;
    float yaw;
    float scale;
    /** @brief Semianchura máxima del torso anatómico en unidades de malla. */
    float meshRadius;
    /** @brief Radio radial bruto medido en el tercio central de la malla. */
    float measuredMeshRadius;
    /** @brief Longitud axial de la malla, medida una vez al construir la caché. */
    float meshLength;
    /** @brief Longitud axial canónica compartida por todos los niveles de detalle. */
    float calibrationLength;
    /** @brief Escala axial del fenotipo canónico usado para construir la malla. */
    float axialTotalScale;
    /** @brief Factor malla->mundo derivado de la longitud axial de referencia. */
    float calibrationFactor;
    /** @brief Multiplicador de avance de la animación derivado de genome.speed. */
    float gaitCadence;
    /** @brief Sesgo postural [-1,1] aplicado como una pequeña inclinación de la pose. */
    float postureLean;
    /** @brief Frecuencia perceptiva disponible para un renderizador con escaneo cefálico. */
    float headScanRate;
    SurfaceRecipe surfaceRecipe;
} EcosystemVisualDrawPacket;

/** @brief Estado opaco; no se copia por valor. */
typedef struct EcosystemVisual {
    void* state;
} EcosystemVisual;

/** @brief Inicializa caché, fallback y el único trabajador de mallado. */
bool EcosystemVisual_Init(EcosystemVisual*, EcosystemWorld*);
/** @brief Cambia el mundo vinculado y destruye todas las ranuras anteriores. */
bool EcosystemVisual_BindWorld(EcosystemVisual*, EcosystemWorld*);
/** @brief Sincroniza por valor; nunca conserva EcosystemEntity*. */
bool EcosystemVisual_Sync(EcosystemVisual*);
/** @brief Avanza reconciliación, animación, publicación y como máximo una cola. */
bool EcosystemVisual_Update(EcosystemVisual*, float deltaSeconds);
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
bool EcosystemVisual_DeformBody(const EcosystemVisualDrawPacket*,Mesh* output);

/** @brief Selecciona la superficie dibujada más próxima a un rayo mundial.
 * @param packets Paquetes del último frame, incluida su pose y escala.
 * @param count Número de paquetes.
 * @param origin Origen mundial del rayo.
 * @param direction Dirección mundial, normalizada internamente.
 * @param nearDistance Inicio del intervalo visible, en unidades mundiales.
 * @param farDistance Fin del intervalo visible, en unidades mundiales.
 * @return ID estable del individuo alcanzado, o cero sobre el fondo.
 */
AIObjectId EcosystemVisual_PickRay(const EcosystemVisualDrawPacket* packets,size_t count,
    Vector3 origin,Vector3 direction,float nearDistance,float farDistance);

/** @brief Comprueba que cada entidad viva tenga una única ranura válida. */
bool EcosystemVisual_Validate(const EcosystemVisual*);
/** @brief Consulta el hash determinista de una clave estructural. */
uint64_t EcosystemVisual_CacheKeyHash(EcosystemVisualCacheKey);
/** @brief Cuenta ranuras, entradas residentes y trabajos generados. */
size_t EcosystemVisual_SlotCount(const EcosystemVisual*);
size_t EcosystemVisual_CacheCount(const EcosystemVisual*);
size_t EcosystemVisual_BuildCount(const EcosystemVisual*);
size_t EcosystemVisual_CacheHitCount(const EcosystemVisual*);

#endif
