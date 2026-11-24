/** @file EcosystemResources.h
 * @brief Libro de recursos ambientales: modo legado categórico y modo molecular extensible. */
#ifndef ECOSYSTEM_RESOURCES_H
#define ECOSYSTEM_RESOURCES_H
#include "AIWorld.h"
#include "CreaturePhysiology.h"
#include "MoleculeRegistry.h"
#include "SubstanceRegistry.h"

enum { ECO_PLANT=1u, ECO_MEAT=2u, ECO_WATER=4u };

/** @brief Política de regeneración ambiental desacoplada de la química. */
typedef enum ResourceRegenPolicy {
    RESOURCE_REGEN_NONE = 0,
    RESOURCE_REGEN_LINEAR = 1,
    RESOURCE_REGEN_INFINITE = 2,
    RESOURCE_REGEN_DEPLETE_RELOCATE = 3 /**< Al agotarse desaparece y reaparece en otra zona tras un retardo/rareza. */
} ResourceRegenPolicy;

/** @brief Política de decaimiento o descomposición ambiental desacoplada de ECO_MEAT. */
typedef enum ResourceDecayPolicy {
    RESOURCE_DECAY_NONE = 0,
    RESOURCE_DECAY_MASS_LOSS = 1,
    RESOURCE_DECAY_DECOMPOSE = 2
} ResourceDecayPolicy;

typedef struct EcosystemResource {
    AIObjectId id;
    unsigned category, originalSpecies;
    float biomass, maximumBiomass, regenerationRate, nutritionalValue;
    double remainingLifetime;
    bool infinite, accessible;

    /* Campos del modo molecular (separan química, física, ciclo de vida y presentación). */
    bool molecular;
    SubstanceHandle substanceHandle;
    SubstancePhase phase;
    SubstancePresentationModel presentationModel;
    float color[3];
    float hardness;
    ResourceRegenPolicy regenPolicy;
    ResourceDecayPolicy decayPolicy;
    float decayRate; /**< Fracción/s transformada en productos de descomposición. */
    MoleculeHandle decayTargetMolecule; /**< Handle de decay_products si aplica. */
    bool hasCustomComposition;
    SubstanceComponent composition[SUBSTANCE_MAX_COMPONENTS];
    uint16_t componentCount;
    SensorySignature sensorySignature;
    float lastSignatureDecayFraction; /**< Umbral perezoso para recalcular firma sólo si cambia. */
    uint32_t compositionRevision;

    /* Política anti-camping de agotamiento, rareza y reaparición relocalizada. */
    float respawnDelaySeconds;   /**< Tiempo de espera tras agotarse antes de intentar reaparecer. */
    float respawnTimer;          /**< Cuenta atrás activa mientras `depleted == true`. */
    float minRelocateDistance;   /**< Distancia mínima a la ubicación previa al reaparecer. */
    float spawnMinDist;          /**< Radio mínimo desde el centro de la arena para reaparición. */
    float spawnMaxDist;          /**< Radio máximo desde el centro (<=0 usa límite de la arena). */
    float rarityWeight;          /**< Probabilidad (0, 1] de brotar al vencer el temporizador (rareza). */
    bool depleted;               /**< true cuando se agotó y espera relocalización. */
    bool needsRelocation;        /**< Señal para que EcosystemWorld reubique el recurso. */

    /* Metadatos diagnósticos opcionales (nunca otorgados como conocimiento mágico a la IA). */
    AIObjectId sourceCreatureId;
    AIObjectId killerCreatureId;
    double deathTime;
    CreatureDeathCause sourceDeathCause;
    double externalMassIntroduced; /**< Masa total introducida si es reservorio infinito. */
} EcosystemResource;

/** @brief Inicializa un recurso molecular a partir de una sustancia compilada del registro. */
EcosystemResource EcosystemResource_FromSubstance(const SubstanceDef *substance,
                                                  float initialMass,
                                                  float maximumMass,
                                                  ResourceRegenPolicy regenPolicy,
                                                  float regenerationRate,
                                                  ResourceDecayPolicy decayPolicy,
                                                  double lifetimeSeconds,
                                                  float decayRate,
                                                  MoleculeHandle decayTargetMolecule);

/** @brief Regenera o descompone sin crear masa y actualiza firma sensorial sólo si varía. */
void EcosystemResource_Update(EcosystemResource *, float dt);

/** @brief Variante molecular con registro de moléculas para recomputar firma al descomponerse. */
void EcosystemResource_UpdateMolecular(EcosystemResource *, const MoleculeRegistry *molecules, float dt);

/** @brief Devuelve cantidad real retirada. El llamador valida contacto y ordena solicitudes. */
float EcosystemResource_Consume(EcosystemResource *, float requested);

/** @brief Disponibilidad actual, también para recursos explícitamente infinitos. */
bool EcosystemResource_Available(const EcosystemResource *);

/** @brief Fracción observable [0, 1] de disponibilidad (1.0 si es reservorio infinito). */
float EcosystemResource_AvailabilityFraction(const EcosystemResource *);

/** @brief Indica si el recurso ha expirado por ciclo de vida o agotamiento terminal. */
bool EcosystemResource_ShouldRemove(const EcosystemResource *);

#endif
