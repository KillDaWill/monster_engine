/**
 * @file NeuralBrainIO.c
 * @brief Parser y escritor JSON estricto por campos para archivos *.brain.json.
 */
#include "NeuralBrainIO.h"
#include <ctype.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void setError(char *error, size_t errorSize, const char *fmt, ...) {
    if (!error || errorSize == 0) return;
    va_list args;
    va_start(args, fmt);
    vsnprintf(error, errorSize, fmt, args);
    va_end(args);
}

/* ========================================================================== */
/* Escritura JSON                                                             */
/* ========================================================================== */

typedef struct JsonWriter {
    char *buf;
    size_t cap;
    size_t len;
    bool overflow;
} JsonWriter;

static void jwAppendf(JsonWriter *w, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int needed = 0;
    if (w->buf && w->len < w->cap) {
        needed = vsnprintf(w->buf + w->len, w->cap - w->len, fmt, args);
    } else {
        needed = vsnprintf(NULL, 0, fmt, args);
    }
    va_end(args);
    if (needed < 0) {
        w->overflow = true;
        return;
    }
    if (w->buf && w->len + (size_t)needed >= w->cap) {
        w->overflow = true;
    }
    w->len += (size_t)needed;
}

static void jwEscapedString(JsonWriter *w, const char *s) {
    jwAppendf(w, "\"");
    for (const unsigned char *p = (const unsigned char *)(s ? s : ""); *p; ++p) {
        unsigned char c = *p;
        if (c == '"' || c == '\\') {
            jwAppendf(w, "\\%c", (char)c);
        } else if (c == '\n') {
            jwAppendf(w, "\\n");
        } else if (c == '\r') {
            jwAppendf(w, "\\r");
        } else if (c == '\t') {
            jwAppendf(w, "\\t");
        } else if (c < 32u) {
            jwAppendf(w, "\\u%04x", (unsigned)c);
        } else {
            jwAppendf(w, "%c", (char)c);
        }
    }
    jwAppendf(w, "\"");
}

static void jwFloatArray(JsonWriter *w, const float *arr, size_t count, unsigned perLine) {
    jwAppendf(w, "[\n      ");
    for (size_t i = 0; i < count; ++i) {
        float v = arr[i] == 0.0f ? 0.0f : arr[i];
        jwAppendf(w, "%.9g", (double)v);
        if (i + 1u < count) {
            if (perLine && ((i + 1u) % perLine) == 0u) {
                jwAppendf(w, ",\n      ");
            } else {
                jwAppendf(w, ", ");
            }
        }
    }
    jwAppendf(w, "\n    ]");
}

