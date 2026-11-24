/**
 * @file EvolutionArena.c
 * @brief Construcción reproducible de Evolution Arena con Brumacerno, Velkar y cerebros neuronales.
 */
#include "EvolutionArena.h"
#include "CreatureRecipes.h"
#include "EcosystemScenario.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void setErr(char *err, size_t errSize, const char *msg) {
    if (err && errSize > 0) snprintf(err, errSize, "%s", msg ? msg : "error");
}

const char *EvolutionFounderMode_Name(EvolutionFounderMode mode) {
    switch (mode) {
    case EVOLUTION_FOUNDERS_RANDOM:   return "random";
    case EVOLUTION_FOUNDERS_SEEDED:   return "seeded";
    case EVOLUTION_FOUNDERS_IMPORTED: return "imported";
    default:                          return "unknown";
    }
}

bool EvolutionFounderMode_FromName(const char *name, EvolutionFounderMode *out) {
    if (!name || !out) return false;
    if (strcmp(name, "random") == 0) {
        *out = EVOLUTION_FOUNDERS_RANDOM;
        return true;
    }
    if (strcmp(name, "seeded") == 0 || strcmp(name, "seeded_baseline") == 0 ||
        strcmp(name, "baseline") == 0) {
        *out = EVOLUTION_FOUNDERS_SEEDED;
        return true;
    }
    if (strcmp(name, "imported") == 0) {
        *out = EVOLUTION_FOUNDERS_IMPORTED;
        return true;
    }
    return false;
}

EvolutionArenaConfig EvolutionArenaConfig_Default(void) {
    return (EvolutionArenaConfig){
        .seed = 42u,
        .arenaSize = 40.0f,
        .brumacernos = 16u,
        .velkares = 6u,
        .plants = 36u,
        .ponds = 2u,
        .obstacles = 5u,
        .capacity = 128u,
        .step = 1.0 / 60.0,
        .day = 120.0,
        .days = 30.0,
        .controller = CREATURE_CONTROLLER_NEURAL,
        .founderMode = EVOLUTION_FOUNDERS_SEEDED,
        .mutation = NeuralMutationConfig_Default(),
        .neural = NeuralControllerConfig_Default(),
        .founderDiversityRate = 0.15f,
        .founderDiversitySigma = 0.08f,
        .disablePhysicalMutation = false,
        .founderBrainPath = NULL,
        .brumacernoBrainPath = NULL,
        .velkarBrainPath = NULL,
        .legacyScenario = NULL
    };
}

bool EvolutionArenaConfig_ApplyLegacyScenario(EvolutionArenaConfig *cfg, const char *name) {
    if (!cfg || !name) return false;
    EcosystemConfig eco = EcosystemConfig_Default();
    if (!EcosystemConfig_Scenario(&eco, name)) return false;
    cfg->legacyScenario = eco.scenario;
    cfg->controller = CREATURE_CONTROLLER_UTILITY;
    cfg->arenaSize = 48.0f;
    cfg->brumacernos = eco.herbivores;
    cfg->velkares = eco.carnivores;
    cfg->plants = eco.plants;
    cfg->ponds = 2u;
    cfg->obstacles = 0u;
    cfg->capacity = eco.capacity;
    return true;
}

bool EvolutionArenaConfig_Validate(const EvolutionArenaConfig *cfg) {
    if (!cfg) return false;
    if (!isfinite(cfg->arenaSize) || cfg->arenaSize < 12.0f || cfg->arenaSize > 400.0f) return false;
    if (cfg->capacity == 0 || cfg->capacity > 10000u) return false;
    if (cfg->brumacernos > cfg->capacity || cfg->velkares > cfg->capacity - cfg->brumacernos) return false;
    if (cfg->plants > 10000u || cfg->ponds > 16u || cfg->obstacles > ECOSYSTEM_MAX_OBSTACLES) return false;
    if (!isfinite(cfg->step) || cfg->step <= 0.0 || !isfinite(cfg->day) || cfg->day <= 0.0) return false;
    if (!isfinite(cfg->days) || cfg->days < 0.0) return false;
    if (!NeuralMutationConfig_Validate(&cfg->mutation)) return false;
    if (!NeuralControllerConfig_Validate(&cfg->neural)) return false;
    if (!isfinite(cfg->founderDiversityRate) || cfg->founderDiversityRate < 0.0f || cfg->founderDiversityRate > 1.0f)
        return false;
    if (!isfinite(cfg->founderDiversitySigma) || cfg->founderDiversitySigma < 0.0f || cfg->founderDiversitySigma > 2.0f)
        return false;
    return true;
}

