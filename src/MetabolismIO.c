/**
 * @file MetabolismIO.c
 * @brief Carga y validación estricta JSON de moléculas, sustancias y perfiles metabólicos.
 */
#define _XOPEN_SOURCE 700
#include "MetabolismIO.h"
#include <ctype.h>
#include <errno.h>
#include <ftw.h>
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

typedef struct MJsonReader {
    const char *p;
    const char *end;
    char *error;
    size_t errorSize;
} MJsonReader;

static void mjSpace(MJsonReader *r) {
    while (r->p < r->end && isspace((unsigned char)*r->p)) ++r->p;
}

static bool mjTake(MJsonReader *r, char c) {
    mjSpace(r);
    if (r->p >= r->end || *r->p != c) return false;
    ++r->p;
    return true;
}

static bool mjParseString(MJsonReader *r, char *out, size_t cap) {
    if (!mjTake(r, '"')) {
        setErr(r->error, r->errorSize, "Se esperaba una cadena JSON");
        return false;
    }
    size_t n = 0;
    while (r->p < r->end && *r->p != '"') {
        unsigned char c = (unsigned char)*r->p++;
        if (c < 32u) {
            setErr(r->error, r->errorSize, "Carácter de control inválido en cadena JSON");
            return false;
        }
        if (c == '\\') {
            if (r->p >= r->end) {
                setErr(r->error, r->errorSize, "Escape incompleto en cadena JSON");
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
                setErr(r->error, r->errorSize, "Secuencia de escape JSON no soportada");
                return false;
            }
        }
        if (n + 1u >= cap) {
            setErr(r->error, r->errorSize, "Cadena JSON excede longitud permitida (%zu)", cap - 1u);
            return false;
        }
        out[n++] = (char)c;
    }
    if (r->p >= r->end || *r->p != '"') {
        setErr(r->error, r->errorSize, "Cadena JSON sin cierre");
        return false;
    }
    ++r->p;
    out[n] = '\0';
    return true;
}

static bool mjParseNumber(MJsonReader *r, double *out) {
    mjSpace(r);
    const char *start = r->p;
    if (r->p < r->end && *r->p == '-') ++r->p;
    if (r->p >= r->end) {
        setErr(r->error, r->errorSize, "Número JSON inesperado al final");
        return false;
    }
    if (*r->p == '0') {
        ++r->p;
    } else if (*r->p >= '1' && *r->p <= '9') {
        while (r->p < r->end && isdigit((unsigned char)*r->p)) ++r->p;
    } else {
        setErr(r->error, r->errorSize, "Token numérico JSON inválido");
        return false;
    }
    if (r->p < r->end && *r->p == '.') {
        ++r->p;
        if (r->p >= r->end || !isdigit((unsigned char)*r->p)) {
            setErr(r->error, r->errorSize, "Fracción decimal JSON inválida");
            return false;
        }
        while (r->p < r->end && isdigit((unsigned char)*r->p)) ++r->p;
    }
    if (r->p < r->end && (*r->p == 'e' || *r->p == 'E')) {
        ++r->p;
        if (r->p < r->end && (*r->p == '+' || *r->p == '-')) ++r->p;
        if (r->p >= r->end || !isdigit((unsigned char)*r->p)) {
            setErr(r->error, r->errorSize, "Exponente numérico JSON inválido");
            return false;
        }
        while (r->p < r->end && isdigit((unsigned char)*r->p)) ++r->p;
    }
    size_t len = (size_t)(r->p - start);
    if (len == 0 || len >= 64u) {
        setErr(r->error, r->errorSize, "Literal numérico demasiado largo");
        return false;
    }
    char tmp[64];
    memcpy(tmp, start, len);
    tmp[len] = '\0';
    char *endPtr = NULL;
    errno = 0;
    double val = strtod(tmp, &endPtr);
    if (errno == ERANGE || endPtr != tmp + len || !isfinite(val)) {
        setErr(r->error, r->errorSize, "Valor numérico fuera de rango o no finito");
        return false;
    }
    *out = val;
    return true;
}

static bool mjParseFloat(MJsonReader *r, float *out) {
    double v = 0.0;
    if (!mjParseNumber(r, &v)) return false;
    float f = (float)v;
    if (!isfinite(f)) {
        setErr(r->error, r->errorSize, "Valor float no finito");
        return false;
    }
    *out = f;
    return true;
}

static bool mjParseU32(MJsonReader *r, uint32_t *out) {
    double v = 0.0;
    if (!mjParseNumber(r, &v)) return false;
    if (v < 0.0 || floor(v) != v || v > (double)UINT32_MAX) {
        setErr(r->error, r->errorSize, "Entero sin signo fuera de rango");
        return false;
    }
    *out = (uint32_t)v;
    return true;
}

static bool mjSkipValue(MJsonReader *r, int depth);

