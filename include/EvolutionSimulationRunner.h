/**
 * @file EvolutionSimulationRunner.h
 * @brief Hilo de simulación dedicado, cola de comandos con acuse y snapshots inmutables
 *        con triple buffering y propiedad explícita de lectura/escritura.
 */
#ifndef EVOLUTION_SIMULATION_RUNNER_H
#define EVOLUTION_SIMULATION_RUNNER_H

#include "EvolutionArena.h"
#include "AnatomicalReach.h"
#include "WorldStaticData.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVO_RUNNER_MAX_SPECIES 8u
#define EVO_RUNNER_CMD_QUEUE_CAPACITY 64u
#define EVO_RUNNER_ACK_HISTORY 64u

typedef struct EvolutionEntitySnapshot {
    AIObjectId id;
    uint32_t slotIndex;
    uint32_t recipeId;
    uint32_t entitySeed;
    uint32_t morphologyBucket;
    uint32_t growthStep;
    unsigned anatomyGeneration;
    unsigned speciesIndex;
    unsigned speciesId;
    unsigned generation;
    unsigned offspringCount;
    CreatureLifeStage lifeStage;
    CreatureAction action;
    CreatureGoal goal;
    ReachStatus reachStatus;
    Vector3 position;
    Vector3 velocity;
    Vector3 eyePos;
    Vector3 mouth;
    Vector3 oralPivot;
    Vector3 oralElbow;
    Vector3 targetPos;
    Vector3 groundNormal;
    Quaternion orientation;
    float yaw;
    float speed;
    float developmentScale;
    float speciesRadius;
    float clearance;
    float crouch;
    float maturity;
    float ageSeconds;
    float maturityAgeSeconds;
    float anatomicalScale;
    CreatureGenome genome;
    float health, energy, hydration, fatigue;
    bool oralActive;
    bool hasTarget;
    bool gestating;
    bool mateReady;
    bool hasAnatomicalBody;
    AIObjectId targetId;
    uint8_t cervicalJointCount;
    int16_t cervicalJointIndices[REACH_MAX_JOINTS];
    Quaternion cervicalRotations[REACH_MAX_JOINTS];
    Vector3 headWorldPos;
    float standingHeight;
    float trunkLength;
    Vector3 anatomyCenter;
    float modelLength;
} EvolutionEntitySnapshot;

typedef struct EvolutionResourceSnapshot {
    AIObjectId id;
    unsigned category;
    Vector3 position;
    float radius;
    float biomassFraction;
    /* Información diagnóstica molecular para visualización e inspector de sustancias. */
    bool molecular;
    char substanceId[32];
    char substanceName[40];
    SubstancePhase phase;
    SubstancePresentationModel presentationModel;
    float color[3];
    float mass, maxMass, hardness;
    ResourceRegenPolicy regenPolicy;
    ResourceDecayPolicy decayPolicy;
    double remainingLifetime;
    unsigned componentCount;
    char compNames[8][28];
    float compFractions[8];
    SensorySignature sensorySignature;
    AIObjectId sourceCreatureId;
    AIObjectId killerCreatureId;
    uint16_t waterBodyId;
    uint32_t shoreSiteIndex;
} EvolutionResourceSnapshot;

typedef struct EvolutionObstacleSnapshot {
    AIObjectId id;
    Vector3 position;
    float radius;
    float height;
} EvolutionObstacleSnapshot;

typedef struct EvolutionSelectedSnapshot {
    bool valid;
    AIObjectId entityId;
    unsigned speciesIndex;
    unsigned speciesId;
    unsigned generation;
    unsigned offspringCount;
    AIObjectId parents[2];
    double age;
    double observedSurvival;
    CreatureLifeStage lifeStage;
    CreatureAction action;
    AIObjectId targetId;
    bool gestating;
    bool mateReady;
    float health, energy, hydration, fatigue;
    CreatureControllerType controllerType;
    bool hasNeuralBrain;
    NeuralGenome genome;
    NeuralBrainRuntime runtime;
    NeuralControllerState neuralState;
    unsigned navPointCount;
    unsigned navPointIndex;
    Vector3 navPoints[32];
    /* Diagnóstico digestivo y metabólico del individuo seleccionado (Sección 34). */
    bool hasMetabolism;
    char metabolicProfileName[48];
    char lastSubstanceName[40];
    float digestiveMass, digestiveCapacity, digestiveFullness, wasteMass;
    float nutrientReserve, toxicity, digestiveDiscomfort;
    float recentEnergyAbsorbed, recentHydrationAbsorbed, recentMetabolicDamage;
    double totalIngestedMass, totalEnergyGenerated, totalHydrationGenerated, totalToxinExposure;
    unsigned digestiveEntryCount;
    char digestiveMoleculeNames[METABOLIC_STATE_MAX_ACTIVE][32];
    float digestiveMoleculeMasses[METABOLIC_STATE_MAX_ACTIVE];
} EvolutionSelectedSnapshot;