bool NeuralBrain_SerializeJSON(const NeuralGenome *genome, char *out, size_t outSize,
                               size_t *writtenOut) {
    if (!NeuralGenome_Validate(genome)) return false;

    uint64_t fp = NeuralGenome_Fingerprint(genome);
    uint64_t founderFp = genome->metadata.founderFingerprint ? genome->metadata.founderFingerprint : fp;
    char fpHex[17], founderHex[17];
    snprintf(fpHex, sizeof(fpHex), "%016llx", (unsigned long long)fp);
    snprintf(founderHex, sizeof(founderHex), "%016llx", (unsigned long long)founderFp);

    JsonWriter w = {.buf = out, .cap = outSize, .len = 0, .overflow = false};
    jwAppendf(&w, "{\n");
    jwAppendf(&w, "  \"version\": %u,\n", genome->formatVersion);
    jwAppendf(&w, "  \"architecture\": ");
    jwEscapedString(&w, NeuralArchitecture_Name(genome->architecture));
    jwAppendf(&w, ",\n");
    jwAppendf(&w, "  \"sensor_schema\": ");
    jwEscapedString(&w, genome->sensorSchema);
    jwAppendf(&w, ",\n");
    jwAppendf(&w, "  \"action_schema\": ");
    jwEscapedString(&w, genome->actionSchema);
    jwAppendf(&w, ",\n");
    jwAppendf(&w, "  \"activation\": \"tanh\",\n");
    jwAppendf(&w, "  \"recurrent_update\": \"simultaneous\",\n");
    jwAppendf(&w, "  \"matrix_layout\": \"row_major\",\n");
    jwAppendf(&w, "  \"precision\": \"float32\",\n");
    if (genome->architecture == NEURAL_ARCH_FIXED_RNN) {
        jwAppendf(&w, "  \"topology\": {\n");
        jwAppendf(&w, "    \"inputs\": %u,\n", genome->inputCount);
        jwAppendf(&w, "    \"hidden\": %u,\n", genome->hiddenCount);
        jwAppendf(&w, "    \"outputs\": %u\n", genome->outputCount);
        jwAppendf(&w, "  },\n");
        jwAppendf(&w, "  \"weights\": {\n");
        jwAppendf(&w, "    \"w_xh\": ");
        jwFloatArray(&w, genome->w_xh, NEURAL_RNN_W_XH_COUNT, 16u);
        jwAppendf(&w, ",\n    \"w_hh\": ");
        jwFloatArray(&w, genome->w_hh, NEURAL_RNN_W_HH_COUNT, 12u);
        jwAppendf(&w, ",\n    \"w_hy\": ");
        jwFloatArray(&w, genome->w_hy, NEURAL_RNN_W_HY_COUNT, 12u);
        jwAppendf(&w, "\n  },\n");
        jwAppendf(&w, "  \"biases\": {\n");
        jwAppendf(&w, "    \"b_h\": ");
        jwFloatArray(&w, genome->b_h, NEURAL_RNN_B_H_COUNT, 12u);
        jwAppendf(&w, ",\n    \"b_y\": ");
        jwFloatArray(&w, genome->b_y, NEURAL_RNN_B_Y_COUNT, 10u);
        jwAppendf(&w, "\n  },\n");
    } else if (genome->architecture == NEURAL_ARCH_CANDIDATE_POLICY) {
        const NeuralCandidatePolicyParams *p = &genome->v2;
        jwAppendf(&w, "  \"topology\": {\n");
        jwAppendf(&w, "    \"context_inputs\": %u,\n", NEURAL_V2_CONTEXT_INPUTS);
        jwAppendf(&w, "    \"context_hidden\": %u,\n", NEURAL_V2_CONTEXT_HIDDEN);
        jwAppendf(&w, "    \"global_actions\": %u,\n", NEURAL_V2_GLOBAL_ACTIONS);
        jwAppendf(&w, "    \"resource_features\": %u,\n", NEURAL_V2_RES_FEAT);
        jwAppendf(&w, "    \"resource_hidden\": %u,\n", NEURAL_V2_RES_HIDDEN);
        jwAppendf(&w, "    \"max_resource_candidates\": %u,\n", NEURAL_V2_MAX_RESOURCE_CANDIDATES);
        jwAppendf(&w, "    \"creature_features\": %u,\n", NEURAL_V2_CRE_FEAT);
        jwAppendf(&w, "    \"creature_hidden\": %u,\n", NEURAL_V2_CRE_HIDDEN);
        jwAppendf(&w, "    \"creature_actions\": %u,\n", NEURAL_V2_CRE_ACTIONS);
        jwAppendf(&w, "    \"max_creature_candidates\": %u\n", NEURAL_V2_MAX_CREATURE_CANDIDATES);
        jwAppendf(&w, "  },\n");
        jwAppendf(&w, "  \"weights\": {\n");
        jwAppendf(&w, "    \"ctx_w_xh\": ");
        jwFloatArray(&w, p->ctx_w_xh, NEURAL_V2_CTX_WXH_COUNT, 16u);
        jwAppendf(&w, ",\n    \"ctx_w_hh\": ");
        jwFloatArray(&w, p->ctx_w_hh, NEURAL_V2_CTX_WHH_COUNT, 16u);
        jwAppendf(&w, ",\n    \"global_w\": ");
        jwFloatArray(&w, p->global_w, NEURAL_V2_GLB_W_COUNT, 16u);
        jwAppendf(&w, ",\n    \"res_w1\": ");
        jwFloatArray(&w, p->res_w1, NEURAL_V2_RES_W1_COUNT, 15u);
        jwAppendf(&w, ",\n    \"res_w2\": ");
        jwFloatArray(&w, p->res_w2, NEURAL_V2_RES_W2_COUNT, 12u);
        jwAppendf(&w, ",\n    \"cre_w1\": ");
        jwFloatArray(&w, p->cre_w1, NEURAL_V2_CRE_W1_COUNT, 15u);
        jwAppendf(&w, ",\n    \"cre_w2\": ");
        jwFloatArray(&w, p->cre_w2, NEURAL_V2_CRE_W2_COUNT, 10u);
        jwAppendf(&w, "\n  },\n");
        jwAppendf(&w, "  \"biases\": {\n");
        jwAppendf(&w, "    \"ctx_b_h\": ");
        jwFloatArray(&w, p->ctx_b_h, NEURAL_V2_CTX_BH_COUNT, 16u);
        jwAppendf(&w, ",\n    \"global_b\": ");
        jwFloatArray(&w, p->global_b, NEURAL_V2_GLB_B_COUNT, 3u);
        jwAppendf(&w, ",\n    \"res_b1\": ");
        jwFloatArray(&w, p->res_b1, NEURAL_V2_RES_B1_COUNT, 12u);
        jwAppendf(&w, ",\n    \"res_b2\": ");
        jwFloatArray(&w, p->res_b2, NEURAL_V2_RES_B2_COUNT, 1u);
        jwAppendf(&w, ",\n    \"cre_b1\": ");
        jwFloatArray(&w, p->cre_b1, NEURAL_V2_CRE_B1_COUNT, 10u);
        jwAppendf(&w, ",\n    \"cre_b2\": ");
        jwFloatArray(&w, p->cre_b2, NEURAL_V2_CRE_B2_COUNT, 4u);
        jwAppendf(&w, "\n  },\n");
    }
    jwAppendf(&w, "  \"metadata\": {\n");
    jwAppendf(&w, "    \"label\": ");
    jwEscapedString(&w, genome->metadata.label);
    jwAppendf(&w, ",\n    \"origin\": ");
    jwEscapedString(&w, genome->metadata.origin);
    jwAppendf(&w, ",\n    \"lineage_id\": %llu,\n", (unsigned long long)genome->metadata.lineageId);
    jwAppendf(&w, "    \"generation\": %u,\n", genome->metadata.generation);
    jwAppendf(&w, "    \"parent_ids\": [%u, %u],\n",
              genome->metadata.parentIds[0], genome->metadata.parentIds[1]);
    if (genome->metadata.trainedSpecies[0]) {
        jwAppendf(&w, "    \"trained_species\": ");
        jwEscapedString(&w, genome->metadata.trainedSpecies);
        jwAppendf(&w, ",\n");
    }
    if (genome->metadata.trainedEnvironment[0]) {
        jwAppendf(&w, "    \"trained_environment\": ");
        jwEscapedString(&w, genome->metadata.trainedEnvironment);
        jwAppendf(&w, ",\n");
    }
    if (genome->metadata.trainerGeneration > 0u || genome->metadata.validationFitness > 0.0f) {
        jwAppendf(&w, "    \"trainer_generation\": %u,\n", genome->metadata.trainerGeneration);
        jwAppendf(&w, "    \"validation_fitness\": %.6f,\n", (double)genome->metadata.validationFitness);
        jwAppendf(&w, "    \"validation_survival\": %.6f,\n", (double)genome->metadata.validationSurvival);
    }
    jwAppendf(&w, "    \"fingerprint\": \"%s\",\n", fpHex);
    jwAppendf(&w, "    \"founder_fingerprint\": \"%s\"\n", founderHex);
    jwAppendf(&w, "  }\n");
    jwAppendf(&w, "}\n");

    if (writtenOut) *writtenOut = w.len;
    if (out && w.overflow) return false;
    return true;
}

/* ========================================================================== */
/* Lectura y validación estricta JSON                                         */
/* ========================================================================== */

typedef struct JsonReader {
    const char *p;
    const char *end;
    char *error;
    size_t errorSize;
} JsonReader;

static void jrSpace(JsonReader *r) {
    while (r->p < r->end && isspace((unsigned char)*r->p)) ++r->p;
}

static bool jrTake(JsonReader *r, char c) {
    jrSpace(r);
    if (r->p >= r->end || *r->p != c) return false;
    ++r->p;
    return true;
}