static bool mjSkipValue(MJsonReader *r, int depth) {
    if (depth > 16) {
        setErr(r->error, r->errorSize, "Profundidad JSON excesiva");
        return false;
    }
    mjSpace(r);
    if (r->p >= r->end) {
        setErr(r->error, r->errorSize, "Fin inesperado de JSON");
        return false;
    }
    char c = *r->p;
    if (c == '"') {
        char dummy[256];
        return mjParseString(r, dummy, sizeof(dummy));
    }
    if (c == '{') {
        ++r->p;
        mjSpace(r);
        if (r->p < r->end && *r->p != '}') {
            for (;;) {
                char key[128];
                if (!mjParseString(r, key, sizeof(key)) || !mjTake(r, ':')) return false;
                if (!mjSkipValue(r, depth + 1)) return false;
                if (!mjTake(r, ',')) break;
            }
        }
        return mjTake(r, '}');
    }
    if (c == '[') {
        ++r->p;
        mjSpace(r);
        if (r->p < r->end && *r->p != ']') {
            for (;;) {
                if (!mjSkipValue(r, depth + 1)) return false;
                if (!mjTake(r, ',')) break;
            }
        }
        return mjTake(r, ']');
    }
    if (c == 't' && (size_t)(r->end - r->p) >= 4u && memcmp(r->p, "true", 4) == 0) {
        r->p += 4;
        return true;
    }
    if (c == 'f' && (size_t)(r->end - r->p) >= 5u && memcmp(r->p, "false", 5) == 0) {
        r->p += 5;
        return true;
    }
    if (c == 'n' && (size_t)(r->end - r->p) >= 4u && memcmp(r->p, "null", 4) == 0) {
        r->p += 4;
        return true;
    }
    double num = 0.0;
    return mjParseNumber(r, &num);
}

static bool mjCheckEOF(MJsonReader *r) {
    mjSpace(r);
    if (r->p != r->end) {
        setErr(r->error, r->errorSize, "Contenido inesperado tras el cierre del documento JSON");
        return false;
    }
    return true;
}

static bool readFileBounded(const char *path, char **outBuf, size_t *outLen,
                            char *error, size_t errorSize) {
    if (!path || !path[0] || !outBuf || !outLen) {
        setErr(error, errorSize, "Ruta de archivo inválida");
        return false;
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        setErr(error, errorSize, "No se pudo abrir '%s': %s", path, strerror(errno));
        return false;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        setErr(error, errorSize, "No se pudo medir '%s'", path);
        return false;
    }
    long sz = ftell(f);
    if (sz <= 0 || (size_t)sz > METABOLISM_MAX_FILE_BYTES) {
        fclose(f);
        setErr(error, errorSize, "Tamaño de archivo inválido o excesivo en '%s'", path);
        return false;
    }
    rewind(f);
    char *buf = malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(f);
        setErr(error, errorSize, "Sin memoria al leer '%s'", path);
        return false;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) {
        free(buf);
        setErr(error, errorSize, "Lectura incompleta de '%s'", path);
        return false;
    }
    buf[got] = '\0';
    *outBuf = buf;
    *outLen = got;
    return true;
}

/* ========================================================================== */
/* Firma sensorial                                                            */
/* ========================================================================== */

static bool parseSensorySignature(MJsonReader *r, SensorySignature *sig, bool allowNegativeOffset) {
    memset(sig, 0, sizeof(*sig));
    if (!mjTake(r, '{')) {
        setErr(r->error, r->errorSize, "Se esperaba objeto 'sensory_signature'");
        return false;
    }
    mjSpace(r);
    if (r->p < r->end && *r->p != '}') {
        for (;;) {
            char key[48];
            if (!mjParseString(r, key, sizeof(key)) || !mjTake(r, ':')) return false;
            float val = 0.0f;
            if (!mjParseFloat(r, &val)) return false;
            float lo = allowNegativeOffset ? -1.0f : 0.0f;
            if (val < lo || val > 1.0f) {
                setErr(r->error, r->errorSize, "Canal sensorial '%s' fuera de rango [%.1f, 1.0]",
                       key, (double)lo);
                return false;
            }
            bool matched = false;
            for (unsigned c = 0; c < SENSORY_SIGNATURE_CHANNELS; ++c) {
                if (strcmp(key, SensoryChannel_Name((SensoryChannel)c)) == 0) {
                    sig->channels[c] = val;
                    matched = true;
                    break;
                }
            }
            if (!matched) {
                setErr(r->error, r->errorSize, "Canal sensorial desconocido: '%s'", key);
                return false;
            }
            if (!mjTake(r, ',')) break;
        }
    }
    if (!mjTake(r, '}')) {
        setErr(r->error, r->errorSize, "Falta '}' en 'sensory_signature'");
        return false;
    }
    return true;
}

/* ========================================================================== */
/* Carga de moléculas                                                         */
/* ========================================================================== */

