#include "EcosystemWorld.h"
#include "CreatureActions.h"
#include "CreatureNavigation.h"
#include "EcosystemAnatomy.h"
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void invalidate(EcosystemWorld *, AIObjectId);
static void statistics(EcosystemWorld *);
static float clamp(float v, float a, float b) { return fmaxf(a, fminf(b, v)); }
static bool finitePosition(Vector3 p) { return isfinite(p.x) && isfinite(p.y) && isfinite(p.z); }

/* ========================================================================== */
/* Tabla hash abierta O(1) con borrado backward-shift                         */
/* ========================================================================== */

static uint32_t nextPow2U32(uint32_t v) {
    uint32_t p = 16u;
    while (p < v && p < (1u << 28)) p <<= 1;
    return p;
}

static inline uint32_t hashU32(uint32_t k, uint32_t mask) {
    return (k * 2654435761u) & mask;
}

static bool idMapInit(EcosystemIdMap *m, uint32_t expectedItems) {
    uint32_t cap = nextPow2U32(expectedItems * 2u + 8u);
    m->keys = calloc(cap, sizeof(uint32_t));
    m->values = calloc(cap, sizeof(uint32_t));
    if (!m->keys || !m->values) {
        free(m->keys);
        free(m->values);
        memset(m, 0, sizeof(*m));
        return false;
    }
    m->capacity = cap;
    m->mask = cap - 1u;
    return true;
}

static void idMapFree(EcosystemIdMap *m) {
    if (!m) return;
    free(m->keys);
    free(m->values);
    memset(m, 0, sizeof(*m));
}

static bool idMapGrow(EcosystemIdMap *m, uint32_t newExpected) {
    EcosystemIdMap next = {0};
    if (!idMapInit(&next, newExpected)) return false;
    for (uint32_t i = 0; i < m->capacity; ++i) {
        uint32_t k = m->keys[i];
        if (!k) continue;
        uint32_t pos = hashU32(k, next.mask);
        while (next.keys[pos] != 0u) pos = (pos + 1u) & next.mask;
        next.keys[pos] = k;
        next.values[pos] = m->values[i];
    }
    idMapFree(m);
    *m = next;
    return true;
}

static void idMapPut(EcosystemIdMap *m, uint32_t key, uint32_t val) {
    if (!m->keys || !key) return;
    uint32_t pos = hashU32(key, m->mask);
    while (m->keys[pos] != 0u && m->keys[pos] != key) {
        pos = (pos + 1u) & m->mask;
    }
    m->keys[pos] = key;
    m->values[pos] = val;
}

static int idMapGet(const EcosystemIdMap *m, uint32_t key) {
    if (!m->keys || !key) return -1;
    uint32_t pos = hashU32(key, m->mask);
    while (m->keys[pos] != 0u) {
        if (m->keys[pos] == key) return (int)m->values[pos];
        pos = (pos + 1u) & m->mask;
    }
    return -1;
}

static void idMapRemove(EcosystemIdMap *m, uint32_t key) {
    if (!m->keys || !key) return;
    uint32_t mask = m->mask;
    uint32_t pos = hashU32(key, mask);
    while (m->keys[pos] != 0u) {
        if (m->keys[pos] == key) {
            uint32_t hole = pos;
            uint32_t cur = (hole + 1u) & mask;
            while (m->keys[cur] != 0u) {
                uint32_t home = hashU32(m->keys[cur], mask);
                uint32_t distCur = (cur - home) & mask;
                uint32_t distHole = (hole - home) & mask;
                if (distHole < distCur) {
                    m->keys[hole] = m->keys[cur];
                    m->values[hole] = m->values[cur];
                    hole = cur;
                }
                cur = (cur + 1u) & mask;
            }
            m->keys[hole] = 0u;
            m->values[hole] = 0u;
            return;
        }
        pos = (pos + 1u) & mask;
    }
}

/* ========================================================================== */
/* Eventos, bitácora, linajes, fundadores e historial parental                */
/* ========================================================================== */

static void event(EcosystemWorld *w, unsigned kind, AIObjectId a, AIObjectId b) {
    if (w->eventOutput)
        fprintf(w->eventOutput, "%llu,%.9f,%u,%u,%u\n", (unsigned long long)w->clock.tick,
                w->clock.seconds, kind, a, b);
    uint64_t v[] = {w->clock.tick, kind, a, b};
    for (unsigned i = 0; i < 4; ++i) {
        w->statistics.eventHash ^= v[i];
        w->statistics.eventHash *= UINT64_C(1099511628211);
    }
}

static void recordIntervention(EcosystemWorld *w, const char *action, AIObjectId a, AIObjectId b,
                               uint64_t oldA, uint64_t newA, uint64_t oldB, uint64_t newB) {
    if (!w) return;
    EcosystemIntervention *slot =
        &w->interventions[w->interventionCount % ECOSYSTEM_INTERVENTION_LOG_CAPACITY];
    memset(slot, 0, sizeof(*slot));
    slot->tick = w->clock.tick;
    slot->seconds = w->clock.seconds;
    snprintf(slot->action, sizeof(slot->action), "%s", action ? action : "assign");
    slot->actorA = a; slot->actorB = b;
    slot->oldFingerprintA = oldA; slot->newFingerprintA = newA;
    slot->oldFingerprintB = oldB; slot->newFingerprintB = newB;
    ++w->interventionCount;
}

static EcosystemLineage *findLineage(EcosystemWorld *w, uint64_t lineageId) {
    if (!w || !w->lineages || !lineageId) return NULL;
    for (unsigned i = 0; i < w->lineageCount; ++i) {
        if (w->lineages[i].lineageId == lineageId) return &w->lineages[i];
    }
    return NULL;
}

static EcosystemLineage *findOrCreateLineage(EcosystemWorld *w, uint64_t lineageId,
                                             uint64_t externalLineageId,
                                             uint64_t founderFp, unsigned speciesId) {
    if (!w || !lineageId) return NULL;
    EcosystemLineage *existing = findLineage(w, lineageId);
    if (existing) {
        if (speciesId > 0u && speciesId <= 32u) existing->speciesMask |= (1u << (speciesId - 1u));
        return existing;
    }
    if (w->lineageCount >= w->lineageCapacity) {
        unsigned nextCap = w->lineageCapacity ? w->lineageCapacity * 2u : ECOSYSTEM_INITIAL_LINEAGE_CAPACITY;
        if (nextCap > 65536u) return NULL;
        EcosystemLineage *grown = realloc(w->lineages, nextCap * sizeof(*grown));
        if (!grown) return NULL;
        w->lineages = grown;
        w->lineageCapacity = nextCap;
    }
    EcosystemLineage *l = &w->lineages[w->lineageCount++];
    memset(l, 0, sizeof(*l));
    l->lineageId = lineageId;
    l->externalLineageId = externalLineageId;
    l->founderFingerprint = founderFp;
    l->speciesId = speciesId;
    if (speciesId > 0u && speciesId <= 32u) l->speciesMask = (1u << (speciesId - 1u));
    l->firstSeenSeconds = w->clock.seconds;
    l->lastActiveSeconds = w->clock.seconds;
    return l;
}

static void registerFounderGenome(EcosystemWorld *w, const NeuralGenome *g) {
    if (!w || !NeuralGenome_Validate(g)) return;
    uint64_t lid = g->metadata.lineageId;
    if (!lid) return;
    for (unsigned i = 0; i < w->founderCount; ++i) {
        if (w->founders[i].lineageId == lid) return;
    }
    if (w->founderCount >= w->founderCapacity) {
        unsigned nextCap = w->founderCapacity ? w->founderCapacity * 2u : ECOSYSTEM_INITIAL_FOUNDER_CAPACITY;
        if (nextCap > 65536u) return;
        EcosystemFounderRecord *grown = realloc(w->founders, nextCap * sizeof(*grown));
        if (!grown) return;
        w->founders = grown;
        w->founderCapacity = nextCap;
    }
    EcosystemFounderRecord *rec = &w->founders[w->founderCount++];
    memset(rec, 0, sizeof(*rec));
    rec->lineageId = lid;
    rec->fingerprint = g->metadata.fingerprint;
    NeuralGenome_Clone(&rec->genome, g);
}

static float computeFounderDistance(const EcosystemWorld *w, const NeuralGenome *g, bool *missingOut) {
    if (missingOut) *missingOut = false;
    if (!w || !w->founders || !g || !g->valid) return 0.0f;
    uint64_t lid = g->metadata.lineageId;
    for (unsigned i = 0; i < w->founderCount; ++i) {
        if (w->founders[i].lineageId == lid) {
            return NeuralGenome_Distance(g, &w->founders[i].genome);
        }
    }
    /* Jamás comparar silenciosamente contra un fundador no relacionado. */
    if (missingOut) *missingOut = true;
    return 0.0f;
}

static EcosystemParentRecord *ensureParentRecord(EcosystemWorld *w, AIObjectId id, bool alive) {
    if (!w || !id) return NULL;
    int idx = idMapGet(&w->parentHistoryMap, id);
    if (idx >= 0 && (unsigned)idx < w->parentHistoryCount) {
        return &w->parentHistory[idx];
    }
    if (w->parentHistoryCount >= w->parentHistoryCapacity) {
        unsigned nextCap = w->parentHistoryCapacity ? w->parentHistoryCapacity * 2u : 512u;
        if (nextCap > 131072u) return NULL;
        EcosystemParentRecord *grown = realloc(w->parentHistory, nextCap * sizeof(*grown));
        if (!grown) return NULL;
        w->parentHistory = grown;
        w->parentHistoryCapacity = nextCap;
        if (w->parentHistoryCount * 2u + 8u >= w->parentHistoryMap.capacity) {
            idMapGrow(&w->parentHistoryMap, nextCap);
        }
    }
    unsigned slot = w->parentHistoryCount++;
    w->parentHistory[slot] = (EcosystemParentRecord){
        .entityId = id, .offspringCount = 0u, .matureOffspringCount = 0u,
        .reproductiveOffspringCount = 0u, .alive = alive
    };
    idMapPut(&w->parentHistoryMap, id, slot);
    return &w->parentHistory[slot];
}

const EcosystemParentRecord *EcosystemWorld_GetParentRecord(const EcosystemWorld *w, AIObjectId id) {
    if (!w || !id) return NULL;
    int idx = idMapGet(&w->parentHistoryMap, id);
    if (idx >= 0 && (unsigned)idx < w->parentHistoryCount) return &w->parentHistory[idx];
    return NULL;
}

static double observedSurvivalSeconds(const EcosystemWorld *w, const EcosystemEntity *e) {
    double s = w->clock.seconds - e->bornAt;
    return s > 0.0 ? s : 0.0;
}

static void updateArchiveEntry(EcosystemArchiveEntry *dst, const EcosystemWorld *w,
                               const EcosystemEntity *e, bool stillLiving) {
    if (!dst || !w || !e || e->mind.controllerType != CREATURE_CONTROLLER_NEURAL) return;
    const NeuralGenome *g = CreatureMind_GetNeuralGenome(&e->mind);
    if (!g) return;
    dst->valid = true;
    dst->stillLiving = stillLiving;
    dst->entityId = e->id;
    dst->speciesId = w->species[e->speciesIndex].id;
    dst->generation = e->generation;
    dst->offspringCount = e->offspringCount;
    dst->survivalSeconds = observedSurvivalSeconds(w, e);
    dst->biologicalAge = e->physiology.age;
    dst->lineageId = g->metadata.lineageId;
    NeuralGenome_Clone(&dst->genome, g);
}

static void markArchiveDead(EcosystemWorld *w, AIObjectId id) {
    if (w->archiveBestOffspring.valid && w->archiveBestOffspring.entityId == id)
        w->archiveBestOffspring.stillLiving = false;
    if (w->archiveLongestSurvival.valid && w->archiveLongestSurvival.entityId == id)
        w->archiveLongestSurvival.stillLiving = false;
    if (w->archiveHighestGeneration.valid && w->archiveHighestGeneration.entityId == id)
        w->archiveHighestGeneration.stillLiving = false;
}

static void syncArchiveOffspring(EcosystemWorld *w, AIObjectId parentId, unsigned newOffspring) {
    if (w->archiveBestOffspring.valid && w->archiveBestOffspring.entityId == parentId)
        w->archiveBestOffspring.offspringCount = newOffspring;
    if (w->archiveLongestSurvival.valid && w->archiveLongestSurvival.entityId == parentId)
        w->archiveLongestSurvival.offspringCount = newOffspring;
    if (w->archiveHighestGeneration.valid && w->archiveHighestGeneration.entityId == parentId)
        w->archiveHighestGeneration.offspringCount = newOffspring;
}

static void considerForArchives(EcosystemWorld *w, const EcosystemEntity *e, bool stillLiving) {
    if (!w || !e || e->mind.controllerType != CREATURE_CONTROLLER_NEURAL) return;
    if (!CreatureMind_GetNeuralGenome(&e->mind)) return;
    double surv = observedSurvivalSeconds(w, e);
    if (!w->archiveBestOffspring.valid ||
        e->offspringCount > w->archiveBestOffspring.offspringCount ||
        (e->offspringCount == w->archiveBestOffspring.offspringCount &&
         surv > w->archiveBestOffspring.survivalSeconds)) {
        updateArchiveEntry(&w->archiveBestOffspring, w, e, stillLiving);
    }
    if (!w->archiveLongestSurvival.valid || surv > w->archiveLongestSurvival.survivalSeconds) {
        updateArchiveEntry(&w->archiveLongestSurvival, w, e, stillLiving);
    }
    if (!w->archiveHighestGeneration.valid ||
        e->generation > w->archiveHighestGeneration.generation ||
        (e->generation == w->archiveHighestGeneration.generation &&
         e->offspringCount > w->archiveHighestGeneration.offspringCount)) {
        updateArchiveEntry(&w->archiveHighestGeneration, w, e, stillLiving);
    }
}

/* ========================================================================== */
/* Ciclo de vida del mundo                                                    */
/* ========================================================================== */

EcosystemSpecies EcosystemSpecies_Default(unsigned id, unsigned category, unsigned diet,
                                          unsigned prey, unsigned threats) {
    bool carnivore = category == ECO_MEAT;
    EcosystemSpecies s = {
        .id = id, .category = category, .recipeId = carnivore ? 4u : 3u,
        .brain = *BrainProfile_Find("forager"),
        .physiology = CreaturePhysiology_Default(),
        .reproduction = CreatureReproduction_Default(),
        .speed = carnivore ? 2.8f : 2.2f,
        .radius = carnivore ? 0.206f : 0.278f,
        .oralReach = carnivore ? 0.785f : 1.589f,
        .attackRange = carnivore ? 0.69f : 1.398f,
        .damage = 0.36f, .attackCooldown = 0.8f, .attackCost = 0.008f,
        .edibleBiomass = carnivore ? 2.332f : 5.515f,
        .nutrition = carnivore ? 0.480f : 0.203f,
        .metabolicProfile = METABOLIC_PROFILE_INVALID_HANDLE,
        .tissueSubstance = SUBSTANCE_INVALID_HANDLE,
        .tissueMass = carnivore ? 2.8f : 5.5f
    };
    snprintf(s.name, sizeof(s.name), "perfil_%u", id);
    s.physiology.dietMask = diet;
    s.physiology.preyMask = prey;
    s.physiology.canAttackPhysically = (prey != 0);
    s.physiology.waterInteraction = true;
    s.brain.name = "ecologico"; s.brain.dietMask = diet; s.brain.preyMask = prey; s.brain.threatMask = threats;
    s.brain.dangerDistance = 5; s.brain.perception.range = 14; s.brain.perception.horizontalFOV = 5.5f;
    s.brain.perception.verticalFOV = 3.14f; s.brain.perception.memoryDuration = 30;
    s.brain.needs[NEED_HUNGER].exponent = 1; s.brain.needs[NEED_HYDRATION].exponent = 1;
    s.brain.weights[ACTION_WANDER] = 0.08f; s.brain.weights[ACTION_SEARCH_FOOD] = 0.7f;
    s.brain.weights[ACTION_APPROACH_FOOD] = 2.4f; s.brain.weights[ACTION_SEARCH_WATER] = 0.65f;
    s.brain.weights[ACTION_APPROACH_WATER] = 2.3f; s.brain.weights[ACTION_REST] = 1.5f;
    s.brain.weights[ACTION_CHASE] = prey ? 2.5f : 0; s.brain.weights[ACTION_ESCAPE] = threats ? 5 : 0;
    s.brain.weights[ACTION_APPROACH_MATE] = 0.6f; s.brain.switchMargin = 0.04f; s.brain.commitment = 0.5f;
    s.brain.needs[NEED_HUNGER].satisfactionRate = 0.12f;
    s.brain.needs[NEED_HYDRATION].satisfactionRate = 0.15f;
    s.physiology.maximumAge = 1800; s.physiology.oldAge = 1300;
    return s;
}

