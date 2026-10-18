/** @file SimulationClock.h
 * @brief Reloj fijo independiente de presentación y tiempo biológico. */
#ifndef SIMULATION_CLOCK_H
#define SIMULATION_CLOCK_H
#include <stdbool.h>
#include <stdint.h>
typedef struct SimulationClock {
    double seconds, accumulator, fixedStep, secondsPerDay, speed;
    uint64_t tick;
    unsigned maxStepsPerFrame;
    bool paused, behind;
} SimulationClock;
/** @brief Inicializa parámetros finitos positivos; false ante configuración inválida. */
bool SimulationClock_Init(SimulationClock *, double step, double day, unsigned budget);
/** @brief Acumula tiempo de presentación; pausa no acumula deuda nueva. */
void SimulationClock_Accumulate(SimulationClock *, double realSeconds);
/** @brief Consume un paso pendiente, sin descartar deuda; el llamador limita el bucle. */
bool SimulationClock_Pop(SimulationClock *);
/** @brief Avanza exactamente un tick, salvo pausa. Para ejecución sin presentación. */
bool SimulationClock_Tick(SimulationClock *);
/** @brief Días completos y fracción horaria del día ecológico. */
double SimulationClock_Days(const SimulationClock *);
/** @brief Fracción [0,1) del día actual. */
double SimulationClock_TimeOfDay(const SimulationClock *);
#endif
