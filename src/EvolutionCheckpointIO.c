/**
 * @file EvolutionCheckpointIO.c
 * @brief Serialización atómica y recuperación verificada de checkpoints del entrenador.
 */
#include "EvolutionCheckpointIO.h"
#include "NeuralBrainIO.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void setErr(char *err, size_t errSize, const char *fmt, ...) {
    if (!err || errSize == 0) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(err, errSize, fmt, args);
    va_end(args);
}

static void ensureParentDir(const char *path) {
    if (!path) return;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) return;
    *slash = '\0';
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

static uint64_t activeProfileFingerprint(const BrainEvolutionLab *lab) {
    MetabolicProfileHandle ph = MetabolicProfileRegistry_Find(&lab->db.profiles,
                                                              lab->species.metabolismProfileId);
    const MetabolicProfile *mp = MetabolicProfileRegistry_Get(&lab->db.profiles, ph);
    return mp ? mp->fingerprint : 0u;
}

static uint64_t activeWorldRecipeHash(const BrainEvolutionLab *lab) {
    if (lab->stageCount == 0u) return 0u;
    unsigned idx = lab->currentStage < lab->stageCount ? lab->currentStage : 0u;
    const EvolutionEnvironmentConfig *env = &lab->stages[idx].env;
    return env->hasWorld ? env->cachedWorldRecipe.recipeHash : 0u;
}

static uint32_t activeWorldGeneratorVersion(const BrainEvolutionLab *lab) {
    if (lab->stageCount == 0u) return 0u;
    unsigned idx = lab->currentStage < lab->stageCount ? lab->currentStage : 0u;
    const EvolutionEnvironmentConfig *env = &lab->stages[idx].env;
    return env->hasWorld ? env->cachedWorldRecipe.generatorVersion : 0u;
}

static uint64_t activeEnvironmentFingerprint(const BrainEvolutionLab *lab) {
    if (lab->stageCount == 0u) return 0u;
    unsigned idx = lab->currentStage < lab->stageCount ? lab->currentStage : 0u;
    return lab->stages[idx].env.fingerprint;
}

static bool writeGenomeInline(FILE *f, const NeuralGenome *g) {
    size_t needed = 0;
    if (!NeuralBrain_SerializeJSON(g, NULL, 0, &needed) || needed == 0) return false;
    char *buf = malloc(needed + 1u);
    if (!buf) return false;
    bool ok = NeuralBrain_SerializeJSON(g, buf, needed + 1u, &needed) &&
              fwrite(buf, 1, needed, f) == needed;
    free(buf);
    return ok;
}

