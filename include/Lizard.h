/**
 * @file Lizard.h
 * @brief Fenotipo semántico y factoría compartida de lagarto terrestre.
 */

#ifndef MONSTER_LIZARD_H
#define MONSTER_LIZARD_H

#include "Anatomy.h"
#include "Head.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

struct Monster;

/** @struct LizardPhenotype
 * @brief Controles semánticos continuos de un lagarto terrestre generalizado.
 */
typedef struct LizardPhenotype {
    float totalScale; /**< Escala uniforme del animal. */
    float neckLength; /**< Longitud relativa del cuello. */
    float neckWidth; /**< Anchura relativa del cuello. */
    float shoulderWidth; /**< Anchura de la cintura pectoral. */
    float thoraxWidth; /**< Anchura máxima torácica. */
    float thoraxHeight; /**< Altura máxima torácica. */
    float abdomenWidth; /**< Anchura abdominal. */
    float abdomenHeight; /**< Altura abdominal. */
    float pelvicWidth; /**< Anchura de la región pélvica. */
    float pelvicHeight; /**< Altura de la región pélvica. */
    float trunkLength; /**< Longitud conjunta del tronco. */
    float bodyFlattening; /**< Compresión dorsoventral independiente. */
    float forelimbLength; /**< Longitud semántica de miembros anteriores. */
    float forelimbThickness; /**< Robustez de miembros anteriores. */
    float hindlimbLength; /**< Longitud semántica de miembros posteriores. */
    float hindlimbThickness; /**< Robustez de miembros posteriores. */
    float appendageDevelopment; /**< Emergencia ontogenética de miembros y dedos [0,1]. */
    float manualDigitLengths[5]; /**< Longitudes relativas I-V; permite dominancia III o IV. */
    float tailLength; /**< Longitud de la cola desde la pelvis. */
    float tailBaseWidth; /**< Radio transversal de la base caudal. */
    float tailBaseHeight; /**< Radio vertical de la base caudal. */
    float tailTipWidth; /**< Radio transversal distal. */
    float tailTipHeight; /**< Radio vertical distal. */
    float tailTaperCurve; /**< Exponente del ahusamiento caudal. */
    float colorMaturity; /**< Maduración cromática del preset, independiente de la pose. */
    float pigmentation; /**< Aparición ontogenética de pigmento corporal [0,1]. */
    float cephalicDevelopment; /**< Diferenciación de cabeza, ojos y sistema oral [0,1]. */
    float eyeProportion; /**< Reserva semántica de proporción ocular ontogenética. */
    HeadPhenotype head; /**< Fenotipo cefálico poseído. */
} LizardPhenotype;

/** @return Fenotipo juvenil válido con la topología compartida. */
LizardPhenotype LizardPreset_Juvenile(void);
/** @return Fenotipo larvario vermiforme, blanco y sin apéndices visibles. */
LizardPhenotype LizardPreset_Larva(void);
/** @return Fenotipo adulto válido con la topología compartida. */
LizardPhenotype LizardPreset_Adult(void);
/** Interpola dos fenotipos en espacio semántico y normaliza el resultado. */
LizardPhenotype LizardPhenotype_Interpolate(const LizardPhenotype* juvenile,
                                             const LizardPhenotype* adult,
                                             float age);
/** Normaliza todos los controles a rangos geométricamente seguros. */
void LizardPhenotype_Normalize(LizardPhenotype* phenotype);
/** Resuelve el fenotipo en estaciones y conexiones explícitas. */
bool Lizard_ResolveAnatomy(const LizardPhenotype* phenotype, AnatomyGraph* graph);
/** Instala cabeza, ojos, boca, paleta y grafo anatómico en un Monster. */
bool Lizard_BuildMonster(struct Monster* monster, const LizardPhenotype* phenotype);
/** Calcula la edad ontogenética inversa correspondiente a una escala corporal total. */
float Lizard_AgeFromScale(float scale);
/** Calcula la edad inversa entre dos escalas fenotípicas concretas. */
float Lizard_AgeFromScaleBetween(float scale, float initialScale, float finalScale);

#ifdef __cplusplus
}
#endif

#endif
