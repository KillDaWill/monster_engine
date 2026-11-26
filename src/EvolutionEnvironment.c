/**
 * @file EvolutionEnvironment.c
 * @brief Carga JSON estricta y construcción determinista de episodios de evaluación.
 */
#include "EvolutionEnvironment.h"
#include "CreatureNavigation.h"
#include "WorldStaticData.h"
#include "WorldPlacement.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void setErr(char *err, size_t errSize, const char *fmt, ...) {
    if (!err || errSize == 0) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(err, errSize, fmt, args);
    va_end(args);
}

static void fnv1aBytes(uint64_t *h, const void *d, size_t n) {
    const unsigned char *p = (const unsigned char *)d;
    for (size_t i = 0; i < n; ++i) { *h ^= (uint64_t)p[i]; *h *= UINT64_C(1099511628211); }
}

typedef struct EJson {
    const char *p, *end;
    char *error;
    size_t errorSize;
} EJson;

static void ejSpace(EJson *r) { while (r->p < r->end && isspace((unsigned char)*r->p)) ++r->p; }
static bool ejTake(EJson *r, char c) { ejSpace(r); if (r->p >= r->end || *r->p != c) return false; ++r->p; return true; }

static bool ejString(EJson *r, char *out, size_t cap) {
    if (!ejTake(r, '"')) { setErr(r->error, r->errorSize, "Se esperaba cadena JSON"); return false; }
    size_t n = 0;
    while (r->p < r->end && *r->p != '"') {
        unsigned char c = (unsigned char)*r->p++;
        if (c == '\\' && r->p < r->end) c = (unsigned char)*r->p++;
        if (n + 1u >= cap) { setErr(r->error, r->errorSize, "Cadena JSON demasiado larga"); return false; }
        out[n++] = (char)c;
    }
    if (r->p >= r->end || *r->p != '"') { setErr(r->error, r->errorSize, "Cadena sin cierre"); return false; }
    ++r->p;
    out[n] = '\0';
    return true;
}

static bool ejNumber(EJson *r, double *out) {
    ejSpace(r);
    const char *start = r->p;
    if (r->p < r->end && *r->p == '-') ++r->p;
    while (r->p < r->end && (isdigit((unsigned char)*r->p) || *r->p == '.' ||
                             *r->p == 'e' || *r->p == 'E' || *r->p == '+' || *r->p == '-')) {
        if ((*r->p == '+' || *r->p == '-') && r->p > start && r->p[-1] != 'e' && r->p[-1] != 'E') break;
        ++r->p;
    }
    size_t len = (size_t)(r->p - start);
    if (len == 0 || len >= 64u) { setErr(r->error, r->errorSize, "Número JSON inválido"); return false; }
    char tmp[64];
    memcpy(tmp, start, len);
    tmp[len] = '\0';
    char *ep = NULL;
    errno = 0;
    double v = strtod(tmp, &ep);
    if (errno == ERANGE || ep != tmp + len || !isfinite(v)) {
        setErr(r->error, r->errorSize, "Número JSON no finito");
        return false;
    }
    *out = v;
    return true;
}

static bool ejNull(EJson *r) {
    ejSpace(r);
    if ((size_t)(r->end - r->p) >= 4u && memcmp(r->p, "null", 4) == 0) { r->p += 4; return true; }
    return false;
}

static bool parseRegenPolicy(const char *s, ResourceRegenPolicy *out) {
    if (strcmp(s, "none") == 0) { *out = RESOURCE_REGEN_NONE; return true; }
    if (strcmp(s, "linear") == 0) { *out = RESOURCE_REGEN_LINEAR; return true; }
    if (strcmp(s, "infinite") == 0) { *out = RESOURCE_REGEN_INFINITE; return true; }
    if (strcmp(s, "deplete_relocate") == 0) { *out = RESOURCE_REGEN_DEPLETE_RELOCATE; return true; }
    return false;
}

static bool parseDistributionMode(const char *s, ResourceDistributionMode *out) {
    if (strcmp(s, "uniform") == 0) { *out = RESOURCE_DIST_UNIFORM; return true; }
    if (strcmp(s, "clustered") == 0) { *out = RESOURCE_DIST_CLUSTERED; return true; }
    if (strcmp(s, "scattered") == 0) { *out = RESOURCE_DIST_SCATTERED; return true; }
    if (strcmp(s, "ring") == 0) { *out = RESOURCE_DIST_RING; return true; }
    return false;
}

static bool parseDecayPolicy(const char *s, ResourceDecayPolicy *out) {
    if (strcmp(s, "none") == 0) { *out = RESOURCE_DECAY_NONE; return true; }
    if (strcmp(s, "mass_loss") == 0) { *out = RESOURCE_DECAY_MASS_LOSS; return true; }
    if (strcmp(s, "decompose") == 0) { *out = RESOURCE_DECAY_DECOMPOSE; return true; }
    return false;
}

static bool parseBgPolicy(const char *s, BackgroundPolicyType *out) {
    if (strcmp(s, "idle") == 0) { *out = BG_POLICY_IDLE; return true; }
    if (strcmp(s, "wander") == 0) { *out = BG_POLICY_WANDER; return true; }
    if (strcmp(s, "utility") == 0) { *out = BG_POLICY_UTILITY; return true; }
    if (strcmp(s, "brain") == 0) { *out = BG_POLICY_BRAIN; return true; }
    return false;
}

