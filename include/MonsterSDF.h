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
#include "Anatomy.h"
#include "SDFPrimitives.h"
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
    bool enableConnectorPruning;   /**< Poda conservadora de conectores para acelerar el muestreo (defecto true) */
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
    Vector3 forward, side, up; /**< Marco de segmento precalculado. */
    float length; /**< Longitud del segmento. */
    float invBaLengthSquared;
    float r1;
    float r2;
    float radiusDelta;
    float widthA;
    float heightA;
    float widthB;
    float heightB;
    AnatomyId fromId;
    AnatomyId toId;
    BodyConnectionKind kind;
    Color color;
    AABB3D bounds;             /**< AABB que encierra el volumen conservador del conector */
    float distanceLowerBoundScale; /**< Cota conservadora de anisotropía para poda. */
    size_t groupCount; /**< Conectores consecutivos del mismo miembro; cero fuera del inicio. */
    AABB3D groupBounds; /**< Caja conservadora de la rama completa. */
    float groupLowerBoundScale, groupSmoothness; /**< Cotas para descartar la rama sin cambiar el orden. */
    float maxRadius;           /**< Radio máximo de sección transversal */
    float localSmoothness;     /**< Suavizado local de unión suave precalculado */
} MonsterSDFConnector;

/**
 * @struct MonsterSDFMouth
 * @brief Snapshot compilado de una cavidad bucal con rotación matricial e invariantes precálculadas.
 */
