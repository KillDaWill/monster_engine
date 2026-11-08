/**
 * @file LongHairScheduler.h
 * @brief Scheduler determinista y con propiedad explícita para las fases paralelas del pelo largo.
 *
 * El scheduler envuelve un `SDFSamplingPool` **privado y persistente**. Nunca comparte el
 * `currentWorkFn/currentContext/jobGeneration` del pool global usado por el mesher o el pipeline
 * de morph, de modo que no puede corromperlo ni entrar en conflicto con él.
 *
 * Contrato principal:
 * - Una llamada a `LongHairScheduler_ParallelFor` o `LongHairScheduler_RunPhases` es síncrona:
 *   cuando retorna, ningún worker conserva el callback, el contexto ni las fases prestadas.
 * - El propietario debe sincronizar la vida útil del objeto: `Destroy` sólo admite un job
 *   ya activo como excepción a la quiescencia externa. No hace seguros lectores o submitters
 *   que hayan entrado antes o concurrentemente con la destrucción.
 * - Sólo puede haber un trabajo activo por scheduler. Otra llamada concurrente recibe
 *   `LONG_HAIR_SCHEDULER_BUSY` en lugar de bloquearse; una llamada anidada desde un callback
 *   del scheduler actualmente activo recibe `LONG_HAIR_SCHEDULER_NESTED`. En una cadena
 *   A->B->A, la llamada interior a A ve B como scheduler activo y recibe `BUSY` de A.
 * - Las particiones son contiguas, fijas y calculadas por el scheduler. El índice de partición
 *   se entrega como `threadIndex` estable; el hilo llamador siempre participa como partición 0.
 * - El callback debe escribir únicamente las ranuras derivadas de `[start,end)` y su resultado
 *   debe ser independiente del tamaño de partición. Así el resultado es determinista con
 *   1, 2, 4 u 8 participantes.
 * - Si el callback devuelve `false`, el trabajo se marca como fallido, las particiones que aún
 *   no han empezado se omiten y el llamador debe descartar la salida parcial (el scheduler no
 *   hace rollback). El camino de éxito sí es determinista.
 *
 * El scheduler es independiente del renderer. Nunca se deben programar llamadas OpenGL en sus
 * callbacks: los workers no son el hilo de render.
 */

#ifndef MONSTER_LONG_HAIR_SCHEDULER_H
#define MONSTER_LONG_HAIR_SCHEDULER_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Máximo absoluto de participantes (incluido el hilo llamador). */
#define LONG_HAIR_SCHEDULER_MAX_THREADS 8
/** @brief Presupuesto por defecto; deja margen al pool de mesh/morph y al hilo de render. */
#define LONG_HAIR_SCHEDULER_DEFAULT_MAX_THREADS 4

/** @brief Resultado de una llamada al scheduler. */
typedef enum LongHairSchedulerStatus {
    LONG_HAIR_SCHEDULER_OK = 0,             /**< Todas las fases terminaron correctamente. */
    LONG_HAIR_SCHEDULER_INVALID_ARGUMENT,   /**< Scheduler, fases, callback o rango inválido. */
    LONG_HAIR_SCHEDULER_BUSY,               /**< Otro trabajo sigue activo en este scheduler. */
    LONG_HAIR_SCHEDULER_NESTED,             /**< Llamada anidada desde un callback del mismo scheduler. */
    LONG_HAIR_SCHEDULER_SHUTDOWN,           /**< El scheduler ya no admite trabajos. */
    LONG_HAIR_SCHEDULER_CALLBACK_FAILED     /**< Un callback devolvió false; salida parcial. */
} LongHairSchedulerStatus;

typedef struct LongHairScheduler LongHairScheduler;

/**
 * @brief Trabajo de una partición contigua `[startIndex,endIndex)`.
 * @param context Contexto prestado por el llamador; no se retiene tras el retorno.
 * @param startIndex Primer índice inclusivo de la partición.
 * @param endIndex Índice final exclusivo; puede ser igual a `startIndex` (partición vacía omitida).
 * @param threadIndex Índice de partición estable en `[0, participantes)`.
 * @return `true` para continuar; `false` para solicitar el aborto cooperativo del trabajo.
 */
typedef bool (*LongHairSchedulerWorkFn)(void* context, int startIndex, int endIndex, int threadIndex);

/**
 * @brief Una fase completa con barrera al terminar.
 *
 * `LongHairScheduler_RunPhases` ejecuta las fases en orden. Cada fase termina con una barrera
 * completa (join de todos los participantes) antes de que empiece la siguiente. Esto permite
 * que la fase 3 lea las guías ya construidas por las fases 1 y 2.
 */
typedef struct LongHairSchedulerPhase {
    int totalItems;                   /**< Número de elementos; 0 es una fase vacía válida. */
    LongHairSchedulerWorkFn workFn;   /**< Callback; obligatorio si `totalItems > 0`. */
    void* context;                    /**< Contexto prestado sólo durante la fase. */
} LongHairSchedulerPhase;

/** @brief Configuración de creación. Un struct a cero equivale a la configuración por defecto. */
typedef struct LongHairSchedulerConfig {
    int threadCount;          /**< Participantes totales; 0 = auto, 1 = serial forzado. */
    int maxThreads;           /**< Tope inclusivo; 0 = `LONG_HAIR_SCHEDULER_DEFAULT_MAX_THREADS`. */
    bool requireParallel;     /**< Si el count resuelto >1, falla si el pool no da >1; count 1 sigue serial. */
} LongHairSchedulerConfig;

