/** @file EcosystemWorld.h
 * @brief Propiedad de entidades, recursos, estadísticas, evolución neuronal y fases de simulación pura C. */
#ifndef ECOSYSTEM_WORLD_H
#define ECOSYSTEM_WORLD_H
#include "SimulationClock.h"
#include "CreatureMind.h"
#include "CreaturePhysiology.h"
#include "CreatureReproduction.h"
#include "CreatureMetabolism.h"
#include "MetabolismIO.h"
#include "EcosystemResources.h"
#include "EvolutionJobSystem.h"
#include "EvolutionProfiler.h"
#include "NeuralBrainIO.h"
#include <stdio.h>
#include <stdint.h>

/** @brief Modo de metabolismo del mundo: legado categórico o molecular guiado por datos. */
typedef enum EcosystemMetabolismMode {
    ECOSYSTEM_METABOLISM_LEGACY = 0,
    ECOSYSTEM_METABOLISM_MOLECULAR = 1
} EcosystemMetabolismMode;

#define ECOSYSTEM_DEFAULT_OBSTACLE_CAPACITY 64u
#define ECOSYSTEM_MAX_GENERATION_BUCKETS 64u
#define ECOSYSTEM_INTERVENTION_LOG_CAPACITY 64u
#define ECOSYSTEM_INITIAL_LINEAGE_CAPACITY 256u
#define ECOSYSTEM_INITIAL_FOUNDER_CAPACITY 256u

typedef struct EcosystemObstacle {
    AIObjectId id;
    Vector3 position;
    float radius, height;
} EcosystemObstacle;

typedef struct EcosystemIntervention {
    uint64_t tick;
    double seconds;
    char action[24];
    AIObjectId actorA, actorB;
    uint64_t oldFingerprintA, newFingerprintA;
    uint64_t oldFingerprintB, newFingerprintB;
} EcosystemIntervention;

typedef struct EcosystemLineage {
    uint64_t lineageId;
    uint64_t externalLineageId; /**< ID original externo cuando fue remapeado al importar. */
    uint64_t founderFingerprint;
    unsigned speciesId;         /**< Primera especie observada (compatibilidad). */
    uint32_t speciesMask;       /**< Máscara de especies que han portado este linaje. */
    unsigned livingCount;
    uint64_t totalMembers;
    uint64_t totalOffspring;
    unsigned maxGeneration;
    double firstSeenSeconds;
    double lastActiveSeconds;
} EcosystemLineage;

typedef struct EcosystemFounderRecord {
    uint64_t lineageId;
    uint64_t fingerprint;
    NeuralGenome genome;
} EcosystemFounderRecord;

typedef struct EcosystemParentRecord {
    AIObjectId entityId;
    unsigned offspringCount;
    unsigned matureOffspringCount;
    unsigned reproductiveOffspringCount;
    bool alive;
} EcosystemParentRecord;

typedef struct EcosystemArchiveEntry {
    bool valid;
    bool stillLiving;
    AIObjectId entityId;
    unsigned speciesId;
    unsigned generation;
    unsigned offspringCount;
    double survivalSeconds;     /**< Duración observada de supervivencia (seconds - bornAt). */
    double biologicalAge;       /**< Edad fisiológica biológica separada. */
    uint64_t lineageId;
    NeuralGenome genome;
} EcosystemArchiveEntry;

typedef struct EcosystemSpecies {
    unsigned id, category;
    uint32_t recipeId; /**< Receta anatómica persistente asociada a la categoría. */
    bool anatomicalOral; /**< Contacto cervical resuelto desde la receta, también en headless. */
    char name[32];
    BrainProfile brain;
    PhysiologyProfile physiology;
    ReproductionProfile reproduction;
    float speed, radius, oralReach, attackRange, damage, attackCooldown, attackCost;
    float turnRate; /**< Velocidad angular máxima; cero conserva el valor genérico. */
    float edibleBiomass, nutrition;
    /* Configuración molecular opcional por especie (modo ECOSYSTEM_METABOLISM_MOLECULAR). */
    MetabolicProfileHandle metabolicProfile;
    SubstanceHandle tissueSubstance;
    float tissueMass;
} EcosystemSpecies;

