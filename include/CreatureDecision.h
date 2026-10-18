/** @file CreatureDecision.h
 * @brief Inteligencia modular independiente del render y de la anatomía. */
#ifndef CREATUREDECISION_H
#define CREATUREDECISION_H
#include "CreatureMind.h"
/** @brief Selección utility sobre memoria individual; no descubre recursos
 * globales. */
void CreatureDecision_Update(CreatureMind *, const AIWorld *, const CreatureBodyState *);

#endif
