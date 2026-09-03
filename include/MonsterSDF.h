/**
 * @file MonsterSDF.h
 * @brief Representación geométrica implícita compilada de un monstruo (Signed Distance Fields).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_SDF_H
#define MONSTER_SDF_H

#include "Vector.h"
#include "Color.h"
#include "Transform3D.h"
#include "AABB.h"
#include "SDFOperations.h"
#include "SDFSampling.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Monster;
typedef struct MonsterSDF MonsterSDF;

/**
 * @struct MonsterSDFConfig
 * @brief Parámetros de configuración para la mezcla y evaluación SDF del monstruo.
 */
typedef struct MonsterSDFConfig {
    float bodySmoothness;          /**< Factor k de mezcla suave entre partes del cuerpo */
    float connectionSmoothness;    /**< Factor k de mezcla suave para conectores entre nodos */
    float mouthSmoothness;         /**< Factor k de sustracción suave para cavidades bucales */
    float connectionRadiusFactor;  /**< Escala de radio para los conectores cónicos */
    float boundsPadding;           /**< Margen extra asignado al Bounding Box (AABB) */
} MonsterSDFConfig;

/**
 * @struct MonsterSDFBodyPart
 * @brief Snapshot compilado de una parte del cuerpo con invariantes geométricas precálculadas.
 */
typedef struct MonsterSDFBodyPart {
    Vector3 center;
    Vector3 radii;
    Vector3 invRadii;
    Vector3 invRadiiSquared;
    float minRadius;
    Color color;
} MonsterSDFBodyPart;

/**
 * @struct MonsterSDFConnector
 * @brief Snapshot compilado de un conector entre partes con invariantes precálculadas.
 */
typedef struct MonsterSDFConnector {
    Vector3 a;
    Vector3 b;
    Vector3 ba;
    float invBaLengthSquared;
    float r1;
    float r2;
    float radiusDelta;
    Color color;
} MonsterSDFConnector;

/**
 * @struct MonsterSDFMouth
 * @brief Snapshot compilado de una cavidad bucal con rotación matricial e invariantes precálculadas.
 */
typedef struct MonsterSDFMouth {
    Vector3 center;
    RotationBasis3D inverseRotation;
    Vector3 muzzleCenterLocal;
    Vector3 muzzleHalfExtents;
    float muzzleSmoothness;
    Color skinColor;
    Vector3 entranceCenterLocal;
    Vector3 entranceHalfExtents;
    Vector3 cavityCenterLocal;
    Vector3 cavityRadii;
    Color insideColor;
    float entranceToCavitySmoothness;
    float rimBevel;
    Vector3 jawCenterLocal;
    Vector3 jawRadii;
    Vector3 hingeCenterLocal;
    float hingeRadius;
    float throatRadius;
    float jawRearMass;
    float jawMuscle;
    bool lowerBeak; /**< Usa la receta ahusada de pico inferior. */
    Vector3 hostCenterLocal;
    Vector3 hostRadii;
    Vector3 craniumCenterLocal;
    Vector3 craniumRadii;
    Vector3 snoutCenterLocal;
    Vector3 snoutRadii;
    Vector3 cheekCenterLocal;
    Vector3 cheekRadii;
    Vector3 browCenterLocal;
    Vector3 browRadii;
    bool anatomicalHead; /**< Activa la receta compilada por HeadAnatomy. */
    Vector3 faceRootLocal; /**< Raíz local del rostro ahusado. */
    Vector3 faceTipLocal; /**< Extremo local del rostro ahusado. */
    Vector3 faceRootRadii; /**< Radios proximales del rostro. */
    Vector3 faceTipRadii; /**< Radios distales del rostro. */
    Vector3 leftOrbitCenterLocal; /**< Centro del cutter orbital izquierdo. */
    Vector3 rightOrbitCenterLocal; /**< Centro del cutter orbital derecho. */
    Vector3 orbitRadii; /**< Radios de ambos cutters orbitales. */
    Vector3 leftOrbitRimCenterLocal; /**< Centro del reborde izquierdo. */
    Vector3 rightOrbitRimCenterLocal; /**< Centro del reborde derecho. */
    Vector3 orbitRimRadii; /**< Radios externos del reborde orbital. */
    Vector3 noseCenterLocal; /**< Centro de almohadilla nasal. */
    Vector3 noseRadii; /**< Radios de almohadilla nasal. */
    Vector3 leftNostrilCenterLocal; /**< Cutter de narina izquierda. */
    Vector3 rightNostrilCenterLocal; /**< Cutter de narina derecha. */
    Vector3 nostrilRadii; /**< Radios de ambos cutters nasales. */
    Vector3 leftEarCenterLocal; /**< Centro auricular izquierdo. */
    Vector3 rightEarCenterLocal; /**< Centro auricular derecho. */
    Vector3 earRadii; /**< Radios auriculares. */
    float headUnionSmoothness; /**< Suavidad de la receta de cabeza. */
    bool hasNasalPad; /**< Incluye almohadilla nasal diferenciada. */
    bool hasEars; /**< Incluye volúmenes auriculares. */
    AABB3D influenceBounds;
    /* Anclas compartidas de costura derivadas de dimensiones host/boca */
    Vector3 seamSkullLeftLocal;      /**< Ancla craneal superior izquierda */
    Vector3 seamSkullRightLocal;     /**< Ancla craneal superior derecha */
    Vector3 seamJawLeftClosedLocal;  /**< Ancla inferior izquierda (mandíbula cerrada) */
    Vector3 seamJawRightClosedLocal; /**< Ancla inferior derecha (mandíbula cerrada) */
    Vector3 seamGularLocal;          /**< Ancla central gular/garganta */
    Vector3 seamJawAnchorLocal;      /**< Ancla central inferior */
    float seamScale;                 /**< Escala h = max(hingeRadius, throatRadius, slitThickness) */
    AABB3D seamBounds;               /**< Bounds conservadores del tejido blando */
} MonsterSDFMouth;