static bool jrParseString(JsonReader *r, char *out, size_t cap) {
    if (!jrTake(r, '"')) {
        setError(r->error, r->errorSize, "Se esperaba una cadena JSON");
        return false;
    }
    size_t n = 0;
    while (r->p < r->end && *r->p != '"') {
        unsigned char c = (unsigned char)*r->p++;
        if (c < 32u) {
            setError(r->error, r->errorSize, "Carácter de control inválido en cadena JSON");
            return false;
        }
        if (c == '\\') {
            if (r->p >= r->end) {
                setError(r->error, r->errorSize, "Escape incompleto en cadena JSON");
                return false;
            }
            unsigned char esc = (unsigned char)*r->p++;
            switch (esc) {
            case '"': case '\\': case '/': c = esc; break;
            case 'n': c = '\n'; break;
            case 'r': c = '\r'; break;
            case 't': c = '\t'; break;
            case 'b': c = '\b'; break;
            case 'f': c = '\f'; break;
            default:
                setError(r->error, r->errorSize, "Secuencia de escape JSON no soportada");
                return false;
            }
        }
        if (n + 1u >= cap) {
            setError(r->error, r->errorSize, "Cadena JSON excede longitud permitida");
            return false;
        }
        out[n++] = (char)c;
    }
    if (r->p >= r->end || *r->p != '"') {
        setError(r->error, r->errorSize, "Cadena JSON sin cierre");
        return false;
    }
    ++r->p;
    out[n] = '\0';
    return true;
}

static bool jrParseNumber(JsonReader *r, double *out) {
    jrSpace(r);
    const char *start = r->p;
    if (r->p < r->end && *r->p == '-') ++r->p;
    if (r->p >= r->end) {
        setError(r->error, r->errorSize, "Número JSON inesperado al final");
        return false;
    }
    if (*r->p == '0') {
        ++r->p;
    } else if (*r->p >= '1' && *r->p <= '9') {
        while (r->p < r->end && isdigit((unsigned char)*r->p)) ++r->p;
    } else {
        setError(r->error, r->errorSize, "Token numérico JSON inválido");
        return false;
    }
    if (r->p < r->end && *r->p == '.') {
        ++r->p;
        if (r->p >= r->end || !isdigit((unsigned char)*r->p)) {
            setError(r->error, r->errorSize, "Fracción decimal JSON inválida");
            return false;
        }
        while (r->p < r->end && isdigit((unsigned char)*r->p)) ++r->p;
    }
    if (r->p < r->end && (*r->p == 'e' || *r->p == 'E')) {
        ++r->p;
        if (r->p < r->end && (*r->p == '+' || *r->p == '-')) ++r->p;
        if (r->p >= r->end || !isdigit((unsigned char)*r->p)) {
            setError(r->error, r->errorSize, "Exponente numérico JSON inválido");
            return false;
        }
        while (r->p < r->end && isdigit((unsigned char)*r->p)) ++r->p;
    }
    size_t len = (size_t)(r->p - start);
    if (len == 0 || len >= 64u) {
        setError(r->error, r->errorSize, "Literal numérico JSON demasiado largo");
        return false;
    }
    char tmp[64];
    memcpy(tmp, start, len);
    tmp[len] = '\0';
    char *endPtr = NULL;
    errno = 0;
    double val = strtod(tmp, &endPtr);
    if (errno == ERANGE || endPtr != tmp + len || !isfinite(val)) {
        setError(r->error, r->errorSize, "Valor numérico fuera de rango o no finito");
        return false;
    }
    *out = val;
    return true;
}

static bool jrParseU64(JsonReader *r, uint64_t *out, uint64_t maxVal) {
    double v = 0.0;
    if (!jrParseNumber(r, &v)) return false;
    if (v < 0.0 || floor(v) != v || v > (double)maxVal) {
        setError(r->error, r->errorSize, "Entero sin signo fuera de rango");
        return false;
    }
    *out = (uint64_t)v;
    return true;
}

static bool jrParseFloatArray(JsonReader *r, float *dst, size_t expectedCount, const char *fieldName) {
    if (!jrTake(r, '[')) {
        setError(r->error, r->errorSize, "Se esperaba arreglo JSON en '%s'", fieldName);
        return false;
    }
    size_t count = 0;
    jrSpace(r);
    if (r->p < r->end && *r->p != ']') {
        for (;;) {
            if (count >= expectedCount) {
                setError(r->error, r->errorSize,
                         "Arreglo '%s' excede la longitud esperada (%zu)", fieldName, expectedCount);
                return false;
            }
            double val = 0.0;
            if (!jrParseNumber(r, &val)) return false;
            if (fabs(val) > (double)NEURAL_GENOME_MAX_WEIGHT_LIMIT) {
                setError(r->error, r->errorSize,
                         "Parámetro fuera de límites [%.1f, %.1f] en '%s[%zu]'",
                         -(double)NEURAL_GENOME_MAX_WEIGHT_LIMIT,
                         (double)NEURAL_GENOME_MAX_WEIGHT_LIMIT, fieldName, count);
                return false;
            }
            float f = (float)val;
            if (!isfinite(f)) {
                setError(r->error, r->errorSize, "Parámetro no finito en '%s[%zu]'", fieldName, count);
                return false;
            }
            dst[count++] = (f == 0.0f) ? 0.0f : f;
            if (!jrTake(r, ',')) break;
        }
    }
    if (!jrTake(r, ']')) {
        setError(r->error, r->errorSize, "Falta cierre ']' en '%s'", fieldName);
        return false;
    }
    if (count != expectedCount) {
        setError(r->error, r->errorSize, "Dimensión incorrecta en '%s': %zu (esperado %zu)",
                 fieldName, count, expectedCount);
        return false;
    }
    return true;
}

static bool parseHexU64(const char *s, uint64_t *out) {
    if (!s || strlen(s) != 16u) return false;
    uint64_t val = 0u;
    for (unsigned i = 0; i < 16u; ++i) {
        unsigned char c = (unsigned char)s[i];
        unsigned d;
        if (c >= '0' && c <= '9') d = (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') d = (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') d = (unsigned)(c - 'A' + 10);
        else return false;
        val = (val << 4) | (uint64_t)d;
    }
    *out = val;
    return true;
}

