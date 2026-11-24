/**
 * @file MetabolismIO.h
 * @brief Carga y validación estricta JSON de moléculas, sustancias y perfiles metabólicos.
 *
 * No realiza ningún procesamiento JSON durante la simulación; todos los datos externos
 * se validan, compilan a manejadores compactos y congelan antes de iniciar el mundo.
 */
#ifndef METABOLISM_IO_H
#define METABOLISM_IO_H

#include "MoleculeRegistry.h"
#include "SubstanceRegistry.h"
#include "MetabolicProfile.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define METABOLISM_MAX_FILE_BYTES (512u * 1024u)

/** @brief Contexto congelado e inmutable con los tres registros metabólicos cargados. */
typedef struct MetabolismDatabase {
    MoleculeRegistry molecules;
    SubstanceRegistry substances;
    MetabolicProfileRegistry profiles;
    bool loaded;
    bool frozen;
} MetabolismDatabase;

/** @brief Carga y valida un catálogo de moléculas desde cadena JSON. */
bool MetabolismIO_LoadMoleculesJSON(const char *json, size_t length, MoleculeRegistry *reg,
                                    char *error, size_t errorSize);

/** @brief Carga un archivo `molecules.json` en el registro dado. */
bool MetabolismIO_LoadMoleculesFile(const char *path, MoleculeRegistry *reg,
                                    char *error, size_t errorSize);

/** @brief Carga y valida una sustancia (`*.substance.json`) desde cadena JSON. */
bool MetabolismIO_LoadSubstanceJSON(const char *json, size_t length, SubstanceRegistry *reg,
                                    SubstanceHandle *outHandle, char *error, size_t errorSize);

/** @brief Carga un archivo `*.substance.json` en el registro de sustancias. */
bool MetabolismIO_LoadSubstanceFile(const char *path, SubstanceRegistry *reg,
                                    SubstanceHandle *outHandle, char *error, size_t errorSize);

/** @brief Carga todos los archivos `*.substance.json` ordenados deterministamente en un directorio. */
bool MetabolismIO_LoadSubstancesDirectory(const char *dirPath, SubstanceRegistry *reg,
                                          char *error, size_t errorSize);

/** @brief Carga y compila un perfil metabólico (`*.metabolism.json`) desde cadena JSON. */
bool MetabolismIO_LoadProfileJSON(const char *json, size_t length, MetabolicProfileRegistry *reg,
                                  MetabolicProfileHandle *outHandle, char *error, size_t errorSize);

/** @brief Carga un archivo `*.metabolism.json` en el registro de perfiles. */
bool MetabolismIO_LoadProfileFile(const char *path, MetabolicProfileRegistry *reg,
                                  MetabolicProfileHandle *outHandle, char *error, size_t errorSize);

/** @brief Carga todos los archivos `*.metabolism.json` ordenados deterministamente en un directorio. */
bool MetabolismIO_LoadProfilesDirectory(const char *dirPath, MetabolicProfileRegistry *reg,
                                        char *error, size_t errorSize);

/**
 * @brief Carga completa de una base de datos metabólica desde un directorio raíz
 *        (espera `molecules.json`, `substances/` y `profiles/`) y la congela.
 */
bool MetabolismIO_LoadDatabase(const char *rootDir, MetabolismDatabase *db,
                               char *error, size_t errorSize);

/** @brief Congela los tres registros de la base de datos. */
void MetabolismDatabase_Freeze(MetabolismDatabase *db);

#ifdef __cplusplus
}
#endif

#endif /* METABOLISM_IO_H */
