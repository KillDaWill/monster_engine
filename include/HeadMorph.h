/** @file HeadMorph.h
 * @brief Deformación cefálica continua desde los volúmenes semánticos resueltos.
 */
#ifndef MONSTER_HEAD_MORPH_H
#define MONSTER_HEAD_MORPH_H
#include "Head.h"
#include "Mesh.h"
/** @brief Vinculación dispersa de los vértices cefálicos; independiente de especie. */
typedef struct HeadMorph HeadMorph;
/** @return Deformador vacío o NULL. */
HeadMorph* HeadMorph_Create(void);
/** @brief Libera bases y vinculaciones. */
void HeadMorph_Free(HeadMorph* morph);
/** @brief Copia las vinculaciones y el marco de referencia cefálico. */
bool HeadMorph_Copy(HeadMorph* dst, const HeadMorph* src);
/** @brief Fija las influencias cefálicas al publicar un keyframe. */
bool HeadMorph_Bind(HeadMorph* morph,const Mesh* mesh,const HeadAnatomy* head,Vector3 origin);
/** @brief Aplica el fenotipo resuelto después de la deformación corporal. */
bool HeadMorph_Deform(const HeadMorph* morph,Mesh* mesh,const HeadAnatomy* head,Vector3 origin);
/** @brief Expresa la superficie en la jaula canónica reutilizando sus influencias. */
bool HeadMorph_MapSurface(const HeadMorph* morph,Mesh* mesh,const HeadAnatomy* head,Vector3 origin);
#endif
