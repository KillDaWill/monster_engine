/** @file CreatureActions.h
 * @brief Inteligencia modular independiente del render y de la anatomía. */
#ifndef CREATUREACTIONS_H
#define CREATUREACTIONS_H
#include "CreatureMind.h"
/** @brief Ejecuta estado persistente y genera intención sin tocar anatomía. */
void CreatureActions_Update(CreatureMind *, const AIWorld *, const CreatureBodyState *, float dt);
/** @brief Cancela sin penalizar recursos retirados ni consumir. */
void CreatureActions_Cancel(CreatureMind *);
/** @brief Fallo con cooldown de target y limpieza de navegación. */
void CreatureActions_Fail(CreatureMind *);

#endif