typedef struct EvolutionRenderSnapshot {
    uint64_t sequence;
    uint64_t tick;
    double seconds;
    double days;
    double timeOfDay;
    double publishedAtMs;
    float arenaHalfSize;
    unsigned capacity;
    unsigned entityCount;
    unsigned entityCapacity;
    unsigned brumacernoCount;
    unsigned velkarCount;
    unsigned resourceCount;
    unsigned resourceCapacity;
    unsigned obstacleCount;
    unsigned obstacleCapacity;
    unsigned workerCount;
    bool paused;
    bool behind;
    bool maxSpeedMode;
    double requestedSpeed;
    double achievedSpeed;
    double ticksPerSecond;
    double backlogSeconds;
    double simCpuUtilization;
    double meanTickMs;
    double p95TickMs;
    double p99TickMs;
    EcosystemStatistics statistics;
    uint64_t interventionCount;
    CreatureControllerType controller;
    EcosystemMetabolismMode metabolismMode;
    char founderModeName[32];
    unsigned speciesCount;
    char speciesNames[EVO_RUNNER_MAX_SPECIES][32];
    uint32_t speciesRecipeIds[EVO_RUNNER_MAX_SPECIES];
    EvolutionProfilerCounters simCounters;

    EvolutionEntitySnapshot *entities;
    EvolutionResourceSnapshot *resources;
    EvolutionObstacleSnapshot *obstacles;
    EvolutionSelectedSnapshot selected;

    /* Identidad y referencia retenida segura al mundo estático inmutable (NULL = plano). */
    uint64_t worldEpoch;
    uint64_t worldFingerprint;
    uint64_t worldRecipeHash;
    uint32_t worldSeed;
    uint32_t worldGeneratorVersion;
    const WorldStaticData *staticWorld;
} EvolutionRenderSnapshot;

typedef enum EvolutionCommandType {
    EVO_CMD_NONE = 0,
    EVO_CMD_PAUSE,
    EVO_CMD_RESUME,
    EVO_CMD_TOGGLE_PAUSE,
    EVO_CMD_SET_SPEED,
    EVO_CMD_SET_MAX_SPEED,
    EVO_CMD_SET_WORKERS,
    EVO_CMD_SET_INSPECT_TARGET,
    EVO_CMD_RESET,
    EVO_CMD_ASSIGN_BRAIN,
    EVO_CMD_CLONE_BRAIN,
    EVO_CMD_SWAP_BRAINS,
    EVO_CMD_RESET_BRAIN_RANDOM,
    EVO_CMD_IMPORT_BRAIN,
    EVO_CMD_EXPORT_BRAIN,
    EVO_CMD_STEP_EXACT,
    EVO_CMD_SET_INITIAL_AGE,
    EVO_CMD_SHUTDOWN
} EvolutionCommandType;

typedef enum EvolutionCommandStatus {
    EVO_CMD_STATUS_UNKNOWN = 0,
    EVO_CMD_STATUS_PENDING,
    EVO_CMD_STATUS_OK,
    EVO_CMD_STATUS_FAILED
} EvolutionCommandStatus;

typedef struct EvolutionCommandAck {
    uint64_t commandId;
    EvolutionCommandType type;
    EvolutionCommandStatus status;
    char message[192];
} EvolutionCommandAck;

typedef struct EvolutionSimulationRunner EvolutionSimulationRunner;

/** @brief Crea el corredor de simulación (propietario del EcosystemWorld autoritativo) e inicia su hilo. */
EvolutionSimulationRunner *EvolutionSimulationRunner_Create(const EvolutionArenaConfig *config,
                                                            bool startPaused,
                                                            char *error, size_t errorSize);