static bool parseTopology(JsonReader *r, NeuralGenome *g, unsigned *outTopoKind) {
    if (!jrTake(r, '{')) {
        setError(r->error, r->errorSize, "Se esperaba objeto 'topology'");
        return false;
    }
    bool seenInputs = false, seenHidden = false, seenOutputs = false;
    bool seenV2CtxIn = false, seenV2CtxHid = false, seenV2Glb = false;
    bool seenV2ResF = false, seenV2ResH = false, seenV2CreF = false, seenV2CreH = false, seenV2CreA = false;
    uint32_t v2CtxIn = 0, v2CtxHid = 0, v2Glb = 0, v2ResF = 0, v2ResH = 0, v2CreF = 0, v2CreH = 0, v2CreA = 0;
    jrSpace(r);
    if (r->p < r->end && *r->p != '}') {
        for (;;) {
            char key[48];
            if (!jrParseString(r, key, sizeof(key)) || !jrTake(r, ':')) return false;
            uint64_t val = 0;
            if (!jrParseU64(r, &val, 4096u)) return false;
            if (strcmp(key, "inputs") == 0) {
                if (seenInputs) { setError(r->error, r->errorSize, "Clave duplicada 'inputs'"); return false; }
                g->inputCount = (uint32_t)val; seenInputs = true;
            } else if (strcmp(key, "hidden") == 0) {
                if (seenHidden) { setError(r->error, r->errorSize, "Clave duplicada 'hidden'"); return false; }
                g->hiddenCount = (uint32_t)val; seenHidden = true;
            } else if (strcmp(key, "outputs") == 0) {
                if (seenOutputs) { setError(r->error, r->errorSize, "Clave duplicada 'outputs'"); return false; }
                g->outputCount = (uint32_t)val; seenOutputs = true;
            } else if (strcmp(key, "context_inputs") == 0) {
                if (seenV2CtxIn) { setError(r->error, r->errorSize, "Clave duplicada 'context_inputs'"); return false; }
                v2CtxIn = (uint32_t)val; seenV2CtxIn = true;
            } else if (strcmp(key, "context_hidden") == 0) {
                if (seenV2CtxHid) { setError(r->error, r->errorSize, "Clave duplicada 'context_hidden'"); return false; }
                v2CtxHid = (uint32_t)val; seenV2CtxHid = true;
            } else if (strcmp(key, "global_actions") == 0) {
                if (seenV2Glb) { setError(r->error, r->errorSize, "Clave duplicada 'global_actions'"); return false; }
                v2Glb = (uint32_t)val; seenV2Glb = true;
            } else if (strcmp(key, "resource_features") == 0) {
                if (seenV2ResF) { setError(r->error, r->errorSize, "Clave duplicada 'resource_features'"); return false; }
                v2ResF = (uint32_t)val; seenV2ResF = true;
            } else if (strcmp(key, "resource_hidden") == 0) {
                if (seenV2ResH) { setError(r->error, r->errorSize, "Clave duplicada 'resource_hidden'"); return false; }
                v2ResH = (uint32_t)val; seenV2ResH = true;
            } else if (strcmp(key, "max_resource_candidates") == 0) {
                if (val != NEURAL_V2_MAX_RESOURCE_CANDIDATES) {
                    setError(r->error, r->errorSize, "max_resource_candidates incompatible: %llu", (unsigned long long)val);
                    return false;
                }
            } else if (strcmp(key, "creature_features") == 0) {
                if (seenV2CreF) { setError(r->error, r->errorSize, "Clave duplicada 'creature_features'"); return false; }
                v2CreF = (uint32_t)val; seenV2CreF = true;
            } else if (strcmp(key, "creature_hidden") == 0) {
                if (seenV2CreH) { setError(r->error, r->errorSize, "Clave duplicada 'creature_hidden'"); return false; }
                v2CreH = (uint32_t)val; seenV2CreH = true;
            } else if (strcmp(key, "creature_actions") == 0) {
                if (seenV2CreA) { setError(r->error, r->errorSize, "Clave duplicada 'creature_actions'"); return false; }
                v2CreA = (uint32_t)val; seenV2CreA = true;
            } else if (strcmp(key, "max_creature_candidates") == 0) {
                if (val != NEURAL_V2_MAX_CREATURE_CANDIDATES) {
                    setError(r->error, r->errorSize, "max_creature_candidates incompatible: %llu", (unsigned long long)val);
                    return false;
                }
            } else {
                setError(r->error, r->errorSize, "Clave desconocida en 'topology': '%s'", key);
                return false;
            }
            if (!jrTake(r, ',')) break;
        }
    }
    if (!jrTake(r, '}')) {
        setError(r->error, r->errorSize, "Falta '}' en 'topology'");
        return false;
    }
    if (seenInputs || seenHidden || seenOutputs) {
        if (seenV2CtxIn || seenV2CtxHid || seenV2Glb) {
            setError(r->error, r->errorSize, "Mezcla inválida de topología v1 y v2");
            return false;
        }
        if (!seenInputs || !seenHidden || !seenOutputs) {
            setError(r->error, r->errorSize, "Faltan dimensiones obligatorias en 'topology' v1");
            return false;
        }
        if (g->inputCount != NEURAL_RNN_INPUTS || g->hiddenCount != NEURAL_RNN_HIDDEN ||
            g->outputCount != NEURAL_RNN_OUTPUTS) {
            setError(r->error, r->errorSize,
                     "Topología v1 incompatible: %u->%u->%u (se requiere %u->%u->%u)",
                     g->inputCount, g->hiddenCount, g->outputCount,
                     NEURAL_RNN_INPUTS, NEURAL_RNN_HIDDEN, NEURAL_RNN_OUTPUTS);
            return false;
        }
        *outTopoKind = 1u;
        return true;
    }
    if (!seenV2CtxIn || !seenV2CtxHid || !seenV2Glb ||
        !seenV2ResF || !seenV2ResH || !seenV2CreF || !seenV2CreH || !seenV2CreA) {
        setError(r->error, r->errorSize, "Faltan dimensiones obligatorias en 'topology' v2");
        return false;
    }
    if (v2CtxIn != NEURAL_V2_CONTEXT_INPUTS || v2CtxHid != NEURAL_V2_CONTEXT_HIDDEN ||
        v2Glb != NEURAL_V2_GLOBAL_ACTIONS || v2ResF != NEURAL_V2_RES_FEAT ||
        v2ResH != NEURAL_V2_RES_HIDDEN || v2CreF != NEURAL_V2_CRE_FEAT ||
        v2CreH != NEURAL_V2_CRE_HIDDEN || v2CreA != NEURAL_V2_CRE_ACTIONS) {
        setError(r->error, r->errorSize, "Topología v2 incompatible con recurrent_candidate_policy");
        return false;
    }
    g->inputCount = v2CtxIn;
    g->hiddenCount = v2CtxHid;
    g->outputCount = v2Glb;
    *outTopoKind = 2u;
    return true;
}