bool EvolutionCheckpoint_Save(const BrainEvolutionLab *lab, const char *path,
                              char *error, size_t errorSize) {
    if (!lab || !lab->initialized || !path || !path[0]) {
        setErr(error, errorSize, "Laboratorio no inicializado o ruta de checkpoint inválida");
        return false;
    }
    ensureParentDir(path);
    char tmpPath[600];
    snprintf(tmpPath, sizeof(tmpPath), "%s.tmp", path);
    FILE *f = fopen(tmpPath, "wb");
    if (!f) {
        setErr(error, errorSize, "No se pudo crear archivo temporal '%s': %s", tmpPath, strerror(errno));
        return false;
    }

    uint64_t fpMol = lab->db.molecules.fingerprint;
    uint64_t fpSub = lab->db.substances.fingerprint;
    uint64_t fpProf = activeProfileFingerprint(lab);
    uint64_t fpSp = lab->species.fingerprint;
    uint64_t fpEnv = activeEnvironmentFingerprint(lab);
    uint64_t fpWorld = activeWorldRecipeHash(lab);
    uint32_t worldGenVer = activeWorldGeneratorVersion(lab);

    fprintf(f, "{\n  \"version\": %u,\n", EVOLUTION_CHECKPOINT_VERSION);
    fprintf(f, "  \"fingerprints\": {\n");
    fprintf(f, "    \"molecules\": \"%016llx\",\n", (unsigned long long)fpMol);
    fprintf(f, "    \"substances\": \"%016llx\",\n", (unsigned long long)fpSub);
    fprintf(f, "    \"metabolic_profile\": \"%016llx\",\n", (unsigned long long)fpProf);
    fprintf(f, "    \"species\": \"%016llx\",\n", (unsigned long long)fpSp);
    fprintf(f, "    \"environment\": \"%016llx\",\n", (unsigned long long)fpEnv);
    fprintf(f, "    \"world_recipe\": \"%016llx\",\n", (unsigned long long)fpWorld);
    fprintf(f, "    \"world_generator_version\": %u,\n", worldGenVer);
    fprintf(f, "    \"architecture\": \"%s\"\n  },\n", NeuralArchitecture_Name(lab->config.architecture));

    fprintf(f, "  \"state\": {\n");
    fprintf(f, "    \"current_generation\": %u,\n", lab->currentGeneration);
    fprintf(f, "    \"current_stage\": %u,\n", lab->currentStage);
    fprintf(f, "    \"generations_in_stage\": %u,\n", lab->generationsInCurrentStage);
    fprintf(f, "    \"rng\": %u,\n", lab->rng);
    fprintf(f, "    \"last_improvement_gen\": %u,\n", lab->lastImprovementGen);
    fprintf(f, "    \"total_episodes\": %llu,\n", (unsigned long long)lab->totalEpisodesEvaluated);
    fprintf(f, "    \"total_ticks\": %llu,\n", (unsigned long long)lab->totalTicksSimulated);
    fprintf(f, "    \"elapsed_seconds\": %.6f,\n", lab->elapsedTrainingSeconds);
    fprintf(f, "    \"population_size\": %u,\n", lab->populationSize);
    fprintf(f, "    \"has_best_training\": %s,\n", lab->hasBestTraining ? "true" : "false");
    fprintf(f, "    \"best_training_fitness\": %.6f,\n", (double)lab->bestTrainingMetrics.meanFitness);
    fprintf(f, "    \"best_training_survival\": %.6f,\n", (double)lab->bestTrainingMetrics.survivalRate);
    fprintf(f, "    \"best_training_gen\": %u,\n", lab->bestTrainingGeneration);
    fprintf(f, "    \"has_best_validation\": %s,\n", lab->hasBestValidation ? "true" : "false");
    fprintf(f, "    \"best_validation_fitness\": %.6f,\n", (double)lab->bestValidationMetrics.meanFitness);
    fprintf(f, "    \"best_validation_survival\": %.6f,\n", (double)lab->bestValidationMetrics.survivalRate);
    fprintf(f, "    \"best_validation_gen\": %u\n  },\n", lab->bestValidationGeneration);

    fprintf(f, "  \"best_training_genome\": ");
    if (lab->hasBestTraining) {
        if (!writeGenomeInline(f, &lab->bestTrainingGenome)) { fclose(f); remove(tmpPath); return false; }
    } else {
        fprintf(f, "null");
    }
    fprintf(f, ",\n  \"best_validation_genome\": ");
    if (lab->hasBestValidation) {
        if (!writeGenomeInline(f, &lab->bestValidationGenome)) { fclose(f); remove(tmpPath); return false; }
    } else {
        fprintf(f, "null");
    }

    fprintf(f, ",\n  \"population\": [\n");
    for (unsigned i = 0; i < lab->populationSize; ++i) {
        if (i > 0u) fprintf(f, ",\n");
        if (!writeGenomeInline(f, &lab->population[i])) {
            fclose(f);
            remove(tmpPath);
            setErr(error, errorSize, "Error al serializar genoma %u del checkpoint", i);
            return false;
        }
    }
    fprintf(f, "\n  ],\n  \"history\": [\n");
    for (unsigned i = 0; i < lab->historyCount; ++i) {
        const BrainLabGenerationRecord *r = &lab->history[i];
        fprintf(f,
                "%s    {\"gen\":%u,\"stage\":%u,\"best\":%.6f,\"mean\":%.6f,\"median\":%.6f,"
                "\"surv\":%.6f,\"val_fit\":%.6f,\"val_surv\":%.6f,\"has_val\":%s,\"div\":%.6f,"
                "\"kills\":%llu,\"carcasses\":%llu}",
                i ? ",\n" : "", r->trainerGeneration, r->curriculumStage,
                (double)r->bestTrainFitness, (double)r->meanTrainFitness, (double)r->medianTrainFitness,
                (double)r->bestTrainSurvival, (double)r->validationFitness, (double)r->validationSurvival,
                r->hasValidation ? "true" : "false", (double)r->neuralDiversity,
                (unsigned long long)r->totalKills, (unsigned long long)r->totalCarcassesConsumed);
    }
    fprintf(f, "\n  ]\n}\n");

    bool ok = !ferror(f) && (fflush(f) == 0);
    if (fclose(f) != 0) ok = false;
    if (!ok) {
        remove(tmpPath);
        setErr(error, errorSize, "Fallo de escritura al guardar checkpoint '%s'", path);
        return false;
    }
    if (rename(tmpPath, path) != 0) {
        remove(tmpPath);
        setErr(error, errorSize, "Fallo al reemplazar atómicamente '%s': %s", path, strerror(errno));
        return false;
    }
    return true;
}

