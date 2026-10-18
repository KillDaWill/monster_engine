/** @file NavigationProgress.h
 * @brief Llegada y detección de persecución circular de un objetivo de navegación.
 */
#ifndef NAVIGATION_PROGRESS_H
#define NAVIGATION_PROGRESS_H
#include "Vector.h"
#include <stdbool.h>
/** @brief Estado por objetivo; inicializar con {0} y reiniciar al cambiarlo. */
typedef struct NavigationProgress {
    bool initialized;
    float bestDistance,elapsed,withoutProgress,previousYaw,winding;
} NavigationProgress;
/** @brief Resultado de seguimiento del objetivo. */
typedef enum NavigationGoalStatus { NAVIGATION_ADVANCING, NAVIGATION_ARRIVED, NAVIGATION_STALLED } NavigationGoalStatus;
/** @brief Sigue distancia mínima al objetivo y giro acumulado; llegar prima sobre el estancamiento.
 * @param arrivalRadius Radio de llegada acorde a velocidad y radio de giro.
 * @param dt Tiempo de simulación; cero permite consultar llegada sin avanzar el monitor.
 * @return Llegada, progreso o necesidad de elegir otro objetivo. */
NavigationGoalStatus NavigationProgress_Update(NavigationProgress*,Vector3 position,Vector3 goal,
    float yaw,float speed,float turnRate,float arrivalRadius,float dt);
#endif