static bool parseWeights(JsonReader *r, NeuralGenome *g, unsigned *outWeightsKind) {
    if (!jrTake(r, '{')) {
        setError(r->error, r->errorSize, "Se esperaba objeto 'weights'");
        return false;
    }
    bool seenXh = false, seenHh = false, seenHy = false;
    bool sCtxXh = false, sCtxHh = false, sGlbW = false, sResW1 = false, sResW2 = false, sCreW1 = false, sCreW2 = false;
    jrSpace(r);
    if (r->p < r->end && *r->p != '}') {
        for (;;) {
            char key[32];
            if (!jrParseString(r, key, sizeof(key)) || !jrTake(r, ':')) return false;
            if (strcmp(key, "w_xh") == 0) {
                if (seenXh) { setError(r->error, r->errorSize, "Clave duplicada 'w_xh'"); return false; }
                if (!jrParseFloatArray(r, g->w_xh, NEURAL_RNN_W_XH_COUNT, "w_xh")) return false;
                seenXh = true;
            } else if (strcmp(key, "w_hh") == 0) {
                if (seenHh) { setError(r->error, r->errorSize, "Clave duplicada 'w_hh'"); return false; }
                if (!jrParseFloatArray(r, g->w_hh, NEURAL_RNN_W_HH_COUNT, "w_hh")) return false;
                seenHh = true;
            } else if (strcmp(key, "w_hy") == 0) {
                if (seenHy) { setError(r->error, r->errorSize, "Clave duplicada 'w_hy'"); return false; }
                if (!jrParseFloatArray(r, g->w_hy, NEURAL_RNN_W_HY_COUNT, "w_hy")) return false;
                seenHy = true;
            } else if (strcmp(key, "ctx_w_xh") == 0) {
                if (sCtxXh) { setError(r->error, r->errorSize, "Clave duplicada 'ctx_w_xh'"); return false; }
                if (!jrParseFloatArray(r, g->v2.ctx_w_xh, NEURAL_V2_CTX_WXH_COUNT, "ctx_w_xh")) return false;
                sCtxXh = true;
            } else if (strcmp(key, "ctx_w_hh") == 0) {
                if (sCtxHh) { setError(r->error, r->errorSize, "Clave duplicada 'ctx_w_hh'"); return false; }
                if (!jrParseFloatArray(r, g->v2.ctx_w_hh, NEURAL_V2_CTX_WHH_COUNT, "ctx_w_hh")) return false;
                sCtxHh = true;
            } else if (strcmp(key, "global_w") == 0) {
                if (sGlbW) { setError(r->error, r->errorSize, "Clave duplicada 'global_w'"); return false; }
                if (!jrParseFloatArray(r, g->v2.global_w, NEURAL_V2_GLB_W_COUNT, "global_w")) return false;
                sGlbW = true;
            } else if (strcmp(key, "res_w1") == 0) {
                if (sResW1) { setError(r->error, r->errorSize, "Clave duplicada 'res_w1'"); return false; }
                if (!jrParseFloatArray(r, g->v2.res_w1, NEURAL_V2_RES_W1_COUNT, "res_w1")) return false;
                sResW1 = true;
            } else if (strcmp(key, "res_w2") == 0) {
                if (sResW2) { setError(r->error, r->errorSize, "Clave duplicada 'res_w2'"); return false; }
                if (!jrParseFloatArray(r, g->v2.res_w2, NEURAL_V2_RES_W2_COUNT, "res_w2")) return false;
                sResW2 = true;
            } else if (strcmp(key, "cre_w1") == 0) {
                if (sCreW1) { setError(r->error, r->errorSize, "Clave duplicada 'cre_w1'"); return false; }
                if (!jrParseFloatArray(r, g->v2.cre_w1, NEURAL_V2_CRE_W1_COUNT, "cre_w1")) return false;
                sCreW1 = true;
            } else if (strcmp(key, "cre_w2") == 0) {
                if (sCreW2) { setError(r->error, r->errorSize, "Clave duplicada 'cre_w2'"); return false; }
                if (!jrParseFloatArray(r, g->v2.cre_w2, NEURAL_V2_CRE_W2_COUNT, "cre_w2")) return false;
                sCreW2 = true;
            } else {
                setError(r->error, r->errorSize, "Clave desconocida en 'weights': '%s'", key);
                return false;
            }
            if (!jrTake(r, ',')) break;
        }
    }
    if (!jrTake(r, '}')) {
        setError(r->error, r->errorSize, "Falta '}' en 'weights'");
        return false;
    }
    bool anyV1 = seenXh || seenHh || seenHy;
    bool anyV2 = sCtxXh || sCtxHh || sGlbW || sResW1 || sResW2 || sCreW1 || sCreW2;
    if (anyV1 && anyV2) {
        setError(r->error, r->errorSize, "Mezcla inválida de pesos v1 y v2");
        return false;
    }
    if (anyV1) {
        if (!seenXh || !seenHh || !seenHy) {
            setError(r->error, r->errorSize, "Faltan matrices obligatorias en 'weights' v1");
            return false;
        }
        *outWeightsKind = 1u;
        return true;
    }
    if (!sCtxXh || !sCtxHh || !sGlbW || !sResW1 || !sResW2 || !sCreW1 || !sCreW2) {
        setError(r->error, r->errorSize, "Faltan matrices obligatorias en 'weights' v2");
        return false;
    }
    *outWeightsKind = 2u;
    return true;
}

