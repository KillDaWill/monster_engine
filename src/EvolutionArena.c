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

const char *EvolutionArenaPreset_Name(EvolutionArenaPreset preset) {
    switch (preset) {
    case EVOLUTION_PRESET_SMALL:  return "small";
    case EVOLUTION_PRESET_MEDIUM: return "medium";
    case EVOLUTION_PRESET_LARGE:  return "large";
    case EVOLUTION_PRESET_STRESS: return "stress";
    default:                      return "custom";
    }
}

bool EvolutionArenaConfig_ApplyPreset(EvolutionArenaConfig *cfg, const char *name) {
    if (!cfg || !name) return false;
    if (strcmp(name, "small") == 0) {
        cfg->presetName = "small";
        cfg->arenaSize = 40.0f;
        cfg->brumacernos = 16u;
        cfg->velkares = 6u;
        cfg->plants = 36u;
        cfg->ponds = 2u;
        cfg->obstacles = 5u;
        cfg->capacity = 128u;
        return true;
    }
    if (strcmp(name, "medium") == 0) {
        cfg->presetName = "medium";
        cfg->arenaSize = 90.0f;
        cfg->brumacernos = 76u;
        cfg->velkares = 24u;
        cfg->plants = 160u;
        cfg->ponds = 8u;
        cfg->obstacles = 12u;
        cfg->capacity = 256u;
        return true;
    }
    if (strcmp(name, "large") == 0) {
        cfg->presetName = "large";
        cfg->arenaSize = 200.0f;
        cfg->brumacernos = 400u;
        cfg->velkares = 100u;
        cfg->plants = 800u;
        cfg->ponds = 40u;
        cfg->obstacles = 24u;
        cfg->capacity = 1024u;
        return true;
    }
    if (strcmp(name, "stress") == 0) {
        cfg->presetName = "stress";
        cfg->arenaSize = 280.0f;
        cfg->brumacernos = 800u;
        cfg->velkares = 200u;
        cfg->plants = 1600u;
        cfg->ponds = 80u;
        cfg->obstacles = 36u;
        cfg->capacity = 2048u;
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
        .workers = 0u,
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
        .legacyScenario = NULL,
        .presetName = "small",
        .metabolismMode = ECOSYSTEM_METABOLISM_LEGACY,
        .metabolismDir = "metabolism",
        .includeMixedSubstances = false
    };
}

EvolutionArenaConfig EvolutionArenaConfig_DefaultMolecular(void) {
    EvolutionArenaConfig cfg = EvolutionArenaConfig_Default();
    cfg.metabolismMode = ECOSYSTEM_METABOLISM_MOLECULAR;
    cfg.founderMode = EVOLUTION_FOUNDERS_RANDOM;
    cfg.includeMixedSubstances = true;
    return cfg;
}

