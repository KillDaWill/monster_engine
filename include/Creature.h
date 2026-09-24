/** @file Creature.h
 * @brief Expresión de recetas mediante resolutores modulares.
 */
#ifndef CREATURE_H
#define CREATURE_H
#include "CreatureRecipes.h"
struct Monster;
/** @brief Resuelve blueprint y geometría. @return Éxito. */
bool Creature_ResolveAnatomy(const CreatureRecipe*,const CreaturePhenotype*,AnatomyGraph*);
/** @brief Actualiza anatomía y apariencia preservando animación. @return Éxito. */
bool Creature_ResolveAppearance(struct Monster*,const CreatureRecipe*,const CreaturePhenotype*);
/** @brief Construye criatura y rig. @return Éxito. */
bool Creature_BuildMonster(struct Monster*,const CreatureRecipe*,const CreaturePhenotype*);
/** @brief Invierte la curva de escala entre extremos. @return Edad normalizada. */
float Creature_AgeFromScaleBetween(float scale,float initialScale,float finalScale);
/** @brief Firma adimensional calculada a partir de anatomía resuelta. */
typedef struct MorphologicalSignature {
    float bodyLengthWidthRatio, bodyHeightWidthRatio;
    float headBodyScale, headLengthWidthRatio;
    float forelimbBodyRatio, hindlimbBodyRatio, hindForeRatio, footBodyRatio;
    float tailBodyLengthRatio, tailBaseBodyRatio, eyeHeadRatio;
    float maxOrnamentHeightBodyRatio, distalTailCurvature, forelimbThicknessBodyRatio;
} MorphologicalSignature;
/** @brief Calcula proporciones observables sin usar identidad de variante. */
MorphologicalSignature Creature_MorphologicalSignature(const struct Monster* monster);
/** @brief Comprueba uniones cervical/caudal y asiento ocular; devuelve máscara de errores. */
unsigned Creature_ValidateGeometry(const struct Monster* monster);
#endif
