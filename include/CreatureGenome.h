/** @file CreatureGenome.h
 * @brief Herencia de rasgos semánticos acotados, independiente de recetas. */
#ifndef CREATURE_GENOME_H
#define CREATURE_GENOME_H
#include <stdint.h>
typedef struct CreatureGenome {
    float size, speed, efficiency, perception, pigment, morphology, maturation;
} CreatureGenome;
/** @brief Genoma neutro con multiplicadores unitarios. */
CreatureGenome CreatureGenome_Default(void);
/** @brief PRNG determinista sin estado global. */
float CreatureGenome_Random(uint32_t *);
/** @brief Recombinación biparental y mutación; salida siempre en límites físicos. */
CreatureGenome CreatureGenome_Inherit(const CreatureGenome *,const CreatureGenome *,
    uint32_t *,float probability,float amplitude);
/** @brief Valida todos los rasgos y límites. */
int CreatureGenome_Validate(const CreatureGenome *);
#endif