bool EcosystemWorld_InitArena(EcosystemWorld *w, unsigned cap, unsigned resources, unsigned species,
                              float arenaHalfSize, uint32_t seed, double step, double day) {
    if (!w) return false;
    memset(w, 0, sizeof(*w));
    if (!cap || cap > 100000 || !resources || resources > 100000 || !species || species > 1024 ||
        !isfinite(arenaHalfSize) || arenaHalfSize < 4.0f || arenaHalfSize > 500.0f ||
        !SimulationClock_Init(&w->clock, step, day, 64))
        return false;
    unsigned aiCap = cap + resources + 256u;
    if (!AIWorld_InitCapacity(&w->observations, arenaHalfSize, aiCap)) return false;

    w->entities = calloc(cap, sizeof(*w->entities));
    w->resources = calloc(resources, sizeof(*w->resources));
    w->species = calloc(species, sizeof(*w->species));
    w->birthQueue = calloc(cap, sizeof(*w->birthQueue));
    w->order = calloc(cap, sizeof(*w->order));
    w->moveProposals = calloc(cap, sizeof(*w->moveProposals));

    w->activeEntitySlots = malloc(cap * sizeof(unsigned));
    w->entitySlotToActive = malloc(cap * sizeof(unsigned));
    w->freeEntitySlots = malloc(cap * sizeof(unsigned));
    w->activeResourceSlots = malloc(resources * sizeof(unsigned));
    w->resourceSlotToActive = malloc(resources * sizeof(unsigned));
    w->freeResourceSlots = malloc(resources * sizeof(unsigned));

    w->obstacleCapacity = ECOSYSTEM_DEFAULT_OBSTACLE_CAPACITY;
    w->obstacles = calloc(w->obstacleCapacity, sizeof(*w->obstacles));
    w->lineageCapacity = cap > ECOSYSTEM_INITIAL_LINEAGE_CAPACITY ? cap : ECOSYSTEM_INITIAL_LINEAGE_CAPACITY;
    w->lineages = calloc(w->lineageCapacity, sizeof(*w->lineages));
    w->founderCapacity = cap > ECOSYSTEM_INITIAL_FOUNDER_CAPACITY ? cap : ECOSYSTEM_INITIAL_FOUNDER_CAPACITY;
    w->founders = calloc(w->founderCapacity, sizeof(*w->founders));
    w->parentHistoryCapacity = cap * 2u > 512u ? cap * 2u : 512u;
    w->parentHistory = calloc(w->parentHistoryCapacity, sizeof(*w->parentHistory));

    bool mapsOk = idMapInit(&w->entityMap, cap) &&
                  idMapInit(&w->resourceMap, resources) &&
                  idMapInit(&w->parentHistoryMap, w->parentHistoryCapacity);

    if (!w->entities || !w->resources || !w->species || !w->birthQueue || !w->order ||
        !w->moveProposals || !w->activeEntitySlots || !w->entitySlotToActive || !w->freeEntitySlots ||
        !w->activeResourceSlots || !w->resourceSlotToActive || !w->freeResourceSlots ||
        !w->obstacles || !w->lineages || !w->founders || !w->parentHistory || !mapsOk) {
        EcosystemWorld_Free(w);
        return false;
    }

    for (unsigned i = 0; i < cap; ++i) {
        w->freeEntitySlots[i] = cap - 1u - i;
        w->entitySlotToActive[i] = UINT_MAX;
    }
    w->freeEntityCount = cap;
    for (unsigned i = 0; i < resources; ++i) {
        w->freeResourceSlots[i] = resources - 1u - i;
        w->resourceSlotToActive[i] = UINT_MAX;
    }
    w->freeResourceCount = resources;

    w->capacity = cap;
    w->resourceCapacity = resources;
    w->speciesCapacity = species;
    w->random = seed ? seed : 1;
    w->initialSeed = seed;
    w->statistics.eventHash = UINT64_C(1469598103934665603);
    w->defaultController = CREATURE_CONTROLLER_UTILITY;
    w->defaultArchitecture = NEURAL_ARCH_FIXED_RNN;
    w->metabolismMode = ECOSYSTEM_METABOLISM_LEGACY;
    w->decayTargetMolecule = MOLECULE_INVALID_HANDLE;
    w->neuralMutation = NeuralMutationConfig_Default();
    w->neuralController = NeuralControllerConfig_Default();
    w->nextLineageId = 1u;
    w->configuredWorkers = 1u;
    snprintf(w->founderModeName, sizeof(w->founderModeName), "utility");
    return true;
}

bool EcosystemWorld_Init(EcosystemWorld *w, unsigned cap, unsigned resources, unsigned species,
                         uint32_t seed, double step, double day) {
    return EcosystemWorld_InitArena(w, cap, resources, species, 24.0f, seed, step, day);
}

bool EcosystemWorld_SetWorkerCount(EcosystemWorld *w, unsigned workerCount) {
    if (!w || w->updating) return false;
    if (w->jobs) {
        EvolutionJobSystem_Destroy(w->jobs);
        w->jobs = NULL;
    }
    if (workerCount == 1u) {
        w->configuredWorkers = 1u;
        return true;
    }
    w->jobs = EvolutionJobSystem_Create(workerCount);
    if (!w->jobs) {
        w->configuredWorkers = 1u;
        return false;
    }
    w->configuredWorkers = EvolutionJobSystem_WorkerCount(w->jobs);
    return true;
}

unsigned EcosystemWorld_WorkerCount(const EcosystemWorld *w) {
    if (!w) return 1u;
    return w->jobs ? EvolutionJobSystem_WorkerCount(w->jobs) : 1u;
}

void EcosystemWorld_SetProfiler(EcosystemWorld *w, EvolutionProfiler *profiler) {
    if (w) w->profiler = profiler;
}

bool EcosystemWorld_EnableMolecularMetabolism(EcosystemWorld *w, const MetabolismDatabase *db) {
    if (!w || w->updating || !db || !db->loaded || !db->frozen) return false;
    w->metabolismDb = db;
    w->metabolismMode = ECOSYSTEM_METABOLISM_MOLECULAR;
    w->defaultArchitecture = NEURAL_ARCH_CANDIDATE_POLICY;
    w->decayTargetMolecule = MoleculeRegistry_Find(&db->molecules, "decay_products");
    return true;
}

bool EcosystemWorld_LoadMolecularMetabolism(EcosystemWorld *w, const char *rootDir,
                                            char *error, size_t errorSize) {
    if (!w || w->updating || !rootDir || !rootDir[0]) return false;
    MetabolismDatabase *db = calloc(1, sizeof(*db));
    if (!db) {
        if (error && errorSize) snprintf(error, errorSize, "Sin memoria para base metabólica");
        return false;
    }
    if (!MetabolismIO_LoadDatabase(rootDir, db, error, errorSize)) {
        free(db);
        return false;
    }
    free(w->ownedMetabolismDb);
    w->ownedMetabolismDb = db;
    return EcosystemWorld_EnableMolecularMetabolism(w, db);
}

void EcosystemWorld_Free(EcosystemWorld *w) {
    if (!w) return;
    if (w->jobs) {
        EvolutionJobSystem_Destroy(w->jobs);
        w->jobs = NULL;
    }
    free(w->ownedMetabolismDb);
    w->ownedMetabolismDb = NULL;
    w->metabolismDb = NULL;
    if (w->entities) {
        for (unsigned i = 0; i < w->capacity; ++i) {
            NeuralBrain_Destroy(&w->entities[i].mind.neuralBrain);
            NeuralGenome_Destroy(&w->entities[i].reproduction.gestationNeuralGenomes[0]);
            NeuralGenome_Destroy(&w->entities[i].reproduction.gestationNeuralGenomes[1]);
            EcosystemAnatomy_Destroy(w->entities[i].anatomicalBody);
            w->entities[i].anatomicalBody = NULL;
        }
    }
    free(w->entities); free(w->resources); free(w->species);
    free(w->birthQueue); free(w->order); free(w->moveProposals);
    free(w->activeEntitySlots); free(w->entitySlotToActive); free(w->freeEntitySlots);
    free(w->activeResourceSlots); free(w->resourceSlotToActive); free(w->freeResourceSlots);
    free(w->obstacles); free(w->lineages); free(w->founders); free(w->parentHistory);
    idMapFree(&w->entityMap);
    idMapFree(&w->resourceMap);
    idMapFree(&w->parentHistoryMap);
    AIWorld_Destroy(&w->observations);
    memset(w, 0, sizeof(*w));
}

unsigned EcosystemWorld_AddSpecies(EcosystemWorld *w, EcosystemSpecies s) {
    if (!w || w->updating || !s.id || !s.category || w->speciesCount == w->speciesCapacity ||
        !BrainProfile_Validate(&s.brain) || !isfinite(s.speed) || s.speed <= 0 ||
        !isfinite(s.radius) || s.radius <= 0 || s.radius > 4 ||
        !isfinite(s.oralReach) || s.oralReach <= 0 || !isfinite(s.attackRange) || s.attackRange < 0 ||
        !isfinite(s.damage) || s.damage < 0 || !isfinite(s.attackCooldown) || s.attackCooldown <= 0 ||
        !isfinite(s.attackCost) || s.attackCost < 0 || !isfinite(s.edibleBiomass) || s.edibleBiomass < 0 ||
        !isfinite(s.nutrition) || s.nutrition <= 0)
        return 0;
    if (!CreaturePhysiology_ValidateProfile(&s.physiology) ||
        !CreatureReproduction_ValidateProfile(&s.reproduction) ||
        s.attackRange > s.oralReach)
        return 0;
    for (unsigned i = 0; i < w->speciesCount; ++i)
        if (w->species[i].id == s.id) return 0;
    if (s.physiology.dietMask == 0 && s.brain.dietMask != 0) s.physiology.dietMask = s.brain.dietMask;
    if (s.physiology.preyMask == 0 && s.brain.preyMask != 0) s.physiology.preyMask = s.brain.preyMask;
    if (!s.physiology.canAttackPhysically && s.physiology.preyMask != 0 && s.damage > 0 && s.attackRange > 0)
        s.physiology.canAttackPhysically = true;
    s.name[sizeof(s.name) - 1] = 0;
    w->species[w->speciesCount] = s;
    w->species[w->speciesCount].brain.name = w->species[w->speciesCount].name;
    ++w->speciesCount;
    return s.id;
}

AIObjectId EcosystemWorld_AddObstacle(EcosystemWorld *w, Vector3 position, float radius, float height) {
    if (!w || w->updating || !finitePosition(position) || !isfinite(radius) ||
        radius <= 0.05f || radius > 10.0f || !isfinite(height) || height <= 0.05f || height > 20.0f)
        return 0;
    if (fabsf(position.x) + radius > w->observations.halfSize ||
        fabsf(position.z) + radius > w->observations.halfSize)
        return 0;
    if (w->obstacleCount >= w->obstacleCapacity) {
        unsigned nextCap = w->obstacleCapacity ? w->obstacleCapacity * 2u : 64u;
        if (nextCap > 4096u) return 0;
        EcosystemObstacle *grown = realloc(w->obstacles, nextCap * sizeof(*grown));
        if (!grown) return 0;
        w->obstacles = grown;
        w->obstacleCapacity = nextCap;
    }
    AIObjectId id = AIWorld_Add(&w->observations, (AIWorldObject){
        .type = AI_OBSTACLE, .position = position, .radius = radius, .height = height,
        .visible = true, .occludes = true, .walkable = false, .drinkable = false
    });
    if (!id) return 0;
    w->obstacles[w->obstacleCount++] = (EcosystemObstacle){
        .id = id, .position = position, .radius = radius, .height = height
    };
    return id;
}

EcosystemEntity *EcosystemWorld_Get(EcosystemWorld *w, AIObjectId id) {
    if (!w || !id) return NULL;
    int slot = idMapGet(&w->entityMap, id);
    if (slot >= 0 && (unsigned)slot < w->capacity && w->entities[slot].id == id) {
        return &w->entities[slot];
    }
    return NULL;
}

const EcosystemEntity *EcosystemWorld_GetConst(const EcosystemWorld *w, AIObjectId id) {
    if (!w || !id) return NULL;
    int slot = idMapGet(&w->entityMap, id);
    if (slot >= 0 && (unsigned)slot < w->capacity && w->entities[slot].id == id) {
        return &w->entities[slot];
    }
    return NULL;
}

EcosystemResource *EcosystemWorld_Resource(EcosystemWorld *w, AIObjectId id) {
    if (!w || !id) return NULL;
    int slot = idMapGet(&w->resourceMap, id);
    if (slot >= 0 && (unsigned)slot < w->resourceCapacity && w->resources[slot].id == id) {
        return &w->resources[slot];
    }
    return NULL;
}

static PhysiologyProfile physiologicalProfile(const EcosystemWorld *w, const EcosystemEntity *e) {
    PhysiologyProfile p = w->species[e->speciesIndex].physiology;
    p.maturityAge *= e->genome.maturation;
    return p;
}

/* Prepara el estado físico y anatómico privado de la entidad (seguro en paralelo por entidad). */
static void prepareBody(const EcosystemWorld *w, EcosystemEntity *e) {
    const EcosystemSpecies *s = &w->species[e->speciesIndex];
    PhysiologyProfile p = physiologicalProfile(w, e);
    e->developmentScale = e->physiology.stage == LIFE_JUVENILE
                              ? 0.55f + 0.45f * (float)(e->physiology.age / p.maturityAge)
                              : 1.0f;
    float size = e->genome.size * e->developmentScale;
    e->body.group = s->id;
    e->body.species = s->id;
    e->body.reproductiveRole = e->reproduction.role;
    e->body.energy = e->physiology.energy;
    e->body.hydration = e->physiology.hydration;
    e->body.fatigue = e->physiology.fatigue;
    e->body.health = e->physiology.health;
    e->body.normalizedAge = p.maximumAge > 0 ? clamp((float)(e->physiology.age / p.maximumAge), 0, 1) : 0;
    e->body.molecularMode = (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR);
    if (e->body.molecularMode && e->metabolism.initialized) {
        const MetabolicProfile *mp = w->metabolismDb
            ? MetabolicProfileRegistry_Get(&w->metabolismDb->profiles, s->metabolicProfile)
            : NULL;
        e->body.digestiveFullness = MetabolicState_Fullness(&e->metabolism);
        e->body.nutrientReserve = e->metabolism.nutrientReserve;
        e->body.toxicity = clamp(e->metabolism.toxicity, 0.0f, 1.0f);
        e->body.digestiveDiscomfort = e->metabolism.digestiveDiscomfort;
        e->body.maximumIngestionRate = mp ? mp->maximumIngestionRate : 0.40f;
    } else {
        e->body.digestiveFullness = 0.0f;
        e->body.nutrientReserve = 1.0f;
        e->body.toxicity = 0.0f;
        e->body.digestiveDiscomfort = 0.0f;
        e->body.maximumIngestionRate = 0.0f;
    }
    e->body.canAttack = s->physiology.canAttackPhysically && e->physiology.energy > 0.03f;
    e->body.attackRange = s->attackRange * size;
    bool reproReady = CreatureReproduction_Ready(&e->reproduction, &s->reproduction,
                                                 &e->physiology, w->clock.seconds);
    if (reproReady && e->body.molecularMode && e->metabolism.initialized && w->metabolismDb) {
        const MetabolicProfile *mp = MetabolicProfileRegistry_Get(&w->metabolismDb->profiles, s->metabolicProfile);
        if (mp && e->metabolism.nutrientReserve < mp->minimumReproductionNutrients) reproReady = false;
    }
    e->body.mateReady = reproReady;
    e->body.capabilities = (CreatureCapabilities){
        .ground = true, .waterInteraction = s->physiology.waterInteraction,
        .contactTolerance = 0.09f * size,
        .maximumOralReach = s->oralReach * size * e->genome.morphology,
        .clearance = s->radius * size,
        .bankClearance = s->radius * size,
        .drinkReach = 0.09f * size,
        .mouthForward = 0.48f * size * e->genome.morphology,
        .maximumSpeed = s->speed * e->genome.speed * (0.6f + 0.4f * e->developmentScale) *
                        (1.0f - 0.45f * e->physiology.fatigue),
        .maximumTurn = s->turnRate > 0 ? s->turnRate : 3.0f
    };
    e->body.eye = Vec3_Add(e->body.position, Vec3_Create(0, 0.65f * size, 0));
    e->body.visionYaw = e->body.yaw;
    if (s->anatomicalOral && EcosystemAnatomy_ConfigureProfiled(e, s, w->profiler) &&
        e->anatomicalBody && e->anatomicalBody->tmpl) {
        const EcosystemAnatomy *a = e->anatomicalBody;
        float scale = EcosystemAnatomy_Scale(a, e);
        Vector3 oral = a->tmpl->restOralPointWorld;
        e->body.capabilities.mouthForward = (oral.z - a->center.z) * scale;
    }
}

static void publishBodyObservation(EcosystemWorld *w, const EcosystemEntity *e) {
    const EcosystemSpecies *s = &w->species[e->speciesIndex];
    float size = e->genome.size * e->developmentScale;
    AIWorld_PublishAgent(&w->observations, e->id, e->reproduction.role,
                         e->body.capabilities.maximumSpeed, e->body.position, e->velocity,
                         e->body.capabilities.clearance, 0.65f * size,
                         s->id, s->category, 0u, !e->physiology.cause, e->body.mateReady);
}

