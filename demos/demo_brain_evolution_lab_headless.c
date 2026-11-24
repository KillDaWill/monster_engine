/**
 * @file demo_brain_evolution_lab_headless.c
 * @brief Interfaz de línea de comandos headless de Brain Evolution Lab (sin SDL/OpenGL).
 */
#include "BrainEvolutionLab.h"
#include "EvolutionCheckpointIO.h"
#include "CreatureRecipeRegistry.h"
#include <errno.h>
#include <math.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static volatile bool g_stopRequested = false;

static void handleSignal(int sig) {
    (void)sig;
    g_stopRequested = true;
}

static bool parseDouble(const char *s, double *out) {
    if (!s || !*s) return false;
    char *ep = NULL;
    errno = 0;
    double v = strtod(s, &ep);
    if (errno || !ep || *ep != '\0' || !isfinite(v) || v < 0.0) return false;
    *out = v;
    return true;
}

static void printHelp(void) {
    puts("Uso: ./demos/demo_brain_evolution_lab_headless [OPCIONES]\n"
         "  --species=ARCHIVO.species.json        Configuración de especie (defecto species/brumacerno.species.json)\n"
         "  --environment=ARCHIVO.environment.json Entorno de entrenamiento (defecto environments/brumacerno_mixed.environment.json)\n"
         "  --curriculum=ENV1,ENV2,...            Etapas progresivas separadas por comas\n"
         "  --no-curriculum                       Desactiva currículo multi-etapa\n"
         "  --metabolism-dir=DIR                  Directorio molecular (defecto metabolism)\n"
         "  --recipes-dir=DIR                     Directorio de recetas anatómicas (defecto recipes)\n"
         "  --architecture=recurrent_candidate_policy  Arquitectura neuronal v2\n"
         "  --founders=random|imported            Modo de fundadores (defecto random)\n"
         "  --initial-brain=ARCHIVO.brain.json    Cerebro inicial opcional (v2)\n"
         "  --population=N                        Tamaño de población GA (defecto 128)\n"
         "  --elite=N                             Número de élites exactos (defecto 8)\n"
         "  --tournament=N                        Tamaño de torneo (defecto 4)\n"
         "  --generations=N                       Generaciones máximas del entrenador (defecto 200)\n"
         "  --episode-seconds=S                   Sobrescribe duración de episodio\n"
         "  --training-seeds=N                    Episodios por genoma y generación (defecto 3)\n"
         "  --validation-seeds=N                  Episodios disjuntos de validación (defecto 10)\n"
         "  --validation-interval=N               Frecuencia de validación en generaciones (defecto 5)\n"
         "  --test-seeds=N                        Episodios ciegos de test final (defecto 20)\n"
         "  --mutation-rate=P                     Probabilidad de mutación por peso (defecto 0.05)\n"
         "  --mutation-sigma=S                    Desviación Gaussiana de mutación (defecto 0.15)\n"
         "  --replace-rate=P                      Probabilidad de reemplazo aleatorio (defecto 0.005)\n"
         "  --mutation-only                       Desactiva cruce (sólo mutación)\n"
         "  --workers=auto|N                      Hilos paralelos de evaluación (defecto auto)\n"
         "  --seed=N                              Semilla determinista del laboratorio (defecto 42)\n"
         "  --output=DIR                          Directorio de salida (defecto build/brain-evolution/default)\n"
         "  --resume=CHECKPOINT.json              Reanudar desde un checkpoint validado\n"
         "  --checkpoint-interval=N               Frecuencia de guardado de checkpoint (defecto 10)\n"
         "  --target-fitness=F                    Umbral de convergencia en validación (defecto 0.72)\n"
         "  --min-survival=S                      Supervivencia mínima en validación (defecto 0.90)\n"
         "  --stagnation-limit=N                  Límite de generaciones sin mejora (defecto 40)\n"
         "  --maximum-training-time=S             Presupuesto máximo en segundos reales (0 = sin límite)\n"
         "  --anatomical-oral                     Activa solver cervical anatómico completo durante episodios\n"
         "  --baselines                           Evalúa y muestra políticas triviales de referencia\n"
         "  --report                              Imprime resumen detallado al terminar\n"
         "  --help                                Muestra esta ayuda");
}

