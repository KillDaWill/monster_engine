/**
 * @file EvolutionSpeciesRegistry.c
 * @brief Carga JSON estricta y compilación de configuraciones genéricas de especies.
 */
#include "EvolutionSpeciesRegistry.h"
#include "CreatureRecipes.h"
#include "CreatureRecipeRegistry.h"
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

static void fnv1aBytes(uint64_t *hash, const void *data, size_t len) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < len; ++i) {
        *hash ^= (uint64_t)p[i];
        *hash *= UINT64_C(1099511628211);
    }
}

typedef struct SJson {
    const char *p;
    const char *end;
    char *error;
    size_t errorSize;
} SJson;

static void sjSpace(SJson *r) { while (r->p < r->end && isspace((unsigned char)*r->p)) ++r->p; }
static bool sjTake(SJson *r, char c) { sjSpace(r); if (r->p >= r->end || *r->p != c) return false; ++r->p; return true; }

static bool sjString(SJson *r, char *out, size_t cap) {
    if (!sjTake(r, '"')) { setErr(r->error, r->errorSize, "Se esperaba cadena JSON"); return false; }
    size_t n = 0;
    while (r->p < r->end && *r->p != '"') {
        unsigned char c = (unsigned char)*r->p++;
        if (c == '\\' && r->p < r->end) {
            unsigned char e = (unsigned char)*r->p++;
            if (e == 'n') c = '\n';
            else if (e == 't') c = '\t';
            else c = e;
        }
        if (n + 1u >= cap) { setErr(r->error, r->errorSize, "Cadena JSON demasiado larga"); return false; }
        out[n++] = (char)c;
    }
    if (r->p >= r->end || *r->p != '"') { setErr(r->error, r->errorSize, "Cadena sin cierre"); return false; }
    ++r->p;
    out[n] = '\0';
    return true;
}

static bool sjNumber(SJson *r, double *out) {
    sjSpace(r);
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
        setErr(r->error, r->errorSize, "Número no finito o fuera de rango");
        return false;
    }
    *out = v;
    return true;
}

static bool sjBool(SJson *r, bool *out) {
    sjSpace(r);
    if ((size_t)(r->end - r->p) >= 4u && memcmp(r->p, "true", 4) == 0) { r->p += 4; *out = true; return true; }
    if ((size_t)(r->end - r->p) >= 5u && memcmp(r->p, "false", 5) == 0) { r->p += 5; *out = false; return true; }
    setErr(r->error, r->errorSize, "Se esperaba booleano JSON");
    return false;
}

static bool sjNull(SJson *r) {
    sjSpace(r);
    if ((size_t)(r->end - r->p) >= 4u && memcmp(r->p, "null", 4) == 0) { r->p += 4; return true; }
    return false;
}

void EvolutionSpeciesConfig_InitDefault(EvolutionSpeciesConfig *cfg) {
    if (!cfg) return;
    memset(cfg, 0, sizeof(*cfg));
    cfg->schemaVersion = EVOLUTION_SPECIES_SCHEMA_VERSION;
    cfg->speed = 2.3f;
    cfg->radius = 0.85f;
    cfg->oralReach = 1.5f;
    cfg->attackRange = 0.0f;
    cfg->damage = 0.0f;
    cfg->attackCooldown = 1.0f;
    cfg->attackCost = 0.008f;
    cfg->turnRate = 1.6f;
    cfg->canAttackPhysically = false;
    cfg->waterInteraction = true;
    cfg->anatomicalOral = true;
    cfg->tissueMass = 4.0f;
    cfg->physiology = CreaturePhysiology_Default();
    cfg->physiology.oldAge = 1300.0;
    cfg->physiology.maximumAge = 1800.0;
    cfg->reproduction = CreatureReproduction_Default();
    cfg->perception = (PerceptionProfile){
        .range = 14.0f, .horizontalFOV = 5.5f, .verticalFOV = 3.14f,
        .interval = 0.2f, .memoryDuration = 30.0f
    };
}

