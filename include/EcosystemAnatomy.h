/** @file EcosystemAnatomy.h
 * @brief Cuerpo oral anatómico ligero respaldado por plantillas compartidas, sin mallas. */
#ifndef ECOSYSTEM_ANATOMY_H
#define ECOSYSTEM_ANATOMY_H
#include "EcosystemWorld.h"
#include "EcosystemAnatomyCache.h"
#include "EvolutionProfiler.h"

typedef struct EcosystemAnatomy {
    const EcosystemAnatomyTemplate *tmpl; /**< Plantilla inmutable compartida (propiedad por refCount). */
    AnatomicalReachState state;
    Vector3 cervicalWorldPos[REACH_MAX_JOINTS];
    Quaternion cervicalWorldRot[REACH_MAX_JOINTS];
    Vector3 center;
    float modelLength, standingHeight, crouch;
    unsigned stage, bucket, growthStep;
    double growthSample;
    unsigned generation;
    bool settled;
} EcosystemAnatomy;

/** @brief Construye/reconfigura usando plantillas compartidas sólo cuando cambia etapa/muestra/cubeta. */
bool EcosystemAnatomy_Configure(EcosystemEntity *, const EcosystemSpecies *);
/** @brief Variante instrumentada de configuración anatómica. */
bool EcosystemAnatomy_ConfigureProfiled(EcosystemEntity *, const EcosystemSpecies *, EvolutionProfiler *);
/** @brief Libera el estado mutable y su referencia a la plantilla compartida (seguro con NULL). */
void EcosystemAnatomy_Destroy(EcosystemAnatomy *);
/** @brief Invalida el estado cervical cacheado (p. ej. tras asignar/intercambiar cerebro). */
void EcosystemAnatomy_InvalidateInteraction(EcosystemAnatomy *);
/** @brief Devuelve la longitud de referencia axial, independiente de LOD y cola. */
float EcosystemAnatomy_ModelLength(const CreaturePhenotype *);
/** @brief Escala del cuerpo en el mundo ecológico. */
float EcosystemAnatomy_Scale(const EcosystemAnatomy *, const EcosystemEntity *);
/** @brief Resuelve la boca real con límites articulares; publica el contacto autoritativo. */
void EcosystemAnatomy_Update(EcosystemEntity *, float);
/** @brief Variante instrumentada de actualización anatómica (camino rápido + cadena cervical compacta). */
void EcosystemAnatomy_UpdateProfiled(EcosystemEntity *, float, EvolutionProfiler *);
/** @brief Aplica las rotaciones cervicales autoritativas actuales sobre una pose visual completa. */
void EcosystemAnatomy_ApplyCervicalPose(const EcosystemAnatomy *, const Rig *, SkeletonPose *);
/** @brief Accesos de sólo lectura a la plantilla anatómica compartida. */
static inline const Rig *EcosystemAnatomy_Rig(const EcosystemAnatomy *a) {
    return (a && a->tmpl) ? &a->tmpl->rig : NULL;
}
static inline const AnatomyGraph *EcosystemAnatomy_Graph(const EcosystemAnatomy *a) {
    return (a && a->tmpl) ? &a->tmpl->graph : NULL;
}
static inline const CreaturePhenotype *EcosystemAnatomy_Phenotype(const EcosystemAnatomy *a) {
    return (a && a->tmpl) ? &a->tmpl->phenotype : NULL;
}
static inline const SkeletonPose *EcosystemAnatomy_RestPose(const EcosystemAnatomy *a) {
    return (a && a->tmpl) ? &a->tmpl->rest : NULL;
}
static inline const AnatomicalReachProfile *EcosystemAnatomy_ReachProfile(const EcosystemAnatomy *a) {
    return (a && a->tmpl) ? &a->tmpl->profile : NULL;
}
#endif
