/** @file Tail.h
 * @brief Resolución de anatomía modular reutilizable.
 */
#ifndef CREATURE_TAIL_H
#define CREATURE_TAIL_H
#include "Attachment.h"
#include "CreaturePhenotype.h"

/** @brief Indica que la cola sigue su propia curva fuera del barrido del tronco. */
bool Tail_UsesIndependentCurve(const TailPhenotype* phenotype);

/** @brief Publica geometría y metadatos sin dependencias de especie. @return Éxito. */
bool Tail_Resolve(const TailPhenotype* phenotype,uint32_t moduleInstanceId,const AttachmentSlot* slot,AnatomyGraph* graph);
#endif
