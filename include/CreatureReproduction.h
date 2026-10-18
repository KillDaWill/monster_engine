/** @file CreatureReproduction.h
 * @brief Condiciones reproductivas y gestación biparental con reserva exclusiva. */
#ifndef CREATURE_REPRODUCTION_H
#define CREATURE_REPRODUCTION_H
#include "CreatureGenome.h"
#include "CreaturePhysiology.h"
#include "AIWorld.h"
typedef struct ReproductionProfile {
    double cooldown, gestation, maximumAge;
    unsigned offspring;
    float minimumEnergy, minimumHealth, energyCost, mutationProbability, mutationAmplitude;
} ReproductionProfile;
typedef struct CreatureReproduction {
    AIObjectId partner, parents[2], gestationParents[2];
    CreatureGenome mateGenome;
    double cooldownUntil, due;
    bool gestating;
    unsigned role, mateGeneration;
} CreatureReproduction;
/** @brief Perfil sexual biparental; role=0 gesta y role=1 fecunda. */
ReproductionProfile CreatureReproduction_Default(void);
/** @brief Valida costes, temporizadores y probabilidades. */
bool CreatureReproduction_ValidateProfile(const ReproductionProfile *);
/** @brief Condiciones individuales; la aceptación del otro individuo se valida aparte. */
bool CreatureReproduction_Ready(const CreatureReproduction *,const ReproductionProfile *,
    const CreaturePhysiology *,double now);
#endif