/** @brief Contadores de diagnóstico; se leen de forma consistente bajo el mutex interno. */
typedef struct LongHairSchedulerStats {
    uint64_t jobsStarted;
    uint64_t jobsCompleted;
    uint64_t jobsFailed;
    uint64_t busyRejections;
    uint64_t nestedRejections;
    uint64_t shutdownRejections;
    uint64_t phasesExecuted;
    uint64_t itemsExecuted;
    uint64_t serialJobs;
    uint64_t parallelJobs;
    int threadCount;          /**< Participantes efectivos tras aplicar topes y fallback. */
    int lastPhaseCount;       /**< Fases ejecutadas en el último trabajo admitido. */
} LongHairSchedulerStats;

/** @brief Configuración por defecto: auto, tope 4 y fallback serial permitido. */
LongHairSchedulerConfig LongHairScheduler_DefaultConfig(void);

/**
 * @brief Crea el scheduler y su pool privado persistente.
 *
 * Los hilos se crean una sola vez. No se crean ni destruyen hilos por paso de edad o fase.
 *
 * @param config Configuración; NULL usa `LongHairScheduler_DefaultConfig`.
 * @return Scheduler propietario o NULL si falla la asignación o `requireParallel` no se cumple.
 */
LongHairScheduler* LongHairScheduler_Create(const LongHairSchedulerConfig* config);

/**
 * @brief Detiene el scheduler y libera su pool privado.
 *
 * El propietario debe establecer quiescencia externa ANTES de llamar: impedir que comiencen
 * llamadas nuevas y sincronizar/join todos los submitters y lectores que ya pudieron entrar,
 * incluidos `IsRunning`, `GetThreadCount`, `GetStats` y `ResetStats`. La única operación del
 * scheduler que Destroy admite en vuelo es un job activo; espera a que ese job retorne y luego
 * hace join de los workers. No se admiten otros lectores o llamadas concurrentes durante este
 * protocolo. Esta función NO hace segura una destrucción arbitrariamente concurrente; debe
 * haber un solo propietario/llamador de Destroy, coordinado externamente.
 *
 * Si se invoca desde cualquier callback de cualquier scheduler, se rechaza sin liberar memoria
 * para evitar autobloqueo; el propietario debe destruirlo tras retornar todos esos callbacks.
 *
 * @param scheduler Scheduler propietario; NULL es válido.
 */
void LongHairScheduler_Destroy(LongHairScheduler* scheduler);

/**
 * @brief Indica si el scheduler existe y admite trabajo.
 * @note Requiere la misma protección externa de vida útil que cualquier otra API; no se debe
 *       ejecutar concurrentemente con la destrucción.
 */
bool LongHairScheduler_IsRunning(const LongHairScheduler* scheduler);

/**
 * @brief Participantes efectivos (incluido el llamador). Nunca menor que 1.
 * @note Requiere la misma protección externa de vida útil que cualquier otra API; no se debe
 *       ejecutar concurrentemente con la destrucción.
 */
int LongHairScheduler_GetThreadCount(const LongHairScheduler* scheduler);

/** @brief Índice de partición durante callback del scheduler actual; 0 fuera de callback.
 * Útil para contadores/scratch worker-locales sin atomics por elemento. */
int LongHairScheduler_CurrentPartitionIndex(void);

/**
 * @brief Ejecuta una única fase paralela determinista.
 *
 * `totalItems == 0` es un no-op válido y no invoca el callback. Un valor negativo es inválido.
 *
 * @return Estado del trabajo. En `CALLBACK_FAILED` la salida es parcial y debe descartarse.
 */
LongHairSchedulerStatus LongHairScheduler_ParallelFor(LongHairScheduler* scheduler, int totalItems,
    LongHairSchedulerWorkFn workFn, void* context);

/**
 * @brief Ejecuta varias fases con barrera completa entre ellas y un único trabajo exclusivo.
 *
 * Es la forma recomendada para las tres fases del pelo: raíces/guías, colisión/certificados y
 * folículos. Mantener las tres fases dentro de una sola llamada impide que otro llamador se
 * intercale entre ellas. Si una fase falla, las siguientes no se ejecutan.
 *
 * @param phases Fases en orden; el arreglo y sus contextos sólo se leen durante la llamada.
 * @param phaseCount Número de fases; debe ser mayor que cero.
 */
LongHairSchedulerStatus LongHairScheduler_RunPhases(LongHairScheduler* scheduler,
    const LongHairSchedulerPhase* phases, int phaseCount);

/**
 * @brief Copia los contadores actuales. `outStats` NULL se ignora.
 * @note Requiere la misma protección externa de vida útil que cualquier otra API; no se debe
 *       ejecutar concurrentemente con la destrucción.
 */
void LongHairScheduler_GetStats(LongHairScheduler* scheduler, LongHairSchedulerStats* outStats);

/**
 * @brief Pone a cero los contadores conservando `threadCount`.
 * @note Requiere la misma protección externa de vida útil que cualquier otra API; no se debe
 *       ejecutar concurrentemente con la destrucción.
 */
void LongHairScheduler_ResetStats(LongHairScheduler* scheduler);

/** @brief Nombre estable para logs y tests. */
const char* LongHairScheduler_StatusName(LongHairSchedulerStatus status);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_LONG_HAIR_SCHEDULER_H */