static bool parseBiases(JsonReader *r, NeuralGenome *g, unsigned *outBiasesKind) {
    if (!jrTake(r, '{')) {
        setError(r->error, r->errorSize, "Se esperaba objeto 'biases'");
        return false;
    }
    bool seenH = false, seenY = false;
    bool sCtxB = false, sGlbB = false, sResB1 = false, sResB2 = false, sCreB1 = false, sCreB2 = false;
    jrSpace(r);
    if (r->p < r->end && *r->p != '}') {
        for (;;) {
            char key[32];
            if (!jrParseString(r, key, sizeof(key)) || !jrTake(r, ':')) return false;
            if (strcmp(key, "b_h") == 0) {
                if (seenH) { setError(r->error, r->errorSize, "Clave duplicada 'b_h'"); return false; }
                if (!jrParseFloatArray(r, g->b_h, NEURAL_RNN_B_H_COUNT, "b_h")) return false;
                seenH = true;
            } else if (strcmp(key, "b_y") == 0) {
                if (seenY) { setError(r->error, r->errorSize, "Clave duplicada 'b_y'"); return false; }
                if (!jrParseFloatArray(r, g->b_y, NEURAL_RNN_B_Y_COUNT, "b_y")) return false;
                seenY = true;
            } else if (strcmp(key, "ctx_b_h") == 0) {
                if (sCtxB) { setError(r->error, r->errorSize, "Clave duplicada 'ctx_b_h'"); return false; }
                if (!jrParseFloatArray(r, g->v2.ctx_b_h, NEURAL_V2_CTX_BH_COUNT, "ctx_b_h")) return false;
                sCtxB = true;
            } else if (strcmp(key, "global_b") == 0) {
                if (sGlbB) { setError(r->error, r->errorSize, "Clave duplicada 'global_b'"); return false; }
                if (!jrParseFloatArray(r, g->v2.global_b, NEURAL_V2_GLB_B_COUNT, "global_b")) return false;
                sGlbB = true;
            } else if (strcmp(key, "res_b1") == 0) {
                if (sResB1) { setError(r->error, r->errorSize, "Clave duplicada 'res_b1'"); return false; }
                if (!jrParseFloatArray(r, g->v2.res_b1, NEURAL_V2_RES_B1_COUNT, "res_b1")) return false;
                sResB1 = true;
            } else if (strcmp(key, "res_b2") == 0) {
                if (sResB2) { setError(r->error, r->errorSize, "Clave duplicada 'res_b2'"); return false; }
                if (!jrParseFloatArray(r, g->v2.res_b2, NEURAL_V2_RES_B2_COUNT, "res_b2")) return false;
                sResB2 = true;
            } else if (strcmp(key, "cre_b1") == 0) {
                if (sCreB1) { setError(r->error, r->errorSize, "Clave duplicada 'cre_b1'"); return false; }
                if (!jrParseFloatArray(r, g->v2.cre_b1, NEURAL_V2_CRE_B1_COUNT, "cre_b1")) return false;
                sCreB1 = true;
            } else if (strcmp(key, "cre_b2") == 0) {
                if (sCreB2) { setError(r->error, r->errorSize, "Clave duplicada 'cre_b2'"); return false; }
                if (!jrParseFloatArray(r, g->v2.cre_b2, NEURAL_V2_CRE_B2_COUNT, "cre_b2")) return false;
                sCreB2 = true;
            } else {
                setError(r->error, r->errorSize, "Clave desconocida en 'biases': '%s'", key);
                return false;
            }
            if (!jrTake(r, ',')) break;
        }
    }
    if (!jrTake(r, '}')) {
        setError(r->error, r->errorSize, "Falta '}' en 'biases'");
        return false;
    }
    bool anyV1 = seenH || seenY;
    bool anyV2 = sCtxB || sGlbB || sResB1 || sResB2 || sCreB1 || sCreB2;
    if (anyV1 && anyV2) {
        setError(r->error, r->errorSize, "Mezcla inválida de sesgos v1 y v2");
        return false;
    }
    if (anyV1) {
        if (!seenH || !seenY) {
            setError(r->error, r->errorSize, "Faltan vectores obligatorios en 'biases' v1");
            return false;
        }
        *outBiasesKind = 1u;
        return true;
    }
    if (!sCtxB || !sGlbB || !sResB1 || !sResB2 || !sCreB1 || !sCreB2) {
        setError(r->error, r->errorSize, "Faltan vectores obligatorios en 'biases' v2");
        return false;
    }
    *outBiasesKind = 2u;
    return true;
}

static bool parseMetadata(JsonReader *r, NeuralGenome *g) {
    if (!jrTake(r, '{')) {
        setError(r->error, r->errorSize, "Se esperaba objeto 'metadata'");
        return false;
    }
    bool seenLabel = false, seenOrigin = false, seenLineage = false, seenGen = false,
         seenParents = false, seenFp = false, seenFounderFp = false;
    jrSpace(r);
    if (r->p < r->end && *r->p != '}') {
        for (;;) {
            char key[32];
            if (!jrParseString(r, key, sizeof(key)) || !jrTake(r, ':')) return false;
            if (strcmp(key, "label") == 0) {
                if (seenLabel) { setError(r->error, r->errorSize, "Clave duplicada 'label'"); return false; }
                if (!jrParseString(r, g->metadata.label, sizeof(g->metadata.label))) return false;
                seenLabel = true;
            } else if (strcmp(key, "origin") == 0) {
                if (seenOrigin) { setError(r->error, r->errorSize, "Clave duplicada 'origin'"); return false; }
                if (!jrParseString(r, g->metadata.origin, sizeof(g->metadata.origin))) return false;
                seenOrigin = true;
            } else if (strcmp(key, "lineage_id") == 0) {
                if (seenLineage) { setError(r->error, r->errorSize, "Clave duplicada 'lineage_id'"); return false; }
                if (!jrParseU64(r, &g->metadata.lineageId, UINT64_C(9007199254740991))) return false;
                seenLineage = true;
            } else if (strcmp(key, "generation") == 0) {
                if (seenGen) { setError(r->error, r->errorSize, "Clave duplicada 'generation'"); return false; }
                uint64_t val = 0;
                if (!jrParseU64(r, &val, UINT32_MAX)) return false;
                g->metadata.generation = (uint32_t)val;
                seenGen = true;
            } else if (strcmp(key, "parent_ids") == 0) {
                if (seenParents) { setError(r->error, r->errorSize, "Clave duplicada 'parent_ids'"); return false; }
                if (!jrTake(r, '[')) { setError(r->error, r->errorSize, "Se esperaba '[' en 'parent_ids'"); return false; }
                uint64_t p0 = 0, p1 = 0;
                if (!jrParseU64(r, &p0, UINT32_MAX) || !jrTake(r, ',') ||
                    !jrParseU64(r, &p1, UINT32_MAX) || !jrTake(r, ']')) {
                    setError(r->error, r->errorSize, "'parent_ids' debe contener exactamente 2 enteros");
                    return false;
                }
                g->metadata.parentIds[0] = (uint32_t)p0;
                g->metadata.parentIds[1] = (uint32_t)p1;
                seenParents = true;
            } else if (strcmp(key, "fingerprint") == 0) {
                if (seenFp) { setError(r->error, r->errorSize, "Clave duplicada 'fingerprint'"); return false; }
                char hex[32];
                if (!jrParseString(r, hex, sizeof(hex))) return false;
                uint64_t parsed = 0;
                if (hex[0] != '\0' && !parseHexU64(hex, &parsed)) {
                    setError(r->error, r->errorSize, "Huella hexadecimal inválida en 'fingerprint'");
                    return false;
                }
                g->metadata.fingerprint = parsed;
                seenFp = true;
            } else if (strcmp(key, "founder_fingerprint") == 0) {
                if (seenFounderFp) { setError(r->error, r->errorSize, "Clave duplicada 'founder_fingerprint'"); return false; }
                char hex[32];
                if (!jrParseString(r, hex, sizeof(hex))) return false;
                uint64_t parsed = 0;
                if (hex[0] != '\0' && !parseHexU64(hex, &parsed)) {
                    setError(r->error, r->errorSize, "Huella hexadecimal inválida en 'founder_fingerprint'");
                    return false;
                }
                g->metadata.founderFingerprint = parsed;
                seenFounderFp = true;
            } else if (strcmp(key, "trained_species") == 0) {
                if (!jrParseString(r, g->metadata.trainedSpecies, sizeof(g->metadata.trainedSpecies))) return false;
            } else if (strcmp(key, "trained_environment") == 0) {
                if (!jrParseString(r, g->metadata.trainedEnvironment, sizeof(g->metadata.trainedEnvironment))) return false;
            } else if (strcmp(key, "trainer_generation") == 0) {
                uint64_t val = 0;
                if (!jrParseU64(r, &val, UINT32_MAX)) return false;
                g->metadata.trainerGeneration = (uint32_t)val;
            } else if (strcmp(key, "validation_fitness") == 0) {
                double val = 0.0;
                if (!jrParseNumber(r, &val)) return false;
                g->metadata.validationFitness = (float)val;
            } else if (strcmp(key, "validation_survival") == 0) {
                double val = 0.0;
                if (!jrParseNumber(r, &val)) return false;
                g->metadata.validationSurvival = (float)val;
            } else {
                setError(r->error, r->errorSize, "Clave desconocida en 'metadata': '%s'", key);
                return false;
            }
            if (!jrTake(r, ',')) break;
        }
    }
    if (!jrTake(r, '}')) {
        setError(r->error, r->errorSize, "Falta '}' en 'metadata'");
        return false;
    }
    return true;
}