void EvolutionEnvironment_InitDefault(EvolutionEnvironmentConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->schemaVersion = EVOLUTION_ENV_SCHEMA_VERSION;
    cfg->arenaSize = 36.0f;
    cfg->episodeSeconds = 300.0;
    cfg->stepSeconds = 1.0 / 60.0;
    cfg->daySeconds = 120.0;
    cfg->focalEnergyMin = 0.55f;
    cfg->focalEnergyMax = 0.65f;
    cfg->focalHydrationMin = 0.60f;
    cfg->focalHydrationMax = 0.72f;
    cfg->focalNutrients = 0.55f;
    cfg->focalAge = 240.0;
    cfg->focalSpawnRadius = 3.0f;
    cfg->obstacleMinRadius = 0.9f;
    cfg->obstacleMaxRadius = 1.4f;
    cfg->obstacleHeight = 2.2f;
}

uint64_t EvolutionEnvironment_Fingerprint(const EvolutionEnvironmentConfig *cfg) {
    if (!cfg) return 0u;
    uint64_t h = UINT64_C(14695981039346656037);
    fnv1aBytes(&h, cfg->id, strlen(cfg->id) + 1u);
    fnv1aBytes(&h, &cfg->arenaSize, sizeof(cfg->arenaSize));
    fnv1aBytes(&h, &cfg->episodeSeconds, sizeof(cfg->episodeSeconds));
    fnv1aBytes(&h, &cfg->focalEnergyMin, sizeof(cfg->focalEnergyMin));
    fnv1aBytes(&h, &cfg->focalEnergyMax, sizeof(cfg->focalEnergyMax));
    fnv1aBytes(&h, &cfg->obstacleCount, sizeof(cfg->obstacleCount));
    fnv1aBytes(&h, &cfg->resourceGroupCount, sizeof(cfg->resourceGroupCount));
    for (unsigned i = 0; i < cfg->resourceGroupCount; ++i) {
        const EvolutionEnvResourceGroup *g = &cfg->resources[i];
        fnv1aBytes(&h, g->substanceId, strlen(g->substanceId) + 1u);
        fnv1aBytes(&h, &g->count, sizeof(g->count));
        fnv1aBytes(&h, &g->initialMass, sizeof(g->initialMass));
        fnv1aBytes(&h, &g->regenPolicy, sizeof(g->regenPolicy));
        fnv1aBytes(&h, &g->respawnDelaySeconds, sizeof(g->respawnDelaySeconds));
        fnv1aBytes(&h, &g->rarityWeight, sizeof(g->rarityWeight));
        fnv1aBytes(&h, &g->distribution, sizeof(g->distribution));
    }
    fnv1aBytes(&h, &cfg->backgroundGroupCount, sizeof(cfg->backgroundGroupCount));
    for (unsigned i = 0; i < cfg->backgroundGroupCount; ++i) {
        const EvolutionEnvBackgroundGroup *b = &cfg->backgrounds[i];
        fnv1aBytes(&h, b->speciesPath, strlen(b->speciesPath) + 1u);
        fnv1aBytes(&h, &b->count, sizeof(b->count));
    }
    if (cfg->hasWorld) {
        uint8_t hw = 1u;
        uint32_t pol = (uint32_t)cfg->worldSeedPolicy;
        uint8_t tap = cfg->terrainAwarePlacement ? 1u : 0u;
        fnv1aBytes(&h, &hw, 1u);
        fnv1aBytes(&h, &pol, sizeof(pol));
        fnv1aBytes(&h, &tap, 1u);
        fnv1aBytes(&h, &cfg->cachedWorldRecipe.generatorVersion, sizeof(cfg->cachedWorldRecipe.generatorVersion));
        fnv1aBytes(&h, &cfg->cachedWorldRecipe.recipeHash, sizeof(cfg->cachedWorldRecipe.recipeHash));
    }
    return h;
}