static void updateBody(EcosystemWorld *w, EcosystemEntity *e) {
    prepareBody(w, e);
    publishBodyObservation(w, e);
}

static bool spawnPosition(EcosystemWorld *w, Vector3 p, float radius) {
    if (!AIWorld_Walkable(&w->observations, p, radius, false)) return false;
    if (w->observations.spatial) {
        uint32_t slots[64];
        bool ov = false;
        unsigned n = AIWorld_QuerySlotsRadius(&w->observations, AI_SPATIAL_MASK_CREATURES,
                                              p, radius + 2.0f, slots, 64u, &ov);
        if (!ov) {
            for (unsigned k = 0; k < n; ++k) {
                const AIWorldObject *o = AIWorld_At(&w->observations, slots[k]);
                if (!o || !o->active) continue;
                float sumR = radius + o->radius;
                if (Vec3_LengthSq(Vec3_Sub(p, o->position)) < sumR * sumR) return false;
            }
            return true;
        }
    }
    for (unsigned k = 0; k < w->count; ++k) {
        EcosystemEntity *e = &w->entities[w->activeEntitySlots[k]];
        if (e->id && Vec3_Distance(p, e->body.position) < radius + e->body.capabilities.clearance)
            return false;
    }
    return true;
}

AIObjectId EcosystemWorld_Spawn(EcosystemWorld *w, unsigned species, Vector3 position,
                                double age, float energy, const CreatureGenome *genome) {
    if (!w || w->updating || !finitePosition(position) || fabsf(position.y) > 0.001f ||
        !isfinite(age) || age < 0 || !isfinite(energy) || energy < 0 || energy > 1)
        return 0;
    CreatureGenome g = genome ? *genome : CreatureGenome_Default();
    if (!CreatureGenome_Validate(&g)) return 0;
    unsigned si = 0;
    while (si < w->speciesCount && w->species[si].id != species) ++si;
    if (si == w->speciesCount) return 0;
    double maturity = w->species[si].physiology.maturityAge * g.maturation;
    float development = age < maturity ? 0.55f + 0.45f * (float)(age / maturity) : 1.0f;
    if (w->count >= w->capacity || w->freeEntityCount == 0u ||
        !spawnPosition(w, position, w->species[si].radius * g.size * development)) {
        ++w->statistics.failedSpawns;
        return 0;
    }
    unsigned slot = w->freeEntitySlots[--w->freeEntityCount];
    AIObjectId id = AIWorld_Add(&w->observations, (AIWorldObject){
        .type = AI_CREATURE, .group = w->observations.nextId % 2,
        .position = position, .visible = true, .walkable = true
    });
    if (!id) {
        w->freeEntitySlots[w->freeEntityCount++] = slot;
        ++w->statistics.failedSpawns;
        return 0;
    }
    EcosystemEntity *e = &w->entities[slot];
    memset(e, 0, sizeof(*e));
    e->id = id;
    e->speciesIndex = si;
    e->genome = g;
    e->bornAt = w->clock.seconds;
    e->seed = (uint32_t)(CreatureGenome_Random(&w->random) * 16777216) + id;
    e->reproduction.role = id % 2;
    e->brain = w->species[si].brain;
    e->brain.perception.range *= g.perception;
    e->physiology = CreaturePhysiology_Create(age, energy);
    PhysiologyProfile p = physiologicalProfile(w, e);
    e->physiology.stage = age < p.maturityAge ? LIFE_JUVENILE : age < p.oldAge ? LIFE_ADULT : LIFE_OLD;
    e->maturityRecorded = (e->physiology.stage != LIFE_JUVENILE);
    e->body.position = position;
    e->previousPosition = position;
    e->body.yaw = CreatureGenome_Random(&w->random) * 6.2831853f;
    e->oralLocal = Vec3_Create(0, 0.45f * g.size, 0.48f * g.size);
    e->body.mouth = Vec3_Add(position, e->oralLocal);
    e->body.mouthDirection = Vec3_Create(0, 0, 1);
    CreatureMind_Init(&e->mind, &e->brain, id, e->seed);
    e->mind.externalPhysiology = true;
    e->mind.transactionalIntake = true;
    e->mind.molecularMode = (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR);
    if (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR && w->metabolismDb) {
        const MetabolicProfile *mp =
            MetabolicProfileRegistry_Get(&w->metabolismDb->profiles, w->species[si].metabolicProfile);
        CreatureMetabolism_Init(&e->metabolism, mp, 0.55f);
    } else {
        MetabolicState_Reset(&e->metabolism);
    }
    CreatureMind_SetNeuralConfig(&e->mind, w->neuralController);
    e->mind.time = w->clock.seconds;
    e->mind.nextDecision += w->clock.seconds;
    e->mind.perception.nextRefresh += w->clock.seconds;
    e->mind.started += w->clock.seconds;

    unsigned activeIdx = w->count++;
    w->activeEntitySlots[activeIdx] = slot;
    w->entitySlotToActive[slot] = activeIdx;
    idMapPut(&w->entityMap, id, slot);
    ensureParentRecord(w, id, true);

    updateBody(w, e);
    float size = e->genome.size * e->developmentScale;
    e->oralLocal = Vec3_Create(0, 0, 0.48f * size * e->genome.morphology);
    e->body.mouth = Vec3_Add(position, Vec3_Create(sinf(e->body.yaw) * e->oralLocal.z,
                                                   0.45f * size,
                                                   cosf(e->body.yaw) * e->oralLocal.z));
    e->body.mouthDirection = Vec3_Create(sinf(e->body.yaw), 0, cosf(e->body.yaw));
    if (e->anatomicalBody) EcosystemAnatomy_UpdateProfiled(e, 0.0f, w->profiler);
    event(w, 1, id, species);
    if (!w->updating) statistics(w);
    return id;
}

bool EcosystemWorld_RequestDeath(EcosystemWorld *w, AIObjectId id, CreatureDeathCause cause) {
    EcosystemEntity *e = EcosystemWorld_Get(w, id);
    if (!e || e->physiology.cause || cause <= DEATH_NONE || cause >= DEATH_CAUSE_COUNT) return false;
    e->physiology.cause = cause;
    e->physiology.stage = LIFE_DEAD;
    e->physiology.health = 0;
    return true;
}

AIObjectId EcosystemWorld_AddMolecularResource(EcosystemWorld *w, EcosystemResource r, Vector3 p, float radius) {
    if (!w || !finitePosition(p) || !isfinite(radius) || radius <= 0.0f ||
        !isfinite(r.biomass) || r.biomass < 0.0f ||
        !isfinite(r.maximumBiomass) || r.maximumBiomass < r.biomass ||
        !isfinite(r.regenerationRate) || r.regenerationRate < 0.0f ||
        r.componentCount == 0u || r.componentCount > SUBSTANCE_MAX_COMPONENTS)
        return 0u;
    if (w->freeResourceCount == 0u) return 0u;
    r.molecular = true;
    bool liquid = (r.phase == SUBSTANCE_PHASE_LIQUID);
    unsigned slot = w->freeResourceSlots[--w->freeResourceCount];
    r.id = AIWorld_Add(&w->observations, (AIWorldObject){
        .type = liquid ? AI_WATER : AI_FOOD,
        .position = p, .radius = radius, .height = liquid ? 0.0f : 0.04f,
        .foodCategory = r.category,
        .visible = true, .walkable = !liquid,
        .drinkable = liquid && r.accessible,
        .molecularResource = true,
        .substancePhase = (uint8_t)r.phase,
        .hardness = r.hardness,
        .availabilityFraction = EcosystemResource_AvailabilityFraction(&r),
        .sensorySignature = r.sensorySignature
    });
    if (!r.id) {
        w->freeResourceSlots[w->freeResourceCount++] = slot;
        return 0u;
    }
    w->resources[slot] = r;
    unsigned aIdx = w->activeResourceCount++;
    w->activeResourceSlots[aIdx] = slot;
    w->resourceSlotToActive[slot] = aIdx;
    idMapPut(&w->resourceMap, r.id, slot);
    AIWorld_PublishMolecularResource(&w->observations, r.id, r.originalSpecies, r.category,
                                     EcosystemResource_Available(&r), (uint8_t)r.phase,
                                     r.hardness, EcosystemResource_AvailabilityFraction(&r),
                                     &r.sensorySignature);
    if (!w->updating) statistics(w);
    return r.id;
}

AIObjectId EcosystemWorld_AddResource(EcosystemWorld *w, EcosystemResource r, Vector3 p, float radius) {
    if (r.molecular) return EcosystemWorld_AddMolecularResource(w, r, p, radius);
    if (!w || !finitePosition(p) || !isfinite(radius) || radius <= 0 || !isfinite(r.biomass) ||
        r.biomass < 0 || !isfinite(r.maximumBiomass) || r.maximumBiomass < r.biomass ||
        !isfinite(r.regenerationRate) || r.regenerationRate < 0 ||
        !isfinite(r.nutritionalValue) || r.nutritionalValue <= 0 ||
        (r.category != ECO_PLANT && r.category != ECO_WATER && r.category != ECO_MEAT) ||
        (r.infinite && r.category != ECO_WATER) ||
        (r.category == ECO_MEAT && (!isfinite(r.remainingLifetime) || r.remainingLifetime <= 0)))
        return 0;
    if (w->freeResourceCount == 0u) return 0;
    unsigned slot = w->freeResourceSlots[--w->freeResourceCount];
    r.id = AIWorld_Add(&w->observations, (AIWorldObject){
        .type = r.category == ECO_WATER ? AI_WATER : AI_FOOD,
        .position = p, .radius = radius, .height = r.category == ECO_WATER ? 0.0f : 0.04f,
        .foodCategory = r.category,
        .visible = true, .walkable = r.category != ECO_WATER,
        .drinkable = r.category == ECO_WATER && r.accessible
    });
    if (!r.id) {
        w->freeResourceSlots[w->freeResourceCount++] = slot;
        return 0;
    }
    w->resources[slot] = r;
    unsigned aIdx = w->activeResourceCount++;
    w->activeResourceSlots[aIdx] = slot;
    w->resourceSlotToActive[slot] = aIdx;
    idMapPut(&w->resourceMap, r.id, slot);
    AIWorld_PublishResource(&w->observations, r.id, r.originalSpecies, r.category,
                            EcosystemResource_Available(&r));
    if (!w->updating) statistics(w);
    return r.id;
}

bool EcosystemWorld_RemoveResource(EcosystemWorld *w, AIObjectId id) {
    if (!w || !id) return false;
    int slot = idMapGet(&w->resourceMap, id);
    if (slot < 0 || (unsigned)slot >= w->resourceCapacity || w->resources[slot].id != id) return false;
    AIWorld_Remove(&w->observations, id);
    invalidate(w, id);
    idMapRemove(&w->resourceMap, id);
    unsigned aIdx = w->resourceSlotToActive[slot];
    if (aIdx != UINT_MAX) {
        unsigned lastSlot = w->activeResourceSlots[--w->activeResourceCount];
        if (aIdx < w->activeResourceCount) {
            w->activeResourceSlots[aIdx] = lastSlot;
            w->resourceSlotToActive[lastSlot] = aIdx;
        }
        w->resourceSlotToActive[slot] = UINT_MAX;
    }
    memset(&w->resources[slot], 0, sizeof(w->resources[slot]));
    w->freeResourceSlots[w->freeResourceCount++] = (unsigned)slot;
    if (!w->updating) statistics(w);
    return true;
}

static void invalidate(EcosystemWorld *w, AIObjectId id) {
    for (unsigned k = 0; k < w->count; ++k) {
        EcosystemEntity *e = &w->entities[w->activeEntitySlots[k]];
        if (!e->id) continue;
        CreaturePerception *p = &e->mind.perception;
        for (unsigned j = 0; j < p->count;) {
            if (p->entries[j].id == id) p->entries[j] = p->entries[--p->count];
            else ++j;
        }
        if (e->mind.target == id) CreatureActions_Fail(&e->mind);
        if (e->reproduction.partner == id && !e->reproduction.gestating) e->reproduction.partner = 0;
    }
}

/* ========================================================================== */
/* Interacciones autoritativas: consumo, ataque, boca, movimiento             */
/* ========================================================================== */

float EcosystemWorld_Consume(EcosystemWorld *w, AIObjectId id, AIObjectId resource, float requested) {
    EcosystemEntity *e = EcosystemWorld_Get(w, id);
    EcosystemResource *r = EcosystemWorld_Resource(w, resource);
    if (!e || !r || e->physiology.cause || !isfinite(requested) || requested <= 0.0f) return 0.0f;
    const EcosystemSpecies *sp = &w->species[e->speciesIndex];
    CreatureMind *m = &e->mind;
    CreatureBodyState *b = &e->body;
    InteractionTarget target;

    if (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR || r->molecular) {
        bool liquid = (r->phase == SUBSTANCE_PHASE_LIQUID);
        if (m->target != resource || m->contactTime < 0.25f ||
            m->interactionPhase != INTERACTION_CONSUME ||
            m->action != (liquid ? ACTION_DRINK : ACTION_EAT) ||
            !EcosystemResource_Available(r) ||
            (liquid && !b->capabilities.waterInteraction) ||
            !AIWorld_InteractionTarget(&w->observations, resource, b->mouth,
                                       b->capabilities.contactTolerance, &target) ||
            target.blocked || b->speed > 0.04f || b->reach.objectId != resource ||
            b->reach.status != REACH_REACHED ||
            Vec3_Distance(b->mouth, target.position) > target.positionTolerance ||
            Vec3_Dot(b->mouthDirection, m->interaction.approach) < cosf(m->interaction.orientationTolerance))
            return 0.0f;

        const MetabolicProfile *mp = w->metabolismDb
            ? MetabolicProfileRegistry_Get(&w->metabolismDb->profiles, sp->metabolicProfile)
            : NULL;
        if (!mp || !e->metabolism.initialized) return 0.0f;

        float dt = (float)w->clock.fixedStep;
        float reqMass = requested;
        if (m->intakeMassRequested > 0.0f) reqMass = fminf(reqMass, m->intakeMassRequested);
        float maxAccept = CreatureMetabolism_MaxAcceptableMass(&e->metabolism, mp, reqMass, dt);
        if (maxAccept <= 1e-7f) {
            m->intakeMassRequested = 0.0f;
            /* Penalización fisiológica real por intentar seguir comiendo con el estómago lleno. */
            e->metabolism.digestiveDiscomfort = fminf(1.0f, e->metabolism.digestiveDiscomfort + 0.14f * dt);
            e->physiology.fatigue = fminf(1.0f, e->physiology.fatigue + 0.05f * dt);
            e->physiology.energy = fmaxf(0.0f, e->physiology.energy - 0.012f * dt);
            if (m->controllerType == CREATURE_CONTROLLER_NEURAL)
                CreatureMind_RecordOutcome(m, NEURAL_OUTCOME_STOMACH_FULL);
            return 0.0f;
        }

        /* Retirar exactamente la masa aceptada; jamás desechar masa retirada del recurso. */
        float actual = EcosystemResource_Consume(r, maxAccept);
        if (actual > 0.0f) {
            if (CreatureMetabolism_Ingest(&e->metabolism, mp, r->substanceHandle,
                                          r->composition, r->componentCount, actual, w->clock.seconds)) {
                /* Malestar progresivo al forzar ingesta cerca de saturación o ya saciado al máximo. */
                float fullnessNow = MetabolicState_Fullness(&e->metabolism);
                if (fullnessNow > 0.78f) {
                    e->metabolism.digestiveDiscomfort = fminf(1.0f,
                        e->metabolism.digestiveDiscomfort + (fullnessNow - 0.78f) * actual * 0.45f);
                } else if (e->physiology.energy > 0.96f && e->physiology.hydration > 0.96f && fullnessNow > 0.45f) {
                    e->metabolism.digestiveDiscomfort = fminf(1.0f,
                        e->metabolism.digestiveDiscomfort + actual * 0.08f);
                }
                w->statistics.totalIngestedMass += (double)actual;
                if (r->substanceHandle < SUBSTANCE_MAX_COUNT) {
                    w->statistics.ingestedBySubstance[r->substanceHandle] += (double)actual;
                }
                for (uint16_t ci = 0; ci < r->componentCount; ++ci) {
                    MoleculeHandle mh = r->composition[ci].molecule;
                    if (mh < MOLECULE_MAX_COUNT) {
                        w->statistics.ingestedByMolecule[mh] += (double)(actual * r->composition[ci].fraction);
                    }
                }
                if (liquid) {
                    w->statistics.waterConsumed += (double)actual;
                    ++w->statistics.drinks;
                } else {
                    w->statistics.foodConsumed += (double)actual;
                    ++w->statistics.meals;
                }
                /* Informar únicamente la masa física ingerida (no beneficio nutricional). */
                CreatureMind_RecordIngestion(m, actual);
                event(w, liquid ? 4 : 3, id, resource);
            }
        }
        AIWorld_PublishMolecularResource(&w->observations, r->id, r->originalSpecies, r->category,
                                         EcosystemResource_Available(r), (uint8_t)r->phase,
                                         r->hardness, EcosystemResource_AvailabilityFraction(r),
                                         &r->sensorySignature);
        return actual;
    }

    bool water = r->category == ECO_WATER;
    if (m->target != resource || m->intakeRequested <= 0 || m->contactTime < 0.25f ||
        m->interactionPhase != INTERACTION_CONSUME ||
        m->action != (water ? ACTION_DRINK : ACTION_EAT) ||
        m->intakeNeed != (water ? NEED_HYDRATION : NEED_HUNGER) ||
        !EcosystemResource_Available(r) ||
        (water && !b->capabilities.waterInteraction) ||
        !AIWorld_InteractionTarget(&w->observations, resource, b->mouth,
                                   b->capabilities.contactTolerance, &target) ||
        target.blocked || b->speed > 0.04f || b->reach.objectId != resource ||
        b->reach.status != REACH_REACHED ||
        Vec3_Distance(b->mouth, target.position) > target.positionTolerance ||
        Vec3_Dot(b->mouthDirection, m->interaction.approach) < cosf(m->interaction.orientationTolerance))
        return 0;
    if (!water && !(sp->physiology.dietMask & r->category)) {
        ++w->statistics.failedInteractions;
        if (m->controllerType == CREATURE_CONTROLLER_NEURAL) {
            CreatureActions_Fail(m);
            CreatureMind_RecordFailure(m, NEURAL_OUTCOME_FAILED_DIET);
        }
        return 0;
    }
    float value = water ? 1.0f : r->nutritionalValue;
    float deficit = 1.0f - (water ? e->physiology.hydration : e->physiology.energy);
    if (deficit <= 0) return 0;
    float actual = EcosystemResource_Consume(r, fminf(requested, fminf(m->intakeRequested, deficit) / value));
    if (actual > 0) {
        if (water) {
            CreaturePhysiology_Drink(&e->physiology, actual);
            w->statistics.waterConsumed += actual;
            ++w->statistics.drinks;
            if (m->controllerType == CREATURE_CONTROLLER_NEURAL)
                CreatureMind_RecordOutcome(m, NEURAL_OUTCOME_DRANK_WATER);
        } else {
            CreaturePhysiology_Feed(&e->physiology, actual * value);
            w->statistics.foodConsumed += actual;
            if (r->category == ECO_PLANT) {
                w->statistics.plantConsumed += actual;
                if (m->controllerType == CREATURE_CONTROLLER_NEURAL)
                    CreatureMind_RecordOutcome(m, NEURAL_OUTCOME_CONSUMED_PLANT);
            } else {
                w->statistics.meatConsumed += actual;
                if (m->controllerType == CREATURE_CONTROLLER_NEURAL)
                    CreatureMind_RecordOutcome(m, NEURAL_OUTCOME_CONSUMED_MEAT);
            }
            ++w->statistics.meals;
        }
        CreatureMind_ConfirmIntake(m, actual * value);
        event(w, water ? 4 : 3, id, resource);
    }
    AIWorld_PublishResource(&w->observations, r->id, r->originalSpecies, r->category,
                            EcosystemResource_Available(r));
    return actual;
}