bool NeuralBrain_DeserializeJSON(const char *json, size_t length, NeuralGenome *outGenome,
                                 char *error, size_t errorSize) {
    if (!json || !outGenome) {
        setError(error, errorSize, "Argumentos nulos al deserializar cerebro");
        return false;
    }
    if (length == 0 || length > NEURAL_BRAIN_MAX_FILE_BYTES) {
        setError(error, errorSize, "Tamaño de archivo cerebral fuera de límites (%zu bytes)", length);
        return false;
    }

    NeuralGenome temp;
    memset(&temp, 0, sizeof(temp));
    snprintf(temp.metadata.label, sizeof(temp.metadata.label), "imported_rnn");
    snprintf(temp.metadata.origin, sizeof(temp.metadata.origin), "imported");
    temp.metadata.lineageId = 1u;

    JsonReader r = {.p = json, .end = json + length, .error = error, .errorSize = errorSize};
    if (!jrTake(&r, '{')) {
        setError(error, errorSize, "El documento *.brain.json debe comenzar con '{'");
        return false;
    }

    bool seenVersion = false, seenArch = false, seenSensor = false, seenAction = false;
    bool seenTopology = false, seenWeights = false, seenBiases = false, seenMetadata = false;
    bool seenActivation = false, seenRecurrent = false, seenLayout = false, seenPrecision = false;
    unsigned topoKind = 0u, weightsKind = 0u, biasesKind = 0u;

    jrSpace(&r);
    if (r.p < r.end && *r.p != '}') {
        for (;;) {
            char key[48];
            if (!jrParseString(&r, key, sizeof(key)) || !jrTake(&r, ':')) return false;
            if (strcmp(key, "version") == 0) {
                if (seenVersion) { setError(error, errorSize, "Clave duplicada 'version'"); return false; }
                uint64_t ver = 0;
                if (!jrParseU64(&r, &ver, 1000u)) return false;
                if (ver != NEURAL_BRAIN_FORMAT_VERSION && ver != NEURAL_BRAIN_FORMAT_VERSION_V2) {
                    setError(error, errorSize, "Versión no soportada: %llu (esperada 1 o 2)",
                             (unsigned long long)ver);
                    return false;
                }
                temp.formatVersion = (uint32_t)ver;
                seenVersion = true;
            } else if (strcmp(key, "architecture") == 0) {
                if (seenArch) { setError(error, errorSize, "Clave duplicada 'architecture'"); return false; }
                char archName[48];
                if (!jrParseString(&r, archName, sizeof(archName))) return false;
                if (!NeuralArchitecture_FromName(archName, &temp.architecture)) {
                    setError(error, errorSize, "Arquitectura no soportada: '%s'", archName);
                    return false;
                }
                seenArch = true;
            } else if (strcmp(key, "sensor_schema") == 0) {
                if (seenSensor) { setError(error, errorSize, "Clave duplicada 'sensor_schema'"); return false; }
                if (!jrParseString(&r, temp.sensorSchema, sizeof(temp.sensorSchema))) return false;
                if (strcmp(temp.sensorSchema, NEURAL_SENSOR_SCHEMA_V1) != 0 &&
                    strcmp(temp.sensorSchema, NEURAL_SENSOR_SCHEMA_V2) != 0) {
                    setError(error, errorSize, "Esquema de sensores desconocido: '%s'", temp.sensorSchema);
                    return false;
                }
                seenSensor = true;
            } else if (strcmp(key, "action_schema") == 0) {
                if (seenAction) { setError(error, errorSize, "Clave duplicada 'action_schema'"); return false; }
                if (!jrParseString(&r, temp.actionSchema, sizeof(temp.actionSchema))) return false;
                if (strcmp(temp.actionSchema, NEURAL_ACTION_SCHEMA_V1) != 0 &&
                    strcmp(temp.actionSchema, NEURAL_ACTION_SCHEMA_V2) != 0) {
                    setError(error, errorSize, "Esquema de acciones desconocido: '%s'", temp.actionSchema);
                    return false;
                }
                seenAction = true;
            } else if (strcmp(key, "activation") == 0) {
                if (seenActivation) { setError(error, errorSize, "Clave duplicada 'activation'"); return false; }
                char act[32];
                if (!jrParseString(&r, act, sizeof(act))) return false;
                if (strcmp(act, "tanh") != 0) {
                    setError(error, errorSize, "Función de activación no soportada: '%s'", act);
                    return false;
                }
                seenActivation = true;
            } else if (strcmp(key, "recurrent_update") == 0) {
                if (seenRecurrent) { setError(error, errorSize, "Clave duplicada 'recurrent_update'"); return false; }
                char upd[32];
                if (!jrParseString(&r, upd, sizeof(upd))) return false;
                if (strcmp(upd, "simultaneous") != 0) {
                    setError(error, errorSize, "Semántica recurrente no soportada: '%s'", upd);
                    return false;
                }
                seenRecurrent = true;
            } else if (strcmp(key, "matrix_layout") == 0) {
                if (seenLayout) { setError(error, errorSize, "Clave duplicada 'matrix_layout'"); return false; }
                char layout[32];
                if (!jrParseString(&r, layout, sizeof(layout))) return false;
                if (strcmp(layout, "row_major") != 0) {
                    setError(error, errorSize, "Disposición de matriz no soportada: '%s'", layout);
                    return false;
                }
                seenLayout = true;
            } else if (strcmp(key, "precision") == 0) {
                if (seenPrecision) { setError(error, errorSize, "Clave duplicada 'precision'"); return false; }
                char prec[32];
                if (!jrParseString(&r, prec, sizeof(prec))) return false;
                if (strcmp(prec, "float32") != 0) {
                    setError(error, errorSize, "Precisión no soportada: '%s'", prec);
                    return false;
                }
                seenPrecision = true;
            } else if (strcmp(key, "topology") == 0) {
                if (seenTopology) { setError(error, errorSize, "Clave duplicada 'topology'"); return false; }
                if (!parseTopology(&r, &temp, &topoKind)) return false;
                seenTopology = true;
            } else if (strcmp(key, "weights") == 0) {
                if (seenWeights) { setError(error, errorSize, "Clave duplicada 'weights'"); return false; }
                if (!parseWeights(&r, &temp, &weightsKind)) return false;
                seenWeights = true;
            } else if (strcmp(key, "biases") == 0) {
                if (seenBiases) { setError(error, errorSize, "Clave duplicada 'biases'"); return false; }
                if (!parseBiases(&r, &temp, &biasesKind)) return false;
                seenBiases = true;
            } else if (strcmp(key, "metadata") == 0) {
                if (seenMetadata) { setError(error, errorSize, "Clave duplicada 'metadata'"); return false; }
                if (!parseMetadata(&r, &temp)) return false;
                seenMetadata = true;
            } else {
                setError(error, errorSize, "Campo raíz desconocido: '%s'", key);
                return false;
            }
            if (!jrTake(&r, ',')) break;
        }
    }

    if (!jrTake(&r, '}')) {
        setError(error, errorSize, "Falta cierre '}' en objeto raíz");
        return false;
    }
    jrSpace(&r);
    if (r.p != r.end) {
        setError(error, errorSize, "Datos inesperados tras el cierre del documento JSON");
        return false;
    }
    if (!seenVersion || !seenArch || !seenSensor || !seenAction ||
        !seenTopology || !seenWeights || !seenBiases || !seenMetadata) {
        setError(error, errorSize, "Faltan campos obligatorios en *.brain.json");
        return false;
    }
    unsigned expectedKind = (temp.architecture == NEURAL_ARCH_FIXED_RNN) ? 1u : 2u;
    if (topoKind != expectedKind || weightsKind != expectedKind || biasesKind != expectedKind) {
        setError(error, errorSize, "Inconsistencia entre arquitectura declarada y topología/pesos/sesgos");
        return false;
    }

    temp.valid = true;
    if (!NeuralGenome_Validate(&temp)) {
        setError(error, errorSize, "El genoma deserializado no supera NeuralGenome_Validate");
        return false;
    }
    uint64_t computed = NeuralGenome_UpdateFingerprint(&temp);
    if (temp.metadata.founderFingerprint == 0u) {
        temp.metadata.founderFingerprint = computed;
    }

    *outGenome = temp;
    return true;
}

