/** @file EcosystemAnatomy.h
 * @brief Cuerpo oral anatómico compartido por simulación y presentación, sin mallas. */
#ifndef ECOSYSTEM_ANATOMY_H
#define ECOSYSTEM_ANATOMY_H
#include "EcosystemWorld.h"
#include "MonsterAnimation.h"
#include "CreaturePhenotype.h"
typedef struct EcosystemAnatomy {
    Rig rig;
    AnatomyGraph graph;
    CreaturePhenotype phenotype;
    SkeletonPose rest, pose;
    AnatomicalReachProfile profile;
    AnatomicalReachState state;
    Vector3 center;
    float modelLength, standingHeight, crouch;
    unsigned stage, bucket;
    double growthSample;
    unsigned generation;
} EcosystemAnatomy;
/** @brief Construye/reconfigura únicamente cuando cambia la plantilla corporal. */
bool EcosystemAnatomy_Configure(EcosystemEntity*,const EcosystemSpecies*);
/** @brief Devuelve la longitud de referencia axial, independiente de LOD y cola. */
float EcosystemAnatomy_ModelLength(const CreaturePhenotype*);
/** @brief Escala del cuerpo en el mundo ecológico. */
float EcosystemAnatomy_Scale(const EcosystemAnatomy*,const EcosystemEntity*);
/** @brief Resuelve la boca real con límites articulares; publica el contacto autoritativo. */
void EcosystemAnatomy_Update(EcosystemEntity*,float);
#endif