/* Helpers ligeros para extraer sub-objetos JSON balanceados de un checkpoint. */
static const char *skipWs(const char *p, const char *end) {
    while (p < end && isspace((unsigned char)*p)) ++p;
    return p;
}

static bool extractBalancedObject(const char **pp, const char *end, const char **outStart, size_t *outLen) {
    const char *p = skipWs(*pp, end);
    if (p >= end || *p != '{') return false;
    const char *start = p;
    int depth = 0;
    bool inStr = false;
    while (p < end) {
        char c = *p++;
        if (inStr) {
            if (c == '\\' && p < end) ++p;
            else if (c == '"') inStr = false;
        } else {
            if (c == '"') inStr = true;
            else if (c == '{') ++depth;
            else if (c == '}') {
                --depth;
                if (depth == 0) {
                    *outStart = start;
                    *outLen = (size_t)(p - start);
                    *pp = p;
                    return true;
                }
            }
        }
    }
    return false;
}

static bool findJsonField(const char *obj, const char *objEnd, const char *key, const char **valOut) {
    char pat[96];
    snprintf(pat, sizeof(pat), "\"%s\"", key);
    size_t patLen = strlen(pat);
    const char *p = obj;
    int depth = 0;
    bool inStr = false;
    while (p < objEnd) {
        char c = *p;
        if (inStr) {
            if (c == '\\' && p + 1 < objEnd) p += 2;
            else { if (c == '"') inStr = false; ++p; }
            continue;
        }
        if (c == '{' || c == '[') { ++depth; ++p; continue; }
        if (c == '}' || c == ']') { --depth; ++p; continue; }
        if (depth == 1 && c == '"' && (size_t)(objEnd - p) >= patLen && memcmp(p, pat, patLen) == 0) {
            const char *q = skipWs(p + patLen, objEnd);
            if (q < objEnd && *q == ':') {
                *valOut = skipWs(q + 1, objEnd);
                return true;
            }
        }
        if (c == '"') inStr = true;
        ++p;
    }
    return false;
}

static bool parseHexField(const char *obj, const char *objEnd, const char *key, uint64_t *out) {
    const char *v = NULL;
    if (!findJsonField(obj, objEnd, key, &v) || v >= objEnd || *v != '"') return false;
    ++v;
    char hex[24] = {0};
    size_t n = 0;
    while (v < objEnd && *v != '"' && n + 1u < sizeof(hex)) hex[n++] = *v++;
    if (n != 16u) return false;
    *out = (uint64_t)strtoull(hex, NULL, 16);
    return true;
}

static bool parseStringField(const char *obj, const char *objEnd, const char *key, char *out, size_t cap) {
    const char *v = NULL;
    if (!findJsonField(obj, objEnd, key, &v) || v >= objEnd || *v != '"') return false;
    ++v;
    size_t n = 0;
    while (v < objEnd && *v != '"' && n + 1u < cap) out[n++] = *v++;
    out[n] = '\0';
    return v < objEnd && *v == '"';
}

static bool parseDoubleField(const char *obj, const char *objEnd, const char *key, double *out) {
    const char *v = NULL;
    if (!findJsonField(obj, objEnd, key, &v)) return false;
    char *ep = NULL;
    double d = strtod(v, &ep);
    if (ep == v || !isfinite(d)) return false;
    *out = d;
    return true;
}