typedef struct EcosystemEntity {
    AIObjectId id;
    unsigned speciesIndex, generation, offspringCount;
    uint32_t seed;
    double bornAt;
    float founderDistance;
    bool founderMissing;
    bool maturityRecorded;
    bool reproductiveRecorded;
    CreatureGenome genome;
    BrainProfile brain; /**< Perfil privado resuelto, dirección estable durante su vida. */
    CreatureMind mind;
    CreaturePhysiology physiology;
    MetabolicState metabolism; /**< Estado digestivo individual, independiente del cerebro. */
    CreatureReproduction reproduction;
    CreatureBodyState body;
    AIObjectId lastAttackerId; /**< Diagnóstico causal de depredación; nunca visible a IA. */
    Vector3 previousPosition, velocity, oralLocal, oralPivot, oralElbow;
    double nextAttack;
    float developmentScale, oralMotion;
    void *visual; /**< Préstamo opcional; el núcleo nunca lo consulta ni libera. */
    struct EcosystemAnatomy *anatomicalBody; /**< Propiedad privada del cuerpo oral, sin recursos gráficos. */
} EcosystemEntity;

typedef struct EcosystemStatistics {
    uint64_t births, deaths, deathCauses[DEATH_CAUSE_COUNT], kills, attacks, matings;
    uint64_t conceptions, offspringReachingMaturity, reproductiveOffspring;
    uint64_t failedBirths, failedSpawns, actionFailures, navigationFailures, failedInteractions;
    uint64_t meals, drinks, escapes;
    uint64_t birthsPerGeneration[ECOSYSTEM_MAX_GENERATION_BUCKETS];
    uint64_t intentSelections[ACTION_INTENT_COUNT];
    uint64_t v2ActionSelections[NEURAL_V2_ACTION_COUNT];
    unsigned population, juveniles, maximumGeneration, activeLineages;
    unsigned missingFounderCount;
    double foodConsumed, plantConsumed, meatConsumed, waterConsumed, plantBiomass;
    /* Estadísticas metabólicas detalladas (modo molecular). */
    double ingestedBySubstance[SUBSTANCE_MAX_COUNT];
    double ingestedByMolecule[MOLECULE_MAX_COUNT];
    double totalIngestedMass, totalProcessedMass, totalAbsorbedMass;
    double totalRejectedMass, totalWasteEliminated;
    double totalMetabolicEnergy, totalMetabolicHydration, totalMetabolicNutrients;
    double totalDigestiveEnergyCost, totalToxinExposure, totalDetoxified, totalMetabolicDamage;
    uint64_t toxicityDeaths;
    double averageAge, averageHealth, averageEnergy, averageGeneration;
    double survivalDurationSum, averageSurvivalDuration, maxObservedSurvival;
    uint64_t completedLifetimes;
    double neuralDiversity;      /**< Media determinista de distancia RMS por pares sobre toda la población. */
    double neuralDispersionRms;  /**< Raíz exacta del cuadrado medio de distancias por pares (O(N*P)). */
    double averageFounderDistance, maxFounderDistance;
    uint64_t eventHash; /**< Firma de eventos ordenados para validación determinista. */
} EcosystemStatistics;

typedef struct EcosystemBirth {
    unsigned speciesIndex, generation;
    AIObjectId parents[2];
    CreatureGenome genome;
    bool hasNeuralGenome;
    NeuralGenome neuralGenome;
    Vector3 position;
} EcosystemBirth;

typedef struct EcosystemIdMap {
    uint32_t *keys;
    uint32_t *values;
    uint32_t mask;
    uint32_t capacity;
} EcosystemIdMap;

typedef struct EcosystemMoveProposal {
    Vector3 nextPosition;
    float nextYaw;
    float desiredSpeed;
    bool wantsMove;
} EcosystemMoveProposal;