static bool parseMoleculeObject(MJsonReader *r, MoleculeDef *out) {
    memset(out, 0, sizeof(*out));
    if (!mjTake(r, '{')) {
        setErr(r->error, r->errorSize, "Se esperaba objeto de molécula");
        return false;
    }
    bool seenId = false, seenSensory = false;
    mjSpace(r);
    if (r->p < r->end && *r->p != '}') {
        for (;;) {
            char key[48];
            if (!mjParseString(r, key, sizeof(key)) || !mjTake(r, ':')) return false;
            if (strcmp(key, "id") == 0) {
                if (!mjParseString(r, out->id, sizeof(out->id))) return false;
                seenId = true;
            } else if (strcmp(key, "name") == 0) {
                if (!mjParseString(r, out->name, sizeof(out->name))) return false;
            } else if (strcmp(key, "description") == 0) {
                if (!mjParseString(r, out->description, sizeof(out->description))) return false;
            } else if (strcmp(key, "sensory_signature") == 0) {
                if (!parseSensorySignature(r, &out->sensory, false)) return false;
                seenSensory = true;
            } else {
                setErr(r->error, r->errorSize, "Clave desconocida en molécula: '%s'", key);
                return false;
            }
            if (!mjTake(r, ',')) break;
        }
    }
    if (!mjTake(r, '}')) {
        setErr(r->error, r->errorSize, "Falta '}' en objeto de molécula");
        return false;
    }
    if (!seenId || !seenSensory) {
        setErr(r->error, r->errorSize, "Molécula incompleta: requiere 'id' y 'sensory_signature'");
        return false;
    }
    return true;
}

bool MetabolismIO_LoadMoleculesJSON(const char *json, size_t length, MoleculeRegistry *reg,
                                    char *error, size_t errorSize) {
    if (!json || length == 0 || !reg) {
        setErr(error, errorSize, "Entrada JSON de moléculas vacía o registro nulo");
        return false;
    }
    if (reg->frozen) {
        setErr(error, errorSize, "No se puede cargar moléculas en un registro congelado");
        return false;
    }

    MoleculeRegistry temp = *reg;
    MJsonReader r = {.p = json, .end = json + length, .error = error, .errorSize = errorSize};
    if (!mjTake(&r, '{')) {
        setErr(error, errorSize, "Se esperaba objeto raíz en molecules.json");
        return false;
    }
    bool seenVersion = false, seenMolecules = false;
    uint32_t version = 0;
    mjSpace(&r);
    if (r.p < r.end && *r.p != '}') {
        for (;;) {
            char key[48];
            if (!mjParseString(&r, key, sizeof(key)) || !mjTake(&r, ':')) return false;
            if (strcmp(key, "version") == 0) {
                if (!mjParseU32(&r, &version)) return false;
                seenVersion = true;
                if (version != MOLECULE_SCHEMA_VERSION) {
                    setErr(error, errorSize, "Versión de esquema de moléculas no soportada: %u", version);
                    return false;
                }
            } else if (strcmp(key, "molecules") == 0) {
                if (!mjTake(&r, '[')) {
                    setErr(error, errorSize, "Se esperaba arreglo 'molecules'");
                    return false;
                }
                mjSpace(&r);
                if (r.p < r.end && *r.p != ']') {
                    for (;;) {
                        MoleculeDef def;
                        if (!parseMoleculeObject(&r, &def)) return false;
                        if (!MoleculeRegistry_Register(&temp, &def, NULL, error, errorSize)) return false;
                        if (!mjTake(&r, ',')) break;
                    }
                }
                if (!mjTake(&r, ']')) {
                    setErr(error, errorSize, "Falta ']' en arreglo 'molecules'");
                    return false;
                }
                seenMolecules = true;
            } else {
                setErr(error, errorSize, "Clave raíz desconocida en molecules.json: '%s'", key);
                return false;
            }
            if (!mjTake(&r, ',')) break;
        }
    }
    if (!mjTake(&r, '}') || !mjCheckEOF(&r)) return false;
    if (!seenVersion || !seenMolecules || temp.count == 0u) {
        setErr(error, errorSize, "molecules.json requiere 'version' y al menos una molécula");
        return false;
    }
    *reg = temp;
    return true;
}

bool MetabolismIO_LoadMoleculesFile(const char *path, MoleculeRegistry *reg,
                                    char *error, size_t errorSize) {
    char *buf = NULL;
    size_t len = 0;
    if (!readFileBounded(path, &buf, &len, error, errorSize)) return false;
    bool ok = MetabolismIO_LoadMoleculesJSON(buf, len, reg, error, errorSize);
    free(buf);
    return ok;
}

/* ========================================================================== */
/* Carga de sustancias                                                        */
/* ========================================================================== */

