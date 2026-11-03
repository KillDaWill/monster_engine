/** @file LongHair.h
 * @brief Grupos anatómicos y guías de pelo largo; sin recursos gráficos ni fibras persistentes. */
#ifndef MONSTER_LONG_HAIR_H
#define MONSTER_LONG_HAIR_H
#include "Anatomy.h"
#include "Color.h"
#include "Skeleton.h"
#define LONG_HAIR_MAX_GROUPS 4
#define LONG_HAIR_MAX_ANCHORS 8
#define LONG_HAIR_POINTS 10
#define LONG_HAIR_GPU_ROWS 16
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
    Vector3 posedNormal;
    float length;
    unsigned groupIndex;
    bool visible, rootValid;
} LongHairGuide;
/** @brief Propiedad exclusiva del runtime visual; nunca se copia al fenotipo. */
typedef struct LongHairSet {
    LongHairGuide* guides;
    size_t count;
    LongHairGroup groups[LONG_HAIR_MAX_GROUPS];
    size_t groupCount;
    float scale;
    uint64_t fingerprint, generation;
} LongHairSet;
typedef float (*LongHairDistance)(const void*,Vector3);
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
/** @brief Libera sólo memoria propia. @param set Runtime, admite NULL. */
void LongHairSet_Free(LongHairSet* set);
/** @brief Transporta guías por las dos estaciones propietarias, sin regenerar hijos.
 * @return false ante pose incompatible; las guías no válidas quedan ocultas. */
bool LongHairSet_Pose(LongHairSet* set,const Skeleton* skeleton,const SkeletonPose* pose);
/** @brief Catmull-Rom común para CPU y ribbons. @return Punto suave de la guía. */
Vector3 LongHairGuide_Evaluate(const LongHairGuide* guide,float parameter);
/** @brief Empaqueta sólo guías y controles, no vértices de hijos. @return Filas escritas. */
size_t LongHairSet_Pack(const LongHairSet* set,float (*rows)[4],size_t capacity);
#endif
