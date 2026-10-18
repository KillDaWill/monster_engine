/** @file CreaturePhysiology.h
 * @brief Reservas autoritativas y supervivencia sin representación visual. */
#ifndef CREATURE_PHYSIOLOGY_H
#define CREATURE_PHYSIOLOGY_H
#include <stdbool.h>
typedef enum CreatureDeathCause { DEATH_NONE, DEATH_STARVATION, DEATH_DEHYDRATION,
    DEATH_PREDATION, DEATH_AGE, DEATH_REMOVED, DEATH_CAUSE_COUNT } CreatureDeathCause;
typedef enum CreatureLifeStage { LIFE_JUVENILE, LIFE_ADULT, LIFE_OLD, LIFE_DEAD } CreatureLifeStage;
typedef struct PhysiologyProfile {
    float basalCost, hydrationCost, movementCost, starvationDamage, dehydrationDamage;
    float recoveryRate, fatigueRate, restRate;
    double maturityAge, oldAge, maximumAge;
} PhysiologyProfile;
typedef struct CreaturePhysiology {
    float energy, hydration, fatigue, health;
    double age;
    CreatureLifeStage stage;
    CreatureDeathCause cause;
} CreaturePhysiology;
/** @brief Perfil normalizado en reservas [0,1] por segundo simulado. */
PhysiologyProfile CreaturePhysiology_Default(void);
/** @brief Comprueba tasas, edades y límites antes de registrar un perfil. */
bool CreaturePhysiology_ValidateProfile(const PhysiologyProfile *);
/** @brief Estado sano; el siguiente paso resuelve la etapa según perfil. */
CreaturePhysiology CreaturePhysiology_Create(double age,float energy);
/** @brief Metabolismo, recuperación y daño progresivo; no revive muertos. */
void CreaturePhysiology_Update(CreaturePhysiology *,const PhysiologyProfile *,float dt,
    float speed,float efficiency,bool resting);
/** @brief Aplica transferencia confirmada; cantidades no positivas no nutren. */
void CreaturePhysiology_Feed(CreaturePhysiology *,float nutrition);
/** @brief Aplica agua confirmada y acotada. */
void CreaturePhysiology_Drink(CreaturePhysiology *,float amount);
#endif
