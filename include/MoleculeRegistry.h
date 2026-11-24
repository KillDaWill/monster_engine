/**
 * @file MoleculeRegistry.h
 * @brief Registro extensible y versionado de componentes moleculares con firmas sensoriales.
 *
 * Los componentes moleculares representan moléculas simplificadas o familias de compuestos.
 * Su identidad y firma sensorial observable son independientes del perfil metabólico de
 * cualquier especie. Durante la simulación el registro se congela y los identificadores
 * textuales se resuelven una sola vez a manejadores compactos @ref MoleculeHandle.
 */
#ifndef MOLECULE_REGISTRY_H
#define MOLECULE_REGISTRY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MOLECULE_SCHEMA_VERSION 1u
#define MOLECULE_MAX_COUNT 64u
#define MOLECULE_ID_MAX 48u
#define MOLECULE_NAME_MAX 64u
#define MOLECULE_DESC_MAX 160u
#define MOLECULE_INVALID_HANDLE UINT16_MAX

#define SENSORY_SIGNATURE_CHANNELS 6u

/** @brief Manejador interno compacto de una molécula registrada (sin cadenas en rutas calientes). */
typedef uint16_t MoleculeHandle;

/** @brief Canales observables aproximados de olor, sabor y textura química [0, 1]. */
typedef enum SensoryChannel {
    SENSORY_MOISTURE = 0,
    SENSORY_PLANT_AROMA = 1,
    SENSORY_ORGANIC_AROMA = 2,
    SENSORY_LIPID_AROMA = 3,
    SENSORY_SWEETNESS = 4,
    SENSORY_DECAY_AROMA = 5
} SensoryChannel;

/** @brief Firma sensorial observable compacta, independiente de compatibilidad metabólica. */
typedef struct SensorySignature {
    float channels[SENSORY_SIGNATURE_CHANNELS];
} SensorySignature;

/** @brief Definición inmutable de un componente molecular registrado. */
typedef struct MoleculeDef {
    MoleculeHandle handle;
    char id[MOLECULE_ID_MAX];
    char name[MOLECULE_NAME_MAX];
    char description[MOLECULE_DESC_MAX];
    SensorySignature sensory;
    bool valid;
} MoleculeDef;

/** @brief Registro acotado y congelable de componentes moleculares. */
typedef struct MoleculeRegistry {
    uint32_t schemaVersion;
    MoleculeDef items[MOLECULE_MAX_COUNT];
    uint16_t count;
    bool frozen;
    uint64_t fingerprint;
} MoleculeRegistry;

/** @brief Nombre textual estable de un canal sensorial observable. */
const char *SensoryChannel_Name(SensoryChannel channel);

/** @brief Valida que todos los canales de una firma sensorial sean finitos y en [0, 1]. */
bool SensorySignature_Validate(const SensorySignature *sig);

/** @brief Inicializa un registro vacío en estado mutable. */
void MoleculeRegistry_Init(MoleculeRegistry *reg);

/** @brief Valida sintácticamente un identificador estable de molécula/sustancia/perfil. */
bool MoleculeRegistry_ValidateId(const char *id);

/** @brief Registra una molécula nueva si el registro no está congelado y el ID es único. */
bool MoleculeRegistry_Register(MoleculeRegistry *reg, const MoleculeDef *def,
                               MoleculeHandle *outHandle, char *error, size_t errorSize);

/** @brief Congela el registro para uso concurrente de sólo lectura y fija su huella FNV-1a. */
uint64_t MoleculeRegistry_Freeze(MoleculeRegistry *reg);

/** @brief Resuelve un identificador estable a su manejador compacto; devuelve MOLECULE_INVALID_HANDLE si falta. */
MoleculeHandle MoleculeRegistry_Find(const MoleculeRegistry *reg, const char *id);

/** @brief Consulta O(1) de la definición por manejador compacto; NULL si es inválido. */
const MoleculeDef *MoleculeRegistry_Get(const MoleculeRegistry *reg, MoleculeHandle handle);

/** @brief Calcula la huella canónica FNV-1a de 64 bits del registro completo. */
uint64_t MoleculeRegistry_ComputeFingerprint(const MoleculeRegistry *reg);

#ifdef __cplusplus
}
#endif

#endif /* MOLECULE_REGISTRY_H */
