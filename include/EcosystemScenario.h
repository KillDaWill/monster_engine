/** @file EcosystemScenario.h
 * @brief Configuración reproducible de condiciones iniciales; sin eventos programados. */
#ifndef ECOSYSTEM_SCENARIO_H
#define ECOSYSTEM_SCENARIO_H
#include "EcosystemWorld.h"
typedef struct EcosystemConfig {
    uint32_t seed;
    unsigned herbivores,carnivores,plants,capacity;
    double step,day,days;
    const char *scenario;
} EcosystemConfig;
/** @brief Condiciones de referencia, editables antes de crear el mundo. */
EcosystemConfig EcosystemConfig_Default(void);
/** @brief Aplica escenario A..E o nombre. Devuelve false si se desconoce. */
bool EcosystemConfig_Scenario(EcosystemConfig *,const char *);
/** @brief Construye terreno plano, dos aguas, plantas e individuos. Limpia ante error. */
bool EcosystemScenario_Create(EcosystemWorld *,const EcosystemConfig *);
#endif
