/** @file EcosystemResources.h
 * @brief Libro de biomasa y agua con transferencias conservativas. */
#ifndef ECOSYSTEM_RESOURCES_H
#define ECOSYSTEM_RESOURCES_H
#include "AIWorld.h"
enum { ECO_PLANT=1u, ECO_MEAT=2u, ECO_WATER=4u };
typedef struct EcosystemResource {
    AIObjectId id;
    unsigned category, originalSpecies;
    float biomass, maximumBiomass, regenerationRate, nutritionalValue;
    double remainingLifetime;
    bool infinite, accessible;
} EcosystemResource;
/** @brief Regenera sin exceder capacidad; cadáveres decaen hasta agotarse. */
void EcosystemResource_Update(EcosystemResource *,float dt);
/** @brief Devuelve cantidad real retirada. El llamador valida contacto y ordena solicitudes. */
float EcosystemResource_Consume(EcosystemResource *,float requested);
/** @brief Disponibilidad actual, también para agua explícitamente infinita. */
bool EcosystemResource_Available(const EcosystemResource *);
#endif