bool MetabolismIO_LoadSubstanceJSON(const char *json, size_t length, SubstanceRegistry *reg,
                                    SubstanceHandle *outHandle, char *error, size_t errorSize) {
    if (!json || length == 0 || !reg || !reg->molecules) {
        setErr(error, errorSize, "Entrada JSON de sustancia inválida");
        return false;
    }
    if (reg->frozen) {
        setErr(error, errorSize, "No se puede cargar sustancia en registro congelado");
        return false;
    }

    SubstanceDef def;
    memset(&def, 0, sizeof(def));
    def.phase = SUBSTANCE_PHASE_SOLID;
    def.hardness = 0.2f;
    def.model = SUBSTANCE_MODEL_GENERIC;
    def.color[0] = 0.5f; def.color[1] = 0.5f; def.color[2] = 0.5f;

    MJsonReader r = {.p = json, .end = json + length, .error = error, .errorSize = errorSize};
    if (!mjTake(&r, '{')) {
        setErr(error, errorSize, "Se esperaba objeto raíz en archivo de sustancia");
        return false;
    }
    bool seenVersion = false, seenId = false, seenComp = false;
    mjSpace(&r);
    if (r.p < r.end && *r.p != '}') {
        for (;;) {
            char key[64];
            if (!mjParseString(&r, key, sizeof(key)) || !mjTake(&r, ':')) return false;
            if (strcmp(key, "version") == 0) {
                if (!mjParseU32(&r, &def.schemaVersion)) return false;
                seenVersion = true;
            } else if (strcmp(key, "id") == 0) {
                if (!mjParseString(&r, def.id, sizeof(def.id))) return false;
                seenId = true;
            } else if (strcmp(key, "name") == 0) {
                if (!mjParseString(&r, def.name, sizeof(def.name))) return false;
            } else if (strcmp(key, "composition") == 0) {
                if (!mjTake(&r, '{')) {
                    setErr(error, errorSize, "Se esperaba objeto 'composition'");
                    return false;
                }
                mjSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char molId[MOLECULE_ID_MAX];
                        if (!mjParseString(&r, molId, sizeof(molId)) || !mjTake(&r, ':')) return false;
                        float frac = 0.0f;
                        if (!mjParseFloat(&r, &frac)) return false;
                        MoleculeHandle mh = MoleculeRegistry_Find(reg->molecules, molId);
                        if (mh == MOLECULE_INVALID_HANDLE) {
                            setErr(error, errorSize, "Sustancia referencia molécula desconocida: '%s'", molId);
                            return false;
                        }
                        if (def.componentCount >= SUBSTANCE_MAX_COMPONENTS) {
                            setErr(error, errorSize, "Demasiados componentes en sustancia (máx %u)",
                                   SUBSTANCE_MAX_COMPONENTS);
                            return false;
                        }
                        def.components[def.componentCount++] = (SubstanceComponent){
                            .molecule = mh, .fraction = frac
                        };
                        if (!mjTake(&r, ',')) break;
                    }
                }
                if (!mjTake(&r, '}')) {
                    setErr(error, errorSize, "Falta '}' en 'composition'");
                    return false;
                }
                seenComp = true;
            } else if (strcmp(key, "physical_properties") == 0) {
                if (!mjTake(&r, '{')) {
                    setErr(error, errorSize, "Se esperaba objeto 'physical_properties'");
                    return false;
                }
                mjSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char pk[48];
                        if (!mjParseString(&r, pk, sizeof(pk)) || !mjTake(&r, ':')) return false;
                        if (strcmp(pk, "phase") == 0) {
                            char phStr[32];
                            if (!mjParseString(&r, phStr, sizeof(phStr))) return false;
                            if (!SubstancePhase_FromName(phStr, &def.phase)) {
                                setErr(error, errorSize, "Fase física desconocida: '%s'", phStr);
                                return false;
                            }
                        } else if (strcmp(pk, "hardness") == 0) {
                            if (!mjParseFloat(&r, &def.hardness)) return false;
                        } else {
                            setErr(error, errorSize, "Propiedad física desconocida: '%s'", pk);
                            return false;
                        }
                        if (!mjTake(&r, ',')) break;
                    }
                }
                if (!mjTake(&r, '}')) {
                    setErr(error, errorSize, "Falta '}' en 'physical_properties'");
                    return false;
                }
            } else if (strcmp(key, "presentation") == 0) {
                if (!mjTake(&r, '{')) {
                    setErr(error, errorSize, "Se esperaba objeto 'presentation'");
                    return false;
                }
                mjSpace(&r);
                if (r.p < r.end && *r.p != '}') {
                    for (;;) {
                        char prk[48];
                        if (!mjParseString(&r, prk, sizeof(prk)) || !mjTake(&r, ':')) return false;
                        if (strcmp(prk, "model") == 0) {
                            char mStr[32];
                            if (!mjParseString(&r, mStr, sizeof(mStr))) return false;
                            if (!SubstancePresentationModel_FromName(mStr, &def.model)) {
                                setErr(error, errorSize, "Modelo visual desconocido: '%s'", mStr);
                                return false;
                            }
                        } else if (strcmp(prk, "color") == 0) {
                            if (!mjTake(&r, '[')) {
                                setErr(error, errorSize, "Se esperaba arreglo RGB en 'color'");
                                return false;
                            }
                            for (unsigned ci = 0; ci < 3u; ++ci) {
                                if (!mjParseFloat(&r, &def.color[ci])) return false;
                                if (ci < 2u && !mjTake(&r, ',')) {
                                    setErr(error, errorSize, "'color' requiere exactamente 3 componentes");
                                    return false;
                                }
                            }
                            if (!mjTake(&r, ']')) {
                                setErr(error, errorSize, "Falta ']' en 'color'");
                                return false;
                            }
                        } else if (strcmp(prk, "sensory_offset") == 0) {
                            if (!parseSensorySignature(&r, &def.sensoryOffset, true)) return false;
                            def.hasSensoryOverride = true;
                        } else {
                            setErr(error, errorSize, "Propiedad de presentación desconocida: '%s'", prk);
                            return false;
                        }
                        if (!mjTake(&r, ',')) break;
                    }
                }
                if (!mjTake(&r, '}')) {
                    setErr(error, errorSize, "Falta '}' en 'presentation'");
                    return false;
                }
            } else if (strcmp(key, "description") == 0) {
                if (!mjSkipValue(&r, 0)) return false;
            } else {
                setErr(error, errorSize, "Clave raíz desconocida en sustancia: '%s'", key);
                return false;
            }
            if (!mjTake(&r, ',')) break;
        }
    }
    if (!mjTake(&r, '}') || !mjCheckEOF(&r)) return false;
    if (!seenVersion || !seenId || !seenComp) {
        setErr(error, errorSize, "Sustancia incompleta: requiere 'version', 'id' y 'composition'");
        return false;
    }
    return SubstanceRegistry_Register(reg, &def, outHandle, error, errorSize);
}