typedef struct MonsterSDFMouth {
    Vector3 center;
    AABB3D visualBounds; /**< Caja mundial conservadora de mandíbula y costura articuladas. */
    Vector3 visualJawPivot; /**< Pivote de articulación del snapshot visual. */
    float visualJawAngle, visualJawScale, visualJawBoundRadius; /**< Ángulo en radianes y desarrollo de mandíbula. */
    RotationBasis3D inverseRotation;
    Vector3 muzzleCenterLocal;
    Vector3 muzzleHalfExtents;
    float muzzleSmoothness;
    float cephalicDevelopment; /**< Peso continuo de rasgos cefálicos sobre la forma larvaria simple. */
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
    Vector3 leftTemporalCenterLocal;
    Vector3 rightTemporalCenterLocal;
    Vector3 temporalRadii;
    Vector3 leftMaxillaryCenterLocal;
    Vector3 rightMaxillaryCenterLocal;
    Vector3 maxillaryRadii;
    Vector3 leftBrowCenterLocal;
    Vector3 rightBrowCenterLocal;
    Vector3 browRadii;
    bool sweptSkull; /**< Cráneo y rostro del lagarto comparten un único barrido. */
    SDFSweepStation headStations[6]; /**< Perfil cefálico resuelto. */
    bool anatomicalHead; /**< Activa la receta compilada por HeadAnatomy. */
    Vector3 faceRootLocal; /**< Raíz local del rostro ahusado. */
    Vector3 faceMidLocal; /**< Sección nasal local intermedia. */
    Vector3 faceTipLocal; /**< Extremo local del rostro ahusado. */
    Vector3 faceRootRadii; /**< Radios proximales del rostro. */
    Vector3 faceMidRadii; /**< Radios de la sección nasal. */
    Vector3 faceTipRadii; /**< Radios distales del rostro. */
    Vector3 leftOrbitCenterLocal; /**< Centro del cutter orbital izquierdo. */
    Vector3 rightOrbitCenterLocal; /**< Centro del cutter orbital derecho. */
    Vector3 orbitRadii; /**< Radios de ambos cutters orbitales. */
    Vector3 leftOrbitRimCenterLocal; /**< Centro del reborde izquierdo. */
    Vector3 rightOrbitRimCenterLocal; /**< Centro del reborde derecho. */
    Vector3 orbitRimRadii; /**< Radios externos del reborde orbital. */
    Vector3 leftOrbitNormal;
    Vector3 rightOrbitNormal;
    float orbitSocketDepth;
    Vector3 noseCenterLocal; /**< Centro de almohadilla nasal. */
    Vector3 noseRadii; /**< Radios de almohadilla nasal. */
    Vector3 leftNostrilCenterLocal; /**< Cutter de narina izquierda. */
    Vector3 rightNostrilCenterLocal; /**< Cutter de narina derecha. */
    Vector3 nostrilRadii; /**< Radios de ambos cutters nasales. */
    Vector3 leftTympanumCenterLocal;
    Vector3 rightTympanumCenterLocal;
    Vector3 tympanumRadii;
    float tympanumDepth;
    Vector3 leftEarCenterLocal; /**< Centro auricular izquierdo. */
    Vector3 rightEarCenterLocal; /**< Centro auricular derecho. */
    Vector3 earRadii; /**< Radios auriculares. */
    float headUnionSmoothness; /**< Suavidad de la receta de cabeza. */
    float headBodySmoothness; /**< Suavidad local de la transición cefalocervical. */
    Vector3 neckCollarRootLocal; /**< Inicio occipital del collar local. */
    Vector3 neckCollarTipLocal; /**< Extremo del collar enterrado en el cuello corporal. */
    Vector3 neckCollarRootRadii; /**< Radios occipitales del collar. */
    Vector3 neckCollarTipRadii; /**< Radios terminales, menores que el cuello corporal. */
    bool hasNasalPad; /**< Incluye almohadilla nasal diferenciada. */
    bool hasEars; /**< Incluye volúmenes auriculares. */
    bool hasTympana;
    bool taperedMandible;
    float faceRounding;
    AABB3D influenceBounds;
    AABB3D headBounds; /**< AABB mundial ajustada al campo local de cabeza. */
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

/** Contexto del cuerpo grueso sin duplicar la cabeza anatómica. */
typedef struct MonsterSDFBodyField {
    const MonsterSDF* owner;
} MonsterSDFBodyField;

/** Contexto mundial del campo local de cabeza anatómica. */
typedef struct MonsterSDFHeadField {
    const MonsterSDF* owner;
    size_t mouthIndex;
} MonsterSDFHeadField;

/** Modos de inspección del campo anatómico de la cabeza. */
typedef enum MonsterHeadDebugMode {
    MONSTER_HEAD_DEBUG_FULL = 0,
    MONSTER_HEAD_DEBUG_CRANIUM,
    MONSTER_HEAD_DEBUG_SNOUT,
    MONSTER_HEAD_DEBUG_UPPER_HEAD,
    MONSTER_HEAD_DEBUG_JAW,
    MONSTER_HEAD_DEBUG_BRIDGES,
    MONSTER_HEAD_DEBUG_CAVITY,
    MONSTER_HEAD_DEBUG_SLIT,
    MONSTER_HEAD_DEBUG_ROSTRUM,
    MONSTER_HEAD_DEBUG_ORBIT_CAVITIES,
    MONSTER_HEAD_DEBUG_PERIORBITAL,
    MONSTER_HEAD_DEBUG_NOSTRILS,
    MONSTER_HEAD_DEBUG_LOCAL_HEAD
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

    SDFSweepStation axialStations[16]; /**< Receta axial continua del lagarto. */
    int axialStationCount; /**< Cero conserva la ruta heredada. */
    float appendageDevelopment; /**< Desarrollo compilado usado para presupuestar detalle local. */
    MonsterSDFConfig config;
    AABB3D bounds;
    AABB3D bodyBounds; /**< Bounds del cuerpo grueso particionado. */
    bool hasPartitionedHead; /**< Existe una cabeza local que no debe duplicarse en cuerpo. */
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

/** Obtiene el cuerpo grueso; excluye cabeza anatómica y conector HEAD -> NECK. */
SDFField MonsterSDF_GetBodyField(const MonsterSDF* sdf, MonsterSDFBodyField* context);

/** Obtiene la cabeza anatómica mundial, incluidos cutters y collar posterior. */
SDFField MonsterSDF_GetHeadField(const MonsterSDF* sdf, size_t mouthIndex, MonsterSDFHeadField* context);

/**
 * @brief Wrapper de evaluación completa compatible con la firma SDFEvaluateFn.
 */
SDFSample MonsterSDF_EvaluateWrapper(const void* context, Vector3 point);

/**
 * @brief Evalúa única y exclusivamente la distancia escalar en cualquier punto 3D del espacio.
 */
float MonsterSDF_EvaluateDistance(const MonsterSDF* sdf, Vector3 point);

/** @brief Campo corporal más mandíbula y bisagra articuladas del mismo snapshot.
 * @param sdf Geometría compilada del fotograma.
 * @param point Punto mundial.
 * @return Distancia al volumen visual completo, excluidos ojos primitivos.
 */
float MonsterSDF_EvaluateVisualDistance(const MonsterSDF* sdf, Vector3 point);

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

/**
 * @brief Obtiene las cajas AABB de influencia geométrica conservadora de todos los componentes del monstruo.
 * @param sdf Puntero al MonsterSDF compilado.
 * @param outBoxes Arreglo de salida donde se escribirán las AABBs.
 * @param capacity Capacidad máxima del arreglo outBoxes.
 * @return Cantidad de cajas escritas.
 */
size_t MonsterSDF_GetComponentBounds(const MonsterSDF* sdf, AABB3D* outBoxes, size_t capacity);

/** Capacidad compartida: cuatro cabezas de siete rasgos y cuatro pares autopodio/distal. */
#define MONSTER_SDF_DETAIL_REGION_CAPACITY (7 * 4 + 2 * 4)

/** @brief Deriva regiones mundiales por escala de rasgo y muestras por diámetro. */
size_t MonsterSDF_GetDetailRegions(const MonsterSDF* sdf, float samplesPerDiameter,
    SDFDetailRegion* regions, size_t capacity);

/**
 * @brief Habilita/deshabilita la recolección de estadísticas de poda en el hilo actual.
 */
void MonsterSDF_EnableThreadStats(bool enable);

/**
 * @brief Reinicia los contadores de poda de conectores en el hilo actual.
 */
void MonsterSDF_ResetThreadStats(void);

/**
 * @brief Obtiene los contadores de poda acumulados en el hilo actual.
 */
void MonsterSDF_GetThreadStats(size_t* outCandidate, size_t* outExact, size_t* outPruned);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_SDF_H