bool EvolutionEnvironment_DeserializeJSON(const char *json, size_t length,
                                          EvolutionEnvironmentConfig *out,
                                          char *error, size_t errorSize) {
    if (!json || length == 0 || !out) return false;
    EvolutionEnvironmentConfig cfg;
    EvolutionEnvironment_InitDefault(&cfg);
    EJson r = {.p = json, .end = json + length, .error = error, .errorSize = errorSize};
    if (!ejTake(&r, '{')) { setErr(error, errorSize, "Se esperaba '{' en *.environment.json"); return false; }

    bool seenVer = false, seenId = false;
    ejSpace(&r);
    if (r.p < r.end && *r.p != '}') {
        for (;;) {
            char key[48];
            if (!ejString(&r, key, sizeof(key)) || !ejTake(&r, ':')) return false;
            if (strcmp(key, "version") == 0) {
                double v = 0; if (!ejNumber(&r, &v) || v != EVOLUTION_ENV_SCHEMA_VERSION) {
                    setErr(error, errorSize, "Versión de entorno no soportada"); return false;
                }
                cfg.schemaVersion = (uint32_t)v; seenVer = true;
            } else if (strcmp(key, "id") == 0) {
                if (!ejString(&r, cfg.id, sizeof(cfg.id))) return false;
                seenId = true;
            } else if (strcmp(key, "name") == 0) {
                if (!ejString(&r, cfg.name, sizeof(cfg.name))) return false;
            } else if (strcmp(key, "description") == 0) {
                if (!ejString(&r, cfg.description, sizeof(cfg.description))) return false;
            } else if (strcmp(key, "arena_size") == 0) {
                double v = 0; if (!ejNumber(&r, &v) || v < 12.0 || v > 1024.0) return false;
                cfg.arenaSize = (float)v;
            } else if (strcmp(key, "world") == 0) {
                if (!ejNull(&r)) {
                    if (!ejString(&r, cfg.worldPath, sizeof(cfg.worldPath))) return false;
                    cfg.hasWorld = (cfg.worldPath[0] != '\0');
                }
            } else if (strcmp(key, "world_seed_policy") == 0) {
                char pol[32];
                if (!ejString(&r, pol, sizeof(pol))) return false;
                if (strcmp(pol, "fixed") == 0) cfg.worldSeedPolicy = WORLD_SEED_POLICY_FIXED;
                else if (strcmp(pol, "per_episode") == 0) cfg.worldSeedPolicy = WORLD_SEED_POLICY_PER_EPISODE;
                else { setErr(error, errorSize, "world_seed_policy desconocida: '%s'", pol); return false; }
            } else if (strcmp(key, "resource_placement") == 0) {
                char mode[32];
                if (!ejString(&r, mode, sizeof(mode))) return false;
                if (strcmp(mode, "terrain_aware") == 0) cfg.terrainAwarePlacement = true;
                else if (strcmp(mode, "flat") == 0 || strcmp(mode, "legacy") == 0) cfg.terrainAwarePlacement = false;
                else { setErr(error, errorSize, "resource_placement desconocido: '%s'", mode); return false; }
            } else if (strcmp(key, "episode_seconds") == 0) {
                double v = 0; if (!ejNumber(&r, &v) || v < 10.0 || v > 36000.0) return false;
                cfg.episodeSeconds = v;
            } else if (strcmp(key, "focal_initial") == 0) {
                if (!ejTake(&r, '{')) return false;
                ejSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char fk[48]; double v = 0;
                        if (!ejString(&r, fk, sizeof(fk)) || !ejTake(&r, ':') || !ejNumber(&r, &v)) return false;
                        if (strcmp(fk, "energy_min") == 0) cfg.focalEnergyMin = (float)v;
                        else if (strcmp(fk, "energy_max") == 0) cfg.focalEnergyMax = (float)v;
                        else if (strcmp(fk, "hydration_min") == 0) cfg.focalHydrationMin = (float)v;
                        else if (strcmp(fk, "hydration_max") == 0) cfg.focalHydrationMax = (float)v;
                        else if (strcmp(fk, "nutrients") == 0) cfg.focalNutrients = (float)v;
                        else if (strcmp(fk, "age") == 0) cfg.focalAge = v;
                        else if (strcmp(fk, "spawn_radius") == 0) cfg.focalSpawnRadius = (float)v;
                        else { setErr(error, errorSize, "Clave desconocida en focal_initial: '%s'", fk); return false; }
                        if (!ejTake(&r, ',')) break;
                    }
                }
                if (!ejTake(&r, '}')) return false;
            } else if (strcmp(key, "obstacles") == 0) {
                if (!ejTake(&r, '{')) return false;
                ejSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char ok[48]; double v = 0;
                        if (!ejString(&r, ok, sizeof(ok)) || !ejTake(&r, ':') || !ejNumber(&r, &v)) return false;
                        if (strcmp(ok, "count") == 0) cfg.obstacleCount = (unsigned)v;
                        else if (strcmp(ok, "min_radius") == 0) cfg.obstacleMinRadius = (float)v;
                        else if (strcmp(ok, "max_radius") == 0) cfg.obstacleMaxRadius = (float)v;
                        else if (strcmp(ok, "height") == 0) cfg.obstacleHeight = (float)v;
                        else { setErr(error, errorSize, "Clave desconocida en obstacles: '%s'", ok); return false; }
                        if (!ejTake(&r, ',')) break;
                    }
                }
                if (!ejTake(&r, '}')) return false;
            } else if (strcmp(key, "resources") == 0) {
                if (!ejTake(&r, '[')) return false;
                ejSpace(&r);
                if (r.p < r.end && *r.p != ']') {
                    for (;;) {
                        if (cfg.resourceGroupCount >= EVOLUTION_ENV_MAX_RESOURCE_GROUPS) {
                            setErr(error, errorSize, "Demasiados grupos de recursos en entorno");
                            return false;
                        }
                        EvolutionEnvResourceGroup *rg = &cfg.resources[cfg.resourceGroupCount];
                        memset(rg, 0, sizeof(*rg));
                        rg->initialMass = 4.0f; rg->maxMass = 4.0f; rg->radius = 0.8f;
                        rg->regenPolicy = RESOURCE_REGEN_LINEAR; rg->regenRate = 0.02f;
                        rg->respawnDelaySeconds = 20.0f; rg->rarityWeight = 1.0f;
                        rg->minRelocateDist = 7.0f;
                        rg->distribution = RESOURCE_DIST_UNIFORM;
                        rg->clusterCount = 2u; rg->clusterRadius = 3.5f; rg->minSpacing = 4.0f;
                        rg->decayPolicy = RESOURCE_DECAY_NONE; rg->lifetimeSeconds = 180.0;
                        rg->minDistFromCenter = 2.0f; rg->maxDistFromCenter = cfg.arenaSize * 0.42f;
                        if (!ejTake(&r, '{')) return false;
                        ejSpace(&r);
                        if (r.p < r.end && *r.p != '}') {
                            for (;;) {
                                char rk[48]; double v = 0; char sv[64];
                                if (!ejString(&r, rk, sizeof(rk)) || !ejTake(&r, ':')) return false;
                                if (strcmp(rk, "substance") == 0) { if (!ejString(&r, rg->substanceId, sizeof(rg->substanceId))) return false; }
                                else if (strcmp(rk, "count") == 0) { if (!ejNumber(&r, &v)) return false; rg->count = (unsigned)v; }
                                else if (strcmp(rk, "initial_mass") == 0) { if (!ejNumber(&r, &v)) return false; rg->initialMass = (float)v; }
                                else if (strcmp(rk, "max_mass") == 0) { if (!ejNumber(&r, &v)) return false; rg->maxMass = (float)v; }
                                else if (strcmp(rk, "radius") == 0) { if (!ejNumber(&r, &v)) return false; rg->radius = (float)v; }
                                else if (strcmp(rk, "regen_policy") == 0) { if (!ejString(&r, sv, sizeof(sv)) || !parseRegenPolicy(sv, &rg->regenPolicy)) return false; }
                                else if (strcmp(rk, "regen_rate") == 0) { if (!ejNumber(&r, &v)) return false; rg->regenRate = (float)v; }
                                else if (strcmp(rk, "respawn_delay") == 0) { if (!ejNumber(&r, &v) || v < 0.0) return false; rg->respawnDelaySeconds = (float)v; }
                                else if (strcmp(rk, "rarity") == 0) { if (!ejNumber(&r, &v) || v <= 0.0 || v > 1.0) return false; rg->rarityWeight = (float)v; }
                                else if (strcmp(rk, "min_relocate_dist") == 0) { if (!ejNumber(&r, &v) || v < 0.0) return false; rg->minRelocateDist = (float)v; }
                                else if (strcmp(rk, "distribution") == 0) { if (!ejString(&r, sv, sizeof(sv)) || !parseDistributionMode(sv, &rg->distribution)) return false; }
                                else if (strcmp(rk, "cluster_count") == 0) { if (!ejNumber(&r, &v) || v < 1.0) return false; rg->clusterCount = (unsigned)v; }
                                else if (strcmp(rk, "cluster_radius") == 0) { if (!ejNumber(&r, &v) || v <= 0.0) return false; rg->clusterRadius = (float)v; }
                                else if (strcmp(rk, "min_spacing") == 0) { if (!ejNumber(&r, &v) || v < 0.0) return false; rg->minSpacing = (float)v; }
                                else if (strcmp(rk, "decay_policy") == 0) { if (!ejString(&r, sv, sizeof(sv)) || !parseDecayPolicy(sv, &rg->decayPolicy)) return false; }
                                else if (strcmp(rk, "lifetime") == 0) { if (!ejNumber(&r, &v)) return false; rg->lifetimeSeconds = v; }
                                else if (strcmp(rk, "decay_rate") == 0) { if (!ejNumber(&r, &v)) return false; rg->decayRate = (float)v; }
                                else if (strcmp(rk, "min_dist") == 0) { if (!ejNumber(&r, &v)) return false; rg->minDistFromCenter = (float)v; }
                                else if (strcmp(rk, "max_dist") == 0) { if (!ejNumber(&r, &v)) return false; rg->maxDistFromCenter = (float)v; }
                                else { setErr(error, errorSize, "Clave desconocida en resource group: '%s'", rk); return false; }
                                if (!ejTake(&r, ',')) break;
                            }
                        }
                        if (!ejTake(&r, '}')) return false;
                        if (rg->maxMass < rg->initialMass) rg->maxMass = rg->initialMass;
                        ++cfg.resourceGroupCount;
                        if (!ejTake(&r, ',')) break;
                    }
                }
                if (!ejTake(&r, ']')) return false;
            } else if (strcmp(key, "background_creatures") == 0) {
                if (!ejTake(&r, '[')) return false;
                ejSpace(&r);
                if (r.p < r.end && *r.p != ']') {
                    for (;;) {
                        if (cfg.backgroundGroupCount >= EVOLUTION_ENV_MAX_BACKGROUND_GROUPS) return false;
                        EvolutionEnvBackgroundGroup *bg = &cfg.backgrounds[cfg.backgroundGroupCount];
                        memset(bg, 0, sizeof(*bg));
                        bg->policy = BG_POLICY_WANDER;
                        bg->initialEnergy = 0.90f; bg->initialHydration = 0.90f; bg->initialHealth = 1.0f;
                        bg->speedScale = 1.0f;
                        bg->minDistFromFocal = 4.0f; bg->maxDistFromFocal = cfg.arenaSize * 0.40f;
                        if (!ejTake(&r, '{')) return false;
                        ejSpace(&r);
                        if (r.p < r.end && *r.p != '}') {
                            for (;;) {
                                char bk[48]; double v = 0; char sv[64];
                                if (!ejString(&r, bk, sizeof(bk)) || !ejTake(&r, ':')) return false;
                                if (strcmp(bk, "species") == 0) { if (!ejString(&r, bg->speciesPath, sizeof(bg->speciesPath))) return false; }
                                else if (strcmp(bk, "count") == 0) { if (!ejNumber(&r, &v)) return false; bg->count = (unsigned)v; }
                                else if (strcmp(bk, "policy") == 0) { if (!ejString(&r, sv, sizeof(sv)) || !parseBgPolicy(sv, &bg->policy)) return false; }
                                else if (strcmp(bk, "brain") == 0) {
                                    if (!ejNull(&r)) { if (!ejString(&r, bg->brainPath, sizeof(bg->brainPath))) return false; }
                                }
                                else if (strcmp(bk, "initial_energy") == 0) { if (!ejNumber(&r, &v)) return false; bg->initialEnergy = (float)v; }
                                else if (strcmp(bk, "initial_hydration") == 0) { if (!ejNumber(&r, &v)) return false; bg->initialHydration = (float)v; }
                                else if (strcmp(bk, "initial_health") == 0) { if (!ejNumber(&r, &v)) return false; bg->initialHealth = (float)v; }
                                else if (strcmp(bk, "speed_scale") == 0) { if (!ejNumber(&r, &v)) return false; bg->speedScale = (float)v; }
                                else if (strcmp(bk, "min_dist") == 0) { if (!ejNumber(&r, &v)) return false; bg->minDistFromFocal = (float)v; }
                                else if (strcmp(bk, "max_dist") == 0) { if (!ejNumber(&r, &v)) return false; bg->maxDistFromFocal = (float)v; }
                                else { setErr(error, errorSize, "Clave desconocida en background_creatures: '%s'", bk); return false; }
                                if (!ejTake(&r, ',')) break;
                            }
                        }
                        if (!ejTake(&r, '}')) return false;
                        ++cfg.backgroundGroupCount;
                        if (!ejTake(&r, ',')) break;
                    }
                }
                if (!ejTake(&r, ']')) return false;
            } else {
                setErr(error, errorSize, "Clave desconocida en entorno: '%s'", key);
                return false;
            }
            if (!ejTake(&r, ',')) break;
        }
    }
    if (!ejTake(&r, '}')) { setErr(error, errorSize, "Falta '}' en *.environment.json"); return false; }
    if (!seenVer || !seenId) {
        setErr(error, errorSize, "Entorno incompleto: requiere 'version' e 'id'");
        return false;
    }
    if (!cfg.name[0]) snprintf(cfg.name, sizeof(cfg.name), "%s", cfg.id);
    if (cfg.hasWorld) {
        if (!WorldRecipe_Preset(cfg.worldPath, &cfg.cachedWorldRecipe)) {
            if (!WorldRecipe_LoadFile(cfg.worldPath, &cfg.cachedWorldRecipe, error, errorSize)) {
                return false;
            }
        }
        if (cfg.arenaSize <= 36.1f && cfg.cachedWorldRecipe.size > 0.0f) {
            cfg.arenaSize = cfg.cachedWorldRecipe.size;
        }
        cfg.terrainAwarePlacement = !cfg.cachedWorldRecipe.flat;
    }
    cfg.valid = true;
    cfg.fingerprint = EvolutionEnvironment_Fingerprint(&cfg);
    *out = cfg;
    return true;
}

