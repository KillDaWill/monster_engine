/** @file CreatureRecipeBrumacerno.c
 * @brief Respaldo compilado del mismo documento bovino que carga el catálogo.
 * @details El JSON es la fuente canónica; Make regenera el inicializador.
 */
#include "CreatureRecipes.h"
#include <pthread.h>

static CreatureRecipe brumacernoRecipe =
#include "CreatureRecipeBrumacerno.generated.inc"
;
static pthread_once_t brumacernoOnce=PTHREAD_ONCE_INIT;

/** @brief Aplica la misma normalización que la lectura de documentos externos. */
static void Brumacerno_Init(void) {
    CreaturePhenotype_Normalize(&brumacernoRecipe.seed);
    CreaturePhenotype_Normalize(&brumacernoRecipe.larva);
    CreaturePhenotype_Normalize(&brumacernoRecipe.juvenile);
    CreaturePhenotype_Normalize(&brumacernoRecipe.adult);
}

const CreatureRecipe* CreatureRecipes_Brumacerno(void) {
    pthread_once(&brumacernoOnce,Brumacerno_Init);
    return &brumacernoRecipe;
}
