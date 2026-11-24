/**
 * @file demo_evolution_arena_headless.c
 * @brief Simulador headless reproducible de Evolution Arena (sin SDL/OpenGL).
 */
#include "EvolutionArena.h"
#include "CreatureRecipeRegistry.h"
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

static bool parseNumber(const char *s, double *out) {
    if (!s || !*s) return false;
    char *end = NULL;
    errno = 0;
    *out = strtod(s, &end);
    return !errno && end && !*end && isfinite(*out) && *out >= 0.0;
}

static void ensureParentDir(const char *path) {
    if (!path) return;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) return;
    *slash = '\0';
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            mkdir(tmp, 0755);
            *p = '/';
        }
    }
    mkdir(tmp, 0755);
}

static void printHelp(void) {
    puts("Uso: ./demos/demo_evolution_arena_headless [OPCIONES]\n"
         "  --seed=N                     Semilla PRNG determinista (defecto 42)\n"
         "  --days=N                     Días simulados (defecto 30)\n"
         "  --arena-size=N               Ancho/profundidad física de la arena (defecto 40)\n"
         "  --brumacernos=N              Población inicial de Brumacerno (defecto 16; alias --herbivores=N)\n"
         "  --velkares=N                 Población inicial de Velkar (defecto 6; alias --carnivores=N)\n"
         "  --plants=N                   Plantas regenerables (defecto 36)\n"
         "  --ponds=N                    Estanques de agua (defecto 2)\n"
         "  --obstacles=N                Obstáculos estáticos (defecto 5)\n"
         "  --capacity=N                 Capacidad máxima de población (defecto 128)\n"
         "  --fixed-dt=N                 Paso fijo en segundos (defecto 1/60)\n"
         "  --day-duration=N             Segundos simulados por día (defecto 120)\n"
         "  --controller=neural|utility  Controlador conductual (defecto neural)\n"
         "  --founders=random|seeded|imported  Inicialización de fundadores (defecto seeded)\n"
         "  --brain=ARCHIVO              Archivo *.brain.json común para fundadores\n"
         "  --brumacerno-brain=ARCHIVO   Archivo *.brain.json para fundadores Brumacerno\n"
         "  --velkar-brain=ARCHIVO       Archivo *.brain.json para fundadores Velkar\n"
         "  --mutation-rate=N            Probabilidad de mutación por parámetro (defecto 0.05)\n"
         "  --mutation-sigma=N           Desviación Gaussiana de mutación (defecto 0.15)\n"
         "  --replace-rate=N             Probabilidad de reemplazo por parámetro (defecto 0.005)\n"
         "  --weight-limit=N             Cota absoluta de pesos (defecto 5.0)\n"
         "  --crossover=neuron|uniform|mutation-only  Recombinación (defecto neuron)\n"
         "  --exploration-noise=N        Ruido Gaussiano determinista en salidas (defecto 0.0)\n"
         "  --founder-diversity-rate=N   Tasa de diversificación inicial en modo seeded (defecto 0.15)\n"
         "  --founder-diversity-sigma=N  Sigma de diversificación inicial en modo seeded (defecto 0.08)\n"
         "  --no-physical-mutation       Congela mutación física manteniendo evolución neural\n"
         "  --scenario=reference|A|B|C|D|E  Escenario ecológico clásico de regresión\n"
         "  --recipes-dir=DIR            Directorio de recetas (defecto recipes)\n"
         "  --summary=ARCHIVO.json       Resumen JSON final (stdout si se omite)\n"
         "  --history=ARCHIVO.csv        Historia periódica CSV\n"
         "  --events=ARCHIVO.csv         Registro detallado de eventos CSV\n"
         "  --export-brains=DIR          Exporta archivos observacionales *.brain.json\n"
         "  --trace                      Imprime instantáneas diagnósticas en stderr");
}