static bool parseBoolField(const char *obj, const char *objEnd, const char *key, bool *out) {
    const char *v = NULL;
    if (!findJsonField(obj, objEnd, key, &v)) return false;
    if ((size_t)(objEnd - v) >= 4u && memcmp(v, "true", 4) == 0) { *out = true; return true; }
    if ((size_t)(objEnd - v) >= 5u && memcmp(v, "false", 5) == 0) { *out = false; return true; }
    return false;
}

bool EvolutionCheckpoint_Load(BrainEvolutionLab *lab, const char *path, bool allowBranch,
                              char *error, size_t errorSize) {
    if (!lab || !lab->initialized || !path || !path[0]) {
        setErr(error, errorSize, "Laboratorio no inicializado o ruta de checkpoint inválida");
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        setErr(error, errorSize, "No se pudo abrir checkpoint '%s': %s", path, strerror(errno));
        return false;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0 || sz > 64L * 1024L * 1024L) {
        fclose(f);
        setErr(error, errorSize, "Tamaño de checkpoint inválido: %ld bytes", sz);
        return false;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1u);
    if (!buf) { fclose(f); return false; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';
    const char *end = buf + got;

    double ver = 0.0;
    if (!parseDoubleField(buf, end, "version", &ver) || (uint32_t)ver != EVOLUTION_CHECKPOINT_VERSION) {
        free(buf);
        setErr(error, errorSize, "Versión de checkpoint no soportada");
        return false;
    }

    /* 1) Validar huellas de definiciones (rechaza reanudación incompatible salvo branch explícito). */
    const char *fpVal = NULL;
    if (!findJsonField(buf, end, "fingerprints", &fpVal)) {
        free(buf);
        setErr(error, errorSize, "Checkpoint sin bloque 'fingerprints'");
        return false;
    }
    const char *fpObj = NULL;
    size_t fpLen = 0;
    if (!extractBalancedObject(&fpVal, end, &fpObj, &fpLen)) {
        free(buf);
        setErr(error, errorSize, "Bloque 'fingerprints' malformado");
        return false;
    }
    const char *fpEnd = fpObj + fpLen;
    uint64_t ckMol = 0, ckSub = 0, ckProf = 0, ckSp = 0, ckEnv = 0;
    char ckArch[48] = {0};
    if (!parseHexField(fpObj, fpEnd, "molecules", &ckMol) ||
        !parseHexField(fpObj, fpEnd, "substances", &ckSub) ||
        !parseHexField(fpObj, fpEnd, "metabolic_profile", &ckProf) ||
        !parseHexField(fpObj, fpEnd, "species", &ckSp) ||
        !parseHexField(fpObj, fpEnd, "environment", &ckEnv) ||
        !parseStringField(fpObj, fpEnd, "architecture", ckArch, sizeof(ckArch))) {
        free(buf);
        setErr(error, errorSize, "Huellas incompletas en checkpoint");
        return false;
    }

    NeuralArchitectureType ckArchType = NEURAL_ARCH_NONE;
    if (!NeuralArchitecture_FromName(ckArch, &ckArchType) || ckArchType != lab->config.architecture) {
        free(buf);
        setErr(error, errorSize, "Arquitectura del checkpoint ('%s') incompatible con la configuración actual", ckArch);
        return false;
    }
    uint64_t ckWorld = 0;
    double ckWorldGen = 0.0;
    bool hasCkWorld = parseHexField(fpObj, fpEnd, "world_recipe", &ckWorld);
    bool hasCkWorldGen = parseDoubleField(fpObj, fpEnd, "world_generator_version", &ckWorldGen);

    if (!allowBranch) {
        if (ckMol != lab->db.molecules.fingerprint ||
            ckSub != lab->db.substances.fingerprint ||
            ckProf != activeProfileFingerprint(lab) ||
            ckSp != lab->species.fingerprint ||
            ckEnv != activeEnvironmentFingerprint(lab) ||
            (hasCkWorld && ckWorld != activeWorldRecipeHash(lab)) ||
            (hasCkWorldGen && (uint32_t)ckWorldGen != activeWorldGeneratorVersion(lab))) {
            free(buf);
            setErr(error, errorSize,
                   "Checkpoint incompatible: cambiaron moléculas, sustancias, perfil metabólico, especie, entorno o mundo");
            return false;
        }
    }

    /* 2) Parsear estado escalar. */
    const char *stVal = NULL;
    const char *stObj = NULL;
    size_t stLen = 0;
    if (!findJsonField(buf, end, "state", &stVal) || !extractBalancedObject(&stVal, end, &stObj, &stLen)) {
        free(buf);
        setErr(error, errorSize, "Bloque 'state' ausente o inválido en checkpoint");
        return false;
    }
    const char *stEnd = stObj + stLen;
    double curGen = 0, curStage = 0, genInStage = 0, rngVal = 1, lastImp = 0, totEp = 0, totTk = 0, elSec = 0, popSz = 0;
    double btFit = 0, btSurv = 0, btGen = 0, bvFit = 0, bvSurv = 0, bvGen = 0;
    bool hasBT = false, hasBV = false;
    if (!parseDoubleField(stObj, stEnd, "current_generation", &curGen) ||
        !parseDoubleField(stObj, stEnd, "current_stage", &curStage) ||
        !parseDoubleField(stObj, stEnd, "rng", &rngVal) ||
        !parseDoubleField(stObj, stEnd, "population_size", &popSz)) {
        free(buf);
        setErr(error, errorSize, "Estado de checkpoint incompleto");
        return false;
    }
    parseDoubleField(stObj, stEnd, "generations_in_stage", &genInStage);
    parseDoubleField(stObj, stEnd, "last_improvement_gen", &lastImp);
    parseDoubleField(stObj, stEnd, "total_episodes", &totEp);
    parseDoubleField(stObj, stEnd, "total_ticks", &totTk);
    parseDoubleField(stObj, stEnd, "elapsed_seconds", &elSec);
    parseBoolField(stObj, stEnd, "has_best_training", &hasBT);
    parseDoubleField(stObj, stEnd, "best_training_fitness", &btFit);
    parseDoubleField(stObj, stEnd, "best_training_survival", &btSurv);
    parseDoubleField(stObj, stEnd, "best_training_gen", &btGen);
    parseBoolField(stObj, stEnd, "has_best_validation", &hasBV);
    parseDoubleField(stObj, stEnd, "best_validation_fitness", &bvFit);
    parseDoubleField(stObj, stEnd, "best_validation_survival", &bvSurv);
    parseDoubleField(stObj, stEnd, "best_validation_gen", &bvGen);

    unsigned ckPopSize = (unsigned)popSz;
    if (ckPopSize == 0u || ckPopSize != lab->populationSize) {
        free(buf);
        setErr(error, errorSize, "Tamaño de población del checkpoint (%u) no coincide con %u",
               ckPopSize, lab->populationSize);
        return false;
    }

    /* 3) Deserializar población en buffer temporal antes de mutar lab (carga atómica). */
    NeuralGenome *tmpPop = calloc(ckPopSize, sizeof(*tmpPop));
    if (!tmpPop) { free(buf); return false; }
    const char *popVal = NULL;
    if (!findJsonField(buf, end, "population", &popVal) || *popVal != '[') {
        free(tmpPop); free(buf);
        setErr(error, errorSize, "Arreglo 'population' ausente en checkpoint");
        return false;
    }
    const char *cursor = popVal + 1;
    for (unsigned i = 0; i < ckPopSize; ++i) {
        const char *gStart = NULL;
        size_t gLen = 0;
        if (!extractBalancedObject(&cursor, end, &gStart, &gLen) ||
            !NeuralBrain_DeserializeJSON(gStart, gLen, &tmpPop[i], error, errorSize)) {
            free(tmpPop); free(buf);
            return false;
        }
        cursor = skipWs(cursor, end);
        if (cursor < end && *cursor == ',') ++cursor;
    }

    NeuralGenome tmpBT = {0}, tmpBV = {0};
    if (hasBT) {
        const char *v = NULL, *gs = NULL; size_t gl = 0;
        if (!findJsonField(buf, end, "best_training_genome", &v) ||
            !extractBalancedObject(&v, end, &gs, &gl) ||
            !NeuralBrain_DeserializeJSON(gs, gl, &tmpBT, error, errorSize)) {
            free(tmpPop); free(buf);
            return false;
        }
    }
    if (hasBV) {
        const char *v = NULL, *gs = NULL; size_t gl = 0;
        if (!findJsonField(buf, end, "best_validation_genome", &v) ||
            !extractBalancedObject(&v, end, &gs, &gl) ||
            !NeuralBrain_DeserializeJSON(gs, gl, &tmpBV, error, errorSize)) {
            free(tmpPop); free(buf);
            return false;
        }
    }

    /* Commit atómico en lab. */
    memcpy(lab->population, tmpPop, ckPopSize * sizeof(*tmpPop));
    free(tmpPop);
    lab->currentGeneration = (uint32_t)curGen;
    lab->currentStage = ((unsigned)curStage < lab->stageCount) ? (unsigned)curStage : 0u;
    lab->generationsInCurrentStage = (unsigned)genInStage;
    lab->rng = (uint32_t)rngVal ? (uint32_t)rngVal : 1u;
    lab->lastImprovementGen = (uint32_t)lastImp;
    lab->totalEpisodesEvaluated = (uint64_t)totEp;
    lab->totalTicksSimulated = (uint64_t)totTk;
    lab->elapsedTrainingSeconds = elSec;
    lab->hasBestTraining = hasBT;
    if (hasBT) {
        lab->bestTrainingGenome = tmpBT;
        lab->bestTrainingMetrics.meanFitness = (float)btFit;
        lab->bestTrainingMetrics.survivalRate = (float)btSurv;
        lab->bestTrainingGeneration = (uint32_t)btGen;
    }
    lab->hasBestValidation = hasBV;
    if (hasBV) {
        lab->bestValidationGenome = tmpBV;
        lab->bestValidationMetrics.meanFitness = (float)bvFit;
        lab->bestValidationMetrics.survivalRate = (float)bvSurv;
        lab->bestValidationGeneration = (uint32_t)bvGen;
    }
    /* Restaurar historial de generaciones si está presente en el checkpoint. */
    lab->historyCount = 0u;
    const char *histVal = NULL;
    if (lab->history && lab->historyCapacity > 0u && findJsonField(buf, end, "history", &histVal) && *histVal == '[') {
        const char *hc = skipWs(histVal + 1, end);
        while (hc < end && *hc != ']' && lab->historyCount < lab->historyCapacity) {
            const char *hs = NULL;
            size_t hl = 0;
            if (!extractBalancedObject(&hc, end, &hs, &hl)) break;
            const char *he = hs + hl;
            BrainLabGenerationRecord rec = {0};
            double vGen = 0, vSt = 0, vBest = 0, vMean = 0, vMed = 0, vSurv = 0, vVal = 0, vValS = 0, vDiv = 0, vK = 0, vC = 0;
            bool vHasVal = false;
            parseDoubleField(hs, he, "gen", &vGen);
            parseDoubleField(hs, he, "stage", &vSt);
            parseDoubleField(hs, he, "best", &vBest);
            parseDoubleField(hs, he, "mean", &vMean);
            parseDoubleField(hs, he, "median", &vMed);
            parseDoubleField(hs, he, "surv", &vSurv);
            parseDoubleField(hs, he, "val_fit", &vVal);
            parseDoubleField(hs, he, "val_surv", &vValS);
            parseBoolField(hs, he, "has_val", &vHasVal);
            parseDoubleField(hs, he, "div", &vDiv);
            parseDoubleField(hs, he, "kills", &vK);
            parseDoubleField(hs, he, "carcasses", &vC);
            rec.trainerGeneration = (uint32_t)vGen;
            rec.curriculumStage = (unsigned)vSt;
            rec.bestTrainFitness = (float)vBest;
            rec.meanTrainFitness = (float)vMean;
            rec.medianTrainFitness = (float)vMed;
            rec.bestTrainSurvival = (float)vSurv;
            rec.validationFitness = (float)vVal;
            rec.validationSurvival = (float)vValS;
            rec.hasValidation = vHasVal;
            rec.neuralDiversity = (float)vDiv;
            rec.totalKills = (uint64_t)vK;
            rec.totalCarcassesConsumed = (uint64_t)vC;
            lab->history[lab->historyCount++] = rec;
            hc = skipWs(hc, end);
            if (hc < end && *hc == ',') ++hc;
        }
    }

    lab->status = LAB_STATUS_RUNNING;
    free(buf);
    return true;
}