bool NeuralBrain_LoadFile(const char *path, NeuralGenome *outGenome,
                          char *error, size_t errorSize) {
    if (!path || !path[0] || !outGenome) {
        setError(error, errorSize, "Ruta o destino nulo al cargar cerebro");
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        setError(error, errorSize, "No se pudo abrir '%s': %s", path, strerror(errno));
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        setError(error, errorSize, "No se pudo determinar el tamaño de '%s'", path);
        return false;
    }
    long size = ftell(f);
    if (size <= 0 || (size_t)size > NEURAL_BRAIN_MAX_FILE_BYTES) {
        fclose(f);
        setError(error, errorSize, "Archivo '%s' vacío o demasiado grande (%ld bytes)", path, size);
        return false;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        setError(error, errorSize, "Error al posicionar lectura de '%s'", path);
        return false;
    }
    char *buffer = (char *)malloc((size_t)size + 1u);
    if (!buffer) {
        fclose(f);
        setError(error, errorSize, "Sin memoria para leer '%s'", path);
        return false;
    }
    size_t readBytes = fread(buffer, 1, (size_t)size, f);
    fclose(f);
    if (readBytes != (size_t)size) {
        free(buffer);
        setError(error, errorSize, "Lectura incompleta de '%s'", path);
        return false;
    }
    buffer[readBytes] = '\0';
    bool ok = NeuralBrain_DeserializeJSON(buffer, readBytes, outGenome, error, errorSize);
    free(buffer);
    return ok;
}

bool NeuralBrain_SaveFile(const NeuralGenome *genome, const char *path, bool overwrite,
                          char *error, size_t errorSize) {
    if (!NeuralGenome_Validate(genome)) {
        setError(error, errorSize, "Genoma neuronal inválido; exportación cancelada");
        return false;
    }
    if (!path || !path[0]) {
        setError(error, errorSize, "Ruta de exportación vacía");
        return false;
    }
    if (!overwrite) {
        FILE *exists = fopen(path, "rb");
        if (exists) {
            fclose(exists);
            setError(error, errorSize,
                     "El archivo '%s' ya existe (active sobrescritura para reemplazarlo)", path);
            return false;
        }
    }
    size_t needed = 0;
    if (!NeuralBrain_SerializeJSON(genome, NULL, 0, &needed) || needed == 0) {
        setError(error, errorSize, "No se pudo calcular el tamaño JSON del cerebro");
        return false;
    }
    char *buffer = (char *)malloc(needed + 1u);
    if (!buffer) {
        setError(error, errorSize, "Sin memoria al serializar cerebro");
        return false;
    }
    if (!NeuralBrain_SerializeJSON(genome, buffer, needed + 1u, &needed)) {
        free(buffer);
        setError(error, errorSize, "Fallo al serializar cerebro");
        return false;
    }
    FILE *f = fopen(path, "wb");
    if (!f) {
        free(buffer);
        setError(error, errorSize, "No se pudo crear '%s': %s", path, strerror(errno));
        return false;
    }
    size_t written = fwrite(buffer, 1, needed, f);
    int closeErr = fclose(f);
    free(buffer);
    if (written != needed || closeErr != 0) {
        setError(error, errorSize, "Error de escritura en '%s'", path);
        return false;
    }
    return true;
}