static bool buildSpecies(EcosystemSpecies *brumOut, EcosystemSpecies *velkOut,
                         const EvolutionArenaConfig *cfg, char *error, size_t errorSize) {
    /* Resolver las recetas reales mediante el registro/catálogo vigente. */
    const CreatureRecipe *brumRecipe = CreatureRecipes_Find(3u);
    if (!brumRecipe) brumRecipe = CreatureRecipes_Brumacerno();
    const CreatureRecipe *velkRecipe = CreatureRecipes_Find(4u);
    if (!velkRecipe) velkRecipe = CreatureRecipes_Velkar();
    if (!brumRecipe || !velkRecipe) {
        setErr(error, errorSize, "No se pudieron resolver las recetas de Brumacerno (3) y Velkar (4)");
        return false;
    }

    EcosystemSpecies brum = EcosystemSpecies_Default(1u, 1u, ECO_PLANT, 0u, 2u);
    EcosystemSpecies velk = EcosystemSpecies_Default(2u, 2u, ECO_MEAT, 1u, 0u);
    snprintf(brum.name, sizeof(brum.name), "Brumacerno");
    snprintf(velk.name, sizeof(velk.name), "Velkar");
    brum.recipeId = brumRecipe->id;
    velk.recipeId = velkRecipe->id;
    brum.anatomicalOral = true;
    velk.anatomicalOral = true;
    brum.radius = 0.95f;
    velk.radius = 0.80f;
    velk.oralReach = 2.0f;
    velk.attackRange = 1.9f;
    brum.turnRate = 1.0f;
    velk.turnRate = 2.0f;

    /* Propiedades biológicas explícitas separadas de las preferencias cerebrales. */
    brum.physiology.dietMask = ECO_PLANT;
    brum.physiology.preyMask = 0u;
    brum.physiology.canAttackPhysically = false;
    brum.physiology.waterInteraction = true;

    velk.physiology.dietMask = ECO_MEAT;
    velk.physiology.preyMask = 1u;
    velk.physiology.canAttackPhysically = true;
    velk.physiology.waterInteraction = true;

    if (cfg->legacyScenario && strcmp(cfg->legacyScenario, "generations") == 0) {
        brum.speed = 2.5f;
        velk.speed = 2.25f;
        velk.damage = 0.2f;
        velk.attackCooldown = 1.2f;
        brum.turnRate = 1.5f;
        velk.turnRate = 1.7f;
    } else if (!cfg->legacyScenario) {
        /* Calibración equilibrada para Evolution Arena (40x40): permite persecución y fuga reales. */
        brum.speed = 2.35f;
        velk.speed = 2.65f;
        velk.damage = 0.28f;
        velk.attackCooldown = 1.0f;
        brum.turnRate = 1.4f;
        velk.turnRate = 2.0f;
    }

    *brumOut = brum;
    *velkOut = velk;
    return true;
}

