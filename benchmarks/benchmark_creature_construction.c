/**
 * @file benchmark_creature_construction.c
 * @brief Perfil por fases, entre especies y sin renderer, de creación procedural.
 *
 * Este ejecutable informa medidas; no modifica el runtime del motor.
 * Ver docs/performance/creature-generation-agent-j.md para método y límites.
 */
#define _POSIX_C_SOURCE 200809L
#include "Creature.h"
#include "CreatureRecipeIO.h"
#include "Fur.h"
#include "Monster.h"
#include "MonsterAnimation.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "SurfaceMapper.h"

#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct Options {
    const char* species;
    const char* recipePath;
    const char* outputPath;
    unsigned repetitions;
    float voxelSize;
} Options;

static double NowMs(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0.0;
    return (double)t.tv_sec * 1000.0 + (double)t.tv_nsec / 1.0e6;
}

static void CsvString(FILE* f, const char* s) {
    fputc('"', f);
    for (; *s; ++s) {
        if (*s == '"') fputc('"', f);
        fputc(*s, f);
    }
    fputc('"', f);
}

static void WriteRow(FILE* f, const char* species, unsigned recipeId, int stage,
    unsigned rep, const char* phase, const char* status, double ms,
    size_t distanceCalls, size_t candidates, size_t exact, size_t pruned,
    size_t vertices, size_t triangles, size_t cpuBytes) {
    bool regionQueries = strncmp(phase,"sdf_region_",11)==0;
    bool meshPhase = strcmp(phase,"mesh_generation")==0 || strcmp(phase,"mesh_generation_steady")==0;
    bool meshData = meshPhase || strcmp(phase,"surface_mapping")==0 || strcmp(phase,"short_fur_roots")==0;
    bool measured = strcmp(status,"measured")==0;
    const char* sampleClass =
        (strcmp(phase,"sdf_build_steady")==0 || strcmp(phase,"mesh_generation_steady")==0)
            ? "steady_object_reuse" : rep==0?"cold":"steady_after_warmup";
    CsvString(f, species); fprintf(f, ",%u,%d,%u,%s,", recipeId, stage, rep, sampleClass);
    CsvString(f, phase); fputc(',', f); CsvString(f, status);
    if (strcmp(status,"measured")==0 || strcmp(status,"failed")==0) fprintf(f, ",%.6f", ms); else fputc(',', f);
    fputc(',',f); if(measured && (regionQueries||meshPhase))fprintf(f,"%zu",distanceCalls);
    fputc(',',f); if(measured && meshPhase)fprintf(f,"%zu",candidates);
    fputc(',',f); if(measured && meshPhase)fprintf(f,"%zu",exact);
    fputc(',',f); if(measured && meshPhase)fprintf(f,"%zu",pruned);
    fputc(',',f); if(measured && meshData)fprintf(f,"%zu",vertices);
    fputc(',',f); if(measured && meshData)fprintf(f,"%zu",triangles);
    fputc(',',f); if(measured && cpuBytes)fprintf(f,"%zu",cpuBytes);
    fputc(',',f); if(measured && strcmp(phase,"short_fur_roots")==0)fprintf(f,"%zu",cpuBytes/sizeof(FurRoot));
    /* GPU memory is intentionally null: this executable has no renderer. */
    fputs(",\n", f);
}

static const CreatureRecipe* BuiltinRecipe(const char* species) {
    if (strcmp(species, "lizard") == 0) return CreatureRecipes_Lizard();
    if (strcmp(species, "dog") == 0) return CreatureRecipes_Dog();
    if (strcmp(species, "brumacerno") == 0) return CreatureRecipes_Brumacerno();
    if (strcmp(species, "velkar") == 0) return CreatureRecipes_Velkar();
    return NULL;
}

static size_t MonsterBytes(const Monster* m) {
    return sizeof(*m) + m->bodyPartCapacity * sizeof(*m->bodyParts) +
        m->eyeCapacity * sizeof(*m->eyes) + m->mouthCapacity * sizeof(*m->mouths) +
        (m->traitCapacity+m->combatTraitCapacity)*sizeof(void*) +
        m->visualTraitCapacity*sizeof(void*) + (m->animation?sizeof(*m->animation):0);
}