typedef struct EcosystemWorld {
    SimulationClock clock;
    AIWorld observations;
    EcosystemSpecies *species;
    unsigned speciesCount, speciesCapacity;
    EcosystemEntity *entities;
    EcosystemResource *resources;
    EcosystemBirth *birthQueue;
    unsigned *order;
    EcosystemMoveProposal *moveProposals;

    /* Listas compactas y tablas hash O(1) para entidades y recursos. */
    EcosystemIdMap entityMap;
    EcosystemIdMap resourceMap;
    unsigned *activeEntitySlots;
    unsigned *entitySlotToActive;
    unsigned *freeEntitySlots;
    unsigned freeEntityCount;
    unsigned *activeResourceSlots;
    unsigned *resourceSlotToActive;
    unsigned *freeResourceSlots;
    unsigned activeResourceCount;
    unsigned freeResourceCount;

    EcosystemObstacle *obstacles;
    unsigned obstacleCount, obstacleCapacity;
    unsigned capacity, resourceCapacity, birthCount, count;
    uint32_t random, initialSeed;
    bool updating;
    /* Configuración evolutiva neuronal y metabólica. */
    EcosystemMetabolismMode metabolismMode;
    const MetabolismDatabase *metabolismDb; /**< Base congelada prestada o apuntando a ownedMetabolismDb. */
    MetabolismDatabase *ownedMetabolismDb;  /**< Propiedad opcional liberada en EcosystemWorld_Free. */
    MoleculeHandle decayTargetMolecule;     /**< Handle pre-resuelto de decay_products. */
    CreatureControllerType defaultController;
    NeuralArchitectureType defaultArchitecture;
    NeuralMutationConfig neuralMutation;
    NeuralControllerConfig neuralController;
    bool disablePhysicalMutation;
    uint64_t nextLineageId;
    char founderModeName[32];
    /* Estadísticas, linajes, fundadores e historial parental dinámicos. */
    EcosystemStatistics statistics;
    EcosystemLineage *lineages;
    unsigned lineageCount, lineageCapacity;
    EcosystemFounderRecord *founders;
    unsigned founderCount, founderCapacity;
    EcosystemParentRecord *parentHistory;
    unsigned parentHistoryCount, parentHistoryCapacity;
    EcosystemIdMap parentHistoryMap;
    EcosystemArchiveEntry archiveBestOffspring;
    EcosystemArchiveEntry archiveLongestSurvival;
    EcosystemArchiveEntry archiveHighestGeneration;
    EcosystemIntervention interventions[ECOSYSTEM_INTERVENTION_LOG_CAPACITY];
    uint64_t interventionCount;
    /* Paralelismo determinista y perfilado opcionales. */
    EvolutionJobSystem *jobs;
    unsigned configuredWorkers;
    EvolutionProfiler *profiler; /**< Préstamo opcional; NULL desactiva perfilado con coste cero. */
    FILE *eventOutput; /**< Préstamo diagnóstico opcional, nunca se cierra desde el núcleo. */
} EcosystemWorld;

/** @brief Perfil declarativo de ejemplo; ninguna mecánica consulta nombres. */
EcosystemSpecies EcosystemSpecies_Default(unsigned id,unsigned category,unsigned diet,unsigned prey,unsigned threats);
/** @brief Reserva arrays acotados configurables (arena 48x48 por compatibilidad). No copiar por valor. */
bool EcosystemWorld_Init(EcosystemWorld *,unsigned populationCap,unsigned resourceCap,unsigned speciesCap,uint32_t seed,double step,double day);
/** @brief Reserva arrays acotados con semiancho de arena configurable (ej. halfSize=20 para 40x40). */
bool EcosystemWorld_InitArena(EcosystemWorld *,unsigned populationCap,unsigned resourceCap,unsigned speciesCap,
                              float arenaHalfSize,uint32_t seed,double step,double day);
/** @brief Configura el número de trabajadores CPU para las fases paralelas deterministas (1=serial, 0=auto). */
bool EcosystemWorld_SetWorkerCount(EcosystemWorld *, unsigned workerCount);
/** @brief Consulta el número efectivo de trabajadores activos (1 si serial). */
unsigned EcosystemWorld_WorkerCount(const EcosystemWorld *);
/** @brief Habilita modo molecular asociando una base metabólica congelada (prestada). */
bool EcosystemWorld_EnableMolecularMetabolism(EcosystemWorld *, const MetabolismDatabase *db);
/** @brief Carga y toma propiedad de una base metabólica desde disco para este mundo. */
bool EcosystemWorld_LoadMolecularMetabolism(EcosystemWorld *, const char *rootDir,
                                            char *error, size_t errorSize);