bool MetabolismIO_LoadSubstanceFile(const char *path, SubstanceRegistry *reg,
                                    SubstanceHandle *outHandle, char *error, size_t errorSize) {
    char *buf = NULL;
    size_t len = 0;
    if (!readFileBounded(path, &buf, &len, error, errorSize)) return false;
    bool ok = MetabolismIO_LoadSubstanceJSON(buf, len, reg, outHandle, error, errorSize);
    free(buf);
    return ok;
}

/* ========================================================================== */
/* Carga de perfiles metabólicos                                              */
/* ========================================================================== */

static bool parseProcessingRule(MJsonReader *r, const MoleculeRegistry *molecules,
                                MetabolicProcessingRule *rule) {
    memset(rule, 0, sizeof(*rule));
    rule->molecule = MOLECULE_INVALID_HANDLE;
    if (!mjTake(r, '{')) {
        setErr(r->error, r->errorSize, "Se esperaba objeto en 'processing_rules'");
        return false;
    }
    bool seenMol = false;
    mjSpace(r);
    if (r->p < r->end && *r->p != '}') {
        for (;;) {
            char key[48];
            if (!mjParseString(r, key, sizeof(key)) || !mjTake(r, ':')) return false;
            if (strcmp(key, "molecule") == 0) {
                char molId[MOLECULE_ID_MAX];
                if (!mjParseString(r, molId, sizeof(molId))) return false;
                rule->molecule = MoleculeRegistry_Find(molecules, molId);
                if (rule->molecule == MOLECULE_INVALID_HANDLE) {
                    setErr(r->error, r->errorSize, "Regla metabólica referencia molécula desconocida: '%s'", molId);
                    return false;
                }
                seenMol = true;
            } else if (strcmp(key, "rate") == 0) {
                if (!mjParseFloat(r, &rule->rate)) return false;
            } else if (strcmp(key, "efficiency") == 0) {
                if (!mjParseFloat(r, &rule->efficiency)) return false;
            } else if (strcmp(key, "energy_cost") == 0 || strcmp(key, "digestive_energy_cost") == 0) {
                if (!mjParseFloat(r, &rule->digestiveEnergyCost)) return false;
            } else if (strcmp(key, "waste_fraction") == 0) {
                if (!mjParseFloat(r, &rule->wasteFraction)) return false;
            } else if (strcmp(key, "effects") == 0) {
                if (!mjTake(r, '{')) {
                    setErr(r->error, r->errorSize, "Se esperaba objeto 'effects'");
                    return false;
                }
                mjSpace(r);
                if (r->p < r->end && *r->p != '}') {
                    for (;;) {
                        char ek[48];
                        if (!mjParseString(r, ek, sizeof(ek)) || !mjTake(r, ':')) return false;
                        float val = 0.0f;
                        if (!mjParseFloat(r, &val)) return false;
                        if (strcmp(ek, "energy") == 0) rule->energyYield = val;
                        else if (strcmp(ek, "hydration") == 0) rule->hydrationYield = val;
                        else if (strcmp(ek, "nutrients") == 0) rule->nutrientYield = val;
                        else if (strcmp(ek, "toxicity") == 0) rule->toxicityYield = val;
                        else if (strcmp(ek, "discomfort") == 0) rule->discomfortYield = val;
                        else {
                            setErr(r->error, r->errorSize, "Efecto metabólico desconocido: '%s'", ek);
                            return false;
                        }
                        if (!mjTake(r, ',')) break;
                    }
                }
                if (!mjTake(r, '}')) {
                    setErr(r->error, r->errorSize, "Falta '}' en 'effects'");
                    return false;
                }
            } else if (strcmp(key, "outputs") == 0) {
                if (!mjTake(r, '{')) {
                    setErr(r->error, r->errorSize, "Se esperaba objeto 'outputs'");
                    return false;
                }
                mjSpace(r);
                if (r->p < r->end && *r->p != '}') {
                    for (;;) {
                        char outMolId[MOLECULE_ID_MAX];
                        if (!mjParseString(r, outMolId, sizeof(outMolId)) || !mjTake(r, ':')) return false;
                        float ratio = 0.0f;
                        if (!mjParseFloat(r, &ratio)) return false;
                        MoleculeHandle outHandle = MoleculeRegistry_Find(molecules, outMolId);
                        if (outHandle == MOLECULE_INVALID_HANDLE) {
                            setErr(r->error, r->errorSize, "Producto desconocido en 'outputs': '%s'", outMolId);
                            return false;
                        }
                        if (rule->outputCount >= METABOLIC_RULE_MAX_OUTPUTS) {
                            setErr(r->error, r->errorSize, "Demasiados productos en regla metabólica");
                            return false;
                        }
                        rule->outputs[rule->outputCount++] = (MetabolicRuleOutput){
                            .molecule = outHandle, .massRatio = ratio
                        };
                        if (!mjTake(r, ',')) break;
                    }
                }
                if (!mjTake(r, '}')) {
                    setErr(r->error, r->errorSize, "Falta '}' en 'outputs'");
                    return false;
                }
            } else {
                setErr(r->error, r->errorSize, "Clave desconocida en regla metabólica: '%s'", key);
                return false;
            }
            if (!mjTake(r, ',')) break;
        }
    }
    if (!mjTake(r, '}')) {
        setErr(r->error, r->errorSize, "Falta '}' en regla metabólica");
        return false;
    }
    if (!seenMol) {
        setErr(r->error, r->errorSize, "Regla metabólica sin campo 'molecule'");
        return false;
    }
    return true;
}

