/**
 * @file NeuralBrainIO.h
 * @brief Serialización y validación estricta del formato portable *.brain.json.
 *
 * Independiente de recetas anatómicas, del layout binario de structs y de OpenGL/SDL.
 * Usa precisión de 9 dígitos significativos (%.9g) para recuperación exacta de float32.
 */
#ifndef NEURAL_BRAIN_IO_H
#define NEURAL_BRAIN_IO_H

#include "NeuralGenome.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Tamaño máximo permitido al leer un archivo *.brain.json (512 KiB). */
#define NEURAL_BRAIN_MAX_FILE_BYTES (512u * 1024u)

/**
 * @brief Serializa un genoma neuronal validado a JSON legible en un búfer de memoria.
 * @param genome Genoma de origen (debe pasar NeuralGenome_Validate).
 * @param out Búfer de destino (puede ser NULL para consultar el tamaño requerido).
 * @param outSize Tamaño del búfer en bytes.
 * @param writtenOut Bytes escritos sin incluir el terminador nulo (opcional).
 * @return true si el genoma es válido y cupo entero en el búfer (o si out==NULL).
 */
bool NeuralBrain_SerializeJSON(const NeuralGenome *genome, char *out, size_t outSize,
                               size_t *writtenOut);

/**
 * @brief Deserializa y valida estrictamente un documento JSON en un genoma temporal.
 *
 * Ante cualquier error de sintaxis, versión, arquitectura, esquema, dimensiones,
 * NaN/Inf o rango, devuelve false y deja @p outGenome sin modificar.
 */
bool NeuralBrain_DeserializeJSON(const char *json, size_t length, NeuralGenome *outGenome,
                                 char *error, size_t errorSize);

/**
 * @brief Carga y valida un archivo *.brain.json sin alterar @p outGenome si falla.
 */
bool NeuralBrain_LoadFile(const char *path, NeuralGenome *outGenome,
                          char *error, size_t errorSize);

/**
 * @brief Guarda un genoma neuronal validado en un archivo *.brain.json.
 * @param overwrite Si es false, falla si el archivo de destino ya existe.
 */
bool NeuralBrain_SaveFile(const NeuralGenome *genome, const char *path, bool overwrite,
                          char *error, size_t errorSize);

#ifdef __cplusplus
}
#endif

#endif /* NEURAL_BRAIN_IO_H */