/** @brief Registra un recurso molecular validando sustancia y propiedades físicas. */
AIObjectId EcosystemWorld_AddMolecularResource(EcosystemWorld *, EcosystemResource, Vector3, float radius);
/** @brief Asocia un perfilador opcional (NULL desactiva). */
void EcosystemWorld_SetProfiler(EcosystemWorld *, EvolutionProfiler *);
/** @brief Libera todos los arrays propios; visuales pertenecen al adaptador. */
void EcosystemWorld_Free(EcosystemWorld *);
/** @brief Copia y valida especie; cero indica error, devuelve id lógico de especie. */
unsigned EcosystemWorld_AddSpecies(EcosystemWorld *,EcosystemSpecies);
/** @brief Registra obstáculo cilíndrico estático físico y oclusor dentro de la arena. */
AIObjectId EcosystemWorld_AddObstacle(EcosystemWorld *,Vector3 position,float radius,float height);
/** @brief Crea individuo independiente fuera de actualización; cero si no hay capacidad/espacio. */
AIObjectId EcosystemWorld_Spawn(EcosystemWorld *,unsigned speciesId,Vector3,double age,float energy,const CreatureGenome *);
/** @brief Préstamo hasta siguiente mutación; nunca guardar este puntero en memoria de IA. */
EcosystemEntity *EcosystemWorld_Get(EcosystemWorld *,AIObjectId);
/** @brief Consulta de sólo lectura por ID estable. */
const EcosystemEntity *EcosystemWorld_GetConst(const EcosystemWorld *,AIObjectId);
/** @brief Consulta el registro histórico de descendencia de un progenitor (incluso fallecido). */
const EcosystemParentRecord *EcosystemWorld_GetParentRecord(const EcosystemWorld *, AIObjectId);
/** @brief Solicita muerte idempotente; se retira al final del siguiente tick activo. */
bool EcosystemWorld_RequestDeath(EcosystemWorld *,AIObjectId,CreatureDeathCause);
/** @brief Registra recurso compatible con AIWorld, comprobando geometría y cantidades. */
AIObjectId EcosystemWorld_AddResource(EcosystemWorld *,EcosystemResource,Vector3,float radius);
/** @brief Retira recurso e invalida targets por identidad, sin reutilizar ID. */
bool EcosystemWorld_RemoveResource(EcosystemWorld *,AIObjectId);
/** @brief Consulta inmediata del libro, NULL si fue retirado. */
EcosystemResource *EcosystemWorld_Resource(EcosystemWorld *,AIObjectId);
/** @brief Un tick fijo autoritativo. La pausa congela todos los procesos. */
void EcosystemWorld_Step(EcosystemWorld *);
/** @brief Acumula presentación y procesa hasta presupuesto; conserva deuda. */
unsigned EcosystemWorld_Advance(EcosystemWorld *,double realSeconds);
/** @brief Ataque resuelto: verifica fase, capacidad física, vida, orientación, alcance y cooldown. */
bool EcosystemWorld_Attack(EcosystemWorld *,AIObjectId attacker,AIObjectId prey);
/** @brief Transfiere sólo con solicitud, contacto estable, dieta física y disponibilidad actuales. */
float EcosystemWorld_Consume(EcosystemWorld *,AIObjectId creature,AIObjectId resource,float requested);
/** @brief Asigna un genoma neuronal validado a una criatura existente sin alterar su cuerpo. */
bool EcosystemWorld_AssignBrain(EcosystemWorld *,AIObjectId id,const NeuralGenome *genome,const char *reason);
/** @brief Copia independientemente el cerebro de sourceId a targetId. */
bool EcosystemWorld_CloneBrain(EcosystemWorld *,AIObjectId sourceId,AIObjectId targetId);
/** @brief Intercambia atómicamente los cerebros de dos criaturas sin alterar cuerpos ni fisiología. */
bool EcosystemWorld_SwapBrains(EcosystemWorld *,AIObjectId idA,AIObjectId idB);
/** @brief Reemplaza el cerebro del individuo por un nuevo genoma aleatorio determinista. */
bool EcosystemWorld_ResetBrainRandom(EcosystemWorld *,AIObjectId id);
/** @brief Exporta el cerebro de un individuo a un archivo *.brain.json. */
bool EcosystemWorld_ExportBrain(const EcosystemWorld *,AIObjectId id,const char *path,bool overwrite,char *error,size_t errorSize);
/** @brief Importa y valida un archivo *.brain.json asignándolo al individuo sin corromperlo si falla. */
bool EcosystemWorld_ImportBrain(EcosystemWorld *,AIObjectId id,const char *path,char *error,size_t errorSize);
/** @brief Exporta archivos observacionales de cerebros representativos y su manifiesto JSON. */
bool EcosystemWorld_ExportArchiveBrains(const EcosystemWorld *,const char *directory,char *error,size_t errorSize);
/** @brief Recalcula métricas de diversidad neuronal (costo acotado, bajo demanda o muestreo). */
void EcosystemWorld_ComputeNeuralMetrics(EcosystemWorld *);
/** @brief Habilita eventos CSV detallados; NULL desactiva. Archivo prestado por el llamador. */
void EcosystemWorld_SetEventOutput(EcosystemWorld *,FILE *);
/** @brief Estadísticas JSON finales con poblaciones, linajes y métricas evolutivas. No cierra el archivo. */
bool EcosystemWorld_WriteJSON(const EcosystemWorld *,FILE *);
/** @brief Fila CSV de historia; header controla la cabecera inicial. */
bool EcosystemWorld_WriteCSV(const EcosystemWorld *,FILE *,bool header);
#endif
