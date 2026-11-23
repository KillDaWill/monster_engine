/**
 * @file Fin.h
 * @brief Módulo anatómico genérico reutilizable para aletas medianas, pareadas y series de pínulas.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_FIN_H
#define CREATURE_FIN_H

#include "Attachment.h"
#include "CreaturePhenotype.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    FIN_NODE_ROOT = 1,
    FIN_NODE_MID  = 2,
    FIN_NODE_TIP  = 3
};

/** @brief Normaliza parámetros de una aleta preservando rangos físicos y numéricos seguros. */
void FinPhenotype_Normalize(FinPhenotype* fin);

/** @brief Interpola dos fenotipos de aleta para desarrollo ontogenético y metamorfosis. */
FinPhenotype FinPhenotype_Interpolate(const FinPhenotype* a, const FinPhenotype* b, float t);

/** @brief Calcula el marco anatómico local (origen, eje de envergadura, eje de cuerda y normal). */
void Fin_ComputeFrame(const FinPhenotype* fin,
                      const AttachmentSlot* slot,
                      Vector3* outOrigin,
                      Vector3* outSpanDir,
                      Vector3* outChordDir,
                      Vector3* outNormalDir);

/** @brief Resuelve las estaciones anatómicas y soporte esquelético de una aleta o serie de pínulas. */
bool Fin_Resolve(const FinPhenotype* fin,
                 uint32_t moduleInstanceId,
                 const AttachmentSlot* slot,
                 AnatomyGraph* graph);

#ifdef __cplusplus
}
#endif

#endif
