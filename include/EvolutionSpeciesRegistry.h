/**
 * @file EvolutionSpeciesRegistry.h
 * @brief Configuración genérica y reutilizable de especies por datos (`species/NOMBRE.species.json`).
 *
 * Asocia receta anatómica, perfil metabólico, sustancia tisular de cadáver, parámetros
 * físicos, fisiológicos, reproductivos, perceptivos y cerebro inicial opcional sin
 * ramas codificadas por especie en el núcleo neuronal o metabólico.
 */
#ifndef EVOLUTION_SPECIES_REGISTRY_H
#define EVOLUTION_SPECIES_REGISTRY_H

#include "EcosystemWorld.h"
#include "MetabolismIO.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVOLUTION_SPECIES_SCHEMA_VERSION 1u
#define EVOLUTION_SPECIES_MAX_COUNT 32u

typedef struct EvolutionSpeciesConfig {
    uint32_t schemaVersion;
    char id[48];
    char name[64];
    char recipePath[256];
    uint32_t recipeId;
    char metabolismProfileId[METABOLIC_PROFILE_ID_MAX];
    char tissueSubstanceId[SUBSTANCE_ID_MAX];
    float tissueMass;
    char startingBrainPath[256];
    bool hasStartingBrain;

    /* Capacidades físicas/mecánicas */
    float speed;
    float radius;
    float oralReach;
    float attackRange;
    float damage;
    float attackCooldown;
    float attackCost;
    float turnRate;
    bool canAttackPhysically;
    bool waterInteraction;
    bool anatomicalOral;
    bool groundLocomotion;
    TraversalProfile traversal;
    bool hasCustomTraversal;

    /* Fisiología, reproducción y percepción */
    PhysiologyProfile physiology;
    ReproductionProfile reproduction;
    PerceptionProfile perception;

    uint64_t fingerprint;
    bool valid;
} EvolutionSpeciesConfig;

/** @brief Inicializa valores base seguros antes de cargar un JSON de especie. */
void EvolutionSpeciesConfig_InitDefault(EvolutionSpeciesConfig *cfg);

/** @brief Carga y valida estrictamente un archivo `*.species.json` desde memoria. */
bool EvolutionSpeciesConfig_DeserializeJSON(const char *json, size_t length,
                                            EvolutionSpeciesConfig *out,
                                            char *error, size_t errorSize);

/** @brief Carga un archivo `*.species.json` desde disco. */
bool EvolutionSpeciesConfig_LoadFile(const char *path, EvolutionSpeciesConfig *out,
                                     char *error, size_t errorSize);

/** @brief Calcula huella canónica FNV-1a de la definición de especie. */
uint64_t EvolutionSpeciesConfig_Fingerprint(const EvolutionSpeciesConfig *cfg);

/**
 * @brief Compila una definición de especie contra una base metabólica congelada y
 *        el registro de recetas anatómicas en un @ref EcosystemSpecies ejecutable.
 */
bool EvolutionSpeciesConfig_BuildEcosystemSpecies(const EvolutionSpeciesConfig *cfg,
                                                  const MetabolismDatabase *db,
                                                  unsigned numericSpeciesId,
                                                  EcosystemSpecies *outSpecies,
                                                  char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif

#endif /* EVOLUTION_SPECIES_REGISTRY_H */