bool EcosystemWorld_Attack(EcosystemWorld *w, AIObjectId attacker, AIObjectId prey) {
    EcosystemEntity *a = EcosystemWorld_Get(w, attacker);
    EcosystemEntity *b = EcosystemWorld_Get(w, prey);
    if (!a || !b || a == b || a->physiology.cause || b->physiology.cause) return false;
    const EcosystemSpecies *s = &w->species[a->speciesIndex];
    const EcosystemSpecies *t = &w->species[b->speciesIndex];
    Vector3 delta = Vec3_Sub(b->body.position, a->body.position);
    Vector3 front = Vec3_Create(sinf(a->body.yaw), 0, cosf(a->body.yaw));
    if (a->mind.action != ACTION_ATTACK || a->mind.target != prey) return false;
    if (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR) {
        /* Modo molecular: sólo restricciones físicas/mecánicas, desacoplado de dieta y metabolismo. */
        if (!a->body.canAttack || !s->physiology.canAttackPhysically || s->damage <= 0.0f || s->attackRange <= 0.0f) {
            ++w->statistics.failedInteractions;
            if (a->mind.controllerType == CREATURE_CONTROLLER_NEURAL) {
                CreatureActions_Fail(&a->mind);
                CreatureMind_RecordFailure(&a->mind, NEURAL_OUTCOME_FAILED_ATTACK);
            }
            return false;
        }
    } else {
        if (!a->body.canAttack || !s->physiology.canAttackPhysically ||
            !(s->physiology.preyMask & t->category) || !(s->physiology.dietMask & ECO_MEAT)) {
            ++w->statistics.failedInteractions;
            if (a->mind.controllerType == CREATURE_CONTROLLER_NEURAL) {
                CreatureActions_Fail(&a->mind);
                CreatureMind_RecordFailure(&a->mind, NEURAL_OUTCOME_FAILED_ATTACK);
            }
            return false;
        }
    }
    if (w->clock.seconds < a->nextAttack || a->physiology.energy < s->attackCost ||
        Vec3_Length(delta) > fminf(a->body.attackRange, a->body.capabilities.maximumOralReach) ||
        Vec3_Dot(front, Vec3_Normalize(delta)) < 0.6f ||
        !AIWorld_ClearSegment(&w->observations, a->body.mouth, b->body.position, 0, prey))
        return false;
    if (a->anatomicalBody) {
        Vector3 center = Vec3_Add(b->body.position, Vec3_Create(0, 0.55f * b->developmentScale * b->genome.size, 0));
        if (a->body.reach.objectId != prey || a->body.reach.status != REACH_REACHED ||
            Vec3_Distance(a->body.mouth, center) > b->body.capabilities.clearance + 0.15f)
            return false;
    }
    a->nextAttack = w->clock.seconds + s->attackCooldown;
    a->physiology.energy -= s->attackCost;
    b->physiology.health = fmaxf(0, b->physiology.health - s->damage);
    b->lastAttackerId = a->id;
    if (a->mind.controllerType == CREATURE_CONTROLLER_NEURAL)
        CreatureMind_RecordOutcome(&a->mind, NEURAL_OUTCOME_ATTACK_HIT);
    if (b->mind.controllerType == CREATURE_CONTROLLER_NEURAL)
        CreatureMind_RecordDamage(&b->mind, s->damage);
    ++w->statistics.attacks;
    event(w, 5, attacker, prey);
    if (b->physiology.health <= 0 && EcosystemWorld_RequestDeath(w, prey, DEATH_PREDATION))
        ++w->statistics.kills;
    return true;
}

static void needs(EcosystemEntity *e) {
    e->mind.needs.urgency[NEED_HUNGER] = e->brain.needs[NEED_HUNGER].enabled ? 1 - e->physiology.energy : 0;
    e->mind.needs.urgency[NEED_HYDRATION] = e->brain.needs[NEED_HYDRATION].enabled ? 1 - e->physiology.hydration : 0;
    e->mind.needs.urgency[NEED_REST] = e->brain.needs[NEED_REST].enabled ? e->physiology.fatigue : 0;
}

static void mouth(const EcosystemWorld *w, EcosystemEntity *e, float dt) {
    if (e->anatomicalBody) {
        CreatureIntent saved = e->mind.intent;
        const EcosystemEntity *prey =
            e->mind.action == ACTION_ATTACK ? EcosystemWorld_GetConst(w, e->mind.target) : NULL;
        if (prey) {
            Vector3 center = Vec3_Add(prey->body.position,
                                      Vec3_Create(0, 0.55f * prey->developmentScale * prey->genome.size, 0));
            Vector3 point = Vec3_Add(center,
                                     Vec3_Scale(Vec3_Normalize(Vec3_Sub(e->body.mouth, center)),
                                                prey->body.capabilities.clearance * 0.96f));
            Vector3 approach = Vec3_Normalize(Vec3_Sub(point, e->body.mouth));
            if (Vec3_LengthSq(approach) < 0.5f)
                approach = Vec3_Create(sinf(e->body.yaw), 0, cosf(e->body.yaw));
            e->mind.intent.interact = true;
            e->mind.intent.interaction = (InteractionTarget){
                .valid = true, .objectId = prey->id, .type = INTERACTION_EAT,
                .position = point, .normal = {0, 1, 0}, .approach = approach,
                .positionTolerance = 0.12f, .orientationTolerance = 1.4f, .moving = true
            };
        }
        EcosystemAnatomy_UpdateProfiled(e, dt, w->profiler);
        e->mind.intent = saved;
        return;
    }
    float size = e->genome.size * e->developmentScale;
    Vector3 pivot = Vec3_Add(e->body.position, Vec3_Create(0, 0.45f * size, 0));
    Vector3 front = Vec3_Create(sinf(e->body.yaw), 0, cosf(e->body.yaw));
    Vector3 goal = Vec3_Add(pivot, Vec3_Scale(front, 0.48f * size * e->genome.morphology));
    InteractionTarget target = e->mind.intent.interaction;
    bool interacting = e->mind.intent.interact && target.valid;
    if (interacting) {
        InteractionTarget fresh;
        if (!AIWorld_InteractionTarget(&w->observations, target.objectId, e->body.mouth,
                                       e->body.capabilities.contactTolerance, &fresh))
            interacting = false;
        else {
            target.blocked = fresh.blocked;
            goal = target.position;
        }
    }
    Vector3 radial = Vec3_Sub(goal, pivot);
    float reach = e->body.capabilities.maximumOralReach;
    bool possible = Vec3_Length(radial) <= reach;
    if (!possible) goal = Vec3_Add(pivot, Vec3_Scale(Vec3_Normalize(radial), reach));
    Vector3 old = Vec3_Add(pivot, Vec3_Create(
        e->oralLocal.x * cosf(e->body.yaw) + e->oralLocal.z * sinf(e->body.yaw),
        e->oralLocal.y,
        -e->oralLocal.x * sinf(e->body.yaw) + e->oralLocal.z * cosf(e->body.yaw)));
    Vector3 delta = Vec3_Sub(goal, old);
    float length = Vec3_Length(delta);
    Vector3 next = Vec3_Add(old, Vec3_Scale(Vec3_Normalize(delta), fminf(length, 0.9f * size * dt)));
    radial = Vec3_Sub(next, pivot);
    if (Vec3_Length(radial) > reach) next = Vec3_Add(pivot, Vec3_Scale(Vec3_Normalize(radial), reach));
    e->oralMotion = dt > 0 ? Vec3_Distance(old, next) / dt : 0;
    Vector3 local = Vec3_Sub(next, pivot);
    e->oralLocal = Vec3_Create(local.x * cosf(e->body.yaw) - local.z * sinf(e->body.yaw), local.y,
                               local.x * sinf(e->body.yaw) + local.z * cosf(e->body.yaw));
    e->oralPivot = pivot;
    Vector3 axis = Vec3_Sub(next, pivot);
    float distance = Vec3_Length(axis);
    Vector3 perpendicular = Vec3_Cross(Vec3_Normalize(axis), Vec3_Create(0, 1, 0));
    if (Vec3_LengthSq(perpendicular) < 1e-6f) perpendicular = Vec3_Create(1, 0, 0);
    float bend = sqrtf(fmaxf(0, reach * reach * 0.25f - distance * distance * 0.25f));
    e->oralElbow = Vec3_Add(Vec3_Add(pivot, Vec3_Scale(axis, 0.5f)),
                            Vec3_Scale(Vec3_Normalize(perpendicular), bend));
    Vector3 desired = interacting ? target.approach : front;
    Vector3 turn = Vec3_Sub(desired, e->body.mouthDirection);
    e->body.mouthDirection = Vec3_Normalize(Vec3_Add(e->body.mouthDirection, Vec3_Scale(turn, fminf(1, 4 * dt))));
    e->body.mouth = next;
    e->body.reach = (ReachResult){
        .objectId = interacting ? target.objectId : 0,
        .mouth = next, .direction = e->body.mouthDirection,
        .stability = e->oralMotion, .error = Vec3_Distance(next, goal),
        .status = !interacting ? REACH_INVALID
                : target.blocked ? REACH_BLOCKED
                : !possible ? REACH_UNREACHABLE
                : Vec3_Distance(next, target.position) <= target.positionTolerance ? REACH_REACHED
                : REACH_APPROACHING
    };
}

static inline float segmentDistanceSqXZ(Vector3 from, Vector3 to, Vector3 p) {
    float dx = to.x - from.x, dz = to.z - from.z;
    float l = dx * dx + dz * dz;
    float t = l > 1e-8f ? clamp(((p.x - from.x) * dx + (p.z - from.z) * dz) / l, 0.0f, 1.0f) : 0.0f;
    float rx = from.x + dx * t - p.x;
    float rz = from.z + dz * t - p.z;
    return rx * rx + rz * rz;
}

/* Propuesta de movimiento con evitación recíproca local (RVO-inspirada) sobre el índice espacial.
 * Lee únicamente observaciones inmutables -> seguro en paralelo. */
static void computeMovementProposal(const EcosystemWorld *w, const EcosystemEntity *e,
                                    float dt, EcosystemMoveProposal *out) {
    const CreatureBodyState *b = &e->body;
    CreatureMotorCommand c = CreatureMotor_Resolve(&e->mind.intent, b);
    uint64_t spatialCandCount = 0u;
    uint64_t collisionTests = 0u;

    if (c.speed > 0.04f) {
        Vector3 direction = Vec3_Create(sinf(c.heading), 0, cosf(c.heading));
        float look = fminf(Vec3_Distance(b->position, e->mind.intent.destination),
                           fmaxf(0.45f, c.speed * 0.65f));
        float queryR = look + b->capabilities.clearance + 1.8f;

        uint32_t stackSlots[128];
        bool ov = false;
        unsigned cand = AIWorld_QuerySlotsRadius(&w->observations, AI_SPATIAL_MASK_CREATURES,
                                                 b->position, queryR, stackSlots, 128u, &ov);
        spatialCandCount += cand;

        /* Evitación recíproca determinista: acumular vector de repulsión/desvío tangencial
         * considerando velocidad relativa de vecinos para resolver atascos en estanques. */
        Vector3 prefVel = Vec3_Scale(direction, c.speed);
        Vector3 avoidForce = Vec3_Zero();
        float worstUrgency = 0.0f;
        bool immediateBlock = false;
        Vector3 ahead = Vec3_Add(b->position, Vec3_Scale(direction, look));

        for (unsigned k = 0; k < cand; ++k) {
            const AIWorldObject *other = AIWorld_At(&w->observations, stackSlots[k]);
            if (!other || !other->active || !other->available || other->id == e->id) continue;
            if (other->id == e->mind.target &&
                (e->mind.action == ACTION_CHASE || e->mind.action == ACTION_ATTACK ||
                 e->mind.action == ACTION_APPROACH_MATE))
                continue;
            ++collisionTests;
            float gap = b->capabilities.clearance + other->radius + 0.08f;
            float rx = other->position.x - b->position.x;
            float rz = other->position.z - b->position.z;
            float distSq = rx * rx + rz * rz;
            float horizonR = gap + look;
            if (distSq > horizonR * horizonR) continue;

            /* Tiempo estimado a colisión (RVO: mitad de responsabilidad con velocidad relativa). */
            float rvx = prefVel.x - other->velocity.x;
            float rvz = prefVel.z - other->velocity.z;
            float rvSq = rvx * rvx + rvz * rvz;
            float ttc = 1.5f;
            if (rvSq > 1e-5f) {
                float proj = (rx * rvx + rz * rvz) / rvSq;
                if (proj > 0.0f && proj < 1.2f) {
                    float cx = rx - rvx * proj;
                    float cz = rz - rvz * proj;
                    if (cx * cx + cz * cz < gap * gap) {
                        ttc = proj;
                    }
                }
            }
            bool segHit = segmentDistanceSqXZ(b->position, ahead, other->position) < gap * gap;
            if (!segHit && ttc >= 1.2f) continue;

            float dist = sqrtf(fmaxf(distSq, 1e-6f));
            float invDist = 1.0f / dist;
            float nx = -rx * invDist, nz = -rz * invDist; /* Alejamiento radial */
            /* Sesgo tangencial derecho determinista para romper empates frontales. */
            float tx = -nz, tz = nx;
            float sideSign = (rx * (-direction.z) + rz * direction.x) >= 0.0f ? 1.0f : -1.0f;
            float urgency = segHit ? (1.5f / fmaxf(0.15f, dist - gap + 0.35f)) : (0.9f / fmaxf(0.15f, ttc));
            if (urgency > worstUrgency) worstUrgency = urgency;
            if (dist < gap * 1.08f) immediateBlock = true;
            avoidForce.x += (nx * 0.55f + tx * sideSign * 0.85f) * urgency;
            avoidForce.z += (nz * 0.55f + tz * sideSign * 0.85f) * urgency;
        }

        if (worstUrgency > 0.0f) {
            Vector3 blended = Vec3_Add(prefVel, Vec3_Scale(avoidForce, fminf(c.speed, 1.4f)));
            blended.y = 0.0f;
            float bLen = Vec3_Length(blended);
            bool chosen = false;
            if (bLen > 0.05f) {
                Vector3 dirCand = Vec3_Scale(blended, 1.0f / bLen);
                Vector3 probe = Vec3_Add(b->position, Vec3_Scale(dirCand, fmaxf(0.4f, b->capabilities.clearance * 0.6f)));
                if (AIWorld_SegmentWalkable(&w->observations, b->position, probe, b->capabilities.clearance, false)) {
                    c.heading = atan2f(dirCand.x, dirCand.z);
                    if (immediateBlock) c.speed *= 0.65f;
                    chosen = true;
                }
            }
            if (!chosen) {
                Vector3 right = Vec3_Create(direction.z, 0, -direction.x);
                float gap = b->capabilities.clearance * 2.0f + 0.25f;
                for (unsigned side = 0; side < 2; ++side) {
                    Vector3 avoid = Vec3_Add(b->position, Vec3_Add(Vec3_Scale(direction, 0.3f),
                                                                   Vec3_Scale(right, (side ? -1.0f : 1.0f) * gap)));
                    if (AIWorld_SegmentWalkable(&w->observations, b->position, avoid, b->capabilities.clearance, false)) {
                        Vector3 d = Vec3_Sub(avoid, b->position);
                        c.heading = atan2f(d.x, d.z);
                        chosen = true;
                        break;
                    }
                }
                if (!chosen) c.speed = 0.0f;
            }
        }
    }

    float turn = remainderf(c.heading - b->yaw, 6.2831853f);
    float nextYaw = b->yaw + clamp(turn, -b->capabilities.maximumTurn * dt, b->capabilities.maximumTurn * dt);
    float speed = c.speed * fmaxf(0.0f, cosf(turn));
    float distance = Vec3_Distance(b->position, e->mind.intent.destination);
    float curvature = 2.0f * fabsf(sinf(turn)) / fmaxf(b->capabilities.clearance, distance);
    if (curvature > 0.001f) speed = copysignf(fminf(fabsf(speed), b->capabilities.maximumTurn / curvature), speed);
    Vector3 nextPos = Vec3_Add(b->position, Vec3_Scale(Vec3_Create(sinf(nextYaw), 0, cosf(nextYaw)), speed * dt));

    out->nextPosition = nextPos;
    out->nextYaw = nextYaw;
    out->desiredSpeed = speed;
    out->wantsMove = fabsf(speed) > 1e-6f;

    if (w->profiler) {
        if (spatialCandCount)
            EvolutionProfiler_AddAtomic(&w->profiler->counters.spatialQueryCandidates, spatialCandCount);
        if (collisionTests)
            EvolutionProfiler_AddAtomic(&w->profiler->counters.exactCollisionTests, collisionTests);
    }
}

