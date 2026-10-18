/** @file CreatureNeeds.h
 * @brief Inteligencia modular independiente del render y de la anatomía. */
#ifndef CREATURENEEDS_H
#define CREATURENEEDS_H
#include "BrainProfile.h"
typedef struct CreatureNeeds {
    float urgency[NEED_COUNT];
} CreatureNeeds;
/** @brief Integra urgencia (1 máxima); dt=0 conserva el estado durante pausa.
 */
void CreatureNeeds_Update(CreatureNeeds *, const BrainProfile *, float dt);
/** @brief Satisface gradualmente una necesidad habilitada. */
void CreatureNeeds_Satisfy(CreatureNeeds *, const BrainProfile *, CreatureNeed, float amount);
/** @brief Evalúa la curva configurable de prioridad. */
float CreatureNeeds_Utility(const CreatureNeeds *, const BrainProfile *, CreatureNeed);

#endif
