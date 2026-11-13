/** @file LongHair.h
 * @brief Grupos anatómicos y guías de pelo largo; sin recursos gráficos ni fibras persistentes. */
#ifndef MONSTER_LONG_HAIR_H
#define MONSTER_LONG_HAIR_H
#include "Anatomy.h"
#include "Color.h"
#include "Skeleton.h"
typedef struct LongHairScheduler LongHairScheduler;
#define LONG_HAIR_MAX_GROUPS 4
#define LONG_HAIR_MAX_ANCHORS 8
#define LONG_HAIR_POINTS 10
#define LONG_HAIR_GPU_ROWS 80
#define LONG_HAIR_BUILD_PHASES 3
#define LONG_HAIR_BUILD_MAX_THREADS 8
/** @brief Medidas optativas de construcción; sin este puntero no se lee el reloj ni se cuentan consultas. */
typedef struct LongHairBuildStats {
    uint64_t distanceCalls[LONG_HAIR_BUILD_PHASES][LONG_HAIR_BUILD_MAX_THREADS];
    double phaseMilliseconds[LONG_HAIR_BUILD_PHASES];
    int participants;
} LongHairBuildStats;
typedef enum LongHairDomainKind { LONG_HAIR_CURVE, LONG_HAIR_PATCH, LONG_HAIR_REGION } LongHairDomainKind;
/** @brief Coordenadas anatómicas estables, no índices de una triangulación. */
typedef struct LongHairDomain {
    LongHairDomainKind kind;
    uint32_t hostModuleId;
    AnatomyRegion region;
    AnatomyId anchors[LONG_HAIR_MAX_ANCHORS];
    size_t anchorCount;
    Vector3 offset, normal;
    float angle, angularSpread, start, end, patchRadius;
} LongHairDomain;
/** @brief Descriptor pequeño serializable; longitudes relativas a la escala corporal. */
typedef struct LongHairGroup {
    uint32_t id, seed;
    LongHairDomain domain;
    unsigned guideCount, childrenPerGuide;
    Vector3 direction;
    float length, lengthVariation, width, rootSpread, clumping, stiffness, gravityInfluence, density;
    float rootFullness; /**< Distribución de folículos entre guías de curva; apertura en otros dominios [0,1]. */
    float bend, wave; /**< Curvatura lateral firmada [-2,2] y ondulación [0,2]. */
    float transparency; /**< Transparencia de cada fibra [0,1], independiente de la densidad. */
    float verticalBias; /**< Dirección vertical en reposo: -1 caída, 0 peinado original, 1 erguido. */
    Color rootColor, tipColor;
} LongHairGroup;
/** @brief Raíz con propiedad y parámetro estable, recuperable tras remallado. */
typedef struct LongHairRoot {
    uint32_t groupId, moduleId, stableRootId;
    AnatomyId fromId, toId;
    Vector3 restPosition, restNormal, restDirection;
    float rootParameter, segmentParameter;
} LongHairRoot;
typedef struct LongHairGuide {
    LongHairRoot root;
    Vector3 restPoints[LONG_HAIR_POINTS], points[LONG_HAIR_POINTS];
    Vector3 restSide, posedSide, posedNormal; /**< Marco cutáneo, no derivado de una tangente casi paralela. */
    Vector3 corrections[LONG_HAIR_POINTS]; /**< Desplazamientos del último contacto, para diagnóstico. */
    float clearance[LONG_HAIR_POINTS]; /**< Radio libre conservador para hijos GPU. */
    float restClearance[LONG_HAIR_POINTS],restPenetration;
    float rootClearance,restRootClearance; /**< Holgura puntual del primer control, para abrir el primer tramo. */
    float length;
    float contactPenetration; /**< Residuo máximo del último campo, sin ocultar fallos. */
    float visibility; /**< Cobertura continua cuando otra superficie tapa la inserción. */
    uint32_t attachmentJoint; /**< Articulación superficial auxiliar; cero usa las estaciones. */
    unsigned groupIndex;
    bool visible, rootValid;
    float follicles[64][4]; /**< Guía vecina, peso, proyección normal y validez de cada folículo GPU. */
} LongHairGuide;
/** @brief Envolvente elipsoidal de estación o barrido entre dos estaciones. */
typedef struct LongHairCollider {
    AnatomyId id, endId;
    Vector3 center, radii, end, endRadii;
    Quaternion inverseRotation;
    float surfaceOffset;
} LongHairCollider;
typedef float (*LongHairDistance)(const void*,Vector3);
/** @brief Propiedad exclusiva del runtime visual; nunca se copia al fenotipo. */
typedef struct LongHairSet {
    LongHairGuide* guides;
    size_t count;
    LongHairGroup groups[LONG_HAIR_MAX_GROUPS];
    size_t groupCount;
    float scale;
    uint64_t fingerprint, generation, poseGeneration;
    LongHairCollider* colliders;
    size_t colliderCount;
    LongHairDistance attachedDistance; /**< Campo en coordenadas mundiales de reposo; opcional. */
    void* attachedContext;
    void (*freeAttachedContext)(void*);
    size_t attachedContextBytes;
    uint32_t collisionJoint; /**< Articulación del campo; cero mantiene un campo mundial estático. */
    LongHairScheduler* scheduler; /**< Pool privado opcional; propiedad exclusiva de este runtime. */
} LongHairSet;
/** @brief Configuración neutra válida. @return Descriptor sin anfitrión. */
LongHairGroup LongHairGroup_Default(void);
/** @brief Sanea límites y valores no finitos. @param group Descriptor mutable. */
void LongHairGroup_Normalize(LongHairGroup* group);
/** @brief Huella de controles explícitos, sin leer padding ni reservas. */
uint64_t LongHairGroups_Fingerprint(const LongHairGroup* groups,size_t count);
/** @brief Construcción transaccional determinista; anfitriones ausentes se retiran.
 * @param distance Proyección opcional al campo actual, nunca a una malla obsoleta.
 * @return false si falla validación o asignación; conserva el runtime anterior. */