static bool initializeFounderBrain(EcosystemWorld *w, AIObjectId id, unsigned speciesId,
                                   const EvolutionArenaConfig *cfg,
                                   const NeuralGenome *importedBrum,
                                   const NeuralGenome *importedVelk,
                                   char *error, size_t errorSize) {
    NeuralGenome genome;
    uint64_t lineageId = w->nextLineageId++;
    const NeuralGenome *imported = (speciesId == 1u) ? importedBrum : importedVelk;

    if (imported != NULL) {
        if (!NeuralGenome_Clone(&genome, imported)) {
            setErr(error, errorSize, "No se pudo clonar cerebro importado");
            return false;
        }
        genome.metadata.lineageId = lineageId;
        if (cfg->founderMode == EVOLUTION_FOUNDERS_IMPORTED) {
            snprintf(genome.metadata.origin, sizeof(genome.metadata.origin), "imported");
        }
    } else if (cfg->founderMode == EVOLUTION_FOUNDERS_RANDOM) {
        if (!NeuralGenome_InitRandom(&genome, &w->random, 0.8f, cfg->mutation.weightLimit, lineageId)) {
            setErr(error, errorSize, "No se pudo inicializar cerebro aleatorio");
            return false;
        }
    } else if (cfg->founderMode == EVOLUTION_FOUNDERS_SEEDED) {
        if (!NeuralGenome_InitSeededBaseline(&genome, lineageId)) {
            setErr(error, errorSize, "No se pudo inicializar cerebro base");
            return false;
        }
        if (cfg->founderDiversityRate > 0.0f && cfg->founderDiversitySigma > 0.0f) {
            NeuralMutationConfig divCfg = cfg->mutation;
            divCfg.mutationRate = cfg->founderDiversityRate;
            divCfg.mutationSigma = cfg->founderDiversitySigma;
            divCfg.replaceRate = 0.0f;
            if (!NeuralGenome_Mutate(&genome, divCfg, &w->random)) {
                setErr(error, errorSize, "Fallo al diversificar cerebro fundador");
                return false;
            }
            genome.metadata.founderFingerprint = genome.metadata.fingerprint;
        }
    } else {
        setErr(error, errorSize, "Modo de fundadores 'imported' requiere al menos un archivo *.brain.json");
        return false;
    }

    bool ok = EcosystemWorld_AssignBrain(w, id, &genome, NULL);
    NeuralGenome_Destroy(&genome);
    if (!ok) {
        setErr(error, errorSize, "Fallo al asignar cerebro neuronal al fundador");
    }
    return ok;
}