uint64_t EvolutionSpeciesConfig_Fingerprint(const EvolutionSpeciesConfig *cfg) {
    if (!cfg) return 0u;
    uint64_t h = UINT64_C(14695981039346656037);
    fnv1aBytes(&h, cfg->id, strlen(cfg->id) + 1u);
    fnv1aBytes(&h, cfg->metabolismProfileId, strlen(cfg->metabolismProfileId) + 1u);
    fnv1aBytes(&h, cfg->tissueSubstanceId, strlen(cfg->tissueSubstanceId) + 1u);
    fnv1aBytes(&h, &cfg->recipeId, sizeof(cfg->recipeId));
    fnv1aBytes(&h, &cfg->speed, sizeof(cfg->speed));
    fnv1aBytes(&h, &cfg->radius, sizeof(cfg->radius));
    fnv1aBytes(&h, &cfg->oralReach, sizeof(cfg->oralReach));
    fnv1aBytes(&h, &cfg->attackRange, sizeof(cfg->attackRange));
    fnv1aBytes(&h, &cfg->damage, sizeof(cfg->damage));
    fnv1aBytes(&h, &cfg->canAttackPhysically, sizeof(cfg->canAttackPhysically));
    fnv1aBytes(&h, &cfg->tissueMass, sizeof(cfg->tissueMass));
    return h;
}