bool EvolutionEnvironment_LoadFile(const char *path, EvolutionEnvironmentConfig *out,
                                   char *error, size_t errorSize) {
    if (!path || !path[0] || !out) return false;
    FILE *f = fopen(path, "rb");
    if (!f) { setErr(error, errorSize, "No se pudo abrir entorno '%s': %s", path, strerror(errno)); return false; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0 || sz > 262144L) { fclose(f); return false; }
    rewind(f);
    char *buf = malloc((size_t)sz + 1u);
    if (!buf) { fclose(f); return false; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    bool ok = EvolutionEnvironment_DeserializeJSON(buf, got, out, error, errorSize);
    free(buf);
    return ok;
}

bool EvolutionEnvironment_ValidateAgainstDatabase(EvolutionEnvironmentConfig *cfg,
                                                  const MetabolismDatabase *db,
                                                  char *error, size_t errorSize) {
    if (!cfg || !cfg->valid || !db || !db->loaded) {
        setErr(error, errorSize, "Entorno o base metabólica inválida");
        return false;
    }
    for (unsigned i = 0; i < cfg->resourceGroupCount; ++i) {
        if (SubstanceRegistry_Find(&db->substances, cfg->resources[i].substanceId) == SUBSTANCE_INVALID_HANDLE) {
            setErr(error, errorSize, "Entorno '%s' referencia sustancia desconocida: '%s'",
                   cfg->id, cfg->resources[i].substanceId);
            return false;
        }
    }
    for (unsigned i = 0; i < cfg->backgroundGroupCount; ++i) {
        EvolutionEnvBackgroundGroup *bg = &cfg->backgrounds[i];
        if (!EvolutionSpeciesConfig_LoadFile(bg->speciesPath, &bg->speciesCfg, error, errorSize))
            return false;
        EcosystemSpecies tmpSp;
        if (!EvolutionSpeciesConfig_BuildEcosystemSpecies(&bg->speciesCfg, db, 10u + i, &tmpSp, error, errorSize))
            return false;
        if (bg->policy == BG_POLICY_BRAIN) {
            if (!bg->brainPath[0] || !NeuralBrain_LoadFile(bg->brainPath, &bg->loadedBrain, error, errorSize)) {
                setErr(error, errorSize, "Entorno '%s' requiere cerebro válido para background '%s'",
                       cfg->id, bg->speciesPath);
                return false;
            }
            bg->hasLoadedBrain = true;
        }
    }
    return true;
}

static float randRange(uint32_t *rng, float lo, float hi) {
    if (hi <= lo) return lo;
    return lo + CreatureGenome_Random(rng) * (hi - lo);
}

uint64_t EvolutionEnvironment_FixedWorldDataFingerprint(const EvolutionEnvironmentConfig *env) {
    if (!env || !env->hasWorld || env->worldSeedPolicy != WORLD_SEED_POLICY_FIXED ||
        env->cachedWorldRecipe.generatorVersion < WORLD_GENERATOR_VERSION_MORPHOLOGY) return 0;
    const WorldStaticData *world = WorldStaticCache_Acquire(&env->cachedWorldRecipe, NULL, 0);
    uint64_t result = world ? world->dataFingerprint : 0;
    WorldStaticData_Release(world);
    return result;
}

bool EvolutionEnvironment_BuildEpisodeWorld(EcosystemWorld *w,
                                            const EvolutionEnvironmentConfig *env,
                                            const EvolutionSpeciesConfig *focalSpecies,
                                            const MetabolismDatabase *db,
                                            const NeuralGenome *focalGenome,
                                            uint32_t episodeSeed,
                                            bool useAnatomicalOral,
                                            AIObjectId *outFocalId,
                                            char *error, size_t errorSize) {
    if (!w || !env || !env->valid || !focalSpecies || !focalSpecies->valid || !db || !db->loaded) {
        setErr(error, errorSize, "Argumentos inválidos al construir episodio");
        return false;
    }

    /* Resolver y compartir el mundo estático procedural si el entorno lo define. */
    const WorldStaticData *staticWorld = NULL;
    float effectiveArenaSize = env->arenaSize;
    if (env->hasWorld && env->cachedWorldRecipe.valid) {
        WorldRecipe epRecipe = env->cachedWorldRecipe;
        if (env->worldSeedPolicy == WORLD_SEED_POLICY_PER_EPISODE) {
            epRecipe.seed = WorldSeed_Derive(episodeSeed ? episodeSeed : 1u, WORLD_SEED_TERRAIN, 1u);
        }
        epRecipe.recipeHash = WorldRecipe_Fingerprint(&epRecipe);
        staticWorld = WorldStaticCache_Acquire(&epRecipe, error, errorSize);
        if (!staticWorld) {
            return false;
        }
        effectiveArenaSize = epRecipe.size;
    }
    bool procedural = (staticWorld != NULL && !staticWorld->isFlat);

    unsigned totalBg = 0u;
    for (unsigned i = 0; i < env->backgroundGroupCount; ++i) totalBg += env->backgrounds[i].count;
    unsigned totalRes = 0u;
    for (unsigned i = 0; i < env->resourceGroupCount; ++i) totalRes += env->resources[i].count;

    unsigned extraHydroRes = procedural
                                 ? (staticWorld->hydrology.bodyCount + staticWorld->hydrology.shoreSiteCount + 32u)
                                 : 0u;
    unsigned popCap = 8u + totalBg * 2u;
    unsigned resCap = 16u + totalRes + popCap + extraHydroRes;
    unsigned spCap = 2u + env->backgroundGroupCount;
    float halfSize = effectiveArenaSize * 0.5f;

    if (!EcosystemWorld_InitArena(w, popCap, resCap, spCap, halfSize,
                                  episodeSeed ? episodeSeed : 1u, env->stepSeconds, env->daySeconds)) {
        WorldStaticData_Release(staticWorld);
        setErr(error, errorSize, "No se pudo inicializar mundo de episodio");
        return false;
    }
    if (staticWorld) {
        EcosystemWorld_AttachStaticWorld(w, staticWorld);
        WorldStaticData_Release(staticWorld);
    }
    if (!EcosystemWorld_EnableMolecularMetabolism(w, db)) {
        EcosystemWorld_Free(w);
        setErr(error, errorSize, "No se pudo habilitar metabolismo molecular en episodio");
        return false;
    }
    if (procedural) {
        EcosystemWorld_RegisterNaturalWaterBodies(w);
    }
    w->defaultController = CREATURE_CONTROLLER_NEURAL;
    w->disablePhysicalMutation = true;

    EcosystemSpecies focalSp;
    if (!EvolutionSpeciesConfig_BuildEcosystemSpecies(focalSpecies, db, 1u, &focalSp, error, errorSize)) {
        EcosystemWorld_Free(w);
        return false;
    }
    if (!useAnatomicalOral) focalSp.anatomicalOral = false;
    if (!EcosystemWorld_AddSpecies(w, focalSp)) {
        EcosystemWorld_Free(w);
        setErr(error, errorSize, "No se pudo registrar especie focal");
        return false;
    }

    /* Registrar especies de fondo (IDs 2, 3, ...). */
    for (unsigned i = 0; i < env->backgroundGroupCount; ++i) {
        EcosystemSpecies bgSp;
        if (!EvolutionSpeciesConfig_BuildEcosystemSpecies(&env->backgrounds[i].speciesCfg, db,
                                                          2u + i, &bgSp, error, errorSize)) {
            EcosystemWorld_Free(w);
            return false;
        }
        if (!useAnatomicalOral) bgSp.anatomicalOral = false;
        if (env->backgrounds[i].speedScale > 0.0f && env->backgrounds[i].speedScale != 1.0f) {
            bgSp.speed *= env->backgrounds[i].speedScale;
        }
        /* Configurar política fija de criaturas de fondo. */
        if (env->backgrounds[i].policy == BG_POLICY_IDLE) {
            for (int a = 0; a < ACTION_COUNT; ++a) bgSp.brain.weights[a] = 0.0f;
            bgSp.brain.weights[ACTION_IDLE] = 1.0f;
            bgSp.brain.needs[NEED_HUNGER].enabled = false;
            bgSp.brain.needs[NEED_HYDRATION].enabled = false;
            bgSp.brain.needs[NEED_REST].enabled = false;
        } else if (env->backgrounds[i].policy == BG_POLICY_WANDER) {
            for (int a = 0; a < ACTION_COUNT; ++a) bgSp.brain.weights[a] = 0.0f;
            bgSp.brain.weights[ACTION_WANDER] = 1.0f;
            bgSp.brain.needs[NEED_HUNGER].enabled = false;
            bgSp.brain.needs[NEED_HYDRATION].enabled = false;
            bgSp.brain.needs[NEED_REST].enabled = false;
            /* Presa errante conserva energía propia para no morir por inanición natural durante el episodio. */
            bgSp.physiology.basalCost *= 0.25f;
            bgSp.physiology.hydrationCost *= 0.25f;
        } else if (env->backgrounds[i].policy == BG_POLICY_UTILITY) {
            bgSp.brain.dietMask = ECO_PLANT;
            bgSp.brain.threatMask = 1u; /* Puede huir de la especie focal si está cerca */
        }
        if (!EcosystemWorld_AddSpecies(w, bgSp)) {
            EcosystemWorld_Free(w);
            setErr(error, errorSize, "No se pudo registrar especie de fondo");
            return false;
        }
    }

    uint32_t rng = episodeSeed ? episodeSeed : 1u;
    float bound = fmaxf(2.0f, halfSize - 2.0f);

    /* 1) Colocar recursos líquidos primero (para que obstáculos y spawns respeten orillas). */
    for (int pass = 0; pass < 2; ++pass) {
        for (unsigned gi = 0; gi < env->resourceGroupCount; ++gi) {
            const EvolutionEnvResourceGroup *rg = &env->resources[gi];
            const SubstanceDef *sub = SubstanceRegistry_Get(
                &db->substances, SubstanceRegistry_Find(&db->substances, rg->substanceId));
            if (!sub) continue;
            bool isLiquid = (sub->phase == SUBSTANCE_PHASE_LIQUID);
            if ((pass == 0 && !isLiquid) || (pass == 1 && isLiquid)) continue;
            /* En mundos procedurales con hidrología natural activa, el agua limpia natural
             * proviene de los lagos y ríos (evitar discos artificiales superpuestos). */
            if (procedural && isLiquid && strcmp(rg->substanceId, "clean_water") == 0 &&
                w->staticWorld->hydrology.shoreSiteCount > 0u) {
                continue;
            }

            float maxD = fminf(rg->maxDistFromCenter, bound - rg->radius - 0.5f);
            float minD = fminf(rg->minDistFromCenter, maxD);

            /* Preparar centros de parches si la distribución es por clústeres. */
            Vector3 clusters[8];
            unsigned numClusters = rg->clusterCount > 8u ? 8u : (rg->clusterCount ? rg->clusterCount : 1u);
            if (rg->distribution == RESOURCE_DIST_CLUSTERED) {
                for (unsigned k = 0; k < numClusters; ++k) {
                    float cAng = (6.2831853f * ((float)k + randRange(&rng, 0.12f, 0.88f))) / (float)numClusters;
                    float cDist = randRange(&rng, fmaxf(minD, 3.0f), fmaxf(minD, maxD - rg->clusterRadius * 0.5f));
                    clusters[k] = Vec3_Create(sinf(cAng) * cDist, 0.0f, cosf(cAng) * cDist);
                }
            }

            Vector3 placedPos[64];
            unsigned placedCount = 0u;

            for (unsigned ci = 0; ci < rg->count; ++ci) {
                bool placed = false;
                for (unsigned attempt = 0; attempt < 96u && !placed; ++attempt) {
                    Vector3 p;
                    if (rg->distribution == RESOURCE_DIST_CLUSTERED) {
                        Vector3 center = clusters[ci % numClusters];
                        float offAng = randRange(&rng, 0.0f, 6.2831853f);
                        float offR = sqrtf(randRange(&rng, 0.05f, 1.0f)) * rg->clusterRadius;
                        p = Vec3_Add(center, Vec3_Create(sinf(offAng) * offR, 0.0f, cosf(offAng) * offR));
                        float lim = bound - rg->radius - 0.5f;
                        p.x = fmaxf(-lim, fminf(lim, p.x));
                        p.z = fmaxf(-lim, fminf(lim, p.z));
                    } else if (rg->distribution == RESOURCE_DIST_SCATTERED) {
                        float angle = randRange(&rng, 0.0f, 6.2831853f);
                        float u = randRange(&rng, 0.0f, 1.0f);
                        float dist = sqrtf(minD * minD + u * (maxD * maxD - minD * minD));
                        p = Vec3_Create(sinf(angle) * dist, 0.0f, cosf(angle) * dist);
                        float reqSpace = (attempt < 64u) ? rg->minSpacing : (rg->minSpacing * 0.5f);
                        bool tooClose = false;
                        for (unsigned pi = 0; pi < placedCount; ++pi) {
                            if (Vec3_Distance(p, placedPos[pi]) < reqSpace) { tooClose = true; break; }
                        }
                        if (tooClose) continue;
                    } else if (rg->distribution == RESOURCE_DIST_RING) {
                        float angle = (6.2831853f * ((float)ci + randRange(&rng, 0.15f, 0.85f))) /
                                      (float)(rg->count ? rg->count : 1u);
                        float dist = randRange(&rng, fmaxf(minD, maxD * 0.72f), maxD);
                        p = Vec3_Create(sinf(angle) * dist, 0.0f, cosf(angle) * dist);
                    } else {
                        float angle = (6.2831853f * ((float)ci + randRange(&rng, 0.1f, 0.9f))) /
                                      (float)(rg->count ? rg->count : 1u);
                        float dist = randRange(&rng, minD, maxD);
                        p = Vec3_Create(sinf(angle) * dist, 0.0f, cosf(angle) * dist);
                    }

                    float clearR = isLiquid ? (rg->radius + 1.4f) : (rg->radius + 0.4f);
                    if (procedural) {
                        WorldHabitatFilter hf = WorldHabitatFilter_Vegetation(clearR);
                        hf.minFertility = (attempt < 48u) ? 0.12f : 0.0f;
                        hf.minMoisture = 0.0f;
                        Vector3 nrm;
                        if (!WorldPlacement_ValidatePoint(w->staticWorld, p.x, p.z, &hf, &p, &nrm)) continue;
                    } else {
                        if (!AIWorld_Walkable(&w->observations, p, clearR, false)) continue;
                    }

                    EcosystemResource res = EcosystemResource_FromSubstance(
                        sub, rg->initialMass, rg->maxMass, rg->regenPolicy, rg->regenRate,
                        rg->decayPolicy, rg->lifetimeSeconds, rg->decayRate, w->decayTargetMolecule);
                    res.respawnDelaySeconds = rg->respawnDelaySeconds;
                    res.rarityWeight = rg->rarityWeight;
                    res.minRelocateDistance = rg->minRelocateDist;
                    res.spawnMinDist = minD;
                    res.spawnMaxDist = maxD;
                    placed = (EcosystemWorld_AddMolecularResource(w, res, p, rg->radius) != 0u);
                    if (placed && placedCount < 64u) placedPos[placedCount++] = p;
                }
            }
        }
    }

    /* 2) Colocar obstáculos adicionales (en plano; en procedural ya existen rocas geológicas). */
    if (!procedural) {
    for (unsigned i = 0; i < env->obstacleCount; ++i) {
        for (unsigned attempt = 0; attempt < 64u; ++attempt) {
            float rad = randRange(&rng, env->obstacleMinRadius, env->obstacleMaxRadius);
            float dist = randRange(&rng, env->focalSpawnRadius + 2.5f, fmaxf(env->focalSpawnRadius + 3.0f, bound - rad - 1.5f));
            float ang = randRange(&rng, 0.0f, 6.2831853f);
            Vector3 p = Vec3_Create(sinf(ang) * dist, 0.0f, cosf(ang) * dist);
            if (!AIWorld_Walkable(&w->observations, p, rad + 1.5f, false)) continue;
            if (EcosystemWorld_AddObstacle(w, p, rad, env->obstacleHeight)) break;
        }
    }
    }

    /* 3) Spawnear individuo focal cerca del centro o de una orilla accesible en terreno procedural. */
    AIObjectId focalId = 0u;
    Vector3 focalPos = Vec3_Zero();
    Vector3 spawnAnchor = Vec3_Zero();
    if (procedural && w->staticWorld->hydrology.shoreSiteCount > 0u) {
        const WorldShoreSite *anchorShore = WorldHydrology_FindNearestShore(&w->staticWorld->hydrology,
                                                                            Vec3_Zero(), halfSize, 0u);
        if (anchorShore) spawnAnchor = anchorShore->bankApproachPoint;
    }
    for (unsigned attempt = 0; attempt < 256u && !focalId; ++attempt) {
        float ang = randRange(&rng, 0.0f, 6.2831853f);
        float spread = procedural ? fmaxf(env->focalSpawnRadius, 6.0f + (float)(attempt / 32u) * 6.0f)
                                  : env->focalSpawnRadius;
        float dist = (attempt == 0u) ? 0.0f : randRange(&rng, 0.0f, spread);
        Vector3 p = Vec3_Create(spawnAnchor.x + sinf(ang) * dist, 0.0f, spawnAnchor.z + cosf(ang) * dist);
        float energy = randRange(&rng, env->focalEnergyMin, env->focalEnergyMax);
        focalId = procedural ? EcosystemWorld_SpawnOnGround(w, 1u, p.x, p.z, env->focalAge, energy, NULL)
                             : EcosystemWorld_Spawn(w, 1u, p, env->focalAge, energy, NULL);
        if (focalId) {
            const EcosystemEntity *fe = EcosystemWorld_GetConst(w, focalId);
            focalPos = fe ? fe->body.position : p;
        }
    }
    if (!focalId) {
        EcosystemWorld_Free(w);
        setErr(error, errorSize, "No se pudo colocar individuo focal en entorno '%s'", env->id);
        return false;
    }

    EcosystemEntity *focalEnt = EcosystemWorld_Get(w, focalId);
    if (focalEnt) {
        focalEnt->physiology.hydration = randRange(&rng, env->focalHydrationMin, env->focalHydrationMax);
        focalEnt->metabolism.nutrientReserve = env->focalNutrients;
        focalEnt->body.yaw = randRange(&rng, 0.0f, 6.2831853f);
    }
    if (focalGenome) {
        if (!EcosystemWorld_AssignBrain(w, focalId, focalGenome, NULL)) {
            EcosystemWorld_Free(w);
            setErr(error, errorSize, "No se pudo asignar genoma focal (arquitectura incompatible)");
            return false;
        }
    }

    /* 4) Spawnear criaturas de fondo con su política fija explícita. */
    for (unsigned bi = 0; bi < env->backgroundGroupCount; ++bi) {
        const EvolutionEnvBackgroundGroup *bg = &env->backgrounds[bi];
        for (unsigned ci = 0; ci < bg->count; ++ci) {
            AIObjectId bgId = 0u;
            for (unsigned attempt = 0; attempt < 128u && !bgId; ++attempt) {
                float ang = (6.2831853f * ((float)ci + randRange(&rng, 0.1f, 0.9f))) /
                            (float)(bg->count ? bg->count : 1u);
                float maxD = fminf(bg->maxDistFromFocal, bound - 1.5f);
                float minD = fminf(bg->minDistFromFocal, maxD);
                float dist = randRange(&rng, minD, maxD);
                Vector3 p = Vec3_Add(focalPos, Vec3_Create(sinf(ang) * dist, 0.0f, cosf(ang) * dist));
                if (fabsf(p.x) > bound - 1.0f || fabsf(p.z) > bound - 1.0f) continue;
                bgId = procedural ? EcosystemWorld_SpawnOnGround(w, 2u + bi, p.x, p.z, 240.0, bg->initialEnergy, NULL)
                                  : EcosystemWorld_Spawn(w, 2u + bi, p, 240.0, bg->initialEnergy, NULL);
            }
            if (bgId) {
                EcosystemEntity *be = EcosystemWorld_Get(w, bgId);
                if (be) {
                    be->physiology.hydration = bg->initialHydration;
                    be->physiology.health = bg->initialHealth;
                    if (bg->policy == BG_POLICY_BRAIN && bg->hasLoadedBrain) {
                        EcosystemWorld_AssignBrain(w, bgId, &bg->loadedBrain, NULL);
                    }
                }
            }
        }
    }

    CreatureNavigation_EnsureCache(&w->observations);
    if (outFocalId) *outFocalId = focalId;
    return true;
}
