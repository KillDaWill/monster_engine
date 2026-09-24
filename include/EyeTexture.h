/** @file EyeTexture.h
 * @brief Textura RGBA determinista para iris, limbo, pupila y esclerótica.
 */
#ifndef MONSTER_EYE_TEXTURE_H
#define MONSTER_EYE_TEXTURE_H
#include "Eye.h"
#include <stddef.h>
#include <stdint.h>

/** Calcula una huella independiente del padding de la estructura Eye. */
uint64_t EyeTexture_Fingerprint(const Eye* eye);
/** Genera una imagen RGBA8 cuadrada, apta para proyección esférica frontal. */
bool EyeTexture_Generate(const Eye* eye, unsigned size, unsigned char* rgba, size_t capacity);

#endif