/* Resolución determinista en orden de ID ascendente con validación conservadora de colisiones. */
static void commitMovementProposal(EcosystemWorld *w, EcosystemEntity *e,
                                   const EcosystemMoveProposal *prop, float dt) {
    CreatureBodyState *b = &e->body;
    b->yaw = prop->nextYaw;
    e->previousPosition = b->position;

    bool clear = false;
    if (prop->wantsMove &&
        AIWorld_SegmentWalkable(&w->observations, b->position, prop->nextPosition,
                                b->capabilities.clearance, false)) {
        clear = true;
        uint32_t stackSlots[96];
        bool ov = false;
        unsigned cand = AIWorld_QuerySlotsSegment(&w->observations, AI_SPATIAL_MASK_CREATURES,
                                                  b->position, prop->nextPosition,
                                                  b->capabilities.clearance + 0.05f,
                                                  stackSlots, 96u, &ov);
        if (!ov) {
            for (unsigned k = 0; k < cand; ++k) {
                const AIWorldObject *other = AIWorld_At(&w->observations, stackSlots[k]);
                if (!other || !other->active || !other->available || other->id == e->id) continue;
                float radii = (b->capabilities.clearance + other->radius) * 0.98f;
                if (segmentDistanceSqXZ(b->position, prop->nextPosition, other->position) < radii * radii) {
                    clear = false;
                    break;
                }
            }
        } else {
            for (unsigned k = 0; clear && k < w->count; ++k) {
                const EcosystemEntity *other = &w->entities[w->activeEntitySlots[k]];
                if (!other->id || other == e || other->physiology.cause) continue;
                float radii = (b->capabilities.clearance + other->body.capabilities.clearance) * 0.98f;
                if (segmentDistanceSqXZ(b->position, prop->nextPosition, other->body.position) < radii * radii) {
                    clear = false;
                }
            }
        }
    }

    if (clear) {
        b->position = prop->nextPosition;
    }
    Vector3 disp = Vec3_Sub(b->position, e->previousPosition);
    e->velocity = dt > 0.0f ? Vec3_Scale(disp, 1.0f / dt) : Vec3_Zero();
    b->speed = dt > 0.0f ? Vec3_Length(disp) / dt : 0.0f;
    /* Publicar posición actualizada en AIWorld para que los siguientes IDs vean el estado real. */
    publishBodyObservation(w, e);
}

static void reproduce(EcosystemWorld *w, EcosystemEntity *a) {
    if (!a->body.mateReady || a->mind.action != ACTION_APPROACH_MATE || a->physiology.cause) return;
    EcosystemEntity *b = EcosystemWorld_Get(w, a->mind.target);
    const ReproductionProfile *r = &w->species[a->speciesIndex].reproduction;
    if (!b || b == a || b->speciesIndex != a->speciesIndex || a->reproduction.role == b->reproduction.role ||
        b->mind.action != ACTION_APPROACH_MATE || b->mind.target != a->id ||
        !CreatureReproduction_Ready(&a->reproduction, r, &a->physiology, w->clock.seconds) ||
        !CreatureReproduction_Ready(&b->reproduction, r, &b->physiology, w->clock.seconds) ||
        Vec3_Distance(a->body.position, b->body.position) >
            a->body.capabilities.clearance + b->body.capabilities.clearance + 0.25f ||
        !AIWorld_LineOfSight(&w->observations, a->body.eye, b->body.eye, b->id))
        return;
    a->reproduction.partner = b->id; b->reproduction.partner = a->id;
    a->physiology.energy -= r->energyCost; b->physiology.energy -= r->energyCost;
    a->reproduction.cooldownUntil = b->reproduction.cooldownUntil = w->clock.seconds + r->cooldown;
    EcosystemEntity *mother = a->reproduction.role == 0 ? a : b;
    EcosystemEntity *father = mother == a ? b : a;
    mother->reproduction.gestating = true;
    mother->reproduction.due = w->clock.seconds + r->gestation;
    mother->reproduction.mateGenome = father->genome;
    mother->reproduction.mateGeneration = father->generation;
    mother->reproduction.gestationParents[0] = mother->id;
    mother->reproduction.gestationParents[1] = father->id;
    const NeuralGenome *motherBrain = CreatureMind_GetNeuralGenome(&mother->mind);
    const NeuralGenome *fatherBrain = CreatureMind_GetNeuralGenome(&father->mind);
    if (motherBrain && fatherBrain) {
        bool okM = NeuralGenome_Clone(&mother->reproduction.gestationNeuralGenomes[0], motherBrain);
        bool okF = NeuralGenome_Clone(&mother->reproduction.gestationNeuralGenomes[1], fatherBrain);
        mother->reproduction.hasGestationNeuralGenomes = okM && okF;
    } else {
        mother->reproduction.hasGestationNeuralGenomes = false;
    }
    mother->reproduction.partner = 0; father->reproduction.partner = 0;
    a->body.mateReady = b->body.mateReady = false;
    if (a->mind.controllerType == CREATURE_CONTROLLER_NEURAL)
        CreatureMind_RecordOutcome(&a->mind, NEURAL_OUTCOME_MATED);
    if (b->mind.controllerType == CREATURE_CONTROLLER_NEURAL)
        CreatureMind_RecordOutcome(&b->mind, NEURAL_OUTCOME_MATED);
    /* Contabilizar descendencia reproductiva para los padres de a y b (una sola vez). */
    EcosystemEntity *pair[2] = {a, b};
    for (unsigned p = 0; p < 2; ++p) {
        if (!pair[p]->reproductiveRecorded) {
            pair[p]->reproductiveRecorded = true;
            if (pair[p]->reproduction.parents[0] || pair[p]->reproduction.parents[1]) {
                ++w->statistics.reproductiveOffspring;
                for (unsigned gp = 0; gp < 2; ++gp) {
                    EcosystemParentRecord *rec = ensureParentRecord(w, pair[p]->reproduction.parents[gp], false);
                    if (rec) ++rec->reproductiveOffspringCount;
                }
            }
        }
    }
    ++w->statistics.matings;
    ++w->statistics.conceptions;
    event(w, 6, a->id, b->id);
}

/* ========================================================================== */
/* Estadísticas y diversidad neuronal                                         */
/* ========================================================================== */

static void statistics(EcosystemWorld *w) {
    EcosystemStatistics *s = &w->statistics;
    s->population = w->count;
    s->juveniles = 0;
    s->missingFounderCount = 0;
    s->averageAge = s->averageHealth = s->averageEnergy = s->averageGeneration = s->plantBiomass = 0.0;
    s->averageFounderDistance = 0.0;
    s->maxFounderDistance = 0.0;
    unsigned neuralCount = 0, neuralWithFounder = 0;

    for (unsigned i = 0; i < w->lineageCount; ++i) w->lineages[i].livingCount = 0;
    for (unsigned k = 0; k < w->count; ++k) {
        EcosystemEntity *e = &w->entities[w->activeEntitySlots[k]];
        if (!e->id) continue;
        s->averageAge += e->physiology.age;
        s->averageHealth += e->physiology.health;
        s->averageEnergy += e->physiology.energy;
        s->averageGeneration += e->generation;
        s->juveniles += (e->physiology.stage == LIFE_JUVENILE);
        double obsSurv = observedSurvivalSeconds(w, e);
        if (obsSurv > s->maxObservedSurvival) s->maxObservedSurvival = obsSurv;
        if (e->mind.controllerType == CREATURE_CONTROLLER_NEURAL) {
            ++neuralCount;
            if (e->founderMissing) {
                ++s->missingFounderCount;
            } else {
                ++neuralWithFounder;
                s->averageFounderDistance += e->founderDistance;
                if (e->founderDistance > s->maxFounderDistance) s->maxFounderDistance = e->founderDistance;
            }
            const NeuralGenome *g = CreatureMind_GetNeuralGenome(&e->mind);
            if (g && g->metadata.lineageId) {
                EcosystemLineage *l = findOrCreateLineage(w, g->metadata.lineageId, 0u,
                                                          g->metadata.founderFingerprint,
                                                          w->species[e->speciesIndex].id);
                if (l) {
                    ++l->livingCount;
                    l->lastActiveSeconds = w->clock.seconds;
                    if (e->generation > l->maxGeneration) l->maxGeneration = e->generation;
                }
            }
        }
    }
    if (w->count) {
        s->averageAge /= w->count;
        s->averageHealth /= w->count;
        s->averageEnergy /= w->count;
        s->averageGeneration /= w->count;
    }
    if (neuralWithFounder) {
        s->averageFounderDistance /= neuralWithFounder;
    }
    s->activeLineages = 0;
    for (unsigned i = 0; i < w->lineageCount; ++i)
        if (w->lineages[i].livingCount > 0) ++s->activeLineages;
    if (s->completedLifetimes > 0) {
        s->averageSurvivalDuration = s->survivalDurationSum / (double)s->completedLifetimes;
    } else if (w->count > 0) {
        double sumObs = 0.0;
        for (unsigned k = 0; k < w->count; ++k) {
            sumObs += observedSurvivalSeconds(w, &w->entities[w->activeEntitySlots[k]]);
        }
        s->averageSurvivalDuration = sumObs / (double)w->count;
    } else {
        s->averageSurvivalDuration = 0.0;
    }
    for (unsigned k = 0; k < w->activeResourceCount; ++k) {
        const EcosystemResource *r = &w->resources[w->activeResourceSlots[k]];
        if (r->id && r->category == ECO_PLANT) s->plantBiomass += r->biomass;
    }
}

void EcosystemWorld_ComputeNeuralMetrics(EcosystemWorld *w) {
    if (!w) return;
    if (w->count < 2u) {
        w->statistics.neuralDiversity = 0.0;
        w->statistics.neuralDispersionRms = 0.0;
        return;
    }

    const NeuralGenome **living = malloc(w->count * sizeof(*living));
    if (!living) return;
    unsigned n = 0;
    /* Usar w->order si coincide o recolectar en orden determinista por ID ascendente. */
    for (unsigned k = 0; k < w->count; ++k) {
        const EcosystemEntity *e = &w->entities[w->activeEntitySlots[k]];
        if (!e->id) continue;
        const NeuralGenome *g = CreatureMind_GetNeuralGenome(&e->mind);
        if (g) living[n++] = g;
    }
    if (n < 2u) {
        w->statistics.neuralDiversity = 0.0;
        w->statistics.neuralDispersionRms = 0.0;
        free(living);
        return;
    }

    /* 1) Dispersión RMS exacta por momentos O(N * P) cubriendo la arquitectura dominante (v1 o v2). */
    NeuralArchitectureType refArch = living[0]->architecture;
    uint32_t paramCount = NeuralGenome_ParamCount(living[0]);
    if (paramCount == 0u || paramCount > NEURAL_MAX_PARAM_COUNT) {
        w->statistics.neuralDiversity = 0.0;
        w->statistics.neuralDispersionRms = 0.0;
        free(living);
        return;
    }
    double sumW[NEURAL_MAX_PARAM_COUNT];
    double sumSqW[NEURAL_MAX_PARAM_COUNT];
    float flat[NEURAL_MAX_PARAM_COUNT];
    memset(sumW, 0, sizeof(double) * paramCount);
    memset(sumSqW, 0, sizeof(double) * paramCount);
    unsigned compatCount = 0u;
    for (unsigned i = 0; i < n; ++i) {
        if (living[i]->architecture != refArch) continue;
        if (NeuralGenome_CopyFlatParams(living[i], flat, paramCount) != paramCount) continue;
        ++compatCount;
        for (uint32_t p = 0; p < paramCount; ++p) {
            double v = (double)flat[p];
            sumW[p] += v;
            sumSqW[p] += v * v;
        }
    }
    if (compatCount >= 2u) {
        double totalVarSum = 0.0;
        double dn = (double)compatCount;
        for (uint32_t p = 0; p < paramCount; ++p) {
            double mean = sumW[p] / dn;
            double diff = sumSqW[p] - dn * mean * mean;
            if (diff > 0.0) totalVarSum += diff;
        }
        double meanPairwiseSq = (2.0 * totalVarSum) / ((dn - 1.0) * (double)paramCount);
        w->statistics.neuralDispersionRms = meanPairwiseSq > 0.0 ? sqrt(meanPairwiseSq) : 0.0;
    } else {
        w->statistics.neuralDispersionRms = 0.0;
    }

    /* 2) Media de distancias RMS individuales por pares (exacta para N<=48, muestreo determinista
     * uniforme que cubre el 100% de la población viva para N>48). */
    double sum = 0.0;
    unsigned pairs = 0;
    if (n <= 48u) {
        for (unsigned i = 0; i < n; ++i) {
            for (unsigned j = i + 1u; j < n; ++j) {
                sum += (double)NeuralGenome_Distance(living[i], living[j]);
                ++pairs;
            }
        }
    } else {
        /* 256 pares deterministas cubriendo toda la población con paso coprimo. */
        const unsigned targetPairs = 256u;
        uint32_t state = 2654435761u ^ (uint32_t)n;
        for (unsigned p = 0; p < targetPairs; ++p) {
            state ^= state << 13; state ^= state >> 17; state ^= state << 5;
            unsigned i = (p * n) / targetPairs;
            unsigned offset = 1u + (state % (n - 1u));
            unsigned j = (i + offset) % n;
            sum += (double)NeuralGenome_Distance(living[i], living[j]);
            ++pairs;
        }
    }
    w->statistics.neuralDiversity = pairs ? (sum / (double)pairs) : 0.0;
    free(living);
}

/* ========================================================================== */
/* Fases paralelas deterministas de un tick                                   */
/* ========================================================================== */

typedef struct TickParallelCtx {
    EcosystemWorld *w;
    unsigned count;
    float dt;
    /* Acumuladores por entidad para reducción determinista en orden de ID ascendente. */
    unsigned *actionFailDelta;
    unsigned *navFailDelta;
    uint8_t *decisionIntent; /* 255 = sin decisión nueva */
    uint8_t *v2ActionOut;    /* 255 = sin decisión v2 nueva */
    uint8_t *escapedFlag;
    MetabolicStepOutcome *metOutcomes;
} TickParallelCtx;