/** Contexto de evaluación de la mandíbula en coordenadas locales de boca. */
typedef struct MonsterSDFJawField {
    const MonsterSDF* owner;
    size_t mouthIndex;
} MonsterSDFJawField;

/** Contexto de evaluación del tejido blando de costura en coordenadas locales de boca. */
typedef struct MonsterSDFSeamField {
    const MonsterSDF* owner;
    size_t mouthIndex;
} MonsterSDFSeamField;

/** Modos de inspección del campo anatómico de la cabeza. */
typedef enum MonsterHeadDebugMode {
    MONSTER_HEAD_DEBUG_FULL = 0,
    MONSTER_HEAD_DEBUG_CRANIUM,
    MONSTER_HEAD_DEBUG_SNOUT,
    MONSTER_HEAD_DEBUG_UPPER_HEAD,
    MONSTER_HEAD_DEBUG_JAW,
    MONSTER_HEAD_DEBUG_BRIDGES,
    MONSTER_HEAD_DEBUG_CAVITY,
    MONSTER_HEAD_DEBUG_SLIT
} MonsterHeadDebugMode;

/**
 * @struct MonsterSDF
 * @brief Estructura de geometría SDF compilada (desacoplada e independiente tras Build).
 * @note No es segura para hilos: no invocar MonsterSDF_Build mientras otra hebra
 *       lee la misma instancia (Build reutiliza los buffers internos en sitio).
 */
struct MonsterSDF {
    MonsterSDFBodyPart* bodyParts;
    size_t bodyPartCount;
    size_t bodyPartCapacity; /**< Capacidad reservada del buffer bodyParts */

    MonsterSDFConnector* connectors;
    size_t connectorCount;
    size_t connectorCapacity; /**< Capacidad reservada del buffer connectors */

    MonsterSDFMouth* mouths;
    size_t mouthCount;
    size_t mouthCapacity; /**< Capacidad reservada del buffer mouths */

    MonsterSDFConfig config;
    AABB3D bounds;
};

/**
 * @brief Retorna la configuración por defecto para la evaluación SDF.
 */
MonsterSDFConfig MonsterSDF_DefaultConfig(void);

/**
 * @brief Crea una estructura MonsterSDF vacía.
 */
MonsterSDF MonsterSDF_Create(void);

/**
 * @brief Compila/Snapshot la geometría de un Monster en un objeto MonsterSDF.
 *
 * Reutiliza los buffers internos de builds previos (crecimiento únicamente),
 * por lo que puede invocarse repetidamente sobre la misma instancia sin
 * fragmentar memoria. En caso de fallo de asignación la instancia queda vacía.
 * @return true si se construyó exitosamente, false si ocurrió fallo de asignación.
 */
bool MonsterSDF_Build(MonsterSDF* sdf, const struct Monster* monster, MonsterSDFConfig config);

/**
 * @brief Libera los recursos de memoria asignados por MonsterSDF_Build.
 */
void MonsterSDF_Free(MonsterSDF* sdf);

/**
 * @brief Evalúa la distancia signed, color y material en cualquier punto 3D del espacio.
 */
SDFSample MonsterSDF_Evaluate(const MonsterSDF* sdf, Vector3 point);

/** Evalúa una capa anatómica aislada para depuración y pruebas. */
SDFSample MonsterSDF_EvaluateDebug(const MonsterSDF* sdf, Vector3 point, MonsterHeadDebugMode mode);

/** Obtiene el campo local de mandíbula compilado para una boca. */
SDFField MonsterSDF_GetJawField(const MonsterSDF* sdf, size_t mouthIndex, MonsterSDFJawField* context);

/** Obtiene el campo local de tejido blando de costura para una boca. */
SDFField MonsterSDF_GetSeamField(const MonsterSDF* sdf, size_t mouthIndex, MonsterSDFSeamField* context);

/**
 * @brief Wrapper de evaluación completa compatible con la firma SDFEvaluateFn.
 */
SDFSample MonsterSDF_EvaluateWrapper(const void* context, Vector3 point);

/**
 * @brief Evalúa única y exclusivamente la distancia escalar en cualquier punto 3D del espacio.
 */
float MonsterSDF_EvaluateDistance(const MonsterSDF* sdf, Vector3 point);

/**
 * @brief Wrapper de evaluación de sólo distancia escalar compatible con la firma SDFDistanceFn.
 */
float MonsterSDF_EvaluateDistanceWrapper(const void* context, Vector3 point);

/**
 * @brief Retorna el Bounding Box (AABB3D) compilado.
 */
AABB3D MonsterSDF_GetBounds(const MonsterSDF* sdf);

/**
 * @brief Wrapper de bounds compatible con la firma SDFBoundsFn.
 */
AABB3D MonsterSDF_GetBoundsWrapper(const void* context);

/**
 * @brief Construye y retorna la estructura agnóstica SDFField vinculada a esta instancia.
 */
SDFField MonsterSDF_GetField(const MonsterSDF* sdf);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_SDF_H