bool LongHairSet_Build(LongHairSet* set,const LongHairGroup* groups,size_t count,const AnatomyGraph* graph,
    float scale,LongHairDistance distance,const void* context);
/** @brief Igual que Build, pero opt-in: callback y contexto deben ser inmutables/thread-safe.
 * @param set Runtime mutable; el scheduler no se transfiere a este runtime.
 * @param scheduler Scheduler prestado, privado del llamador; NULL conserva ejecución serial.
 * @return false ante error; conserva el runtime previo. Ningún worker retiene callback,
 *         scheduler ni contexto al retornar (join síncrono de todas las fases). El callback
 *         no debe destruir el scheduler ni liberar/modificar el runtime propietario.
 * @warning Build/BuildParallel/Pose/Free requieren exclusión externa para el mismo set.
 *          Antes de Free, impedir nuevos submitters/lectores y hacer join de submitters
 *          existentes; Free destruye el scheduler runtime-owned. Nunca liberar el set desde
 *          un callback activo. El caller conserva ownership del scheduler prestado. */
bool LongHairSet_BuildParallel(LongHairSet* set,const LongHairGroup* groups,size_t count,const AnatomyGraph* graph,
    float scale,LongHairDistance distance,const void* context,LongHairScheduler* scheduler);
/** @brief Variante de perfilado optativo; añade costes de reloj/contadores por fase y partición.
 * `stats` se sobrescribe; los hilos sólo escriben su propia ranura. */
bool LongHairSet_BuildParallelProfiled(LongHairSet* set,const LongHairGroup* groups,size_t count,const AnatomyGraph* graph,
    float scale,LongHairDistance distance,const void* context,LongHairScheduler* scheduler,LongHairBuildStats* stats);
/** @brief Libera memoria y pool propios. @param set Runtime, admite NULL.
 * @warning Requiere quiescencia externa: bloquear nuevas llamadas y hacer join de submitters
 *          y lectores activos antes de destruir el runtime. Nunca llamar desde un callback. */
void LongHairSet_Free(LongHairSet* set);
/** @brief Transporta guías por las dos estaciones propietarias, sin regenerar hijos.
 * @return false ante pose incompatible; las guías no válidas quedan ocultas. */
bool LongHairSet_Pose(LongHairSet* set,const Skeleton* skeleton,const SkeletonPose* pose);
/** @brief Ajusta guías y colisionadores existentes a un grafo anatómico reciente sin reconstruir folículos.
 * @return true si el runtime se actualizó o no tenía guías activas. */
bool LongHairSet_TransportToAnatomyGraph(LongHairSet* set,const AnatomyGraph* graph,float scale);
/** @brief Resuelve contactos del tubo guía/hijos contra un campo actual; raíz fija.
 * @param set Runtime mutable; mantiene raíces e identidades.
 * @param distance Distancia firmada en coordenadas de las guías.
 * @param context Contexto prestado, nunca retenido. */
void LongHairSet_Collide(LongHairSet* set,LongHairDistance distance,const void* context);
/** @brief Radio máximo de los hijos, compartido con Child() en GLSL.
 * @param group Descriptor normalizado del grupo.
 * @param scale Escala anatómica positiva.
 * @param parameter Parámetro de curva entre cero y uno.
 * @return Radio solicitado del tubo, incluido el grosor físico. */
float LongHairGroup_Envelope(const LongHairGroup* group,float scale,float parameter);
/** @brief Catmull-Rom común para CPU y ribbons. @return Punto suave de la guía. */
Vector3 LongHairGuide_Evaluate(const LongHairGuide* guide,float parameter);
/** @brief Empaqueta sólo guías y controles, no vértices de hijos. @return Filas escritas. */
size_t LongHairSet_Pack(const LongHairSet* set,float (*rows)[4],size_t capacity);
#endif