static void phasePhysiologyWorker(void *opaque, unsigned start, unsigned end, unsigned workerIdx) {
    (void)workerIdx;
    TickParallelCtx *ctx = (TickParallelCtx *)opaque;
    EcosystemWorld *w = ctx->w;
    float dt = ctx->dt;
    bool molecular = (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR && w->metabolismDb != NULL);
    for (unsigned k = start; k < end; ++k) {
        EcosystemEntity *e = &w->entities[w->order[k]];
        PhysiologyProfile p = physiologicalProfile(w, e);
        p.basalCost *= powf(e->genome.size, 0.75f) * (e->reproduction.gestating ? 1.2f : 1.0f);
        float healthBefore = e->physiology.health;

        if (molecular && e->metabolism.initialized && e->physiology.cause == DEATH_NONE) {
            const MetabolicProfile *mp = MetabolicProfileRegistry_Get(
                &w->metabolismDb->profiles, w->species[e->speciesIndex].metabolicProfile);
            MetabolicStepOutcome mOut = {0};
            if (mp) {
                CreatureMetabolism_Step(&e->metabolism, mp, &e->physiology, &p, dt, &mOut);
                if (e->mind.controllerType == CREATURE_CONTROLLER_NEURAL) {
                    CreatureMind_RecordMetabolicEffects(&e->mind, mOut.energyGenerated,
                                                        mOut.hydrationGenerated, mOut.nutrientsAbsorbed,
                                                        mOut.toxicityDelta, mOut.metabolicDamage);
                }
            }
            if (ctx->metOutcomes) ctx->metOutcomes[k] = mOut;
        } else if (ctx->metOutcomes) {
            memset(&ctx->metOutcomes[k], 0, sizeof(ctx->metOutcomes[k]));
        }

        float healthAfterMet = e->physiology.health;
        CreaturePhysiology_Update(&e->physiology, &p, dt, e->body.speed, e->genome.efficiency,
                                  e->mind.action == ACTION_REST && e->body.speed < 0.04f);
        if (e->mind.controllerType == CREATURE_CONTROLLER_NEURAL && e->physiology.health < healthAfterMet) {
            CreatureMind_RecordDamage(&e->mind, healthAfterMet - e->physiology.health);
        }
        (void)healthBefore;
        needs(e);
        prepareBody(w, e);
    }
}

static void phaseMindWorker(void *opaque, unsigned start, unsigned end, unsigned workerIdx) {
    (void)workerIdx;
    TickParallelCtx *ctx = (TickParallelCtx *)opaque;
    EcosystemWorld *w = ctx->w;
    float dt = ctx->dt;
    uint64_t localNavReqs = 0u, localExpanded = 0u;

    for (unsigned k = start; k < end; ++k) {
        EcosystemEntity *e = &w->entities[w->order[k]];
        ctx->actionFailDelta[k] = 0u;
        ctx->navFailDelta[k] = 0u;
        ctx->decisionIntent[k] = 255u;
        if (ctx->v2ActionOut) ctx->v2ActionOut[k] = 255u;
        ctx->escapedFlag[k] = 0u;
        if (e->physiology.cause) continue;

        unsigned failures = e->mind.actionFailures, nav = e->mind.navigation.failures;
        uint64_t decisionsBefore = e->mind.neuralState.decisionCount;
        e->mind.navigation.lastExpandedNodes = 0u;
        CreatureAction old = e->mind.action;
        const NeedDefinition *social = &e->brain.needs[NEED_SOCIAL];
        e->mind.needs.urgency[NEED_SOCIAL] =
            social->enabled ? clamp(e->mind.needs.urgency[NEED_SOCIAL] + social->rate * dt, 0, 1) : 0;

        CreatureMind_Update(&e->mind, &w->observations, &e->body, dt);

        if (e->mind.navigation.lastExpandedNodes > 0u) {
            ++localNavReqs;
            localExpanded += e->mind.navigation.lastExpandedNodes;
        }
        ctx->actionFailDelta[k] = e->mind.actionFailures - failures;
        ctx->navFailDelta[k] = e->mind.navigation.failures - nav;
        if (e->mind.controllerType == CREATURE_CONTROLLER_NEURAL &&
            e->mind.neuralState.decisionCount > decisionsBefore) {
            NeuralActionIntent intent = e->mind.neuralState.selectedIntent;
            if (intent >= 0 && intent < ACTION_INTENT_COUNT) {
                ctx->decisionIntent[k] = (uint8_t)intent;
            }
            if (ctx->v2ActionOut && e->mind.neuralState.v2.active) {
                NeuralV2ActionType v2a = e->mind.neuralState.v2.selectedAction;
                if (v2a >= 0 && v2a < NEURAL_V2_ACTION_COUNT) {
                    ctx->v2ActionOut[k] = (uint8_t)v2a;
                }
            }
        }
        if (old != ACTION_ESCAPE && e->mind.action == ACTION_ESCAPE) {
            ctx->escapedFlag[k] = 1u;
        }
    }
    if (w->profiler && (localNavReqs || localExpanded)) {
        EvolutionProfiler_AddAtomic(&w->profiler->counters.navigationRequests, localNavReqs);
        EvolutionProfiler_AddAtomic(&w->profiler->counters.astarExpandedNodes, localExpanded);
    }
}

static void phaseMoveProposalWorker(void *opaque, unsigned start, unsigned end, unsigned workerIdx) {
    (void)workerIdx;
    TickParallelCtx *ctx = (TickParallelCtx *)opaque;
    EcosystemWorld *w = ctx->w;
    float dt = ctx->dt;
    for (unsigned k = start; k < end; ++k) {
        EcosystemEntity *e = &w->entities[w->order[k]];
        if (e->physiology.cause) {
            w->moveProposals[k] = (EcosystemMoveProposal){.nextPosition = e->body.position, .nextYaw = e->body.yaw};
            continue;
        }
        computeMovementProposal(w, e, dt, &w->moveProposals[k]);
    }
}

static void phaseAnatomyWorker(void *opaque, unsigned start, unsigned end, unsigned workerIdx) {
    (void)workerIdx;
    TickParallelCtx *ctx = (TickParallelCtx *)opaque;
    EcosystemWorld *w = ctx->w;
    float dt = ctx->dt;
    for (unsigned k = start; k < end; ++k) {
        EcosystemEntity *e = &w->entities[w->order[k]];
        if (e->physiology.cause) continue;
        mouth(w, e, dt);
    }
}

static void commit(EcosystemWorld *w) {
    for (unsigned k = 0; k < w->count; ++k) {
        EcosystemEntity *e = &w->entities[w->order[k]];
        if (!e->id || e->physiology.cause) continue;
        /* Registrar transición a madurez para contabilidad reproductiva. */
        if (!e->maturityRecorded && e->physiology.stage != LIFE_JUVENILE) {
            e->maturityRecorded = true;
            if (e->reproduction.parents[0] || e->reproduction.parents[1]) {
                ++w->statistics.offspringReachingMaturity;
                for (unsigned pi = 0; pi < 2; ++pi) {
                    EcosystemParentRecord *rec = ensureParentRecord(w, e->reproduction.parents[pi], false);
                    if (rec) ++rec->matureOffspringCount;
                }
            }
        }
        const ReproductionProfile *r = &w->species[e->speciesIndex].reproduction;
        if (e->reproduction.gestating && w->clock.seconds + 1e-6 >= e->reproduction.due) {
            float physProb = w->disablePhysicalMutation ? 0.0f : r->mutationProbability;
            float physAmp = w->disablePhysicalMutation ? 0.0f : r->mutationAmplitude;
            for (unsigned j = 0; j < r->offspring; ++j) {
                if (w->birthCount == w->capacity) { ++w->statistics.failedBirths; continue; }
                EcosystemBirth *birth = &w->birthQueue[w->birthCount++];
                memset(birth, 0, sizeof(*birth));
                birth->speciesIndex = e->speciesIndex;
                birth->generation = 1 + (e->generation > e->reproduction.mateGeneration
                                             ? e->generation : e->reproduction.mateGeneration);
                birth->position = e->body.position;
                birth->parents[0] = e->reproduction.gestationParents[0];
                birth->parents[1] = e->reproduction.gestationParents[1];
                birth->genome = CreatureGenome_Inherit(&e->genome, &e->reproduction.mateGenome,
                                                       &w->random, physProb, physAmp);
                if (e->reproduction.hasGestationNeuralGenomes) {
                    if (NeuralGenome_Inherit(&birth->neuralGenome,
                                             &e->reproduction.gestationNeuralGenomes[0],
                                             &e->reproduction.gestationNeuralGenomes[1],
                                             w->neuralMutation, &w->random)) {
                        birth->neuralGenome.metadata.generation = birth->generation;
                        birth->neuralGenome.metadata.parentIds[0] = birth->parents[0];
                        birth->neuralGenome.metadata.parentIds[1] = birth->parents[1];
                        birth->hasNeuralGenome = true;
                    }
                }
            }
            e->reproduction.gestating = false;
            e->reproduction.hasGestationNeuralGenomes = false;
            NeuralGenome_Destroy(&e->reproduction.gestationNeuralGenomes[0]);
            NeuralGenome_Destroy(&e->reproduction.gestationNeuralGenomes[1]);
            e->reproduction.cooldownUntil = w->clock.seconds + r->cooldown;
        }
    }

    unsigned before = w->count;
    for (unsigned k = 0; k < before; ++k) {
        unsigned slot = w->order[k];
        EcosystemEntity *e = &w->entities[slot];
        if (!e->id || !e->physiology.cause) continue;
        const EcosystemSpecies *s = &w->species[e->speciesIndex];
        AIObjectId id = e->id;
        AIObjectId killerId = e->lastAttackerId;
        Vector3 position = e->body.position;
        float bodyScale = e->genome.size * e->developmentScale;
        CreatureDeathCause cause = e->physiology.cause;

        considerForArchives(w, e, false);
        markArchiveDead(w, id);
        EcosystemParentRecord *prec = ensureParentRecord(w, id, false);
        if (prec) prec->alive = false;

        double obsSurv = observedSurvivalSeconds(w, e);
        w->statistics.survivalDurationSum += obsSurv;
        ++w->statistics.completedLifetimes;
        if (obsSurv > w->statistics.maxObservedSurvival) w->statistics.maxObservedSurvival = obsSurv;

        AIWorld_Remove(&w->observations, id);
        idMapRemove(&w->entityMap, id);
        unsigned aIdx = w->entitySlotToActive[slot];
        if (aIdx != UINT_MAX) {
            unsigned lastSlot = w->activeEntitySlots[--w->count];
            if (aIdx < w->count) {
                w->activeEntitySlots[aIdx] = lastSlot;
                w->entitySlotToActive[lastSlot] = aIdx;
            }
            w->entitySlotToActive[slot] = UINT_MAX;
        }
        invalidate(w, id);
        ++w->statistics.deaths;
        ++w->statistics.deathCauses[cause];
        if (cause == DEATH_TOXICITY) ++w->statistics.toxicityDeaths;
        event(w, 2, id, cause);

        NeuralBrain_Destroy(&e->mind.neuralBrain);
        NeuralGenome_Destroy(&e->reproduction.gestationNeuralGenomes[0]);
        NeuralGenome_Destroy(&e->reproduction.gestationNeuralGenomes[1]);
        EcosystemAnatomy_Destroy(e->anatomicalBody);
        memset(e, 0, sizeof(*e));
        w->freeEntitySlots[w->freeEntityCount++] = slot;

        if (cause != DEATH_REMOVED) {
            if (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR && w->metabolismDb &&
                s->tissueSubstance != SUBSTANCE_INVALID_HANDLE) {
                const SubstanceDef *tissue =
                    SubstanceRegistry_Get(&w->metabolismDb->substances, s->tissueSubstance);
                float mass = fmaxf(0.5f, (s->tissueMass > 0.0f ? s->tissueMass : s->edibleBiomass) * bodyScale);
                if (tissue && mass > 0.0f) {
                    EcosystemResource cr = EcosystemResource_FromSubstance(
                        tissue, mass, mass, RESOURCE_REGEN_NONE, 0.0f,
                        RESOURCE_DECAY_DECOMPOSE, 180.0, 0.008f, w->decayTargetMolecule);
                    cr.originalSpecies = s->id;
                    cr.sourceCreatureId = id;
                    cr.killerCreatureId = (cause == DEATH_PREDATION) ? killerId : 0u;
                    cr.deathTime = w->clock.seconds;
                    cr.sourceDeathCause = cause;
                    AIObjectId carcass = EcosystemWorld_AddMolecularResource(w, cr, position, s->radius * 1.5f);
                    if (!carcass) event(w, 9, id, 0);
                }
            } else if (s->edibleBiomass > 0) {
                AIObjectId carcass = EcosystemWorld_AddResource(w, (EcosystemResource){
                    .category = ECO_MEAT, .originalSpecies = s->id,
                    .biomass = s->edibleBiomass, .maximumBiomass = s->edibleBiomass,
                    .nutritionalValue = s->nutrition, .remainingLifetime = 180, .accessible = true
                }, position, s->radius * 1.5f);
                if (!carcass) event(w, 9, id, 0);
            }
        }
    }

    w->updating = false;
    for (unsigned i = 0; i < w->birthCount; ++i) {
        EcosystemBirth *birth = &w->birthQueue[i];
        AIObjectId id = 0;
        for (unsigned j = 0; j < 24 && !id && w->count < w->capacity; ++j) {
            float angle = CreatureGenome_Random(&w->random) * 6.2831853f;
            float distance = 0.65f + CreatureGenome_Random(&w->random) * 2.0f;
            Vector3 p = Vec3_Add(birth->position, Vec3_Create(sinf(angle) * distance, 0, cosf(angle) * distance));
            if (!spawnPosition(w, p, w->species[birth->speciesIndex].radius * birth->genome.size * 0.55f))
                continue;
            id = EcosystemWorld_Spawn(w, w->species[birth->speciesIndex].id, p, 0, 0.8f, &birth->genome);
        }
        if (id) {
            EcosystemEntity *child = EcosystemWorld_Get(w, id);
            bool neuralOk = true;
            if (birth->hasNeuralGenome) {
                neuralOk = CreatureMind_AssignNeuralGenome(&child->mind, &birth->neuralGenome);
                if (!neuralOk) {
                    /* No producir silenciosamente una cría mal inicializada si falla la herencia neural. */
                    EcosystemWorld_RequestDeath(w, id, DEATH_REMOVED);
                    ++w->statistics.failedBirths;
                    event(w, 8, birth->parents[0], 0);
                    NeuralGenome_Destroy(&birth->neuralGenome);
                    continue;
                }
                child->founderDistance = computeFounderDistance(w, &birth->neuralGenome, &child->founderMissing);
                EcosystemLineage *l = findOrCreateLineage(w, birth->neuralGenome.metadata.lineageId, 0u,
                                                          birth->neuralGenome.metadata.founderFingerprint,
                                                          w->species[birth->speciesIndex].id);
                if (l) {
                    ++l->totalMembers;
                    ++l->totalOffspring;
                    if (birth->generation > l->maxGeneration) l->maxGeneration = birth->generation;
                }
            }
            child->generation = birth->generation;
            if (child->generation > w->statistics.maximumGeneration)
                w->statistics.maximumGeneration = child->generation;
            child->reproduction.parents[0] = birth->parents[0];
            child->reproduction.parents[1] = birth->parents[1];
            unsigned genBucket = child->generation < ECOSYSTEM_MAX_GENERATION_BUCKETS
                                     ? child->generation : (ECOSYSTEM_MAX_GENERATION_BUCKETS - 1);
            ++w->statistics.birthsPerGeneration[genBucket];

            /* Contabilidad de descendencia incluyendo progenitores fallecidos (póstuma). */
            for (unsigned pi = 0; pi < 2; ++pi) {
                AIObjectId pid = birth->parents[pi];
                if (!pid) continue;
                EcosystemParentRecord *prec = ensureParentRecord(w, pid, false);
                if (prec) {
                    ++prec->offspringCount;
                    syncArchiveOffspring(w, pid, prec->offspringCount);
                }
                EcosystemEntity *par = EcosystemWorld_Get(w, pid);
                if (par) par->offspringCount = prec ? prec->offspringCount : (par->offspringCount + 1u);
            }
            ++w->statistics.births;
            event(w, 7, id, birth->parents[0]);
        } else {
            ++w->statistics.failedBirths;
            event(w, 8, birth->parents[0], 0);
        }
        NeuralGenome_Destroy(&birth->neuralGenome);
    }
    w->birthCount = 0;
}

static void sortOrderById(EcosystemWorld *w, unsigned count) {
    for (unsigned k = 0; k < count; ++k) {
        unsigned slot = w->activeEntitySlots[k];
        AIObjectId id = w->entities[slot].id;
        unsigned j = k;
        while (j > 0 && w->entities[w->order[j - 1]].id > id) {
            w->order[j] = w->order[j - 1];
            --j;
        }
        w->order[j] = slot;
    }
}

