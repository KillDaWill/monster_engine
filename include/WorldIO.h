/**
 * @file WorldIO.h
 * @brief Recetas versionadas de mundos procedurales (*.world.json), validación estricta,
 *        huellas canónicas y derivación determinista de flujos de semilla.
 */
#ifndef MONSTER_WORLD_IO_H
#define MONSTER_WORLD_IO_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORLD_RECIPE_SCHEMA_VERSION 1u
#define WORLD_GENERATOR_VERSION_LEGACY 1u
#define WORLD_GENERATOR_VERSION_CONTINUOUS 2u
#define WORLD_GENERATOR_VERSION_MORPHOLOGY 3u
#define WORLD_GENERATOR_VERSION_SHORELINE 4u
#define WORLD_GENERATOR_VERSION WORLD_GENERATOR_VERSION_SHORELINE
#define WORLD_MAX_MEMORY_BUDGET_BYTES (128u * 1024u * 1024u)

/** @brief Etiquetas de dominio para separar flujos PRNG deterministas sin interferencia. */
typedef enum WorldSeedDomain {
    WORLD_SEED_TERRAIN    = 0x54455252u,
    WORLD_SEED_HYDROLOGY  = 0x48594452u,
    WORLD_SEED_GEOLOGY    = 0x47454F4Cu,
    WORLD_SEED_BIOME      = 0x42494F4Du,
    WORLD_SEED_VEGETATION = 0x56454745u,
    WORLD_SEED_PLACEMENT  = 0x504C4143u,
    WORLD_SEED_EPISODE    = 0x45504953u,
    WORLD_SEED_EVOLUTION  = 0x45564F4Cu
} WorldSeedDomain;

/** @brief Política de salida de drenaje en los bordes del mapa. */
typedef enum WorldOutletPolicy {
    WORLD_OUTLET_OPEN_BOUNDARY = 0
} WorldOutletPolicy;

/** @brief Política de volumen de los cuerpos de agua naturales. */
typedef enum WorldReservoirPolicy {
    WORLD_RESERVOIR_INFINITE_EXTERNAL = 0,
    WORLD_RESERVOIR_FINITE_FIXED_VOLUME = 1
} WorldReservoirPolicy;

typedef struct WorldTerrainParams {
    uint32_t resolution;        /**< Muestras por eje (impar en [5, 1025], ej. 129, 257, 513). */
    float heightScale;          /**< Amplitud vertical de relieve en u.m. [0, 200]. */
    float baseFrequency;        /**< Frecuencia base en 1 / u.m. (ej. 0.012). */
    uint32_t octaves;           /**< Octavas fBm [1, 8]. */
    float persistence;          /**< Factor de decaimiento de amplitud por octava [0.1, 0.9]. */
    float lacunarity;           /**< Factor multiplicador de frecuencia por octava [1.2, 4.0]. */
    float ridgeWeight;          /**< Peso del relieve en crestas [0, 1]. */
    float domainWarpStrength;   /**< Fuerza de distorsión de dominio normalizada [0, 2]. */
    float valleyShaping;        /**< Perfilado de cuenca/valle central [0, 1]. */
} WorldTerrainParams;

typedef struct WorldErosionParams {
    uint32_t thermalIterations; /**< Iteraciones de erosión térmica acotada [0, 120]. */
    float talusAngleDegrees;    /**< Ángulo de talud en grados [10, 70]. */
    float thermalRate;          /**< Fracción de exceso transferida por paso (0, 0.5]. */
} WorldErosionParams;

typedef struct WorldHydrologyParams {
    bool enabled;
    WorldOutletPolicy outletPolicy;
    float riverDensity;         /**< Densidad normalizada de ríos [0, 1]. */
    float lakeDensity;          /**< Densidad normalizada de lagos en cuencas [0, 1]. */
    float riverWidthScale;      /**< Escala de anchura de cauces [0.25, 4.0]. */
    float riverDepthScale;      /**< Escala de profundidad excavada [0.25, 3.0]. */
    char waterSubstanceId[48];  /**< Identificador real de sustancia (defecto "clean_water"). */
    WorldReservoirPolicy reservoirPolicy;
    float finiteVolume;         /**< Masa total por cuerpo si es FINITE_FIXED_VOLUME. */
} WorldHydrologyParams;

typedef struct WorldGeologyParams {
    float rockDensity;          /**< Densidad normalizada de formaciones rocosas [0, 1]. */
    uint32_t rockVariants;      /**< Número de variantes en catálogo compartido [1, 32]. */
    float minRockScale;         /**< Escala mínima de rocas colisionables [0.3, 6.0]. */
    float maxRockScale;         /**< Escala máxima de rocas colisionables [0.5, 12.0]. */
} WorldGeologyParams;

typedef struct WorldVegetationParams {
    float density;              /**< Densidad normalizada de hábitat vegetal [0, 1]. */
    float fertilityBias;        /**< Sesgo de fertilidad [-0.5, 0.5]. */
    float moistureBias;         /**< Sesgo de humedad [-0.5, 0.5]. */
} WorldVegetationParams;

typedef struct WorldRecipe {
    uint32_t version;
    uint32_t generatorVersion;
    char id[64];
    char name[96];
    uint32_t seed;
    float size;                 /**< Extensión total del dominio cuadrado en u.m. */
    bool flat;                  /**< true: compatibilidad plana analítica Y=0. */

    WorldTerrainParams terrain;
    WorldErosionParams erosion;
    WorldHydrologyParams hydrology;
    WorldGeologyParams geology;
    WorldVegetationParams vegetation;

    uint64_t recipeHash;
    bool valid;
} WorldRecipe;

/** @brief Mezclador estable SplitMix64 para derivar flujos de semilla reproducibles. */
uint32_t WorldSeed_Derive(uint64_t masterSeed, uint64_t domainTag, uint64_t index);

/** @brief Receta procedural base (river_valley moderado). */
WorldRecipe WorldRecipe_Default(void);

/** @brief Receta plana compatible Y=0 con el tamaño indicado. */
WorldRecipe WorldRecipe_Flat(float size);

/** @brief Carga un preset integrado ("flat", "river_valley", "mountain_lakes", "rocky_highlands", "wetlands"). */
bool WorldRecipe_Preset(const char *presetName, WorldRecipe *out);

/** @brief Valida estrictamente todos los rangos, desbordamientos y presupuesto de memoria. */
bool WorldRecipe_Validate(const WorldRecipe *recipe, char *error, size_t errorSize);

/** @brief Huella canónica FNV-1a cuantizada de la receta (independiente de padding). */
uint64_t WorldRecipe_Fingerprint(const WorldRecipe *recipe);

/** @brief Carga estricta desde cadena JSON; rechaza claves desconocidas o valores no finitos. */
bool WorldRecipe_DeserializeJSON(const char *json, size_t length, WorldRecipe *out,
                                 char *error, size_t errorSize);

/** @brief Carga y valida un archivo *.world.json desde disco. */
bool WorldRecipe_LoadFile(const char *path, WorldRecipe *out, char *error, size_t errorSize);

/** @brief Serializa una receta a JSON legible. */
bool WorldRecipe_SerializeJSON(const WorldRecipe *recipe, char *buffer, size_t bufferSize,
                               size_t *outLength);

/** @brief Guarda una receta a disco de forma atómica. */
bool WorldRecipe_SaveFile(const WorldRecipe *recipe, const char *path,
                          char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_WORLD_IO_H */