int main(int argc, char **argv) {
    BrainLabConfig cfg = BrainLabConfig_Default();
    const char *recipesDir = "recipes";
    const char *resumePath = NULL;
    bool printReport = false;
    bool runBaselines = false;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        double num = 0.0;
        if (strcmp(a, "--help") == 0) { printHelp(); return 0; }
        if (strcmp(a, "--mutation-only") == 0) { cfg.mutationOnly = true; continue; }
        if (strcmp(a, "--no-curriculum") == 0) { cfg.useCurriculum = false; cfg.curriculumCount = 0; continue; }
        if (strcmp(a, "--anatomical-oral") == 0) { cfg.useAnatomicalOral = true; continue; }
        if (strcmp(a, "--report") == 0) { printReport = true; continue; }
        if (strcmp(a, "--baselines") == 0) { runBaselines = true; continue; }
        if (strncmp(a, "--species=", 10) == 0) { snprintf(cfg.speciesPath, sizeof(cfg.speciesPath), "%s", a + 10); continue; }
        if (strncmp(a, "--environment=", 14) == 0) { snprintf(cfg.environmentPath, sizeof(cfg.environmentPath), "%s", a + 14); continue; }
        if (strncmp(a, "--metabolism-dir=", 17) == 0) { snprintf(cfg.metabolismDir, sizeof(cfg.metabolismDir), "%s", a + 17); continue; }
        if (strncmp(a, "--recipes-dir=", 14) == 0) { recipesDir = a + 14; continue; }
        if (strncmp(a, "--initial-brain=", 16) == 0) {
            snprintf(cfg.initialBrainPath, sizeof(cfg.initialBrainPath), "%s", a + 16);
            cfg.randomFounders = false;
            continue;
        }
        if (strncmp(a, "--output=", 9) == 0) { snprintf(cfg.outputDir, sizeof(cfg.outputDir), "%s", a + 9); continue; }
        if (strncmp(a, "--resume=", 9) == 0) { resumePath = a + 9; continue; }
        if (strncmp(a, "--architecture=", 15) == 0) {
            if (!NeuralArchitecture_FromName(a + 15, &cfg.architecture)) {
                fprintf(stderr, "Arquitectura desconocida: %s\n", a + 15);
                return 2;
            }
            continue;
        }
        if (strncmp(a, "--founders=", 11) == 0) {
            if (strcmp(a + 11, "random") == 0) cfg.randomFounders = true;
            else if (strcmp(a + 11, "imported") == 0) cfg.randomFounders = false;
            else { fprintf(stderr, "Modo de fundadores inválido: %s\n", a + 11); return 2; }
            continue;
        }
        if (strncmp(a, "--workers=", 10) == 0) {
            if (strcmp(a + 10, "auto") == 0) { cfg.workers = 0u; continue; }
            if (!parseDouble(a + 10, &num)) { fprintf(stderr, "Valor --workers inválido\n"); return 2; }
            cfg.workers = (unsigned)num;
            continue;
        }
        if (strncmp(a, "--curriculum=", 13) == 0) {
            cfg.useCurriculum = true;
            cfg.curriculumCount = 0u;
            const char *p = a + 13;
            while (*p && cfg.curriculumCount < BRAIN_LAB_MAX_CURRICULUM_STAGES) {
                const char *comma = strchr(p, ',');
                size_t len = comma ? (size_t)(comma - p) : strlen(p);
                if (len > 0 && len < sizeof(cfg.curriculumPaths[0])) {
                    memcpy(cfg.curriculumPaths[cfg.curriculumCount], p, len);
                    cfg.curriculumPaths[cfg.curriculumCount][len] = '\0';
                    ++cfg.curriculumCount;
                }
                if (!comma) break;
                p = comma + 1;
            }
            continue;
        }

        const char *eq = strchr(a, '=');
        if (!eq || !parseDouble(eq + 1, &num)) {
            fprintf(stderr, "Argumento inválido: %s\n", a);
            return 2;
        }
        if (strncmp(a, "--population=", 13) == 0) cfg.populationSize = (unsigned)num;
        else if (strncmp(a, "--elite=", 8) == 0) cfg.eliteCount = (unsigned)num;
        else if (strncmp(a, "--tournament=", 13) == 0) cfg.tournamentSize = (unsigned)num;
        else if (strncmp(a, "--generations=", 14) == 0) cfg.maxGenerations = (unsigned)num;
        else if (strncmp(a, "--episode-seconds=", 18) == 0) cfg.episodeSecondsOverride = num;
        else if (strncmp(a, "--training-seeds=", 17) == 0) cfg.trainingSeedsPerGen = (unsigned)num;
        else if (strncmp(a, "--validation-seeds=", 19) == 0) cfg.validationSeeds = (unsigned)num;
        else if (strncmp(a, "--validation-interval=", 22) == 0) cfg.validationInterval = (unsigned)num;
        else if (strncmp(a, "--test-seeds=", 13) == 0) cfg.testSeeds = (unsigned)num;
        else if (strncmp(a, "--mutation-rate=", 16) == 0) cfg.mutation.mutationRate = (float)num;
        else if (strncmp(a, "--mutation-sigma=", 17) == 0) cfg.mutation.mutationSigma = (float)num;
        else if (strncmp(a, "--replace-rate=", 15) == 0) cfg.mutation.replaceRate = (float)num;
        else if (strncmp(a, "--seed=", 7) == 0) cfg.seed = (uint32_t)num;
        else if (strncmp(a, "--checkpoint-interval=", 22) == 0) cfg.checkpointInterval = (unsigned)num;
        else if (strncmp(a, "--target-fitness=", 17) == 0) cfg.targetValidationFitness = (float)num;
        else if (strncmp(a, "--min-survival=", 15) == 0) cfg.minValidationSurvival = (float)num;
        else if (strncmp(a, "--stagnation-limit=", 19) == 0) cfg.stagnationLimit = (unsigned)num;
        else if (strncmp(a, "--maximum-training-time=", 24) == 0) cfg.maxTrainingSeconds = num;
        else { fprintf(stderr, "Argumento desconocido: %s\n", a); return 2; }
    }

    char err[256] = {0};
    if (!CreatureRecipeRegistry_LoadDirectory(recipesDir, err, sizeof(err))) {
        fprintf(stderr, "Error cargando recetas: %s\n", err);
        return 1;
    }

    BrainEvolutionLab lab;
    if (!BrainEvolutionLab_Init(&lab, &cfg, err, sizeof(err))) {
        fprintf(stderr, "Error inicializando Brain Evolution Lab: %s\n", err);
        return 1;
    }

    if (resumePath && resumePath[0]) {
        if (!EvolutionCheckpoint_Load(&lab, resumePath, false, err, sizeof(err))) {
            fprintf(stderr, "Error reanudando checkpoint '%s': %s\n", resumePath, err);
            BrainEvolutionLab_Destroy(&lab);
            return 1;
        }
        fprintf(stderr, "[resume] Checkpoint cargado en gen=%u stage=%u\n",
                lab.currentGeneration, lab.currentStage);
    }

    if (runBaselines) {
        const EvolutionEnvironmentConfig *env = &lab.stages[lab.currentStage].env;
        fprintf(stderr, "=== Políticas triviales de referencia (%s en %s) ===\n", lab.species.id, env->id);
        for (int bp = 0; bp <= EVO_BASELINE_CONSUME_NEAREST; ++bp) {
            EvolutionEpisodeMetrics eps[5];
            for (unsigned s = 0; s < 5u; ++s) {
                EvolutionEvaluator_RunBaselineEpisode(env, &lab.species, &lab.db,
                                                      (EvolutionBaselinePolicy)bp, &lab.config.fitness,
                                                      cfg.seed + 9000u + s, cfg.useAnatomicalOral,
                                                      &eps[s], err, sizeof(err));
            }
            EvolutionAggregateMetrics agg;
            EvolutionFitness_Aggregate(eps, 5u, &agg);
            fprintf(stderr, "  %-16s fit=%.4f surv=%.0f%% E=%.3f H=%.3f HP=%.3f tox=%.3f ing=%.2f\n",
                    EvolutionBaselinePolicy_Name((EvolutionBaselinePolicy)bp),
                    (double)agg.meanFitness, (double)agg.survivalRate * 100.0,
                    (double)agg.meanEnergy, (double)agg.meanHydration,
                    (double)agg.meanHealth, (double)agg.meanToxicity, agg.meanIngestedMass);
        }
    }

    signal(SIGINT, handleSignal);
    signal(SIGTERM, handleSignal);

    fprintf(stderr, "[lab] species=%s env=%s pop=%u gens=%u workers=%u seed=%u mutation_only=%s\n",
            lab.species.id, lab.stages[lab.currentStage].env.id, lab.populationSize,
            lab.config.maxGenerations, lab.workerCount, lab.config.seed,
            lab.config.mutationOnly ? "true" : "false");

    lab.status = LAB_STATUS_RUNNING;
    while (lab.status == LAB_STATUS_RUNNING && !g_stopRequested) {
        if (!BrainEvolutionLab_StepGeneration(&lab, err, sizeof(err))) {
            fprintf(stderr, "Error en generación %u: %s\n", lab.currentGeneration, err);
            BrainEvolutionLab_Destroy(&lab);
            return 1;
        }
        if (lab.historyCount > 0u) {
            const BrainLabGenerationRecord *r = &lab.history[lab.historyCount - 1u];
            fprintf(stderr,
                    "gen=%3u st=%u best=%.4f mean=%.4f med=%.4f surv=%.0f%% val=%.4f(%.0f%%) "
                    "div=%.4f eps/s=%.1f kills=%llu carc=%llu\n",
                    r->trainerGeneration, r->curriculumStage,
                    (double)r->bestTrainFitness, (double)r->meanTrainFitness,
                    (double)r->medianTrainFitness, (double)r->bestTrainSurvival * 100.0,
                    (double)r->validationFitness, (double)r->validationSurvival * 100.0,
                    (double)r->neuralDiversity, r->episodesPerSecond,
                    (unsigned long long)r->totalKills, (unsigned long long)r->totalCarcassesConsumed);
        }
    }
    if (g_stopRequested && lab.status == LAB_STATUS_RUNNING) {
        lab.status = LAB_STATUS_MANUALLY_STOPPED;
    }

    if (lab.config.testSeeds > 0u) {
        BrainEvolutionLab_RunFinalTest(&lab, err, sizeof(err));
    }
    if (!BrainEvolutionLab_ExportOutputs(&lab, err, sizeof(err))) {
        fprintf(stderr, "Error exportando artefactos: %s\n", err);
        BrainEvolutionLab_Destroy(&lab);
        return 1;
    }

    if (printReport || true) {
        printf("STATUS=%s gens=%u episodes=%llu elapsed=%.2fs\n",
               BrainLabStatus_Name(lab.status), lab.currentGeneration,
               (unsigned long long)lab.totalEpisodesEvaluated, lab.elapsedTrainingSeconds);
        if (lab.hasTestEvaluation) {
            printf("BLIND_TEST(n=%u): fit=%.4f±%.4f (med=%.4f min=%.4f max=%.4f) surv=%.1f%% "
                   "HP=%.3f E=%.3f H=%.3f tox=%.3f ing=%.2f eAbs=%.3f hAbs=%.3f kills=%llu carc=%llu\n",
                   lab.testMetrics.episodeCount,
                   (double)lab.testMetrics.meanFitness, (double)lab.testMetrics.ci95HalfWidth,
                   (double)lab.testMetrics.medianFitness, (double)lab.testMetrics.minFitness,
                   (double)lab.testMetrics.maxFitness, (double)lab.testMetrics.survivalRate * 100.0,
                   (double)lab.testMetrics.meanHealth, (double)lab.testMetrics.meanEnergy,
                   (double)lab.testMetrics.meanHydration, (double)lab.testMetrics.meanToxicity,
                   lab.testMetrics.meanIngestedMass, lab.testMetrics.meanEnergyAbsorbed,
                   lab.testMetrics.meanHydrationAbsorbed,
                   (unsigned long long)lab.testMetrics.totalKills,
                   (unsigned long long)lab.testMetrics.totalCarcassesConsumed);
        }
    }

    BrainEvolutionLab_Destroy(&lab);
    return 0;
}