bool MetabolismIO_LoadProfileJSON(const char *json, size_t length, MetabolicProfileRegistry *reg,
                                  MetabolicProfileHandle *outHandle, char *error, size_t errorSize) {
    if (!json || length == 0 || !reg || !reg->molecules) {
        setErr(error, errorSize, "Entrada JSON de perfil metabólico inválida");
        return false;
    }
    if (reg->frozen) {
        setErr(error, errorSize, "No se puede cargar perfil en registro congelado");
        return false;
    }

    MetabolicProfile p;
    MetabolicProfile_InitDefault(&p);
    MJsonReader r = {.p = json, .end = json + length, .error = error, .errorSize = errorSize};
    if (!mjTake(&r, '{')) {
        setErr(error, errorSize, "Se esperaba objeto raíz en perfil metabólico");
        return false;
    }
    bool seenVersion = false, seenId = false;
    mjSpace(&r);
    if (r.p < r.end && *r.p != '}') {
        for (;;) {
            char key[64];
            if (!mjParseString(&r, key, sizeof(key)) || !mjTake(&r, ':')) return false;
            if (strcmp(key, "version") == 0) {
                if (!mjParseU32(&r, &p.schemaVersion)) return false;
                seenVersion = true;
            } else if (strcmp(key, "id") == 0) {
                if (!mjParseString(&r, p.id, sizeof(p.id))) return false;
                seenId = true;
            } else if (strcmp(key, "name") == 0) {
                if (!mjParseString(&r, p.name, sizeof(p.name))) return false;
            } else if (strcmp(key, "description") == 0) {
                if (!mjSkipValue(&r, 0)) return false;
            } else if (strcmp(key, "digestive_capacity") == 0) {
                if (!mjParseFloat(&r, &p.digestiveCapacity)) return false;
            } else if (strcmp(key, "maximum_ingestion_rate") == 0) {
                if (!mjParseFloat(&r, &p.maximumIngestionRate)) return false;
            } else if (strcmp(key, "water_absorption_rate") == 0) {
                if (!mjParseFloat(&r, &p.waterAbsorptionRate)) return false;
            } else if (strcmp(key, "waste_elimination_rate") == 0) {
                if (!mjParseFloat(&r, &p.wasteEliminationRate)) return false;
            } else if (strcmp(key, "unprocessable_passage_rate") == 0) {
                if (!mjParseFloat(&r, &p.unprocessablePassageRate)) return false;
            } else if (strcmp(key, "unprocessable_discomfort_factor") == 0) {
                if (!mjParseFloat(&r, &p.unprocessableDiscomfortFactor)) return false;
            } else if (strcmp(key, "toxin_sensitivity") == 0) {
                if (!mjParseFloat(&r, &p.toxinSensitivity)) return false;
            } else if (strcmp(key, "detoxification_rate") == 0) {
                if (!mjParseFloat(&r, &p.detoxificationRate)) return false;
            } else if (strcmp(key, "detoxification_energy_cost") == 0) {
                if (!mjParseFloat(&r, &p.detoxificationEnergyCost)) return false;
            } else if (strcmp(key, "toxicity_threshold") == 0) {
                if (!mjParseFloat(&r, &p.toxicityThreshold)) return false;
            } else if (strcmp(key, "toxicity_damage_rate") == 0) {
                if (!mjParseFloat(&r, &p.toxicityDamageRate)) return false;
            } else if (strcmp(key, "discomfort_decay_rate") == 0) {
                if (!mjParseFloat(&r, &p.discomfortDecayRate)) return false;
            } else if (strcmp(key, "nutrient_maintenance_rate") == 0) {
                if (!mjParseFloat(&r, &p.nutrientMaintenanceRate)) return false;
            } else if (strcmp(key, "nutrient_recovery_bonus") == 0) {
                if (!mjParseFloat(&r, &p.nutrientRecoveryBonus)) return false;
            } else if (strcmp(key, "minimum_reproduction_nutrients") == 0) {
                if (!mjParseFloat(&r, &p.minimumReproductionNutrients)) return false;
            } else if (strcmp(key, "processing_rules") == 0) {
                if (!mjTake(&r, '[')) {
                    setErr(error, errorSize, "Se esperaba arreglo 'processing_rules'");
                    return false;
                }
                mjSpace(&r);
                if (r.p < r.end && *r.p != ']') {
                    for (;;) {
                        if (p.ruleCount >= METABOLIC_PROFILE_MAX_RULES) {
                            setErr(error, errorSize, "Demasiadas reglas en perfil '%s'", p.id);
                            return false;
                        }
                        if (!parseProcessingRule(&r, reg->molecules, &p.rules[p.ruleCount])) return false;
                        ++p.ruleCount;
                        if (!mjTake(&r, ',')) break;
                    }
                }
                if (!mjTake(&r, ']')) {
                    setErr(error, errorSize, "Falta ']' en 'processing_rules'");
                    return false;
                }
            } else {
                setErr(error, errorSize, "Clave desconocida en perfil metabólico: '%s'", key);
                return false;
            }
            if (!mjTake(&r, ',')) break;
        }
    }
    if (!mjTake(&r, '}') || !mjCheckEOF(&r)) return false;
    if (!seenVersion || !seenId) {
        setErr(error, errorSize, "Perfil metabólico requiere 'version' e 'id'");
        return false;
    }
    return MetabolicProfileRegistry_Register(reg, &p, outHandle, error, errorSize);
}