static void relocateDepletedResource(EcosystemWorld *w, EcosystemResource *r) {
    if (!w || !r || !r->needsRelocation) return;
    r->needsRelocation = false;

    /* Tirada determinista de rareza: alimentos raros pueden tardar ciclos extra en brotar. */
    if (r->rarityWeight > 0.0f && r->rarityWeight < 0.9999f) {
        float roll = CreatureGenome_Random(&w->random);
        if (roll > r->rarityWeight) {
            r->respawnTimer = fmaxf(4.0f, r->respawnDelaySeconds * 0.5f);
            return;
        }
    }

    const AIWorldObject *oldObj = AIWorld_Get(&w->observations, r->id);
    Vector3 oldPos = oldObj ? oldObj->position : Vec3_Zero();
    float objRadius = oldObj ? oldObj->radius : 0.8f;
    float bound = fmaxf(2.0f, w->observations.halfSize - objRadius - 1.5f);
    float maxR = (r->spawnMaxDist > 0.1f && r->spawnMaxDist < bound) ? r->spawnMaxDist : bound;
    float minR = fminf(fmaxf(0.0f, r->spawnMinDist), maxR);
    float minSep = fmaxf(0.0f, r->minRelocateDistance);

    bool moved = false;
    Vector3 chosen = oldPos;
    for (unsigned attempt = 0; attempt < 48u; ++attempt) {
        float ang = CreatureGenome_Random(&w->random) * 6.2831853f;
        /* Distribución uniforme en área dentro del anillo [minR, maxR]. */
        float u = CreatureGenome_Random(&w->random);
        float dist = sqrtf(minR * minR + u * (maxR * maxR - minR * minR));
        Vector3 cand = Vec3_Create(sinf(ang) * dist, 0.0f, cosf(ang) * dist);
        float reqSep = (attempt < 32u) ? minSep : (minSep * 0.5f);
        if (Vec3_Distance(cand, oldPos) < reqSep) continue;
        if (!AIWorld_Walkable(&w->observations, cand, objRadius + 0.45f, false)) continue;
        chosen = cand;
        moved = true;
        break;
    }

    if (moved) {
        AIWorld_Move(&w->observations, r->id, chosen);
    }
    r->biomass = r->maximumBiomass;
    r->depleted = false;
    r->respawnTimer = 0.0f;
}

static void tick(EcosystemWorld *w) {
    EvolutionProfiler *prof = w->profiler;
    double tTick = EvolutionProfiler_Begin(prof);
    float dt = (float)w->clock.fixedStep;
    w->updating = true;

    /* 1) Actualizar recursos ambientales activos y caché estática de navegación. */
    double tRes = EvolutionProfiler_Begin(prof);
    const MoleculeRegistry *molReg = (w->metabolismDb) ? &w->metabolismDb->molecules : NULL;
    for (unsigned k = 0; k < w->activeResourceCount;) {
        unsigned rSlot = w->activeResourceSlots[k];
        EcosystemResource *r = &w->resources[rSlot];
        if (!r->id) { ++k; continue; }
        if (r->molecular) {
            EcosystemResource_UpdateMolecular(r, molReg, dt);
            if (r->needsRelocation) {
                relocateDepletedResource(w, r);
            }
        } else {
            EcosystemResource_Update(r, dt);
        }
        if (EcosystemResource_ShouldRemove(r)) {
            AIObjectId id = r->id;
            EcosystemWorld_RemoveResource(w, id);
            continue; /* RemoveResource intercambia el último elemento en k */
        }
        if (r->molecular) {
            AIWorld_PublishMolecularResource(&w->observations, r->id, r->originalSpecies, r->category,
                                             EcosystemResource_Available(r), (uint8_t)r->phase,
                                             r->hardness, EcosystemResource_AvailabilityFraction(r),
                                             &r->sensorySignature);
        } else {
            AIWorld_PublishResource(&w->observations, r->id, r->originalSpecies, r->category,
                                    EcosystemResource_Available(r));
        }
        ++k;
    }
    CreatureNavigation_EnsureCache(&w->observations);
    EvolutionProfiler_End(prof, EVO_PROF_RESOURCES, tRes);

    /* 2) Orden canónico ascendente por ID de entidad. */
    unsigned count = w->count;
    sortOrderById(w, count);

    /* Reservar scratch compacto en pila o dinámico para reducción determinista de estadísticas. */
    unsigned stackFail[1024], stackNav[1024];
    uint8_t stackIntent[1024], stackV2Act[1024], stackEsc[1024];
    MetabolicStepOutcome stackMet[1024];
    unsigned *failDelta = count <= 1024u ? stackFail : calloc(count, sizeof(unsigned));
    unsigned *navDelta = count <= 1024u ? stackNav : calloc(count, sizeof(unsigned));
    uint8_t *intentOut = count <= 1024u ? stackIntent : calloc(count, sizeof(uint8_t));
    uint8_t *v2ActOut = count <= 1024u ? stackV2Act : calloc(count, sizeof(uint8_t));
    uint8_t *escOut = count <= 1024u ? stackEsc : calloc(count, sizeof(uint8_t));
    MetabolicStepOutcome *metOutcomes =
        (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR)
            ? (count <= 1024u ? stackMet : calloc(count, sizeof(MetabolicStepOutcome)))
            : NULL;

    TickParallelCtx ctx = {
        .w = w, .count = count, .dt = dt,
        .actionFailDelta = failDelta, .navFailDelta = navDelta,
        .decisionIntent = intentOut, .v2ActionOut = v2ActOut,
        .escapedFlag = escOut, .metOutcomes = metOutcomes
    };

    /* 3) Digestión molecular, fisiología y configuración anatómica en paralelo. */
    double tPhys = EvolutionProfiler_Begin(prof);
    EvolutionJobSystem_ParallelFor(w->jobs, count, 32u, phasePhysiologyWorker, &ctx);
    if (metOutcomes) {
        double tMet = EvolutionProfiler_Begin(prof);
        for (unsigned k = 0; k < count; ++k) {
            const MetabolicStepOutcome *mo = &metOutcomes[k];
            w->statistics.totalProcessedMass += (double)mo->processedMass;
            w->statistics.totalAbsorbedMass += (double)mo->absorbedMass;
            w->statistics.totalRejectedMass += (double)mo->rejectedMass;
            w->statistics.totalWasteEliminated += (double)mo->wasteEliminated;
            w->statistics.totalMetabolicEnergy += (double)mo->energyGenerated;
            w->statistics.totalMetabolicHydration += (double)mo->hydrationGenerated;
            w->statistics.totalMetabolicNutrients += (double)mo->nutrientsAbsorbed;
            w->statistics.totalDigestiveEnergyCost += (double)mo->digestiveEnergyCost;
            w->statistics.totalToxinExposure += (double)mo->toxicityAdded;
            w->statistics.totalDetoxified += (double)mo->detoxified;
            w->statistics.totalMetabolicDamage += (double)mo->metabolicDamage;
        }
        EvolutionProfiler_End(prof, EVO_PROF_METABOLISM, tMet);
    }
    EvolutionProfiler_End(prof, EVO_PROF_PHYSIOLOGY, tPhys);

    /* 4) Publicación de observaciones inmutables pre-movimiento. */
    double tObs = EvolutionProfiler_Begin(prof);
    for (unsigned k = 0; k < count; ++k) {
        publishBodyObservation(w, &w->entities[w->order[k]]);
    }
    EvolutionProfiler_End(prof, EVO_PROF_OBS_PUBLISH, tObs);

    /* 5) Percepción, inferencia neuronal, acciones y navegación A* en paralelo sobre snapshot inmutable. */
    double tMind = EvolutionProfiler_Begin(prof);
    EvolutionJobSystem_ParallelFor(w->jobs, count, 16u, phaseMindWorker, &ctx);
    for (unsigned k = 0; k < count; ++k) {
        w->statistics.actionFailures += failDelta[k];
        w->statistics.navigationFailures += navDelta[k];
        if (intentOut[k] < ACTION_INTENT_COUNT) ++w->statistics.intentSelections[intentOut[k]];
        if (v2ActOut[k] < NEURAL_V2_ACTION_COUNT) ++w->statistics.v2ActionSelections[v2ActOut[k]];
        if (escOut[k]) ++w->statistics.escapes;
    }
    EvolutionProfiler_End(prof, EVO_PROF_PERCEPTION, tMind);

    /* 6) Propuestas locales de movimiento y evitación recíproca en paralelo. */
    double tAvoid = EvolutionProfiler_Begin(prof);
    EvolutionJobSystem_ParallelFor(w->jobs, count, 32u, phaseMoveProposalWorker, &ctx);
    EvolutionProfiler_End(prof, EVO_PROF_AVOIDANCE, tAvoid);

    /* 7) Resolución determinista de conflictos de movimiento y publicación de posiciones finales. */
    double tMove = EvolutionProfiler_Begin(prof);
    for (unsigned k = 0; k < count; ++k) {
        EcosystemEntity *e = &w->entities[w->order[k]];
        if (!e->physiology.cause) commitMovementProposal(w, e, &w->moveProposals[k], dt);
    }
    EvolutionProfiler_End(prof, EVO_PROF_MOVEMENT, tMove);

    /* 8) Alcance anatómico cervical en paralelo sobre posiciones finales inmutables. */
    double tAnat = EvolutionProfiler_Begin(prof);
    EvolutionJobSystem_ParallelFor(w->jobs, count, 16u, phaseAnatomyWorker, &ctx);
    for (unsigned k = 0; k < count; ++k) {
        EcosystemEntity *e = &w->entities[w->order[k]];
        if (!e->physiology.cause) prepareBody(w, e);
        publishBodyObservation(w, e);
    }
    EvolutionProfiler_End(prof, EVO_PROF_ANATOMY_REACH, tAnat);

    /* 9) Recursos, daño, ataque y reproducción en orden determinista de ID ascendente. */
    double tRepro = EvolutionProfiler_Begin(prof);
    for (unsigned k = 0; k < count; ++k) {
        EcosystemEntity *e = &w->entities[w->order[k]];
        if (e->physiology.cause) continue;
        if (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR) {
            if (e->mind.intakeMassRequested > 0.0f) {
                EcosystemResource *r = EcosystemWorld_Resource(w, e->mind.target);
                if (r) EcosystemWorld_Consume(w, e->id, r->id, e->mind.intakeMassRequested);
                if (!r || !EcosystemResource_Available(r)) CreatureActions_Fail(&e->mind);
            }
        } else if (e->mind.intakeRequested > 0) {
            EcosystemResource *r = EcosystemWorld_Resource(w, e->mind.target);
            if (r) EcosystemWorld_Consume(w, e->id, r->id,
                                          e->mind.intakeRequested / (r->category == ECO_WATER ? 1.0f : r->nutritionalValue));
            if (!r || !EcosystemResource_Available(r)) CreatureActions_Fail(&e->mind);
        }
        if (e->mind.action == ACTION_ATTACK) EcosystemWorld_Attack(w, e->id, e->mind.target);
        reproduce(w, e);
        needs(e);
    }
    EvolutionProfiler_End(prof, EVO_PROF_REPRODUCTION, tRepro);

    double tBD = EvolutionProfiler_Begin(prof);
    commit(w);
    EvolutionProfiler_End(prof, EVO_PROF_BIRTH_DEATH, tBD);

    double tStats = EvolutionProfiler_Begin(prof);
    statistics(w);
    if (w->clock.tick % 60u == 0u) EcosystemWorld_ComputeNeuralMetrics(w);
    EvolutionProfiler_End(prof, EVO_PROF_STATISTICS, tStats);

    if (count > 1024u) {
        free(failDelta); free(navDelta); free(intentOut); free(v2ActOut); free(escOut);
        free(metOutcomes);
    }
    EvolutionProfiler_End(prof, EVO_PROF_SIM_TICK, tTick);
}

void EcosystemWorld_Step(EcosystemWorld *w) {
    if (w && SimulationClock_Tick(&w->clock)) tick(w);
}

unsigned EcosystemWorld_Advance(EcosystemWorld *w, double seconds) {
    if (!w) return 0;
    SimulationClock_Accumulate(&w->clock, seconds);
    unsigned n = 0;
    while (n < w->clock.maxStepsPerFrame && SimulationClock_Pop(&w->clock)) {
        tick(w);
        ++n;
    }
    w->clock.behind = w->clock.accumulator + 1e-12 >= w->clock.fixedStep;
    return n;
}

/* ========================================================================== */
/* Gestión segura de cerebros neuronales                                      */
/* ========================================================================== */

static void invalidateEntityAnatomyAfterBrainChange(EcosystemEntity *e) {
    if (!e) return;
    EcosystemAnatomy_InvalidateInteraction(e->anatomicalBody);
    e->body.reach = (ReachResult){.status = REACH_INVALID};
}

bool EcosystemWorld_AssignBrain(EcosystemWorld *w, AIObjectId id, const NeuralGenome *genome,
                                const char *reason) {
    if (!w || w->updating || !NeuralGenome_Validate(genome)) return false;
    /* Validar compatibilidad explícita entre modo metabólico y arquitectura neuronal (Sección 19). */
    if (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR &&
        genome->architecture != NEURAL_ARCH_CANDIDATE_POLICY) {
        return false;
    }
    EcosystemEntity *e = EcosystemWorld_Get(w, id);
    if (!e || e->physiology.cause) return false;
    const NeuralGenome *prev = CreatureMind_GetNeuralGenome(&e->mind);
    uint64_t oldFp = prev ? prev->metadata.fingerprint : 0u;
    NeuralGenome copy;
    if (!NeuralGenome_Clone(&copy, genome)) return false;

    uint64_t extLineage = 0u;
    if (copy.metadata.lineageId == 0u) {
        copy.metadata.lineageId = w->nextLineageId++;
    } else if (reason && strcmp(reason, "import") == 0) {
        /* Evitar colisión de ID de linaje importado con un linaje local no relacionado. */
        EcosystemLineage *conflict = findLineage(w, copy.metadata.lineageId);
        if (conflict && conflict->founderFingerprint != copy.metadata.founderFingerprint) {
            extLineage = copy.metadata.lineageId;
            copy.metadata.lineageId = w->nextLineageId++;
        } else if (copy.metadata.lineageId >= w->nextLineageId) {
            w->nextLineageId = copy.metadata.lineageId + 1u;
        }
    }

    if (!CreatureMind_AssignNeuralGenome(&e->mind, &copy)) {
        NeuralGenome_Destroy(&copy);
        return false;
    }
    invalidateEntityAnatomyAfterBrainChange(e);
    registerFounderGenome(w, &copy);
    e->founderDistance = computeFounderDistance(w, &copy, &e->founderMissing);
    EcosystemLineage *l = findOrCreateLineage(w, copy.metadata.lineageId, extLineage,
                                              copy.metadata.founderFingerprint,
                                              w->species[e->speciesIndex].id);
    if (l && !prev) { ++l->totalMembers; }
    uint64_t newFp = e->mind.neuralBrain.genome.metadata.fingerprint;
    if (reason && reason[0]) recordIntervention(w, reason, id, 0, oldFp, newFp, 0, 0);
    NeuralGenome_Destroy(&copy);
    statistics(w);
    return true;
}

bool EcosystemWorld_CloneBrain(EcosystemWorld *w, AIObjectId sourceId, AIObjectId targetId) {
    if (!w || w->updating) return false;
    const EcosystemEntity *src = EcosystemWorld_GetConst(w, sourceId);
    EcosystemEntity *dst = EcosystemWorld_Get(w, targetId);
    if (!src || !dst || src->physiology.cause || dst->physiology.cause) return false;
    const NeuralGenome *srcGenome = CreatureMind_GetNeuralGenome(&src->mind);
    if (!srcGenome) return false;
    NeuralGenome copy;
    if (!NeuralGenome_Clone(&copy, srcGenome)) return false;
    snprintf(copy.metadata.origin, sizeof(copy.metadata.origin), "cloned");
    const NeuralGenome *prevDst = CreatureMind_GetNeuralGenome(&dst->mind);
    uint64_t oldDstFp = prevDst ? prevDst->metadata.fingerprint : 0u;
    bool ok = CreatureMind_AssignNeuralGenome(&dst->mind, &copy);
    if (ok) {
        invalidateEntityAnatomyAfterBrainChange(dst);
        dst->founderDistance = computeFounderDistance(w, &copy, &dst->founderMissing);
        findOrCreateLineage(w, copy.metadata.lineageId, 0u,
                            copy.metadata.founderFingerprint, w->species[dst->speciesIndex].id);
        recordIntervention(w, "clone", sourceId, targetId, srcGenome->metadata.fingerprint,
                           srcGenome->metadata.fingerprint, oldDstFp, copy.metadata.fingerprint);
        statistics(w);
    }
    NeuralGenome_Destroy(&copy);
    return ok;
}