static size_t SdfBytes(const MonsterSDF* s) {
    return sizeof(*s) + s->bodyPartCapacity * sizeof(*s->bodyParts) +
        s->connectorCapacity * sizeof(*s->connectors) +
        s->connectorNodeCapacity * sizeof(*s->connectorNodes) +
        s->mouthCapacity * sizeof(*s->mouths);
}

static size_t MeshBytes(const Mesh* m) {
    return m->vertexCapacity * sizeof(*m->vertices) + m->indexCapacity * sizeof(*m->indices);
}

static bool LoadRecipe(const Options* opt, CreatureRecipeDocument* document,
    const CreatureRecipe** recipe, char* error, size_t errorSize) {
    if (opt->recipePath) {
        if (!CreatureRecipeIO_LoadFile(document, opt->recipePath, error, errorSize)) return false;
        *recipe = &document->recipe;
        return true;
    }
    *recipe = BuiltinRecipe(opt->species);
    if (!*recipe) {
        snprintf(error, errorSize, "unknown built-in species '%s' (use --recipe for JSON)", opt->species);
        return false;
    }
    return true;
}

static void Usage(const char* exe) {
    fprintf(stderr,
        "Usage: %s (--species dog|lizard|brumacerno|velkar | --recipe FILE) [options]\n"
        "  --output FILE.csv  --runs N (default 5, min 2; cold rep 0 + steady reps)  --voxel SIZE (default 0.18)\n",
        exe);
}

static bool ParseOptions(int argc, char** argv, Options* out) {
    *out = (Options){.species="", .outputPath="-", .repetitions=5, .voxelSize=0.18f};
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--species") == 0 && i + 1 < argc) out->species = argv[++i];
        else if (strcmp(argv[i], "--recipe") == 0 && i + 1 < argc) out->recipePath = argv[++i];
        else if (strcmp(argv[i], "--output") == 0 && i + 1 < argc) out->outputPath = argv[++i];
        else if (strcmp(argv[i], "--runs") == 0 && i + 1 < argc) {
            char* end = NULL; unsigned long n = strtoul(argv[++i], &end, 10);
            if (!end || *end || n < 2 || n > 1000) return false;
            out->repetitions = (unsigned)n;
        } else if (strcmp(argv[i], "--voxel") == 0 && i + 1 < argc) {
            char* end = NULL; out->voxelSize = strtof(argv[++i], &end);
            if (!end || *end || !isfinite(out->voxelSize) || out->voxelSize <= 0) return false;
        } else return false;
    }
    return (out->recipePath != NULL) != (out->species[0] != '\0');
}