bool EvolutionSpeciesConfig_DeserializeJSON(const char *json, size_t length,
                                            EvolutionSpeciesConfig *out,
                                            char *error, size_t errorSize) {
    if (!json || length == 0 || !out) {
        setErr(error, errorSize, "JSON de especie vacío o destino nulo");
        return false;
    }
    EvolutionSpeciesConfig cfg;
    EvolutionSpeciesConfig_InitDefault(&cfg);
    SJson r = {.p = json, .end = json + length, .error = error, .errorSize = errorSize};
    if (!sjTake(&r, '{')) { setErr(error, errorSize, "Se esperaba '{' en *.species.json"); return false; }

    bool seenVer = false, seenId = false, seenMet = false, seenTissue = false;
    sjSpace(&r);
    if (r.p < r.end && *r.p != '}') {
        for (;;) {
            char key[48];
            if (!sjString(&r, key, sizeof(key)) || !sjTake(&r, ':')) return false;
            if (strcmp(key, "version") == 0) {
                double v = 0; if (!sjNumber(&r, &v) || v != EVOLUTION_SPECIES_SCHEMA_VERSION) {
                    setErr(error, errorSize, "Versión de *.species.json no soportada"); return false;
                }
                cfg.schemaVersion = (uint32_t)v; seenVer = true;
            } else if (strcmp(key, "id") == 0) {
                if (!sjString(&r, cfg.id, sizeof(cfg.id))) return false;
                seenId = true;
            } else if (strcmp(key, "name") == 0) {
                if (!sjString(&r, cfg.name, sizeof(cfg.name))) return false;
            } else if (strcmp(key, "recipe") == 0) {
                if (!sjString(&r, cfg.recipePath, sizeof(cfg.recipePath))) return false;
            } else if (strcmp(key, "recipe_id") == 0) {
                double v = 0; if (!sjNumber(&r, &v) || v < 0) return false;
                cfg.recipeId = (uint32_t)v;
            } else if (strcmp(key, "metabolism") == 0) {
                if (!sjString(&r, cfg.metabolismProfileId, sizeof(cfg.metabolismProfileId))) return false;
                seenMet = true;
            } else if (strcmp(key, "tissue_substance") == 0) {
                if (!sjString(&r, cfg.tissueSubstanceId, sizeof(cfg.tissueSubstanceId))) return false;
                seenTissue = true;
            } else if (strcmp(key, "tissue_mass") == 0) {
                double v = 0; if (!sjNumber(&r, &v) || v <= 0.0) return false;
                cfg.tissueMass = (float)v;
            } else if (strcmp(key, "starting_brain") == 0) {
                if (sjNull(&r)) {
                    cfg.hasStartingBrain = false;
                    cfg.startingBrainPath[0] = '\0';
                } else {
                    if (!sjString(&r, cfg.startingBrainPath, sizeof(cfg.startingBrainPath))) return false;
                    cfg.hasStartingBrain = cfg.startingBrainPath[0] != '\0';
                }
            } else if (strcmp(key, "physical") == 0) {
                if (!sjTake(&r, '{')) return false;
                sjSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char pk[48]; double v = 0; bool bv = false;
                        if (!sjString(&r, pk, sizeof(pk)) || !sjTake(&r, ':')) return false;
                        if (strcmp(pk, "speed") == 0) { if (!sjNumber(&r, &v)) return false; cfg.speed = (float)v; }
                        else if (strcmp(pk, "radius") == 0) { if (!sjNumber(&r, &v)) return false; cfg.radius = (float)v; }
                        else if (strcmp(pk, "oral_reach") == 0) { if (!sjNumber(&r, &v)) return false; cfg.oralReach = (float)v; }
                        else if (strcmp(pk, "attack_range") == 0) { if (!sjNumber(&r, &v)) return false; cfg.attackRange = (float)v; }
                        else if (strcmp(pk, "damage") == 0) { if (!sjNumber(&r, &v)) return false; cfg.damage = (float)v; }
                        else if (strcmp(pk, "attack_cooldown") == 0) { if (!sjNumber(&r, &v)) return false; cfg.attackCooldown = (float)v; }
                        else if (strcmp(pk, "attack_cost") == 0) { if (!sjNumber(&r, &v)) return false; cfg.attackCost = (float)v; }
                        else if (strcmp(pk, "turn_rate") == 0) { if (!sjNumber(&r, &v)) return false; cfg.turnRate = (float)v; }
                        else if (strcmp(pk, "can_attack") == 0) { if (!sjBool(&r, &bv)) return false; cfg.canAttackPhysically = bv; }
                        else if (strcmp(pk, "water_interaction") == 0) { if (!sjBool(&r, &bv)) return false; cfg.waterInteraction = bv; }
                        else if (strcmp(pk, "anatomical_oral") == 0) { if (!sjBool(&r, &bv)) return false; cfg.anatomicalOral = bv; }
                        else { setErr(error, errorSize, "Clave desconocida en physical: '%s'", pk); return false; }
                        if (!sjTake(&r, ',')) break;
                    }
                }
                if (!sjTake(&r, '}')) return false;
            } else if (strcmp(key, "physiology") == 0) {
                if (!sjTake(&r, '{')) return false;
                sjSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char pk[48]; double v = 0;
                        if (!sjString(&r, pk, sizeof(pk)) || !sjTake(&r, ':')) return false;
                        if (!sjNumber(&r, &v)) return false;
                        if (strcmp(pk, "basal_cost") == 0) cfg.physiology.basalCost = (float)v;
                        else if (strcmp(pk, "hydration_cost") == 0) cfg.physiology.hydrationCost = (float)v;
                        else if (strcmp(pk, "movement_cost") == 0) cfg.physiology.movementCost = (float)v;
                        else if (strcmp(pk, "starvation_damage") == 0) cfg.physiology.starvationDamage = (float)v;
                        else if (strcmp(pk, "dehydration_damage") == 0) cfg.physiology.dehydrationDamage = (float)v;
                        else if (strcmp(pk, "recovery_rate") == 0) cfg.physiology.recoveryRate = (float)v;
                        else if (strcmp(pk, "fatigue_rate") == 0) cfg.physiology.fatigueRate = (float)v;
                        else if (strcmp(pk, "rest_rate") == 0) cfg.physiology.restRate = (float)v;
                        else if (strcmp(pk, "maturity_age") == 0) cfg.physiology.maturityAge = v;
                        else if (strcmp(pk, "old_age") == 0) cfg.physiology.oldAge = v;
                        else if (strcmp(pk, "maximum_age") == 0) cfg.physiology.maximumAge = v;
                        else { setErr(error, errorSize, "Clave desconocida en physiology: '%s'", pk); return false; }
                        if (!sjTake(&r, ',')) break;
                    }
                }
                if (!sjTake(&r, '}')) return false;
            } else if (strcmp(key, "reproduction") == 0) {
                if (!sjTake(&r, '{')) return false;
                sjSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char pk[48]; double v = 0;
                        if (!sjString(&r, pk, sizeof(pk)) || !sjTake(&r, ':')) return false;
                        if (!sjNumber(&r, &v)) return false;
                        if (strcmp(pk, "cooldown") == 0) cfg.reproduction.cooldown = v;
                        else if (strcmp(pk, "gestation") == 0) cfg.reproduction.gestation = v;
                        else if (strcmp(pk, "maximum_age") == 0) cfg.reproduction.maximumAge = v;
                        else if (strcmp(pk, "offspring") == 0) cfg.reproduction.offspring = (unsigned)v;
                        else if (strcmp(pk, "minimum_energy") == 0) cfg.reproduction.minimumEnergy = (float)v;
                        else if (strcmp(pk, "minimum_health") == 0) cfg.reproduction.minimumHealth = (float)v;
                        else if (strcmp(pk, "energy_cost") == 0) cfg.reproduction.energyCost = (float)v;
                        else { setErr(error, errorSize, "Clave desconocida en reproduction: '%s'", pk); return false; }
                        if (!sjTake(&r, ',')) break;
                    }
                }
                if (!sjTake(&r, '}')) return false;
            } else if (strcmp(key, "perception") == 0) {
                if (!sjTake(&r, '{')) return false;
                sjSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char pk[48]; double v = 0;
                        if (!sjString(&r, pk, sizeof(pk)) || !sjTake(&r, ':')) return false;
                        if (!sjNumber(&r, &v)) return false;
                        if (strcmp(pk, "range") == 0) cfg.perception.range = (float)v;
                        else if (strcmp(pk, "horizontal_fov") == 0) cfg.perception.horizontalFOV = (float)v;
                        else if (strcmp(pk, "vertical_fov") == 0) cfg.perception.verticalFOV = (float)v;
                        else if (strcmp(pk, "interval") == 0) cfg.perception.interval = (float)v;
                        else if (strcmp(pk, "memory_duration") == 0) cfg.perception.memoryDuration = (float)v;
                        else { setErr(error, errorSize, "Clave desconocida en perception: '%s'", pk); return false; }
                        if (!sjTake(&r, ',')) break;
                    }
                }
                if (!sjTake(&r, '}')) return false;
            } else {
                setErr(error, errorSize, "Clave desconocida en *.species.json: '%s'", key);
                return false;
            }
            if (!sjTake(&r, ',')) break;
        }
    }
    if (!sjTake(&r, '}')) { setErr(error, errorSize, "Falta '}' en *.species.json"); return false; }
    if (!seenVer || !seenId || !seenMet || !seenTissue) {
        setErr(error, errorSize, "Especie incompleta: requiere 'version', 'id', 'metabolism' y 'tissue_substance'");
        return false;
    }
    if (!MoleculeRegistry_ValidateId(cfg.id)) {
        setErr(error, errorSize, "Identificador de especie inválido: '%s'", cfg.id);
        return false;
    }
    if (!cfg.name[0]) snprintf(cfg.name, sizeof(cfg.name), "%s", cfg.id);
    if (!CreaturePhysiology_ValidateProfile(&cfg.physiology) ||
        !CreatureReproduction_ValidateProfile(&cfg.reproduction)) {
        setErr(error, errorSize, "Parámetros fisiológicos o reproductivos inválidos en especie '%s'", cfg.id);
        return false;
    }
    cfg.valid = true;
    cfg.fingerprint = EvolutionSpeciesConfig_Fingerprint(&cfg);
    *out = cfg;
    return true;
}

