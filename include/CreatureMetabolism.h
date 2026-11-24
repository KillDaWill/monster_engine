/**
 * @file CreatureMetabolism.h
 * @brief Ingestión física, digestión progresiva, absorción, desechos y toxicidad.
 *
 * Separa estrictamente el evento físico de ingestión (transferencia de masa al estómago)
 * de la digestión y consecuencias fisiológicas progresivas (energía, hidratación,
 * nutrientes, malestar, toxicidad y daño metabólico).
 */
#ifndef CREATURE_METABOLISM_H
#define CREATURE_METABOLISM_H

#include "CreaturePhysiology.h"
#include "MetabolicProfile.h"
#include "MetabolicState.h"
#include "SubstanceRegistry.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Resultado exacto de un paso de actualización metabólica para reducción estadística. */
typedef struct MetabolicStepOutcome {
    float processedMass;
    float absorbedMass;
    float rejectedMass;
    float wasteEliminated;
    float energyGenerated;
    float hydrationGenerated;
    float nutrientsAbsorbed;
    float digestiveEnergyCost;
    float toxicityAdded;
    float detoxified;
    float toxicityDelta;
    float metabolicDamage;
    bool diedFromToxicity;
} MetabolicStepOutcome;

/** @brief Inicializa el estado metabólico de un cuerpo según su perfil compilado. */
void CreatureMetabolism_Init(MetabolicState *state, const MetabolicProfile *profile,
                             float initialNutrients);

/**
 * @brief Calcula la masa física máxima que el cuerpo puede aceptar en un paso `dt`.
 *
 * Acota por `maximumIngestionRate * dt` y por la capacidad digestiva libre restante.
 */
float CreatureMetabolism_MaxAcceptableMass(const MetabolicState *state,
                                           const MetabolicProfile *profile,
                                           float requestedMass, float dt);

/**
 * @brief Transfiere exactamente @p acceptedMass unidades de la mezcla al tracto digestivo.
 *
 * No otorga energía ni hidratación inmediata: las consecuencias emergen en @ref CreatureMetabolism_Step.
 */
bool CreatureMetabolism_Ingest(MetabolicState *state,
                               const MetabolicProfile *profile,
                               SubstanceHandle substanceHandle,
                               const SubstanceComponent *components,
                               unsigned componentCount,
                               float acceptedMass,
                               double nowSeconds);

/**
 * @brief Ejecuta un paso fijo de digestión, absorción, eliminación de residuos,
 *        detoxificación e integración fisiológica sobre @ref CreaturePhysiology.
 *
 * Ruta rápida O(1) cuando el tracto digestivo está vacío y sin toxicidad/malestar.
 */
void CreatureMetabolism_Step(MetabolicState *state,
                             const MetabolicProfile *profile,
                             CreaturePhysiology *physiology,
                             const PhysiologyProfile *physProfile,
                             float dt,
                             MetabolicStepOutcome *outOutcome);

#ifdef __cplusplus
}
#endif

#endif /* CREATURE_METABOLISM_H */