int main(int argc, char **argv) {
    EvolutionArenaConfig cfg = EvolutionArenaConfig_Default();
    const char *summaryPath = NULL;
    const char *historyPath = NULL;
    const char *eventsPath = NULL;
    const char *exportDir = NULL;
    const char *recipesDirectory = "recipes";
    bool trace = false;

    /* Primero aplicar --scenario si se especifica para que las banderas explícitas sobrescriban. */
    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--scenario=", 11) == 0) {
            if (!EvolutionArenaConfig_ApplyLegacyScenario(&cfg, argv[i] + 11)) {
                fprintf(stderr, "Escenario desconocido: %s\n", argv[i] + 11);
                return 2;
            }
        }
    }

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        double n = 0.0;
        if (strcmp(a, "--help") == 0) { printHelp(); return 0; }
        if (strcmp(a, "--trace") == 0) { trace = true; continue; }
        if (strcmp(a, "--no-physical-mutation") == 0) { cfg.disablePhysicalMutation = true; continue; }
        if (strncmp(a, "--scenario=", 11) == 0) continue;
        if (strncmp(a, "--summary=", 10) == 0) { summaryPath = a + 10; continue; }
        if (strncmp(a, "--history=", 10) == 0) { historyPath = a + 10; continue; }
        if (strncmp(a, "--events=", 9) == 0) { eventsPath = a + 9; continue; }
        if (strncmp(a, "--export-brains=", 16) == 0) { exportDir = a + 16; continue; }
        if (strncmp(a, "--recipes-dir=", 14) == 0) { recipesDirectory = a + 14; continue; }
        if (strncmp(a, "--brain=", 8) == 0) { cfg.founderBrainPath = a + 8; continue; }
        if (strncmp(a, "--brumacerno-brain=", 19) == 0) { cfg.brumacernoBrainPath = a + 19; continue; }
        if (strncmp(a, "--velkar-brain=", 15) == 0) { cfg.velkarBrainPath = a + 15; continue; }
        if (strncmp(a, "--controller=", 13) == 0) {
            const char *v = a + 13;
            if (strcmp(v, "neural") == 0) cfg.controller = CREATURE_CONTROLLER_NEURAL;
            else if (strcmp(v, "utility") == 0) cfg.controller = CREATURE_CONTROLLER_UTILITY;
            else goto invalid;
            continue;
        }
        if (strncmp(a, "--founders=", 11) == 0) {
            if (!EvolutionFounderMode_FromName(a + 11, &cfg.founderMode)) goto invalid;
            continue;
        }
        if (strncmp(a, "--crossover=", 12) == 0) {
            const char *v = a + 12;
            if (strcmp(v, "neuron") == 0 || strcmp(v, "neuron_wise") == 0)
                cfg.mutation.crossoverMode = NEURAL_CROSSOVER_NEURON_WISE;
            else if (strcmp(v, "uniform") == 0)
                cfg.mutation.crossoverMode = NEURAL_CROSSOVER_UNIFORM;
            else if (strcmp(v, "mutation-only") == 0 || strcmp(v, "mutation_only") == 0)
                cfg.mutation.crossoverMode = NEURAL_CROSSOVER_MUTATION_ONLY;
            else goto invalid;
            continue;
        }
        const char *eq = strchr(a, '=');
        if (!eq || !parseNumber(eq + 1, &n)) goto invalid;

        if (strncmp(a, "--days=", 7) == 0) { cfg.days = n; continue; }
        if (strncmp(a, "--fixed-dt=", 11) == 0) { cfg.step = n; continue; }
        if (strncmp(a, "--day-duration=", 15) == 0) { cfg.day = n; continue; }
        if (strncmp(a, "--arena-size=", 13) == 0) { cfg.arenaSize = (float)n; continue; }
        if (strncmp(a, "--mutation-rate=", 16) == 0) { cfg.mutation.mutationRate = (float)n; continue; }
        if (strncmp(a, "--mutation-sigma=", 17) == 0) { cfg.mutation.mutationSigma = (float)n; continue; }
        if (strncmp(a, "--replace-rate=", 15) == 0) { cfg.mutation.replaceRate = (float)n; continue; }
        if (strncmp(a, "--weight-limit=", 15) == 0) { cfg.mutation.weightLimit = (float)n; continue; }
        if (strncmp(a, "--exploration-noise=", 20) == 0) { cfg.neural.explorationNoise = (float)n; continue; }
        if (strncmp(a, "--founder-diversity-rate=", 25) == 0) { cfg.founderDiversityRate = (float)n; continue; }
        if (strncmp(a, "--founder-diversity-sigma=", 26) == 0) { cfg.founderDiversitySigma = (float)n; continue; }
        if (strncmp(a, "--seed=", 7) == 0) {
            if (n > (double)UINT32_MAX || floor(n) != n) goto invalid;
            cfg.seed = (uint32_t)n;
            continue;
        }
        if (n > 100000.0 || floor(n) != n) goto invalid;
        if (strncmp(a, "--brumacernos=", 14) == 0 || strncmp(a, "--herbivores=", 13) == 0)
            cfg.brumacernos = (unsigned)n;
        else if (strncmp(a, "--velkares=", 11) == 0 || strncmp(a, "--carnivores=", 13) == 0)
            cfg.velkares = (unsigned)n;
        else if (strncmp(a, "--plants=", 9) == 0) cfg.plants = (unsigned)n;
        else if (strncmp(a, "--ponds=", 8) == 0) cfg.ponds = (unsigned)n;
        else if (strncmp(a, "--obstacles=", 12) == 0) cfg.obstacles = (unsigned)n;
        else if (strncmp(a, "--capacity=", 11) == 0) cfg.capacity = (unsigned)n;
        else goto invalid;
        continue;
    invalid:
        fprintf(stderr, "Argumento inválido: %s\n", a);
        return 2;
    }

    if (!EvolutionArenaConfig_Validate(&cfg)) {
        fprintf(stderr, "Configuración de Evolution Arena inválida.\n");
        return 2;
    }
    double ticks = cfg.days * cfg.day / cfg.step;
    if (!isfinite(ticks) || ticks < 0.0 || ticks > 1e10) {
        fprintf(stderr, "Número de ticks fuera de rango.\n");
        return 2;
    }

    char errorBuf[256] = {0};
    if (!CreatureRecipeRegistry_LoadDirectory(recipesDirectory, errorBuf, sizeof(errorBuf))) {
        fprintf(stderr, "%s\n", errorBuf);
        return 1;
    }

    EcosystemWorld world;
    if (!EvolutionArena_Create(&world, &cfg, errorBuf, sizeof(errorBuf))) {
        fprintf(stderr, "No se pudo crear Evolution Arena: %s\n", errorBuf);
        return 1;
    }

    FILE *csv = NULL;
    if (historyPath) {
        ensureParentDir(historyPath);
        csv = fopen(historyPath, "w");
        if (!csv) { perror(historyPath); EcosystemWorld_Free(&world); return 1; }
    }
    FILE *eventFile = NULL;
    if (eventsPath) {
        ensureParentDir(eventsPath);
        eventFile = fopen(eventsPath, "w");
        if (!eventFile) {
            perror(eventsPath);
            if (csv) fclose(csv);
            EcosystemWorld_Free(&world);
            return 1;
        }
    }
    EcosystemWorld_SetEventOutput(&world, eventFile);

    clock_t start = clock();
    uint64_t total = (uint64_t)ceil(ticks - 1e-9);
    double nextSample = 0.0;
    for (uint64_t i = 0; i < total; ++i) {
        EcosystemWorld_Step(&world);
        if (trace && i % 600 == 0) {
            for (unsigned j = 0; j < world.capacity; ++j) {
                EcosystemEntity *e = &world.entities[j];
                if (!e->id) continue;
                fprintf(stderr, "t=%.1f id=%u sp=%u gen=%u intent=%s action=%s target=%u E=%.2f H=%.2f HP=%.2f\n",
                        world.clock.seconds, e->id, e->body.species, e->generation,
                        e->mind.controllerType == CREATURE_CONTROLLER_NEURAL
                            ? NeuralActionIntent_Name(e->mind.neuralState.selectedIntent)
                            : "UTILITY",
                        CreatureAction_Name(e->mind.action), e->mind.target,
                        e->physiology.energy, e->physiology.hydration, e->physiology.health);
            }
        }
        if (csv && world.clock.seconds >= nextSample) {
            EcosystemWorld_ComputeNeuralMetrics(&world);
            if (!EcosystemWorld_WriteCSV(&world, csv, nextSample == 0.0)) {
                fclose(csv);
                if (eventFile) fclose(eventFile);
                EcosystemWorld_Free(&world);
                return 1;
            }
            nextSample += cfg.day / 4.0;
        }
    }
    EcosystemWorld_ComputeNeuralMetrics(&world);
    double elapsed = (double)(clock() - start) / CLOCKS_PER_SEC;

    bool ok = true;
    if (csv && fclose(csv)) ok = false;
    EcosystemWorld_SetEventOutput(&world, NULL);
    if (eventFile) {
        if (ferror(eventFile)) ok = false;
        if (fclose(eventFile)) ok = false;
    }

    if (exportDir) {
        if (!EcosystemWorld_ExportArchiveBrains(&world, exportDir, errorBuf, sizeof(errorBuf))) {
            fprintf(stderr, "Error al exportar cerebros archivados: %s\n", errorBuf);
            ok = false;
        }
    }

    FILE *json = stdout;
    if (summaryPath) {
        ensureParentDir(summaryPath);
        json = fopen(summaryPath, "w");
    }
    if (!json) {
        perror(summaryPath);
        ok = false;
    } else {
        ok = EcosystemWorld_WriteJSON(&world, json) && ok;
        if (summaryPath && fclose(json)) ok = false;
    }

    fprintf(stderr,
            "ticks=%llu cpu_seconds=%.6f mean_tick_ms=%.6f controller=%s founders=%s "
            "population=%u births=%llu deaths=%llu kills=%llu max_gen=%u diversity=%.4f\n",
            (unsigned long long)total, elapsed, total ? elapsed * 1000.0 / (double)total : 0.0,
            world.defaultController == CREATURE_CONTROLLER_NEURAL ? "neural" : "utility",
            world.founderModeName, world.count,
            (unsigned long long)world.statistics.births,
            (unsigned long long)world.statistics.deaths,
            (unsigned long long)world.statistics.kills,
            world.statistics.maximumGeneration, world.statistics.neuralDiversity);

    EcosystemWorld_Free(&world);
    return ok ? 0 : 1;
}