bool EvolutionSpeciesConfig_LoadFile(const char *path, EvolutionSpeciesConfig *out,
                                     char *error, size_t errorSize) {
    if (!path || !path[0] || !out) return false;
    FILE *f = fopen(path, "rb");
    if (!f) {
        setErr(error, errorSize, "No se pudo abrir archivo de especie '%s': %s", path, strerror(errno));
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0 || sz > 262144L) {
        fclose(f);
        setErr(error, errorSize, "Archivo de especie inválido o demasiado grande: '%s'", path);
        return false;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1u);
    if (!buf) { fclose(f); return false; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    bool ok = EvolutionSpeciesConfig_DeserializeJSON(buf, got, out, error, errorSize);
    free(buf);
    return ok;
}

bool EvolutionSpeciesConfig_BuildEcosystemSpecies(const EvolutionSpeciesConfig *cfg,
                                                  const MetabolismDatabase *db,
                                                  unsigned numericSpeciesId,
                                                  EcosystemSpecies *outSpecies,
                                                  char *error, size_t errorSize) {
    if (!cfg || !cfg->valid || !db || !db->loaded || !outSpecies || numericSpeciesId == 0u) {
        setErr(error, errorSize, "Argumentos inválidos al compilar especie");
        return false;
    }
    MetabolicProfileHandle mp = MetabolicProfileRegistry_Find(&db->profiles, cfg->metabolismProfileId);
    if (mp == METABOLIC_PROFILE_INVALID_HANDLE) {
        setErr(error, errorSize, "Especie '%s' referencia perfil metabólico desconocido: '%s'",
               cfg->id, cfg->metabolismProfileId);
        return false;
    }
    SubstanceHandle ts = SubstanceRegistry_Find(&db->substances, cfg->tissueSubstanceId);
    if (ts == SUBSTANCE_INVALID_HANDLE) {
        setErr(error, errorSize, "Especie '%s' referencia sustancia tisular desconocida: '%s'",
               cfg->id, cfg->tissueSubstanceId);
        return false;
    }
    uint32_t recipeId = cfg->recipeId;
    if (recipeId == 0u) recipeId = 3u;
    if (cfg->anatomicalOral) {
        const CreatureRecipe *rec = CreatureRecipes_Find(recipeId);
        if (!rec && cfg->recipePath[0]) {
            CreatureRecipeRegistry_LoadFile(cfg->recipePath, NULL, 0);
            rec = CreatureRecipes_Find(recipeId);
        }
        if (!rec) {
            setErr(error, errorSize, "Especie '%s' no pudo resolver receta anatómica %u ('%s')",
                   cfg->id, recipeId, cfg->recipePath);
            return false;
        }
    }

    EcosystemSpecies sp = EcosystemSpecies_Default(numericSpeciesId, numericSpeciesId, 0u, 0u, 0u);
    snprintf(sp.name, sizeof(sp.name), "%.31s", cfg->name);
    sp.recipeId = recipeId;
    sp.anatomicalOral = cfg->anatomicalOral;
    sp.speed = cfg->speed;
    sp.radius = cfg->radius;
    sp.oralReach = cfg->oralReach;
    sp.attackRange = cfg->attackRange;
    sp.damage = cfg->damage;
    sp.attackCooldown = cfg->attackCooldown;
    sp.attackCost = cfg->attackCost;
    sp.turnRate = cfg->turnRate;
    sp.edibleBiomass = cfg->tissueMass;
    sp.tissueMass = cfg->tissueMass;
    sp.nutrition = 0.25f;
    sp.physiology = cfg->physiology;
    sp.physiology.dietMask = 0u;
    sp.physiology.preyMask = 0u;
    sp.physiology.canAttackPhysically = cfg->canAttackPhysically;
    sp.physiology.waterInteraction = cfg->waterInteraction;
    sp.reproduction = cfg->reproduction;
    sp.brain.perception = cfg->perception;
    sp.metabolicProfile = mp;
    sp.tissueSubstance = ts;

    *outSpecies = sp;
    return true;
}
