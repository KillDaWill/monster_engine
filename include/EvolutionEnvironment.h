/**
 * @file EvolutionEnvironment.h
 * @brief Definiciones de entornos de evaluación por datos (`environments/NOMBRE.environment.json`).
 *
 * Permite construir mundos autoritativos reproducibles para cualquier combinación de
 * especies, sustancias, obstáculos, criaturas de fondo y rangos de aleatorización sin
 * editar código C.
 */
#ifndef EVOLUTION_ENVIRONMENT_H
#define EVOLUTION_ENVIRONMENT_H

#include "EcosystemWorld.h"
#include "EvolutionSpeciesRegistry.h"
#include "MetabolismIO.h"
#include "WorldIO.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVOLUTION_ENV_SCHEMA_VERSION 1u
#define EVOLUTION_ENV_MAX_RESOURCE_GROUPS 16u
#define EVOLUTION_ENV_MAX_BACKGROUND_GROUPS 8u

typedef enum BackgroundPolicyType {
    BG_POLICY_IDLE = 0,
    BG_POLICY_WANDER = 1,
    BG_POLICY_UTILITY = 2,
    BG_POLICY_BRAIN = 3
} BackgroundPolicyType;

/** @brief Algoritmo de distribución espacial de recursos en la arena. */
typedef enum ResourceDistributionMode {
    RESOURCE_DIST_UNIFORM = 0,   /**< Anillo angular con variación aleatoria. */
    RESOURCE_DIST_CLUSTERED = 1, /**< Parches/bosquecillos agrupados alrededor de centros aleatorios. */
    RESOURCE_DIST_SCATTERED = 2, /**< Dispersión tipo Poisson-disc con separación mínima (alimentos raros). */
    RESOURCE_DIST_RING = 3       /**< Banda perimetral exterior (obliga a desplazarse desde el centro). */
} ResourceDistributionMode;

typedef struct EvolutionEnvResourceGroup {
    char substanceId[SUBSTANCE_ID_MAX];
    unsigned count;
    float initialMass;
    float maxMass;
    float radius;
    ResourceRegenPolicy regenPolicy;
    float regenRate;
    float respawnDelaySeconds;
    float rarityWeight;
    float minRelocateDist;
    ResourceDistributionMode distribution;
    unsigned clusterCount;
    float clusterRadius;
    float minSpacing;
    ResourceDecayPolicy decayPolicy;
    double lifetimeSeconds;
    float decayRate;
    float minDistFromCenter;
    float maxDistFromCenter;
} EvolutionEnvResourceGroup;

typedef struct EvolutionEnvBackgroundGroup {
    char speciesPath[256];
    unsigned count;
    BackgroundPolicyType policy;
    char brainPath[256];
    float initialEnergy;
    float initialHydration;
    float initialHealth;
    float speedScale;
    float minDistFromFocal;
    float maxDistFromFocal;
    /* Caché validada en carga */
    EvolutionSpeciesConfig speciesCfg;
    bool hasLoadedBrain;
    NeuralGenome loadedBrain;
} EvolutionEnvBackgroundGroup;

/** @brief Política de semilla del mundo procedural en episodios de entrenamiento/evaluación. */
typedef enum WorldSeedPolicy {
    WORLD_SEED_POLICY_FIXED = 0,       /**< Usa siempre la semilla fija declarada en la receta del mundo. */
    WORLD_SEED_POLICY_PER_EPISODE = 1  /**< Deriva semilla determinista del terreno a partir de episodeSeed. */
} WorldSeedPolicy;

typedef struct EvolutionEnvironmentConfig {
    uint32_t schemaVersion;
    char id[64];
    char name[96];
    char description[192];

    float arenaSize;
    double episodeSeconds;
    double stepSeconds;
    double daySeconds;

    /* Configuración opcional de mundo procedural (vacío = compatibilidad plana heredada). */
    char worldPath[256];
    bool hasWorld;
    WorldSeedPolicy worldSeedPolicy;
    bool terrainAwarePlacement;
    WorldRecipe cachedWorldRecipe;

    /* Inicialización del individuo focal */
    float focalEnergyMin, focalEnergyMax;
    float focalHydrationMin, focalHydrationMax;
    float focalNutrients;
    double focalAge;
    float focalSpawnRadius;

    /* Obstáculos */
    unsigned obstacleCount;
    float obstacleMinRadius, obstacleMaxRadius, obstacleHeight;

    /* Grupos de recursos y criaturas de fondo */
    EvolutionEnvResourceGroup resources[EVOLUTION_ENV_MAX_RESOURCE_GROUPS];
    unsigned resourceGroupCount;
    EvolutionEnvBackgroundGroup backgrounds[EVOLUTION_ENV_MAX_BACKGROUND_GROUPS];
    unsigned backgroundGroupCount;

    uint64_t fingerprint;
    bool valid;
} EvolutionEnvironmentConfig;

/** @brief Inicializa valores por defecto del entorno. */
void EvolutionEnvironment_InitDefault(EvolutionEnvironmentConfig *cfg);

/** @brief Carga y valida un archivo `*.environment.json` desde memoria. */
bool EvolutionEnvironment_DeserializeJSON(const char *json, size_t length,
                                          EvolutionEnvironmentConfig *out,
                                          char *error, size_t errorSize);

/** @brief Carga un archivo `*.environment.json` desde disco. */
bool EvolutionEnvironment_LoadFile(const char *path, EvolutionEnvironmentConfig *out,
                                   char *error, size_t errorSize);

/** @brief Valida todas las sustancias, especies de fondo y cerebros referenciados. */
bool EvolutionEnvironment_ValidateAgainstDatabase(EvolutionEnvironmentConfig *cfg,
                                                  const MetabolismDatabase *db,
                                                  char *error, size_t errorSize);

/** @brief Calcula huella canónica FNV-1a del entorno. */
uint64_t EvolutionEnvironment_Fingerprint(const EvolutionEnvironmentConfig *cfg);

/** @brief Huella física del terreno v3 fijo; cero para otras políticas. */
uint64_t EvolutionEnvironment_FixedWorldDataFingerprint(const EvolutionEnvironmentConfig *env);

/**
 * @brief Construye un @ref EcosystemWorld autoritativo limpio para evaluar una política
 *        focal con la semilla de episodio indicada.
 *
 * Devuelve el ID estable del individuo focal en @p outFocalId.
 */
bool EvolutionEnvironment_BuildEpisodeWorld(EcosystemWorld *world,
                                            const EvolutionEnvironmentConfig *env,
                                            const EvolutionSpeciesConfig *focalSpecies,
                                            const MetabolismDatabase *db,
                                            const NeuralGenome *focalGenome,
                                            uint32_t episodeSeed,
                                            bool useAnatomicalOral,
                                            AIObjectId *outFocalId,
                                            char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif

#endif /* EVOLUTION_ENVIRONMENT_H */
