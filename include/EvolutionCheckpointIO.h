/**
 * @file EvolutionCheckpointIO.h
 * @brief Guardado atómico y recuperación verificada por huellas de checkpoints de Brain Evolution Lab.
 */
#ifndef EVOLUTION_CHECKPOINT_IO_H
#define EVOLUTION_CHECKPOINT_IO_H

#include "BrainEvolutionLab.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EVOLUTION_CHECKPOINT_VERSION 1u

/**
 * @brief Guarda de forma atómica (`archivo.tmp` -> `rename`) el estado completo de
 *        @ref BrainEvolutionLab en frontera de generación, incluyendo población, élites,
 *        PRNG, historial, currículo y huellas de moléculas/sustancias/perfil/especie/entorno.
 */
bool EvolutionCheckpoint_Save(const BrainEvolutionLab *lab, const char *path,
                              char *error, size_t errorSize);

/**
 * @brief Recupera un checkpoint validando compatibilidad estricta de huellas de definiciones.
 *
 * Si las huellas no coinciden y @p allowBranch es false, rechaza la reanudación sin
 * corromper el estado del laboratorio.
 */
bool EvolutionCheckpoint_Load(BrainEvolutionLab *lab, const char *path, bool allowBranch,
                              char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif

#endif /* EVOLUTION_CHECKPOINT_IO_H */
