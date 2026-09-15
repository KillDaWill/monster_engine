/** @file LizardMorph.h
 * @brief Compatibilidad del ager con el deformador anatómico genérico.
 */
#ifndef MONSTER_LIZARD_MORPH_H
#define MONSTER_LIZARD_MORPH_H
#include "AnatomyDeformer.h"
typedef AnatomyDeformer LizardMorph;
typedef AnatomyDeformerBinding LizardMorphBinding;
/** @brief Crea deformador compatible. */
LizardMorph* LizardMorph_Create(void);
/** @brief Libera deformador y buffers. */
void LizardMorph_Free(LizardMorph* morph);
/** @brief Vincula la malla de reposo. */
bool LizardMorph_Bind(LizardMorph* morph,const Mesh* mesh,const AnatomyGraph* rest);
/** @brief Aplica un cambio morfológico. */
bool LizardMorph_Deform(const LizardMorph* morph,const AnatomyGraph* graph,Mesh* mesh);
/** @brief Consulta vinculación. */
bool LizardMorph_IsBound(const LizardMorph* morph);
/** @brief Número de vértices vinculados. */
size_t LizardMorph_GetVertexCount(const LizardMorph* morph);
/** @brief Copia vinculaciones y buffers. */
#define LizardMorph_Copy AnatomyDeformer_Copy
#endif