bool EvolutionArena_Create(EcosystemWorld *w, const EvolutionArenaConfig *cfg,
                           char *error, size_t errorSize) {
    if (!w || !EvolutionArenaConfig_Validate(cfg)) {
        setErr(error, errorSize, "Configuración de Evolution Arena inválida");
        return false;
    }

    /* Si hay modo clásico exacto (A..E o reference) con controlador utilidad,
     * preservar exactamente la secuencia de generación del escenario ecológico. */
    if (cfg->legacyScenario && cfg->controller == CREATURE_CONTROLLER_UTILITY &&
        cfg->obstacles == 0u && cfg->ponds == 2u && fabsf(cfg->arenaSize - 48.0f) < 1e-3f) {
        EcosystemConfig eco = {
            .seed = cfg->seed,
            .herbivores = cfg->brumacernos,
            .carnivores = cfg->velkares,
            .plants = cfg->plants,
            .capacity = cfg->capacity,
            .step = cfg->step,
            .day = cfg->day,
            .days = cfg->days,
            .scenario = cfg->legacyScenario
        };
        if (!EcosystemScenario_Create(w, &eco)) {
            setErr(error, errorSize, "Fallo al construir escenario clásico");
            return false;
        }
        return true;
    }

    /* Cargar cerebros importados si se indicaron rutas (validación previa al mundo). */
    NeuralGenome importedCommon = {0}, importedBrum = {0}, importedVelk = {0};
    bool hasCommon = false, hasBrum = false, hasVelk = false;
    if (cfg->founderBrainPath && cfg->founderBrainPath[0]) {
        if (!NeuralBrain_LoadFile(cfg->founderBrainPath, &importedCommon, error, errorSize)) return false;
        hasCommon = true;
    }
    if (cfg->brumacernoBrainPath && cfg->brumacernoBrainPath[0]) {
        if (!NeuralBrain_LoadFile(cfg->brumacernoBrainPath, &importedBrum, error, errorSize)) return false;
        hasBrum = true;
    }
    if (cfg->velkarBrainPath && cfg->velkarBrainPath[0]) {
        if (!NeuralBrain_LoadFile(cfg->velkarBrainPath, &importedVelk, error, errorSize)) return false;
        hasVelk = true;
    }
    if (cfg->controller == CREATURE_CONTROLLER_NEURAL &&
        cfg->founderMode == EVOLUTION_FOUNDERS_IMPORTED) {
        if (cfg->brumacernos > 0 && !hasBrum && !hasCommon) {
            setErr(error, errorSize, "Modo imported requiere --brain o --brumacerno-brain para Brumacerno");
            return false;
        }
        if (cfg->velkares > 0 && !hasVelk && !hasCommon) {
            setErr(error, errorSize, "Modo imported requiere --brain o --velkar-brain para Velkar");
            return false;
        }
    }

    float halfSize = cfg->arenaSize * 0.5f;
    unsigned resCap = cfg->plants + cfg->ponds + cfg->capacity + 16u;
    if (!EcosystemWorld_InitArena(w, cfg->capacity, resCap, 2u, halfSize, cfg->seed, cfg->step, cfg->day)) {
        setErr(error, errorSize, "No se pudo reservar memoria para Evolution Arena");
        return false;
    }

    w->defaultController = cfg->controller;
    w->neuralMutation = cfg->mutation;
    w->neuralController = cfg->neural;
    w->disablePhysicalMutation = cfg->disablePhysicalMutation;
    snprintf(w->founderModeName, sizeof(w->founderModeName), "%s",
             cfg->controller == CREATURE_CONTROLLER_NEURAL
                 ? EvolutionFounderMode_Name(cfg->founderMode)
                 : "utility");

    EcosystemSpecies brum, velk;
    if (!buildSpecies(&brum, &velk, cfg, error, errorSize)) goto failure;
    if (!EcosystemWorld_AddSpecies(w, brum) || !EcosystemWorld_AddSpecies(w, velk)) {
        setErr(error, errorSize, "Fallo al registrar especies Brumacerno y Velkar");
        goto failure;
    }

    /* Estanques de agua distribuidos simétricamente dentro de la arena física. */
    float pondOffset = fminf(halfSize * 0.42f, 8.5f);
    for (unsigned i = 0; i < cfg->ponds; ++i) {
        float angle = (cfg->ponds == 2u) ? (i == 0u ? -1.5707963f : 1.5707963f)
                                         : (6.2831853f * (float)i / (float)cfg->ponds);
        Vector3 pos = (cfg->ponds == 1u)
                          ? Vec3_Zero()
                          : Vec3_Create(sinf(angle) * pondOffset, 0.0f, cosf(angle) * pondOffset);
        if (!EcosystemWorld_AddResource(w, (EcosystemResource){
                .category = ECO_WATER, .infinite = true, .accessible = true, .nutritionalValue = 1.0f
            }, pos, 1.5f)) {
            setErr(error, errorSize, "No se pudo colocar estanque en la arena");
            goto failure;
        }
    }

    /* Obstáculos estáticos físicos (bloquean visión y navegación). */
    static const float obstaclePresets[5][3] = {
        {  0.0f,  0.0f, 1.35f },
        { -9.5f,  9.0f, 1.15f },
        {  9.5f, -9.0f, 1.15f },
        { -9.0f, -9.5f, 1.10f },
        {  9.0f,  9.5f, 1.10f }
    };
    for (unsigned i = 0; i < cfg->obstacles; ++i) {
        Vector3 pos;
        float radius = 1.15f;
        bool placed = false;
        if (i < 5u && halfSize >= 14.0f) {
            float scale = fminf(1.0f, (halfSize - 3.5f) / 11.5f);
            pos = Vec3_Create(obstaclePresets[i][0] * scale, 0.0f, obstaclePresets[i][1] * scale);
            radius = obstaclePresets[i][2];
            if (AIWorld_Walkable(&w->observations, pos, radius + 1.2f, false)) {
                placed = EcosystemWorld_AddObstacle(w, pos, radius, 2.2f) != 0;
            }
        }
        for (unsigned attempt = 0; attempt < 64u && !placed; ++attempt) {
            float span = fmaxf(2.0f, halfSize - 3.5f);
            pos = Vec3_Create(-span + CreatureGenome_Random(&w->random) * 2.0f * span, 0.0f,
                              -span + CreatureGenome_Random(&w->random) * 2.0f * span);
            radius = 0.9f + CreatureGenome_Random(&w->random) * 0.6f;
            if (AIWorld_Walkable(&w->observations, pos, radius + 1.5f, false)) {
                placed = EcosystemWorld_AddObstacle(w, pos, radius, 2.2f) != 0;
            }
        }
        if (!placed) {
            setErr(error, errorSize, "No se pudo colocar obstáculo estático");
            goto failure;
        }
    }

    /* Plantas regenerables sobre terreno transitable (evitando estanques y obstáculos). */
    bool scarce = cfg->legacyScenario && strcmp(cfg->legacyScenario, "scarce") == 0;
    float plantSpan = fmaxf(2.0f, halfSize - 2.2f);
    for (unsigned i = 0; i < cfg->plants; ++i) {
        bool placed = false;
        for (unsigned attempt = 0; attempt < 128u && !placed; ++attempt) {
            Vector3 p = Vec3_Create(-plantSpan + CreatureGenome_Random(&w->random) * 2.0f * plantSpan, 0.0f,
                                    -plantSpan + CreatureGenome_Random(&w->random) * 2.0f * plantSpan);
            if (!AIWorld_Walkable(&w->observations, p, 1.0f, false)) continue;
            float biomass = scarce ? 0.15f : 5.5f;
            float regen = scarce ? 0.0001f : 0.022f;
            placed = EcosystemWorld_AddResource(w, (EcosystemResource){
                .category = ECO_PLANT, .accessible = true, .biomass = biomass,
                .maximumBiomass = biomass, .regenerationRate = regen, .nutritionalValue = 0.30f
            }, p, 0.8f) != 0;
        }
        if (!placed) {
            setErr(error, errorSize, "No se pudo colocar vegetación en la arena");
            goto failure;
        }
    }

    /* Spawnear Brumacernos y Velkares respetando límites físicos y obstáculos. */
    float spawnSpan = fmaxf(2.0f, halfSize - 2.5f);
    for (unsigned species = 1u; species <= 2u; ++species) {
        unsigned count = (species == 1u) ? cfg->brumacernos : cfg->velkares;
        for (unsigned i = 0; i < count; ++i) {
            AIObjectId id = 0u;
            for (unsigned attempt = 0; attempt < 1000u && !id; ++attempt) {
                Vector3 p = Vec3_Create(-spawnSpan + CreatureGenome_Random(&w->random) * 2.0f * spawnSpan, 0.0f,
                                        -spawnSpan + CreatureGenome_Random(&w->random) * 2.0f * spawnSpan);
                float energy = scarce ? 0.30f : (species == 2u ? 0.58f : 0.85f);
                double age = 180.0 + CreatureGenome_Random(&w->random) * 180.0;
                id = EcosystemWorld_Spawn(w, species, p, age, energy, NULL);
            }
            if (!id) {
                setErr(error, errorSize, "Sin espacio libre para spawnear población inicial");
                goto failure;
            }
            if (cfg->controller == CREATURE_CONTROLLER_NEURAL) {
                const NeuralGenome *impB = hasBrum ? &importedBrum : (hasCommon ? &importedCommon : NULL);
                const NeuralGenome *impV = hasVelk ? &importedVelk : (hasCommon ? &importedCommon : NULL);
                if (!initializeFounderBrain(w, id, species, cfg, impB, impV, error, errorSize)) {
                    goto failure;
                }
            }
        }
    }

    EcosystemWorld_ComputeNeuralMetrics(w);
    return true;

failure:
    EcosystemWorld_Free(w);
    return false;
}