bool EvolutionArenaConfig_ApplyLegacyScenario(EvolutionArenaConfig *cfg, const char *name) {
    if (!cfg || !name) return false;
    EcosystemConfig eco = EcosystemConfig_Default();
    if (!EcosystemConfig_Scenario(&eco, name)) return false;
    cfg->legacyScenario = eco.scenario;
    cfg->presetName = NULL;
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
    if (!isfinite(cfg->arenaSize) || cfg->arenaSize < 12.0f || cfg->arenaSize > 1000.0f) return false;
    if (cfg->capacity == 0 || cfg->capacity > 50000u) return false;
    if (cfg->brumacernos > cfg->capacity || cfg->velkares > cfg->capacity - cfg->brumacernos) return false;
    if (cfg->plants > 50000u || cfg->ponds > 512u || cfg->obstacles > 2048u) return false;
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
    } else if (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR) {
        if (!NeuralGenome_InitRandomV2(&genome, &w->random, 0.75f, cfg->mutation.weightLimit, lineageId)) {
            setErr(error, errorSize, "No se pudo inicializar cerebro aleatorio v2");
            return false;
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

/* Muestreo estratificado determinista en rejilla con jitter acotado. */
static Vector3 stratifiedCellPosition(unsigned index, unsigned total, float span, uint32_t *rng) {
    unsigned cols = (unsigned)ceil(sqrt((double)(total ? total : 1u)));
    unsigned rows = (total + cols - 1u) / cols;
    unsigned gx = index % cols;
    unsigned gz = index / cols;
    float cellW = (2.0f * span) / (float)cols;
    float cellH = (2.0f * span) / (float)rows;
    float jx = 0.2f + CreatureGenome_Random(rng) * 0.6f;
    float jz = 0.2f + CreatureGenome_Random(rng) * 0.6f;
    return Vec3_Create(-span + ((float)gx + jx) * cellW, 0.0f,
                       -span + ((float)gz + jz) * cellH);
}

bool EvolutionArena_Create(EcosystemWorld *w, const EvolutionArenaConfig *cfg,
                           char *error, size_t errorSize) {
    if (!w || !EvolutionArenaConfig_Validate(cfg)) {
        setErr(error, errorSize, "Configuración de Evolution Arena inválida");
        return false;
    }

    if (cfg->legacyScenario && cfg->controller == CREATURE_CONTROLLER_UTILITY &&
        cfg->obstacles == 0u && cfg->ponds == 2u && fabsf(cfg->arenaSize - 48.0f) < 1e-3f) {
        EcosystemConfig eco = {
            .seed = cfg->seed, .herbivores = cfg->brumacernos, .carnivores = cfg->velkares,
            .plants = cfg->plants, .capacity = cfg->capacity, .step = cfg->step,
            .day = cfg->day, .days = cfg->days, .scenario = cfg->legacyScenario
        };
        if (!EcosystemScenario_Create(w, &eco)) {
            setErr(error, errorSize, "Fallo al construir escenario clásico");
            return false;
        }
        return true;
    }

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
    unsigned extraMixed = (cfg->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR && cfg->includeMixedSubstances)
                              ? (cfg->plants / 3u + 16u) : 0u;
    unsigned resCap = cfg->plants + cfg->ponds + cfg->capacity + extraMixed + 64u;
    if (!EcosystemWorld_InitArena(w, cfg->capacity, resCap, 2u, halfSize, cfg->seed, cfg->step, cfg->day)) {
        setErr(error, errorSize, "No se pudo reservar memoria para Evolution Arena");
        return false;
    }

    if (cfg->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR) {
        const char *mDir = (cfg->metabolismDir && cfg->metabolismDir[0]) ? cfg->metabolismDir : "metabolism";
        if (!EcosystemWorld_LoadMolecularMetabolism(w, mDir, error, errorSize)) {
            goto failure;
        }
        if ((hasCommon && importedCommon.architecture != NEURAL_ARCH_CANDIDATE_POLICY) ||
            (hasBrum && importedBrum.architecture != NEURAL_ARCH_CANDIDATE_POLICY) ||
            (hasVelk && importedVelk.architecture != NEURAL_ARCH_CANDIDATE_POLICY)) {
            setErr(error, errorSize,
                   "Configuración incompatible: modo molecular requiere cerebros v2 (recurrent_candidate_policy)");
            goto failure;
        }
    } else {
        if ((hasCommon && importedCommon.architecture != NEURAL_ARCH_FIXED_RNN) ||
            (hasBrum && importedBrum.architecture != NEURAL_ARCH_FIXED_RNN) ||
            (hasVelk && importedVelk.architecture != NEURAL_ARCH_FIXED_RNN)) {
            setErr(error, errorSize,
                   "Configuración incompatible: modo legado requiere cerebros v1 (fixed_rnn)");
            goto failure;
        }
    }

    /* Configurar pool de trabajadores paralelos (0 = auto sólo en poblaciones grandes si no se pidió 1). */
    unsigned workers = cfg->workers;
    if (workers == 0u && (cfg->brumacernos + cfg->velkares) >= 64u) {
        workers = EvolutionJobSystem_RecommendedWorkers();
    } else if (workers == 0u) {
        workers = 1u;
    }
    EcosystemWorld_SetWorkerCount(w, workers);

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
    if (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR && w->metabolismDb) {
        brum.metabolicProfile = MetabolicProfileRegistry_Find(&w->metabolismDb->profiles, "brumacerno");
        brum.tissueSubstance = SubstanceRegistry_Find(&w->metabolismDb->substances, "brumacerno_tissue");
        brum.tissueMass = 5.5f;
        brum.physiology.dietMask = 0u;
        brum.physiology.preyMask = 0u;
        velk.metabolicProfile = MetabolicProfileRegistry_Find(&w->metabolismDb->profiles, "velkar");
        velk.tissueSubstance = SubstanceRegistry_Find(&w->metabolismDb->substances, "velkar_tissue");
        velk.tissueMass = 2.8f;
        velk.physiology.dietMask = 0u;
        velk.physiology.preyMask = 0u;
    }
    if (!EcosystemWorld_AddSpecies(w, brum) || !EcosystemWorld_AddSpecies(w, velk)) {
        setErr(error, errorSize, "Fallo al registrar especies Brumacerno y Velkar");
        goto failure;
    }

    bool distributedLayout = (cfg->ponds > 2u || cfg->arenaSize > 50.0f);
    bool molecular = (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR && w->metabolismDb != NULL);
    const SubstanceDef *cleanWaterSub = molecular
        ? SubstanceRegistry_Get(&w->metabolismDb->substances, SubstanceRegistry_Find(&w->metabolismDb->substances, "clean_water"))
        : NULL;
    const SubstanceDef *contWaterSub = molecular
        ? SubstanceRegistry_Get(&w->metabolismDb->substances, SubstanceRegistry_Find(&w->metabolismDb->substances, "contaminated_water"))
        : NULL;
    const SubstanceDef *grassSub = molecular
        ? SubstanceRegistry_Get(&w->metabolismDb->substances, SubstanceRegistry_Find(&w->metabolismDb->substances, "grass"))
        : NULL;
    const SubstanceDef *fruitSub = molecular
        ? SubstanceRegistry_Get(&w->metabolismDb->substances, SubstanceRegistry_Find(&w->metabolismDb->substances, "fruit"))
        : NULL;
    const SubstanceDef *toxicSub = molecular
        ? SubstanceRegistry_Get(&w->metabolismDb->substances, SubstanceRegistry_Find(&w->metabolismDb->substances, "toxic_plant"))
        : NULL;
    const SubstanceDef *rottenSub = molecular
        ? SubstanceRegistry_Get(&w->metabolismDb->substances, SubstanceRegistry_Find(&w->metabolismDb->substances, "rotten_meat"))
        : NULL;
    const SubstanceDef *lethalSub = molecular
        ? SubstanceRegistry_Get(&w->metabolismDb->substances, SubstanceRegistry_Find(&w->metabolismDb->substances, "deadly_nightshade"))
        : NULL;

    /* 1) Estanques de agua: distribución estratificada en arenas grandes, simétrica en arena pequeña. */
    if (!distributedLayout) {
        float pondOffset = fminf(halfSize * 0.42f, 8.5f);
        for (unsigned i = 0; i < cfg->ponds; ++i) {
            float angle = (cfg->ponds == 2u) ? (i == 0u ? -1.5707963f : 1.5707963f)
                                             : (6.2831853f * (float)i / (float)cfg->ponds);
            Vector3 pos = (cfg->ponds == 1u)
                              ? Vec3_Zero()
                              : Vec3_Create(sinf(angle) * pondOffset, 0.0f, cosf(angle) * pondOffset);
            EcosystemResource pondRes = molecular && cleanWaterSub
                ? EcosystemResource_FromSubstance(cleanWaterSub, 0.0f, 0.0f, RESOURCE_REGEN_INFINITE, 0.0f,
                                                  RESOURCE_DECAY_NONE, 0.0, 0.0f, MOLECULE_INVALID_HANDLE)
                : (EcosystemResource){.category = ECO_WATER, .infinite = true, .accessible = true, .nutritionalValue = 1.0f};
            if (!EcosystemWorld_AddResource(w, pondRes, pos, 1.5f)) {
                setErr(error, errorSize, "No se pudo colocar estanque en la arena");
                goto failure;
            }
        }
    } else {
        float pondSpan = fmaxf(4.0f, halfSize - 5.0f);
        for (unsigned i = 0; i < cfg->ponds; ++i) {
            bool placed = false;
            float pondRadius = 1.8f;
            for (unsigned attempt = 0; attempt < 64u && !placed; ++attempt) {
                Vector3 pos = (attempt < 16u)
                                  ? stratifiedCellPosition(i, cfg->ponds, pondSpan, &w->random)
                                  : Vec3_Create(-pondSpan + CreatureGenome_Random(&w->random) * 2.0f * pondSpan,
                                                0.0f,
                                                -pondSpan + CreatureGenome_Random(&w->random) * 2.0f * pondSpan);
                if (!AIWorld_Walkable(&w->observations, pos, pondRadius + 3.0f, false)) continue;
                EcosystemResource pondRes = molecular && cleanWaterSub
                    ? EcosystemResource_FromSubstance(cleanWaterSub, 0.0f, 0.0f, RESOURCE_REGEN_INFINITE, 0.0f,
                                                      RESOURCE_DECAY_NONE, 0.0, 0.0f, MOLECULE_INVALID_HANDLE)
                    : (EcosystemResource){.category = ECO_WATER, .infinite = true, .accessible = true, .nutritionalValue = 1.0f};
                placed = EcosystemWorld_AddResource(w, pondRes, pos, pondRadius) != 0;
            }
            if (!placed) {
                setErr(error, errorSize, "No se pudo colocar estanque distribuido en la arena");
                goto failure;
            }
        }
    }

    if (molecular && cfg->includeMixedSubstances && contWaterSub && halfSize >= 14.0f) {
        Vector3 cwPos = Vec3_Create(0.0f, 0.0f, fminf(halfSize * 0.48f, 9.5f));
        if (AIWorld_Walkable(&w->observations, cwPos, 2.2f, false)) {
            EcosystemResource cw = EcosystemResource_FromSubstance(
                contWaterSub, 0.0f, 0.0f, RESOURCE_REGEN_INFINITE, 0.0f,
                RESOURCE_DECAY_NONE, 0.0, 0.0f, MOLECULE_INVALID_HANDLE);
            EcosystemWorld_AddMolecularResource(w, cw, cwPos, 1.3f);
        }
    }

    /* 2) Obstáculos estáticos físicos (bloquean visión y navegación). */
    static const float obstaclePresets[5][3] = {
        {  0.0f,  0.0f, 1.35f },
        { -9.5f,  9.0f, 1.15f },
        {  9.5f, -9.0f, 1.15f },
        { -9.0f, -9.5f, 1.10f },
        {  9.0f,  9.5f, 1.10f }
    };
    float obsSpan = fmaxf(2.0f, halfSize - 4.0f);
    for (unsigned i = 0; i < cfg->obstacles; ++i) {
        Vector3 pos;
        float radius = 1.15f;
        bool placed = false;
        if (!distributedLayout && i < 5u && halfSize >= 14.0f) {
            float scale = fminf(1.0f, (halfSize - 3.5f) / 11.5f);
            pos = Vec3_Create(obstaclePresets[i][0] * scale, 0.0f, obstaclePresets[i][1] * scale);
            radius = obstaclePresets[i][2];
            if (AIWorld_Walkable(&w->observations, pos, radius + 1.2f, false)) {
                placed = EcosystemWorld_AddObstacle(w, pos, radius, 2.2f) != 0;
            }
        }
        for (unsigned attempt = 0; attempt < 64u && !placed; ++attempt) {
            pos = (distributedLayout && attempt < 16u)
                      ? stratifiedCellPosition(i, cfg->obstacles, obsSpan, &w->random)
                      : Vec3_Create(-obsSpan + CreatureGenome_Random(&w->random) * 2.0f * obsSpan, 0.0f,
                                    -obsSpan + CreatureGenome_Random(&w->random) * 2.0f * obsSpan);
            radius = 0.9f + CreatureGenome_Random(&w->random) * 0.7f;
            if (AIWorld_Walkable(&w->observations, pos, radius + 2.2f, false)) {
                placed = EcosystemWorld_AddObstacle(w, pos, radius, 2.2f) != 0;
            }
        }
        if (!placed) {
            setErr(error, errorSize, "No se pudo colocar obstáculo estático");
            goto failure;
        }
    }

    /* 3) Plantas regenerables distribuidas sobre terreno transitable. */
    bool scarce = cfg->legacyScenario && strcmp(cfg->legacyScenario, "scarce") == 0;
    float plantSpan = fmaxf(2.0f, halfSize - 2.2f);
    for (unsigned i = 0; i < cfg->plants; ++i) {
        bool placed = false;
        for (unsigned attempt = 0; attempt < 128u && !placed; ++attempt) {
            Vector3 p = (distributedLayout && attempt < 12u)
                            ? stratifiedCellPosition(i, cfg->plants, plantSpan, &w->random)
                            : Vec3_Create(-plantSpan + CreatureGenome_Random(&w->random) * 2.0f * plantSpan, 0.0f,
                                          -plantSpan + CreatureGenome_Random(&w->random) * 2.0f * plantSpan);
            if (!AIWorld_Walkable(&w->observations, p, 1.0f, false)) continue;
            float biomass = scarce ? 0.15f : 5.5f;
            float regen = scarce ? 0.0001f : 0.022f;
            EcosystemResource plantRes;
            if (molecular && grassSub) {
                const SubstanceDef *chosenSub = grassSub;
                ResourceRegenPolicy rp = RESOURCE_REGEN_DEPLETE_RELOCATE;
                ResourceDecayPolicy dp = RESOURCE_DECAY_NONE;
                double life = 0.0;
                float dec = 0.0f;
                float subMass = fminf(biomass, 2.8f);
                float respawnDelay = 18.0f;
                float rarity = 0.90f;
                float minReloc = fminf(halfSize * 0.45f, 8.0f);
                if (cfg->includeMixedSubstances) {
                    if (fruitSub && (i % 8u == 1u)) {
                        chosenSub = fruitSub;
                        subMass = 1.6f;
                        respawnDelay = 65.0f;
                        rarity = 0.45f;
                        minReloc = fminf(halfSize * 0.60f, 11.0f);
                    } else if (toxicSub && (i % 9u == 4u)) {
                        chosenSub = toxicSub;
                        subMass = 3.2f;
                        respawnDelay = 28.0f;
                        rarity = 0.75f;
                    } else if (lethalSub && (i % 13u == 10u)) {
                        chosenSub = lethalSub;
                        subMass = 1.8f;
                        respawnDelay = 45.0f;
                        rarity = 0.60f;
                        minReloc = fminf(halfSize * 0.55f, 10.0f);
                    } else if (rottenSub && (i % 12u == 7u)) {
                        chosenSub = rottenSub;
                        subMass = biomass;
                        rp = RESOURCE_REGEN_NONE;
                        dp = RESOURCE_DECAY_DECOMPOSE;
                        life = 360.0;
                        dec = 0.003f;
                    }
                }
                plantRes = EcosystemResource_FromSubstance(chosenSub, subMass, subMass, rp, regen,
                                                           dp, life, dec, w->decayTargetMolecule);
                plantRes.respawnDelaySeconds = respawnDelay;
                plantRes.rarityWeight = rarity;
                plantRes.minRelocateDistance = minReloc;
                plantRes.spawnMinDist = 2.5f;
                plantRes.spawnMaxDist = fmaxf(4.0f, halfSize - 2.5f);
            } else {
                plantRes = (EcosystemResource){
                    .category = ECO_PLANT, .accessible = true, .biomass = biomass,
                    .maximumBiomass = biomass, .regenerationRate = regen, .nutritionalValue = 0.30f
                };
            }
            placed = EcosystemWorld_AddResource(w, plantRes, p, 0.8f) != 0;
        }
        if (!placed) {
            setErr(error, errorSize, "No se pudo colocar vegetación en la arena");
            goto failure;
        }
    }

    /* 4) Spawnear Brumacernos y Velkares respetando límites físicos y obstáculos. */
    float spawnSpan = fmaxf(2.0f, halfSize - 2.5f);
    unsigned totalInitial = cfg->brumacernos + cfg->velkares;
    unsigned spawnCursor = 0u;
    for (unsigned species = 1u; species <= 2u; ++species) {
        unsigned count = (species == 1u) ? cfg->brumacernos : cfg->velkares;
        for (unsigned i = 0; i < count; ++i, ++spawnCursor) {
            AIObjectId id = 0u;
            for (unsigned attempt = 0; attempt < 1000u && !id; ++attempt) {
                Vector3 p = (distributedLayout && attempt < 16u)
                                ? stratifiedCellPosition(spawnCursor, totalInitial, spawnSpan, &w->random)
                                : Vec3_Create(-spawnSpan + CreatureGenome_Random(&w->random) * 2.0f * spawnSpan, 0.0f,
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

    CreatureNavigation_EnsureCache(&w->observations);
    EcosystemWorld_ComputeNeuralMetrics(w);
    return true;

failure:
    EcosystemWorld_Free(w);
    return false;
}
