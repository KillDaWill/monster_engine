/**
 * @file WorldCollision.h
 * @brief Perfil declarativo de desplazamiento, consultas físicas autoritativas de superficie,
 *        vadeo, colisión barrida, línea de visión 3D y marco corporal completo sobre pendientes.
 */
#ifndef MONSTER_WORLD_COLLISION_H
#define MONSTER_WORLD_COLLISION_H

#include "WorldStaticData.h"
#include "Quaternion.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Capacidades declarativas de desplazamiento físico sobre terreno y agua somera. */
typedef struct TraversalProfile {
    float maxSlopeRadians;      /**< Pendiente máxima transitable (rad, ej. 0.62 ~ 35 deg). */
    float maxStepHeight;        /**< Escalón máximo permitido en una huella (u.m.). */
    float maxWadingDepth;       /**< Profundidad máxima de vadeo seguro (u.m., 0 = sin vadeo). */
    float footprintRadius;      /**< Radio físico de apoyo del cuerpo en XZ. */
    float uphillSpeedFactor;    /**< Factor mínimo de velocidad en pendiente ascendente máxima [0.25..1]. */
    float downhillSpeedFactor;  /**< Factor de velocidad en descenso [0.5..1.15]. */
    float wadingSpeedFactor;    /**< Factor de velocidad en vadeo profundo [0.25..1]. */
    float slopeEnergyCost;      /**< Multiplicador adicional de coste energético al subir (>= 0). */
    float wadingEnergyCost;     /**< Multiplicador adicional de coste energético al vadear (>= 0). */
    float maxBodyTiltRadians;   /**< Inclinación corporal máxima permitida (cabeceo/alabeo). */
    bool canWade;               /**< Permite entrar en agua somera hasta maxWadingDepth. */
    bool canSwim;               /**< Nado completo (fuera de alcance v1: false para terrestres). */
} TraversalProfile;

typedef enum TraversalBlockReason {
    TRAVERSAL_OK             = 0,
    TRAVERSAL_OUT_OF_BOUNDS  = 1,
    TRAVERSAL_TOO_STEEP      = 2,
    TRAVERSAL_STEP_TOO_HIGH  = 3,
    TRAVERSAL_WATER_TOO_DEEP = 4,
    TRAVERSAL_ROCK_BLOCKED   = 5,
    TRAVERSAL_INVALID_INPUT  = 6
} TraversalBlockReason;

typedef struct TraversalResult {
    bool traversable;
    TraversalBlockReason reason;
    float maxSlopeSeen;
    float maxWaterDepthSeen;
    float speedMultiplier;      /**< Factor resultante combinado [0.2..1.15]. */
    float energyMultiplier;     /**< Factor resultante de coste energético [1.0..3.0]. */
    Vector3 resolvedEndPos;     /**< Posición final proyectada sobre el suelo si traversable=true. */
    Vector3 endNormal;          /**< Normal física del suelo en el punto final. */
} TraversalResult;

/** @brief Perfil por defecto compatible con terreno llano y colinas moderadas. */
TraversalProfile TraversalProfile_DefaultLand(float footprintRadius);

/** @brief Consulta autoritativa de superficie del terreno en (query.x, query.z). */
bool World_SampleSurface(const WorldStaticData *w, Vector3 query, SurfaceHit *out);

/** @brief Consulta autoritativa de agua física en (x, z). */
bool World_SampleWater(const WorldStaticData *w, float x, float z, WaterSample *out);

/** @brief Valida si la huella completa del cuerpo puede apoyarse en (groundPos.x, groundPos.z). */
bool World_CanTraverse(const WorldStaticData *w, Vector3 groundPos, const TraversalProfile *profile,
                       TraversalBlockReason *outReason);

/**
 * @brief Valida desplazamiento proyectado sobre el suelo desde @p from hasta @p to.
 *
 * Muestrea a lo largo del barrido (evitando saltar ríos estrechos, acantilados o rocas)
 * y calcula los multiplicadores acotados de velocidad y energía por pendiente y vadeo.
 */
bool World_CheckTraversalSegment(const WorldStaticData *w, Vector3 from, Vector3 to,
                                 const TraversalProfile *profile, TraversalResult *out);

/**
 * @brief Comprueba línea de visión / segmento de contacto 3D sin intersección con terreno ni rocas.
 * @param radius Radio volumétrico opcional (0 para rayo fino de visión).
 */
bool World_ClearSightSegment(const WorldStaticData *w, Vector3 a, Vector3 b,
                             float radius, float endpointTolerance);

/**
 * @brief Construye orientación corporal estable a partir de guiñada (yaw) y normal de terreno.
 *
 * Proyecta el eje frontal deseado sobre el plano tangente del suelo acotando la inclinación
 * a @p maxTiltRadians sin degeneración ni doble aplicación de la normal.
 */
Quaternion World_BuildBodyFrame(float yaw, Vector3 groundNormal, float maxTiltRadians);

/** @brief Transformación compartida modelo-local -> espacio mundial. */
Vector3 World_LocalToWorld(Vector3 localPos, Vector3 modelCenter, float scale,
                           Quaternion bodyOrientation, Vector3 worldOrigin);

/** @brief Transformación inversa exacta espacio mundial -> modelo-local. */
Vector3 World_WorldToLocal(Vector3 worldPos, Vector3 modelCenter, float scale,
                           Quaternion bodyOrientation, Vector3 worldOrigin);

/** @brief Transforma una dirección/normal de modelo-local a mundial. */
Vector3 World_LocalDirToWorld(Vector3 localDir, Quaternion bodyOrientation);

/** @brief Transforma una dirección/normal de mundial a modelo-local. */
Vector3 World_WorldDirToLocal(Vector3 worldDir, Quaternion bodyOrientation);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_WORLD_COLLISION_H */