bool EcosystemWorld_SwapBrains(EcosystemWorld *w, AIObjectId idA, AIObjectId idB) {
    if (!w || w->updating) return false;
    EcosystemEntity *a = EcosystemWorld_Get(w, idA);
    EcosystemEntity *b = EcosystemWorld_Get(w, idB);
    if (!a || !b || a->physiology.cause || b->physiology.cause) return false;
    const NeuralGenome *gA = CreatureMind_GetNeuralGenome(&a->mind);
    const NeuralGenome *gB = CreatureMind_GetNeuralGenome(&b->mind);
    if (!gA || !gB) return false;
    if (a == b) return true;
    NeuralGenome copyA, copyB;
    if (!NeuralGenome_Clone(&copyA, gA)) return false;
    if (!NeuralGenome_Clone(&copyB, gB)) { NeuralGenome_Destroy(&copyA); return false; }
    uint64_t fpA = copyA.metadata.fingerprint, fpB = copyB.metadata.fingerprint;
    bool okA = CreatureMind_AssignNeuralGenome(&a->mind, &copyB);
    bool okB = okA && CreatureMind_AssignNeuralGenome(&b->mind, &copyA);
    if (!okB) {
        CreatureMind_AssignNeuralGenome(&a->mind, &copyA);
        CreatureMind_AssignNeuralGenome(&b->mind, &copyB);
        NeuralGenome_Destroy(&copyA); NeuralGenome_Destroy(&copyB);
        return false;
    }
    invalidateEntityAnatomyAfterBrainChange(a);
    invalidateEntityAnatomyAfterBrainChange(b);
    a->founderDistance = computeFounderDistance(w, &copyB, &a->founderMissing);
    b->founderDistance = computeFounderDistance(w, &copyA, &b->founderMissing);
    findOrCreateLineage(w, copyB.metadata.lineageId, 0u, copyB.metadata.founderFingerprint,
                        w->species[a->speciesIndex].id);
    findOrCreateLineage(w, copyA.metadata.lineageId, 0u, copyA.metadata.founderFingerprint,
                        w->species[b->speciesIndex].id);
    recordIntervention(w, "swap", idA, idB, fpA, fpB, fpB, fpA);
    NeuralGenome_Destroy(&copyA); NeuralGenome_Destroy(&copyB);
    statistics(w);
    return true;
}

bool EcosystemWorld_ResetBrainRandom(EcosystemWorld *w, AIObjectId id) {
    if (!w || w->updating) return false;
    EcosystemEntity *e = EcosystemWorld_Get(w, id);
    if (!e || e->physiology.cause) return false;
    NeuralGenome fresh;
    uint64_t lid = w->nextLineageId++;
    bool initOk = (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR ||
                   w->defaultArchitecture == NEURAL_ARCH_CANDIDATE_POLICY)
                      ? NeuralGenome_InitRandomV2(&fresh, &w->random, 0.75f, w->neuralMutation.weightLimit, lid)
                      : NeuralGenome_InitRandom(&fresh, &w->random, 0.8f, w->neuralMutation.weightLimit, lid);
    if (!initOk) return false;
    bool ok = EcosystemWorld_AssignBrain(w, id, &fresh, "reset_random");
    NeuralGenome_Destroy(&fresh);
    return ok;
}

bool EcosystemWorld_ExportBrain(const EcosystemWorld *w, AIObjectId id, const char *path,
                                bool overwrite, char *error, size_t errorSize) {
    if (!w) return false;
    const EcosystemEntity *e = EcosystemWorld_GetConst(w, id);
    if (!e || e->physiology.cause) {
        if (error && errorSize) snprintf(error, errorSize, "Individuo %u no existe o no está vivo", id);
        return false;
    }
    const NeuralGenome *g = CreatureMind_GetNeuralGenome(&e->mind);
    if (!g) {
        if (error && errorSize) snprintf(error, errorSize, "Individuo %u no posee cerebro neuronal", id);
        return false;
    }
    return NeuralBrain_SaveFile(g, path, overwrite, error, errorSize);
}

bool EcosystemWorld_ImportBrain(EcosystemWorld *w, AIObjectId id, const char *path,
                                char *error, size_t errorSize) {
    if (!w || w->updating) {
        if (error && errorSize) snprintf(error, errorSize, "No se puede importar durante un tick activo");
        return false;
    }
    EcosystemEntity *e = EcosystemWorld_Get(w, id);
    if (!e || e->physiology.cause) {
        if (error && errorSize) snprintf(error, errorSize, "Individuo %u no existe o no está vivo", id);
        return false;
    }
    NeuralGenome loaded;
    if (!NeuralBrain_LoadFile(path, &loaded, error, errorSize)) return false;
    bool ok = EcosystemWorld_AssignBrain(w, id, &loaded, "import");
    NeuralGenome_Destroy(&loaded);
    if (!ok && error && errorSize) snprintf(error, errorSize, "Fallo al asignar el genoma importado");
    return ok;
}

static bool ensureDirectory(const char *dir) {
    if (!dir || !dir[0]) return false;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", dir);
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) != 0 && errno != EEXIST) return false;
    return true;
}

bool EcosystemWorld_ExportArchiveBrains(const EcosystemWorld *w, const char *directory,
                                        char *error, size_t errorSize) {
    if (!w || !directory || !directory[0]) return false;
    if (!ensureDirectory(directory)) {
        if (error && errorSize) snprintf(error, errorSize, "No se pudo crear directorio '%s'", directory);
        return false;
    }
    EcosystemArchiveEntry bestOff = w->archiveBestOffspring;
    EcosystemArchiveEntry longSurv = w->archiveLongestSurvival;
    EcosystemArchiveEntry highGen = w->archiveHighestGeneration;
    EcosystemArchiveEntry oldLineage = {0};
    double oldestLineageBirth = 1e30;

    for (unsigned k = 0; k < w->count; ++k) {
        const EcosystemEntity *e = &w->entities[w->activeEntitySlots[k]];
        if (!e->id || e->physiology.cause || e->mind.controllerType != CREATURE_CONTROLLER_NEURAL) continue;
        const NeuralGenome *g = CreatureMind_GetNeuralGenome(&e->mind);
        if (!g) continue;
        double obsSurv = observedSurvivalSeconds(w, e);
        if (!bestOff.valid || e->offspringCount > bestOff.offspringCount ||
            (e->offspringCount == bestOff.offspringCount && obsSurv > bestOff.survivalSeconds)) {
            updateArchiveEntry(&bestOff, w, e, true);
        }
        if (!longSurv.valid || obsSurv > longSurv.survivalSeconds) {
            updateArchiveEntry(&longSurv, w, e, true);
        }
        if (!highGen.valid || e->generation > highGen.generation ||
            (e->generation == highGen.generation && e->offspringCount > highGen.offspringCount)) {
            updateArchiveEntry(&highGen, w, e, true);
        }
        double firstSeen = w->clock.seconds;
        for (unsigned li = 0; li < w->lineageCount; ++li) {
            if (w->lineages[li].lineageId == g->metadata.lineageId) {
                firstSeen = w->lineages[li].firstSeenSeconds;
                break;
            }
        }
        if (!oldLineage.valid || firstSeen < oldestLineageBirth ||
            (fabs(firstSeen - oldestLineageBirth) < 1e-6 && e->generation > oldLineage.generation)) {
            oldestLineageBirth = firstSeen;
            updateArchiveEntry(&oldLineage, w, e, true);
        }
    }

    const struct { const char *file; const char *criterion; const EcosystemArchiveEntry *entry; } items[] = {
        {"best_offspring.brain.json", "highest_live_offspring", &bestOff},
        {"longest_survival.brain.json", "longest_observed_survival", &longSurv},
        {"highest_generation.brain.json", "highest_generation_reached", &highGen},
        {"oldest_surviving_lineage.brain.json", "oldest_surviving_lineage", &oldLineage}
    };

    char manifestPath[512];
    snprintf(manifestPath, sizeof(manifestPath), "%s/archive_manifest.json", directory);
    FILE *mf = fopen(manifestPath, "wb");
    if (!mf) {
        if (error && errorSize) snprintf(error, errorSize, "No se pudo crear '%s'", manifestPath);
        return false;
    }
    fprintf(mf, "{\n  \"seed\": %u,\n  \"seconds\": %.6f,\n  \"founder_mode\": \"%s\",\n  \"archives\": [\n",
            w->initialSeed, w->clock.seconds, w->founderModeName);
    bool first = true;
    for (unsigned i = 0; i < 4; ++i) {
        if (!items[i].entry->valid) continue;
        char fullPath[512];
        snprintf(fullPath, sizeof(fullPath), "%s/%s", directory, items[i].file);
        if (!NeuralBrain_SaveFile(&items[i].entry->genome, fullPath, true, error, errorSize)) {
            fclose(mf);
            return false;
        }
        fprintf(mf,
                "%s    {\"file\": \"%s\", \"criterion\": \"%s\", \"entity_id\": %u, \"species_id\": %u, "
                "\"generation\": %u, \"live_offspring\": %u, \"offspring\": %u, "
                "\"observed_survival_seconds\": %.6f, \"survival_seconds\": %.6f, \"biological_age\": %.6f, "
                "\"still_living\": %s, \"lineage_id\": %llu, \"fingerprint\": \"%016llx\"}",
                first ? "" : ",\n", items[i].file, items[i].criterion,
                items[i].entry->entityId, items[i].entry->speciesId, items[i].entry->generation,
                items[i].entry->offspringCount, items[i].entry->offspringCount,
                items[i].entry->survivalSeconds, items[i].entry->survivalSeconds,
                items[i].entry->biologicalAge,
                items[i].entry->stillLiving ? "true" : "false",
                (unsigned long long)items[i].entry->lineageId,
                (unsigned long long)items[i].entry->genome.metadata.fingerprint);
        first = false;
    }
    fprintf(mf, "\n  ]\n}\n");
    fclose(mf);
    return true;
}

bool EcosystemWorld_WriteJSON(const EcosystemWorld *w, FILE *f) {
    if (!w || !f) return false;
    const EcosystemStatistics *s = &w->statistics;
    fprintf(f, "{\n  \"seed\":%u,\"tick\":%llu,\"seconds\":%.9f,\"days\":%.9f,\n", w->initialSeed,
            (unsigned long long)w->clock.tick, w->clock.seconds, SimulationClock_Days(&w->clock));
    fprintf(f, "  \"controller\":\"%s\",\"metabolism_mode\":\"%s\",\"founder_mode\":\"%s\",\"arena_width\":%.3f,\"arena_depth\":%.3f,\"obstacles\":%u,\n",
            w->defaultController == CREATURE_CONTROLLER_NEURAL ? "neural" : "utility",
            w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR ? "molecular" : "legacy",
            w->founderModeName, w->observations.halfSize * 2.0f, w->observations.halfSize * 2.0f, w->obstacleCount);
    fprintf(f, "  \"population\":%u,\"juveniles\":%u,\"conceptions\":%llu,\"births\":%llu,\"offspring_mature\":%llu,\"offspring_reproductive\":%llu,\"deaths\":%llu,\"kills\":%llu,\"attacks\":%llu,\"matings\":%llu,\n",
            w->count, s->juveniles, (unsigned long long)s->conceptions, (unsigned long long)s->births,
            (unsigned long long)s->offspringReachingMaturity, (unsigned long long)s->reproductiveOffspring,
            (unsigned long long)s->deaths, (unsigned long long)s->kills,
            (unsigned long long)s->attacks, (unsigned long long)s->matings);
    fprintf(f, "  \"food_consumed\":%.9f,\"water_consumed\":%.9f,\"plant_biomass\":%.9f,\"average_age\":%.9f,\"average_health\":%.9f,\"average_energy\":%.9f,\n",
            s->foodConsumed, s->waterConsumed, s->plantBiomass, s->averageAge, s->averageHealth, s->averageEnergy);
    fprintf(f, "  \"action_failures\":%llu,\"navigation_failures\":%llu,\"failed_interactions\":%llu,\"failed_births\":%llu,\"escapes\":%llu,\"event_hash\":\"%016llx\",\n  \"death_causes\":[",
            (unsigned long long)s->actionFailures, (unsigned long long)s->navigationFailures,
            (unsigned long long)s->failedInteractions, (unsigned long long)s->failedBirths,
            (unsigned long long)s->escapes, (unsigned long long)s->eventHash);
    for (unsigned i = 0; i < DEATH_CAUSE_COUNT; ++i)
        fprintf(f, "%s%llu", i ? "," : "", (unsigned long long)s->deathCauses[i]);
    fprintf(f, "],\n  \"plant_consumed\":%.9f,\"meat_consumed\":%.9f,\n", s->plantConsumed, s->meatConsumed);
    if (w->metabolismMode == ECOSYSTEM_METABOLISM_MOLECULAR) {
        fprintf(f, "  \"metabolism\":{\"ingested_mass\":%.6f,\"processed_mass\":%.6f,\"absorbed_mass\":%.6f,"
                   "\"rejected_mass\":%.6f,\"waste_eliminated\":%.6f,\"energy_generated\":%.6f,"
                   "\"hydration_generated\":%.6f,\"nutrients_absorbed\":%.6f,\"digestive_energy_cost\":%.6f,"
                   "\"toxin_exposure\":%.6f,\"detoxified\":%.6f,\"metabolic_damage\":%.6f,\"toxicity_deaths\":%llu},\n",
                s->totalIngestedMass, s->totalProcessedMass, s->totalAbsorbedMass,
                s->totalRejectedMass, s->totalWasteEliminated, s->totalMetabolicEnergy,
                s->totalMetabolicHydration, s->totalMetabolicNutrients, s->totalDigestiveEnergyCost,
                s->totalToxinExposure, s->totalDetoxified, s->totalMetabolicDamage,
                (unsigned long long)s->toxicityDeaths);
    }
    fprintf(f, "  \"maximum_generation\":%u,\"average_generation\":%.6f,\"average_survival_seconds\":%.6f,\"max_survival_seconds\":%.6f,\n",
            s->maximumGeneration, s->averageGeneration, s->averageSurvivalDuration, s->maxObservedSurvival);
    fprintf(f, "  \"neural_diversity\":%.9f,\"neural_dispersion_rms\":%.9f,\"average_founder_distance\":%.9f,\"max_founder_distance\":%.9f,\"missing_founder_count\":%u,\"registered_founders\":%u,\"registered_lineages\":%u,\"active_lineages\":%u,\"interventions\":%llu,\n",
            s->neuralDiversity, s->neuralDispersionRms, s->averageFounderDistance, s->maxFounderDistance,
            s->missingFounderCount, w->founderCount, w->lineageCount, s->activeLineages,
            (unsigned long long)w->interventionCount);
    fprintf(f, "  \"intent_frequencies\":{");
    for (unsigned i = 0; i < ACTION_INTENT_COUNT; ++i)
        fprintf(f, "%s\"%s\":%llu", i ? "," : "", NeuralActionIntent_Name((NeuralActionIntent)i),
                (unsigned long long)s->intentSelections[i]);
    fprintf(f, "},\n  \"births_per_generation\":[");
    unsigned maxGenBucket = s->maximumGeneration < ECOSYSTEM_MAX_GENERATION_BUCKETS
                                ? s->maximumGeneration : (ECOSYSTEM_MAX_GENERATION_BUCKETS - 1);
    for (unsigned i = 0; i <= maxGenBucket; ++i)
        fprintf(f, "%s%llu", i ? "," : "", (unsigned long long)s->birthsPerGeneration[i]);
    fprintf(f, "],\n  \"species\":[");
    for (unsigned i = 0; i < w->speciesCount; ++i) {
        unsigned n = 0;
        for (unsigned k = 0; k < w->count; ++k)
            n += (w->entities[w->activeEntitySlots[k]].speciesIndex == i);
        fprintf(f, "%s{\"id\":%u,\"name\":\"%s\",\"recipe_id\":%u,\"population\":%u}",
                i ? "," : "", w->species[i].id, w->species[i].name, w->species[i].recipeId, n);
    }
    fprintf(f, "]\n}\n");
    return !ferror(f);
}

bool EcosystemWorld_WriteCSV(const EcosystemWorld *w, FILE *f, bool header) {
    if (!w || !f) return false;
    if (header) {
        fprintf(f, "tick,seconds,population,births,deaths,kills,plant_biomass,food_consumed,water_consumed,average_age,average_health,average_energy,max_generation,neural_diversity,founder_distance");
        for (unsigned i = 0; i < w->speciesCount; ++i) fprintf(f, ",species_%u", w->species[i].id);
        fprintf(f, "\n");
    }
    const EcosystemStatistics *s = &w->statistics;
    fprintf(f, "%llu,%.6f,%u,%llu,%llu,%llu,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%u,%.6f,%.6f",
            (unsigned long long)w->clock.tick, w->clock.seconds, w->count,
            (unsigned long long)s->births, (unsigned long long)s->deaths, (unsigned long long)s->kills,
            s->plantBiomass, s->foodConsumed, s->waterConsumed, s->averageAge, s->averageHealth,
            s->averageEnergy, s->maximumGeneration, s->neuralDiversity, s->averageFounderDistance);
    for (unsigned i = 0; i < w->speciesCount; ++i) {
        unsigned n = 0;
        for (unsigned k = 0; k < w->count; ++k)
            n += (w->entities[w->activeEntitySlots[k]].speciesIndex == i);
        fprintf(f, ",%u", n);
    }
    fprintf(f, "\n");
    return !ferror(f);
}

void EcosystemWorld_SetEventOutput(EcosystemWorld *w, FILE *f) {
    if (!w) return;
    w->eventOutput = f;
    if (f) fprintf(f, "tick,seconds,event,actor,target\n");
}
