/** @file AnimatedVisual.h
 * @brief Vinculación de una generación visual publicada a la animación; propiedad separada.
 */
#ifndef MONSTER_ANIMATED_VISUAL_H
#define MONSTER_ANIMATED_VISUAL_H
#include "MonsterVisual.h"
#include "MonsterAnimation.h"
#include "AnatomyDeformer.h"
typedef struct AnimatedVisual {
    AnatomyDeformer* body;
    Mesh headBase;
    MonsterVisualEye* eyeBases;
    size_t eyeCount;
    SkeletonPose restPose;
    uint64_t generation,restFingerprint,rigGeneration;
    bool bound;
} AnimatedVisual;
/** @brief Inicializa con ceros antes de usar; libera exclusivamente los bindings/bases. */
void AnimatedVisual_Free(AnimatedVisual* binding);
/** @brief Vincula una generación sin deformar. Llamar después de RebuildNow, nunca sobre pose previa. */
bool AnimatedVisual_Bind(AnimatedVisual* binding,const MonsterVisual* visual,const Monster* monster);
/** @brief Aplica pose a cuerpo, cabeza, ojos y puente mandibular sin reconstrucciones. */
bool AnimatedVisual_Deform(const AnimatedVisual* binding,MonsterVisual* visual,const Monster* monster);
#endif
