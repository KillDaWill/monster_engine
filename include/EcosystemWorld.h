/** @file EcosystemWorld.h
 * @brief Propiedad de entidades, recursos, estadísticas y fases de simulación pura C. */
#ifndef ECOSYSTEM_WORLD_H
#define ECOSYSTEM_WORLD_H
#include "SimulationClock.h"
#include "CreatureMind.h"
#include "CreaturePhysiology.h"
#include "CreatureReproduction.h"
#include "EcosystemResources.h"
#include <stdio.h>
#include <stdint.h>
typedef struct EcosystemSpecies {
    unsigned id, category;
    uint32_t recipeId; /**< Receta anatómica persistente asociada a la categoría. */
    bool anatomicalOral; /**< Contacto cervical resuelto desde la receta, también en headless. */
    char name[32];
    BrainProfile brain;
    PhysiologyProfile physiology;
    ReproductionProfile reproduction;
    float speed, radius, oralReach, attackRange, damage, attackCooldown, attackCost;
    float turnRate; /**< Velocidad angular máxima; cero conserva el valor genérico. */
    float edibleBiomass, nutrition;
} EcosystemSpecies;
typedef struct EcosystemEntity {
    AIObjectId id;
    unsigned speciesIndex, generation;
    uint32_t seed;
    CreatureGenome genome;
    BrainProfile brain; /**< Perfil privado resuelto, dirección estable durante su vida. */
    CreatureMind mind;
    CreaturePhysiology physiology;
    CreatureReproduction reproduction;
    CreatureBodyState body;
    Vector3 previousPosition, oralLocal, oralPivot, oralElbow;
    double nextAttack;
    float developmentScale, oralMotion;
    void *visual; /**< Préstamo opcional; el núcleo nunca lo consulta ni libera. */
    struct EcosystemAnatomy* anatomicalBody; /**< Propiedad privada del cuerpo oral, sin recursos gráficos. */
} EcosystemEntity;
typedef struct EcosystemStatistics {
    uint64_t births, deaths, deathCauses[DEATH_CAUSE_COUNT], kills, attacks, matings;
    uint64_t failedBirths, failedSpawns, actionFailures, navigationFailures;
    uint64_t meals, drinks, escapes;
    unsigned population, juveniles, maximumGeneration;
    double foodConsumed, plantConsumed, meatConsumed, waterConsumed, plantBiomass, averageAge, averageHealth;
    uint64_t eventHash; /**< Firma de eventos ordenados para validación determinista. */
} EcosystemStatistics;
typedef struct EcosystemBirth {
    unsigned speciesIndex, generation;
    AIObjectId parents[2];
    CreatureGenome genome;
    Vector3 position;
} EcosystemBirth;
typedef struct EcosystemWorld {
    SimulationClock clock;
    AIWorld observations;
    EcosystemSpecies *species;
    unsigned speciesCount, speciesCapacity;
    EcosystemEntity *entities;
    EcosystemResource *resources;
    EcosystemBirth *birthQueue;
    unsigned *order;
    unsigned capacity, resourceCapacity, birthCount, count;
    uint32_t random, initialSeed;
    bool updating;
    EcosystemStatistics statistics;
    FILE *eventOutput; /**< Préstamo diagnóstico opcional, nunca se cierra desde el núcleo. */
} EcosystemWorld;
/** @brief Perfil declarativo de ejemplo; ninguna mecánica consulta nombres. */
EcosystemSpecies EcosystemSpecies_Default(unsigned id,unsigned category,unsigned diet,unsigned prey,unsigned threats);
/** @brief Reserva arrays acotados configurables. El mundo no se copia por valor. */
bool EcosystemWorld_Init(EcosystemWorld *,unsigned populationCap,unsigned resourceCap,unsigned speciesCap,uint32_t seed,double step,double day);
/** @brief Libera todos los arrays propios; visuales pertenecen al adaptador. */
void EcosystemWorld_Free(EcosystemWorld *);
/** @brief Copia y valida especie; cero indica error, devuelve id lógico de especie. */
unsigned EcosystemWorld_AddSpecies(EcosystemWorld *,EcosystemSpecies);
/** @brief Crea individuo independiente fuera de actualización; cero si no hay capacidad/espacio. */
AIObjectId EcosystemWorld_Spawn(EcosystemWorld *,unsigned speciesId,Vector3,double age,float energy,const CreatureGenome *);
/** @brief Préstamo hasta siguiente mutación; nunca guardar este puntero en memoria de IA. */
EcosystemEntity *EcosystemWorld_Get(EcosystemWorld *,AIObjectId);
/** @brief Solicita muerte idempotente; se retira al final del siguiente tick activo. */
bool EcosystemWorld_RequestDeath(EcosystemWorld *,AIObjectId,CreatureDeathCause);
/** @brief Registra recurso compatible con AIWorld, comprobando geometría y cantidades. */
AIObjectId EcosystemWorld_AddResource(EcosystemWorld *,EcosystemResource,Vector3,float radius);
/** @brief Retira recurso e invalida targets por identidad, sin reutilizar ID. */
bool EcosystemWorld_RemoveResource(EcosystemWorld *,AIObjectId);
/** @brief Consulta inmediata del libro, NULL si fue retirado. */
EcosystemResource *EcosystemWorld_Resource(EcosystemWorld *,AIObjectId);
/** @brief Un tick fijo autoritativo. La pausa congela todos los procesos. */
void EcosystemWorld_Step(EcosystemWorld *);
/** @brief Acumula presentación y procesa hasta presupuesto; conserva deuda. */
unsigned EcosystemWorld_Advance(EcosystemWorld *,double realSeconds);
/** @brief Ataque resuelto: verifica fase, dieta, vida, orientación, alcance y cooldown. */
bool EcosystemWorld_Attack(EcosystemWorld *,AIObjectId attacker,AIObjectId prey);
/** @brief Transfiere sólo con solicitud, contacto estable y disponibilidad actuales. */
float EcosystemWorld_Consume(EcosystemWorld *,AIObjectId creature,AIObjectId resource,float requested);
/** @brief Habilita eventos CSV detallados; NULL desactiva. Archivo prestado por el llamador. */
void EcosystemWorld_SetEventOutput(EcosystemWorld *,FILE *);
/** @brief Estadísticas JSON finales con poblaciones por especie. No cierra el archivo. */
bool EcosystemWorld_WriteJSON(const EcosystemWorld *,FILE *);
/** @brief Fila CSV de historia; header controla la cabecera inicial. */
bool EcosystemWorld_WriteCSV(const EcosystemWorld *,FILE *,bool header);
#endif
