/**
 * @file SubstanceRegistry.h
 * @brief Registro de sustancias definidas como mezclas moleculares compiladas.
 *
 * Separa la composición química, las propiedades físicas de acceso, la presentación visual
 * y la firma sensorial cacheada. Las fracciones deben ser no negativas y sumar 1 dentro de
 * @ref SUBSTANCE_FRACTION_SUM_TOLERANCE.
 */
#ifndef SUBSTANCE_REGISTRY_H
#define SUBSTANCE_REGISTRY_H

#include "MoleculeRegistry.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define SUBSTANCE_SCHEMA_VERSION 1u
#define SUBSTANCE_MAX_COUNT 128u
#define SUBSTANCE_MAX_COMPONENTS 16u
#define SUBSTANCE_ID_MAX 48u
#define SUBSTANCE_NAME_MAX 64u
#define SUBSTANCE_INVALID_HANDLE UINT16_MAX
#define SUBSTANCE_FRACTION_SUM_TOLERANCE 1e-3f

/** @brief Manejador compacto de sustancia compilada. */
typedef uint16_t SubstanceHandle;

/** @brief Fase física para interacción mecánica (independiente de la composición química). */
typedef enum SubstancePhase {
    SUBSTANCE_PHASE_SOLID = 0,
    SUBSTANCE_PHASE_LIQUID = 1,
    SUBSTANCE_PHASE_SEMI_SOLID = 2
} SubstancePhase;

/** @brief Modelo de presentación visual desacoplado de la química y el metabolismo. */
typedef enum SubstancePresentationModel {
    SUBSTANCE_MODEL_VEGETATION = 0,
    SUBSTANCE_MODEL_WATER = 1,
    SUBSTANCE_MODEL_CARCASS = 2,
    SUBSTANCE_MODEL_FRUIT = 3,
    SUBSTANCE_MODEL_GENERIC = 4
} SubstancePresentationModel;

/** @brief Componente molecular compilado dentro de una sustancia o mezcla dinámica. */
typedef struct SubstanceComponent {
    MoleculeHandle molecule;
    float fraction; /**< Fracción másica en [0, 1]. */
} SubstanceComponent;

/** @brief Definición compilada de una sustancia consumible. */
typedef struct SubstanceDef {
    SubstanceHandle handle;
    uint32_t schemaVersion;
    char id[SUBSTANCE_ID_MAX];
    char name[SUBSTANCE_NAME_MAX];
    SubstanceComponent components[SUBSTANCE_MAX_COMPONENTS];
    uint16_t componentCount;
    SubstancePhase phase;
    float hardness; /**< Dureza física en [0, 1]. */
    SubstancePresentationModel model;
    float color[3]; /**< RGB [0, 1] para presentación visual. */
    bool hasSensoryOverride;
    SensorySignature sensoryOffset; /**< Sesgo sensorial opcional por presentación/superficie. */
    SensorySignature cachedSignature;
    bool valid;
} SubstanceDef;

/** @brief Registro acotado de sustancias compiladas contra un @ref MoleculeRegistry. */
typedef struct SubstanceRegistry {
    const MoleculeRegistry *molecules; /**< Referencia prestada que debe sobrevivir al registro. */
    SubstanceDef items[SUBSTANCE_MAX_COUNT];
    uint16_t count;
    bool frozen;
    uint64_t fingerprint;
} SubstanceRegistry;

/** @brief Nombres textuales y parsers de fase física y modelo visual. */
const char *SubstancePhase_Name(SubstancePhase phase);
bool SubstancePhase_FromName(const char *name, SubstancePhase *out);
const char *SubstancePresentationModel_Name(SubstancePresentationModel model);
bool SubstancePresentationModel_FromName(const char *name, SubstancePresentationModel *out);

/** @brief Inicializa el registro de sustancias asociado al registro de moléculas dado. */
void SubstanceRegistry_Init(SubstanceRegistry *reg, const MoleculeRegistry *molecules);

/** @brief Calcula la firma sensorial observable de una mezcla arbitraria de componentes. */
bool SubstanceRegistry_ComputeSignature(const MoleculeRegistry *molecules,
                                        const SubstanceComponent *components,
                                        unsigned componentCount,
                                        const SensorySignature *optionalOffset,
                                        SensorySignature *outSignature);

/** @brief Valida y registra una sustancia, compilando su firma sensorial cacheada. */
bool SubstanceRegistry_Register(SubstanceRegistry *reg, const SubstanceDef *def,
                                SubstanceHandle *outHandle, char *error, size_t errorSize);

/** @brief Congela el registro y fija su huella canónica FNV-1a. */
uint64_t SubstanceRegistry_Freeze(SubstanceRegistry *reg);

/** @brief Resuelve un ID textual a su manejador compacto; SUBSTANCE_INVALID_HANDLE si falta. */
SubstanceHandle SubstanceRegistry_Find(const SubstanceRegistry *reg, const char *id);

/** @brief Consulta O(1) por manejador compacto; NULL si es inválido. */
const SubstanceDef *SubstanceRegistry_Get(const SubstanceRegistry *reg, SubstanceHandle handle);

/** @brief Calcula la huella canónica FNV-1a de todas las sustancias registradas. */
uint64_t SubstanceRegistry_ComputeFingerprint(const SubstanceRegistry *reg);

#ifdef __cplusplus
}
#endif

#endif /* SUBSTANCE_REGISTRY_H */