int main(int argc, char** argv) {
    Options opt;
    if (!ParseOptions(argc, argv, &opt)) { Usage(argv[0]); return 2; }
    FILE* out = stdout;
    if (strcmp(opt.outputPath, "-") != 0) {
        out = fopen(opt.outputPath, "w");
        if (!out) { fprintf(stderr, "cannot open %s: %s\n", opt.outputPath, strerror(errno)); return 2; }
    }
    fputs("species,recipe_id,stage,rep,sample_class,phase,status,wall_ms,distance_calls,connector_candidates,connector_exact,connector_pruned,vertices,triangles,cpu_bytes,root_count,gpu_bytes\n", out);

    FILE* reportOut=out;
    FILE* warmupOut=fopen("/dev/null","w");
    if(!warmupOut) { if(out!=stdout)fclose(out); fprintf(stderr,"cannot open warmup sink\n"); return 2; }
    /* rep 0 is recorded cold; one full unreported pass warms later steady samples. */
    for (unsigned pass = 0; pass <= opt.repetitions; ++pass) {
        unsigned rep=pass==0?0:pass-1;
        out=pass==1?warmupOut:reportOut;
        CreatureRecipeDocument document = {0};
        const CreatureRecipe* recipe = NULL;
        char error[256] = {0};
        double t0 = NowMs();
        if (!LoadRecipe(&opt, &document, &recipe, error, sizeof(error))) {
            fprintf(stderr, "recipe load failed: %s\n", error); if (out != stdout) fclose(out); return 1;
        }
        if (opt.recipePath) WriteRow(out, recipe->name, recipe->id, -1, rep, "recipe_parse", "measured", NowMs()-t0, 0,0,0,0,0,0,sizeof(document));
        else WriteRow(out, recipe->name, recipe->id, -1, rep, "recipe_parse", "not-present_builtin", 0,0,0,0,0,0,0,0);

        for (int stage = 0; stage < 4; ++stage) {
            const CreaturePhenotype* endpoint = CreatureRecipe_GetStage(recipe, (CreatureStage)stage);
            if (!endpoint) continue;
            t0 = NowMs();
            CreaturePhenotype phenotype = CreatureRecipe_EvaluateDevelopment(recipe, (float)stage / 3.0f);
            WriteRow(out, recipe->name, recipe->id, stage, rep, "phenotype_synthesis", "measured", NowMs()-t0,0,0,0,0,0,0,sizeof(phenotype));

            AnatomyGraph* graph = malloc(sizeof(*graph));
            if (!graph) { if (out != stdout) fclose(out); return 1; }
            t0 = NowMs(); bool anatomyOk = Creature_ResolveAnatomy(recipe, endpoint, graph);
            WriteRow(out, recipe->name, recipe->id, stage, rep, "anatomy_resolution", anatomyOk?"measured":"failed", NowMs()-t0,0,0,0,0,0,0,sizeof(*graph));
            free(graph);
            if (!anatomyOk) { if (out != stdout) fclose(out); return 1; }

            Monster monster = Monster_Create();
            t0 = NowMs(); bool buildOk = Creature_BuildMonster(&monster, recipe, endpoint);
            double creationMs=NowMs()-t0;
            WriteRow(out, recipe->name, recipe->id, stage, rep, "creature_creation", buildOk?"measured":"failed", creationMs,0,0,0,0,0,0,MonsterBytes(&monster));
            if (!buildOk) { Monster_Free(&monster); if (out != stdout) fclose(out); return 1; }

            MonsterSDF sdf = MonsterSDF_Create();
            t0 = NowMs(); bool sdfOk = MonsterSDF_Build(&sdf, &monster, MonsterSDF_DefaultConfig());
            double sdfBuildMs=NowMs()-t0;
            WriteRow(out, recipe->name, recipe->id, stage, rep, "sdf_build", sdfOk?"measured":"failed", sdfBuildMs,0,0,0,0,0,0,SdfBytes(&sdf));
            if (!sdfOk) { MonsterSDF_Free(&sdf); Monster_Free(&monster); if (out != stdout) fclose(out); return 1; }
            /* Calienta y valida el reuso antes de la medida steady-state. */
            t0=NowMs(); bool sdfWarmOk=MonsterSDF_Build(&sdf,&monster,MonsterSDF_DefaultConfig());
            if(!sdfWarmOk) { WriteRow(out,recipe->name,recipe->id,stage,rep,"sdf_build_steady","failed",NowMs()-t0,0,0,0,0,0,0,0); MonsterSDF_Free(&sdf); Monster_Free(&monster); if(out!=stdout)fclose(out); return 1; }
            t0=NowMs(); bool steadySdfOk=MonsterSDF_Build(&sdf,&monster,MonsterSDF_DefaultConfig());
            WriteRow(out,recipe->name,recipe->id,stage,rep,"sdf_build_steady",steadySdfOk?"measured":"failed",NowMs()-t0,0,0,0,0,0,0,SdfBytes(&sdf));
            if(!steadySdfOk) { MonsterSDF_Free(&sdf); Monster_Free(&monster); if(out!=stdout)fclose(out); return 1; }

            /* Carga determinista de distancia exacta, estratificada en los bounds. */
            AABB3D bounds = MonsterSDF_GetBounds(&sdf);
            const unsigned divisions = 3, samplesPerRegion = 32;
            for (unsigned z=0; z<divisions; ++z) for (unsigned y=0; y<divisions; ++y) for (unsigned x=0; x<divisions; ++x) {
                size_t calls = 0; t0 = NowMs();
                for (unsigned q=0; q<samplesPerRegion; ++q) {
                    float fx=((float)x + (float)((q*17u)%31u + 0.5f)/31.0f)/(float)divisions;
                    float fy=((float)y + (float)((q*11u)%29u + 0.5f)/29.0f)/(float)divisions;
                    float fz=((float)z + (float)((q*7u)%23u + 0.5f)/23.0f)/(float)divisions;
                    Vector3 p=Vec3_Create(bounds.start.x+fx*(bounds.end.x-bounds.start.x),
                        bounds.start.y+fy*(bounds.end.y-bounds.start.y), bounds.start.z+fz*(bounds.end.z-bounds.start.z));
                    volatile float d=MonsterSDF_EvaluateDistance(&sdf,p); (void)d; ++calls;
                }
                char phase[48]; snprintf(phase,sizeof(phase),"sdf_region_%u_%u_%u",x,y,z);
                WriteRow(out, recipe->name, recipe->id, stage, rep, phase, "measured", NowMs()-t0,calls,0,0,0,0,0,SdfBytes(&sdf));
            }

            SDFMesherConfig cfg = SDFMesher_DefaultConfig();
            cfg.voxelSize = opt.voxelSize; cfg.maxCells = 600000; cfg.maxResolution = 320;
            SDFMesher mesher = SDFMesher_Create(cfg); Mesh mesh = Mesh_Create();
            SDFField field = MonsterSDF_GetField(&sdf);
            t0 = NowMs(); bool meshOk = SDFMesher_GenerateMesh(&mesher, &field, &mesh);
            double meshBuildMs=NowMs()-t0;
            SDFMesherStats ms = mesher.lastStats;
            WriteRow(out, recipe->name, recipe->id, stage, rep, "mesh_generation", meshOk?"measured":"failed", meshBuildMs,
                ms.distanceEvaluationCount,ms.connectorCandidateCount,ms.connectorExactEvaluationCount,ms.connectorPrunedCount,
                mesh.vertexCount,mesh.indexCount/3,MeshBytes(&mesh));
            if (!meshOk) { Mesh_Free(&mesh); SDFMesher_Free(&mesher); MonsterSDF_Free(&sdf); Monster_Free(&monster); if(out!=stdout)fclose(out); return 1; }
            /* Primera repetición de reuso no medida; medir la siguiente invocación. */
            t0=NowMs(); bool meshWarmOk=SDFMesher_GenerateMesh(&mesher,&field,&mesh);
            if(!meshWarmOk) { WriteRow(out,recipe->name,recipe->id,stage,rep,"mesh_generation_steady","failed",NowMs()-t0,0,0,0,0,0,0,0); Mesh_Free(&mesh); SDFMesher_Free(&mesher); MonsterSDF_Free(&sdf); Monster_Free(&monster); if(out!=stdout)fclose(out); return 1; }
            t0=NowMs(); bool steadyMeshOk=SDFMesher_GenerateMesh(&mesher,&field,&mesh);
            SDFMesherStats steadyMs=mesher.lastStats;
            double steadyMeshElapsed=NowMs()-t0;
            WriteRow(out,recipe->name,recipe->id,stage,rep,"mesh_generation_steady",steadyMeshOk?"measured":"failed",steadyMeshElapsed,
                steadyMs.distanceEvaluationCount,steadyMs.connectorCandidateCount,steadyMs.connectorExactEvaluationCount,
                steadyMs.connectorPrunedCount,mesh.vertexCount,mesh.indexCount/3,MeshBytes(&mesh));
            if(!steadyMeshOk) { Mesh_Free(&mesh); SDFMesher_Free(&mesher); MonsterSDF_Free(&sdf); Monster_Free(&monster); if(out!=stdout)fclose(out); return 1; }

            SurfaceMapperStats mapStats;
            t0 = NowMs(); SurfaceMapper_MapMeshWithStats(&mesh,&monster.anatomyGraph,&monster.surfaceMapping,&mapStats);
            double surfaceMapMs=NowMs()-t0;
            WriteRow(out, recipe->name, recipe->id, stage, rep, "surface_mapping", "measured", surfaceMapMs,0,0,0,0,
                mesh.vertexCount,mesh.indexCount/3,MeshBytes(&mesh));

            FurRootSet roots = {0}; size_t rootBytes=0; double furBuildMs=0.0; const FurPhenotype* fur = &monster.surface.integument.fur;
            if (fur->density > 0 && mesh.vertexCount && mesh.indexCount >= 3) {
                t0=NowMs(); bool furOk=FurRootSet_Build(&roots,&mesh,fur->seed,fur->density,200000);
                furBuildMs=NowMs()-t0;
                rootBytes=roots.count*sizeof(FurRoot);
                WriteRow(out, recipe->name, recipe->id, stage, rep, "short_fur_roots", furOk?"measured":"failed", furBuildMs,
                    0,0,0,0,mesh.vertexCount,mesh.indexCount/3,rootBytes);
                FurRootSet_Free(&roots);
                if(!furOk) { Mesh_Free(&mesh); SDFMesher_Free(&mesher); MonsterSDF_Free(&sdf); Monster_Free(&monster); if(out!=stdout)fclose(out); return 1; }
            } else WriteRow(out,recipe->name,recipe->id,stage,rep,"short_fur_roots","not-present",0,0,0,0,0,0,0,0);
            WriteRow(out,recipe->name, recipe->id, stage, rep, "long_hair_rebuild", "not_profiled_in_generic_harness",0,0,0,0,0,0,0,0);
            WriteRow(out,recipe->name,recipe->id,stage,rep,"end_to_end_cpu_creation","not_measured_noncontiguous",0,0,0,0,0,0,0,0);

            if (monster.animation) {
                t0=NowMs(); bool poseOk=MonsterAnimation_Update(&monster,1.0f/60.0f);
                WriteRow(out, recipe->name, recipe->id, stage, rep, "pose_only_update", poseOk?"measured":"failed",NowMs()-t0,0,0,0,0,0,0,sizeof(*monster.animation));
                if(!poseOk) { Mesh_Free(&mesh); SDFMesher_Free(&mesher); MonsterSDF_Free(&sdf); Monster_Free(&monster); if(out!=stdout)fclose(out); return 1; }
            } else WriteRow(out,recipe->name,recipe->id,stage,rep,"pose_only_update","not-present_no_animation",0,0,0,0,0,0,0,0);

            WriteRow(out,recipe->name,recipe->id,stage,rep,"sum_of_profiled_cpu_phases","measured",
                creationMs+sdfBuildMs+meshBuildMs+surfaceMapMs+furBuildMs,0,0,0,0,0,0,
                MonsterBytes(&monster)+SdfBytes(&sdf)+MeshBytes(&mesh)+rootBytes);

            SurfacePhenotype recolored=monster.surface;
            recolored.pigment.baseColor.r=(uint8_t)(recolored.pigment.baseColor.r ^ 1u);
            t0=NowMs(); Monster_SetSurface(&monster,&recolored);
            WriteRow(out,recipe->name,recipe->id,stage,rep,"color_only_data_update","measured",NowMs()-t0,0,0,0,0,0,0,sizeof(recolored));

            if (stage < 3) {
                t0=NowMs(); CreaturePhenotype next=CreaturePhenotype_Interpolate(endpoint,
                    CreatureRecipe_GetStage(recipe,(CreatureStage)(stage+1)),0.5f);
                Monster growth=Monster_Create(); bool growthOk=Creature_BuildMonster(&growth,recipe,&next);
                WriteRow(out,recipe->name,recipe->id,stage,rep,"growth_update_build",growthOk?"measured":"failed",NowMs()-t0,0,0,0,0,0,0,MonsterBytes(&growth));
                Monster_Free(&growth);
                if(!growthOk) { Mesh_Free(&mesh); SDFMesher_Free(&mesher); MonsterSDF_Free(&sdf); Monster_Free(&monster); if(out!=stdout)fclose(out); return 1; }
            } else WriteRow(out,recipe->name,recipe->id,stage,rep,"growth_update_build","not-present_terminal_stage",0,0,0,0,0,0,0,0);
            fflush(out);
            Mesh_Free(&mesh); SDFMesher_Free(&mesher); MonsterSDF_Free(&sdf); Monster_Free(&monster);
        }
    }
    fclose(warmupOut);
    if (reportOut != stdout) fclose(reportOut);
    return 0;
}