bool MetabolismIO_LoadProfileFile(const char *path, MetabolicProfileRegistry *reg,
                                  MetabolicProfileHandle *outHandle, char *error, size_t errorSize) {
    char *buf = NULL;
    size_t len = 0;
    if (!readFileBounded(path, &buf, &len, error, errorSize)) return false;
    bool ok = MetabolismIO_LoadProfileJSON(buf, len, reg, outHandle, error, errorSize);
    free(buf);
    return ok;
}

/* ========================================================================== */
/* Carga determinista de directorios (catalog.txt o escaneo directo via ftw)  */
/* ========================================================================== */

static int cmpStrPtr(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

static bool hasSuffix(const char *s, const char *suffix) {
    size_t sl = strlen(s), sufl = strlen(suffix);
    return sl >= sufl && strcmp(s + sl - sufl, suffix) == 0;
}

typedef struct FtwCollector {
    const char *suffix;
    char **paths;
    size_t count;
    size_t cap;
    bool oom;
} FtwCollector;

static FtwCollector *g_ftwCollector = NULL;

static int ftwCallback(const char *fpath, const struct stat *sb, int typeflag, struct FTW *ftwbuf) {
    (void)sb;
    FtwCollector *c = g_ftwCollector;
    if (!c || typeflag != FTW_F || ftwbuf->level != 1) return 0;
    const char *base = fpath + ftwbuf->base;
    if (base[0] == '.' || !hasSuffix(base, c->suffix)) return 0;
    if (c->count == c->cap) {
        size_t nc = c->cap ? c->cap * 2u : 16u;
        char **np = realloc(c->paths, nc * sizeof(*np));
        if (!np) { c->oom = true; return 1; }
        c->paths = np;
        c->cap = nc;
    }
    size_t len = strlen(fpath);
    c->paths[c->count] = malloc(len + 1u);
    if (!c->paths[c->count]) { c->oom = true; return 1; }
    memcpy(c->paths[c->count], fpath, len + 1u);
    ++c->count;
    return 0;
}

static bool collectSortedFiles(const char *dirPath, const char *suffix,
                               char ***outPaths, size_t *outCount,
                               char *error, size_t errorSize) {
    /* 1) Si existe catalog.txt (convención de recipes/catalog.txt), respetarlo primero. */
    char catPath[512];
    snprintf(catPath, sizeof(catPath), "%s/catalog.txt", dirPath);
    FILE *cat = fopen(catPath, "r");
    if (cat) {
        char **paths = NULL;
        size_t count = 0, cap = 0;
        char line[256];
        while (fgets(line, sizeof(line), cat)) {
            size_t len = strlen(line);
            while (len > 0 && isspace((unsigned char)line[len - 1u])) line[--len] = '\0';
            if (len == 0 || line[0] == '#') continue;
            if (strstr(line, "..") || !hasSuffix(line, suffix)) {
                fclose(cat);
                for (size_t i = 0; i < count; ++i) free(paths[i]);
                free(paths);
                setErr(error, errorSize, "Entrada inválida en '%s': '%s'", catPath, line);
                return false;
            }
            if (count == cap) {
                size_t nc = cap ? cap * 2u : 16u;
                char **np = realloc(paths, nc * sizeof(*np));
                if (!np) {
                    fclose(cat);
                    for (size_t i = 0; i < count; ++i) free(paths[i]);
                    free(paths);
                    setErr(error, errorSize, "Sin memoria leyendo '%s'", catPath);
                    return false;
                }
                paths = np;
                cap = nc;
            }
            char full[640];
            int n = snprintf(full, sizeof(full), "%s/%s", dirPath, line);
            if (n <= 0 || (size_t)n >= sizeof(full)) {
                fclose(cat);
                for (size_t i = 0; i < count; ++i) free(paths[i]);
                free(paths);
                setErr(error, errorSize, "Ruta demasiado larga en '%s'", catPath);
                return false;
            }
            paths[count] = malloc((size_t)n + 1u);
            if (!paths[count]) {
                fclose(cat);
                for (size_t i = 0; i < count; ++i) free(paths[i]);
                free(paths);
                setErr(error, errorSize, "Sin memoria copiando ruta en '%s'", catPath);
                return false;
            }
            memcpy(paths[count], full, (size_t)n + 1u);
            ++count;
        }
        fclose(cat);
        *outPaths = paths;
        *outCount = count;
        return true;
    }

    /* 2) Sin catalog.txt: escanear directorio directamente mediante nftw y ordenar deterministamente. */
    FtwCollector col = {.suffix = suffix, .paths = NULL, .count = 0, .cap = 0, .oom = false};
    g_ftwCollector = &col;
    int rc = nftw(dirPath, ftwCallback, 8, FTW_PHYS);
    g_ftwCollector = NULL;
    if (rc != 0 || col.oom) {
        for (size_t i = 0; i < col.count; ++i) free(col.paths[i]);
        free(col.paths);
        setErr(error, errorSize, "No se pudo listar directorio '%s'", dirPath);
        return false;
    }
    if (col.count > 1u) {
        qsort(col.paths, col.count, sizeof(*col.paths), cmpStrPtr);
    }
    *outPaths = col.paths;
    *outCount = col.count;
    return true;
}

bool MetabolismIO_LoadSubstancesDirectory(const char *dirPath, SubstanceRegistry *reg,
                                          char *error, size_t errorSize) {
    char **paths = NULL;
    size_t count = 0;
    if (!collectSortedFiles(dirPath, ".substance.json", &paths, &count, error, errorSize))
        return false;
    bool ok = true;
    for (size_t i = 0; i < count && ok; ++i) {
        ok = MetabolismIO_LoadSubstanceFile(paths[i], reg, NULL, error, errorSize);
    }
    for (size_t i = 0; i < count; ++i) free(paths[i]);
    free(paths);
    return ok;
}

bool MetabolismIO_LoadProfilesDirectory(const char *dirPath, MetabolicProfileRegistry *reg,
                                        char *error, size_t errorSize) {
    char **paths = NULL;
    size_t count = 0;
    if (!collectSortedFiles(dirPath, ".metabolism.json", &paths, &count, error, errorSize))
        return false;
    bool ok = true;
    for (size_t i = 0; i < count && ok; ++i) {
        ok = MetabolismIO_LoadProfileFile(paths[i], reg, NULL, error, errorSize);
    }
    for (size_t i = 0; i < count; ++i) free(paths[i]);
    free(paths);
    return ok;
}

void MetabolismDatabase_Freeze(MetabolismDatabase *db) {
    if (!db) return;
    MoleculeRegistry_Freeze(&db->molecules);
    SubstanceRegistry_Freeze(&db->substances);
    MetabolicProfileRegistry_Freeze(&db->profiles);
    db->frozen = true;
}

bool MetabolismIO_LoadDatabase(const char *rootDir, MetabolismDatabase *db,
                               char *error, size_t errorSize) {
    if (!rootDir || !rootDir[0] || !db) {
        setErr(error, errorSize, "Directorio raíz o base metabólica nula");
        return false;
    }
    memset(db, 0, sizeof(*db));
    MoleculeRegistry_Init(&db->molecules);
    SubstanceRegistry_Init(&db->substances, &db->molecules);
    MetabolicProfileRegistry_Init(&db->profiles, &db->molecules);

    char path[512];
    snprintf(path, sizeof(path), "%s/molecules.json", rootDir);
    if (!MetabolismIO_LoadMoleculesFile(path, &db->molecules, error, errorSize)) return false;

    snprintf(path, sizeof(path), "%s/substances", rootDir);
    if (!MetabolismIO_LoadSubstancesDirectory(path, &db->substances, error, errorSize)) return false;

    snprintf(path, sizeof(path), "%s/profiles", rootDir);
    if (!MetabolismIO_LoadProfilesDirectory(path, &db->profiles, error, errorSize)) return false;

    db->loaded = true;
    MetabolismDatabase_Freeze(db);
    return true;
}