/** @brief Detiene el hilo de simulación de forma ordenada y libera el mundo y los 3 snapshots. */
void EvolutionSimulationRunner_Destroy(EvolutionSimulationRunner *runner);

/** @brief Encola un comando seguro para ejecutarse en frontera entre ticks. Devuelve ID > 0. */
uint64_t EvolutionSimulationRunner_PostSimple(EvolutionSimulationRunner *runner,
                                              EvolutionCommandType type);
uint64_t EvolutionSimulationRunner_PostSetSpeed(EvolutionSimulationRunner *runner,
                                                double speed, bool maxSpeedMode);
uint64_t EvolutionSimulationRunner_PostSetWorkers(EvolutionSimulationRunner *runner,
                                                  unsigned workers);
uint64_t EvolutionSimulationRunner_PostInspectTarget(EvolutionSimulationRunner *runner,
                                                     AIObjectId entityId);
uint64_t EvolutionSimulationRunner_PostReset(EvolutionSimulationRunner *runner,
                                             const EvolutionArenaConfig *config);
uint64_t EvolutionSimulationRunner_PostAssignBrain(EvolutionSimulationRunner *runner,
                                                   AIObjectId entityId,
                                                   const NeuralGenome *genome,
                                                   const char *reason);
uint64_t EvolutionSimulationRunner_PostCloneBrain(EvolutionSimulationRunner *runner,
                                                  AIObjectId sourceId, AIObjectId targetId);
uint64_t EvolutionSimulationRunner_PostSwapBrains(EvolutionSimulationRunner *runner,
                                                  AIObjectId idA, AIObjectId idB);
uint64_t EvolutionSimulationRunner_PostResetBrainRandom(EvolutionSimulationRunner *runner,
                                                        AIObjectId entityId);
uint64_t EvolutionSimulationRunner_PostImportBrain(EvolutionSimulationRunner *runner,
                                                   AIObjectId entityId, const char *path);
uint64_t EvolutionSimulationRunner_PostExportBrain(EvolutionSimulationRunner *runner,
                                                   AIObjectId entityId, const char *path,
                                                   bool overwrite);
uint64_t EvolutionSimulationRunner_PostStepExact(EvolutionSimulationRunner *runner,
                                                 unsigned ticks);
uint64_t EvolutionSimulationRunner_PostSetInitialAge(EvolutionSimulationRunner *runner,
                                                     double age);

/** @brief Consulta el estado/acuse de un comando encolado (PENDING, OK, FAILED). */
bool EvolutionSimulationRunner_GetAck(EvolutionSimulationRunner *runner, uint64_t commandId,
                                      EvolutionCommandAck *outAck);

/** @brief Espera de forma acotada a que un comando concreto complete (útil para CLI/tests). */
bool EvolutionSimulationRunner_WaitCommand(EvolutionSimulationRunner *runner, uint64_t commandId,
                                           double timeoutSeconds, EvolutionCommandAck *outAck);

/** @brief Extrae acuses completados recientes para actualizar banners de la interfaz. */
bool EvolutionSimulationRunner_PopAck(EvolutionSimulationRunner *runner, EvolutionCommandAck *outAck);

/**
 * @brief Adquiere en propiedad de lectura el último snapshot inmutable publicado.
 *
 * Mientras el lector mantiene el puntero, el hilo de simulación jamás sobrescribirá
 * dicho buffer (triple buffering con propiedad explícita). Debe emparejarse con Release.
 */
const EvolutionRenderSnapshot *EvolutionSimulationRunner_AcquireSnapshot(EvolutionSimulationRunner *runner);

/** @brief Libera la propiedad de lectura del snapshot adquirido previamente. */
void EvolutionSimulationRunner_ReleaseSnapshot(EvolutionSimulationRunner *runner,
                                             const EvolutionRenderSnapshot *snapshot);

/** @brief Reloj monótono en milisegundos compatible con snapshot->publishedAtMs. */
double EvolutionSimulationRunner_NowMs(void);

#ifdef __cplusplus
}
#endif

#endif /* EVOLUTION_SIMULATION_RUNNER_H */
