/**
 * @file test_aquatic_anatomy.c
 * @brief Pruebas integrales para la expansión anatómica acuática (recetas 1021..1025).
 *
 * Cubre carga/round-trip JSON, anatomía modular (fusiforme, vertical curvada, aletas,
 * propulsores caudales, caparazón, perfiles cefálicos), geometría SDF + mallas
 * suplementarias, cinco modos de nado, morphing entre especies, CreatureOffspring V4
 * en ambos órdenes parentales y métricas de rendimiento.
 */

#include "test_utils.h"
#include "Creature.h"
#include "CreatureMorph.h"
#include "CreatureOffspring.h"
#include "CreatureRecipeIO.h"
#include "CreatureRecipeRegistry.h"
#include "Monster.h"
#include "MonsterAger.h"
#include "MonsterAnimation.h"
#include "MonsterSDF.h"
#include "SupplementaryMesh.h"
#include "WorldInterface.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define CHECK(cond) TEST_ASSERT((cond), #cond)

static double NowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec * 1e-6;
}

static const char* const AQUATIC_PATHS[5] = {
    "recipes/atun_rojo.creature.json",
    "recipes/tiburon_martillo.creature.json",
    "recipes/caballito_mar.creature.json",
    "recipes/tortuga_marina.creature.json",
    "recipes/delfin_mular.creature.json"
};
static const CreatureRecipeId AQUATIC_IDS[5] = {1021u, 1022u, 1023u, 1024u, 1025u};

static bool SaveRecipeFile(const CreatureRecipe* recipe, const char* path) {
    FILE* f = fopen(path, "wb");
    if (!f) return false;
    bool ok = CreatureRecipeIO_WriteJSON(f, recipe);
    fclose(f);
    return ok;
}

static bool LoadDoc(const char* path, CreatureRecipeDocument* out) {
    char err[256] = {0};
    memset(out, 0, sizeof(*out));
    if (!CreatureRecipeIO_LoadFile(out, path, err, sizeof(err))) {
        fprintf(stderr, "Error cargando %s: %s\n", path, err);
        return false;
    }
    return true;
}

static void CheckMeshValid(const Mesh* mesh) {
    for (size_t i = 0; i < mesh->vertexCount; ++i) {
        Vector3 p = mesh->vertices[i].position;
        Vector3 n = mesh->vertices[i].normal;
        CHECK(isfinite(p.x) && isfinite(p.y) && isfinite(p.z));
        CHECK(isfinite(n.x) && isfinite(n.y) && isfinite(n.z));
    }
    for (size_t i = 0; i < mesh->indexCount; ++i) {
        CHECK(mesh->indices[i] < mesh->vertexCount);
    }
}

static void test_aquatic_recipes_and_roundtrip(void) {
    CreatureRecipeDocument* docs = calloc(5, sizeof(*docs));
    CHECK(docs != NULL);
    for (int i = 0; i < 5; ++i) {
        CHECK(LoadDoc(AQUATIC_PATHS[i], &docs[i]));
        CHECK(docs[i].recipe.id == AQUATIC_IDS[i]);
        for (int j = 0; j < i; ++j)
            CHECK(docs[i].recipe.id != docs[j].recipe.id);

        size_t order[CREATURE_MAX_MODULES];
        CHECK(CreatureRecipe_ResolutionOrder(&docs[i].recipe, order));
        for (int s = 0; s < 4; ++s) {
            const CreaturePhenotype* p = CreatureRecipe_GetStage(&docs[i].recipe, (CreatureStage)s);
            CHECK(p != NULL);
            CHECK(CreatureRecipe_Validate(&docs[i].recipe, p));
        }

        char tmpPath[128];
        snprintf(tmpPath, sizeof(tmpPath), "/tmp/test_aquatic_rt_%u.creature.json", AQUATIC_IDS[i]);
        CHECK(SaveRecipeFile(&docs[i].recipe, tmpPath));
        CreatureRecipeDocument* rt = calloc(1, sizeof(*rt));
        CHECK(rt != NULL);
        CHECK(LoadDoc(tmpPath, rt));
        CHECK(rt->recipe.id == docs[i].recipe.id);
        CHECK(rt->recipe.bodyPlan.moduleCount == docs[i].recipe.bodyPlan.moduleCount);
        CHECK(rt->recipe.adult.finCount == docs[i].recipe.adult.finCount);
        CHECK(FLOAT_NEAR(rt->recipe.adult.shell.development, docs[i].recipe.adult.shell.development));
        free(rt);
        remove(tmpPath);
    }
    free(docs);
    printf("[PASS] test_aquatic_anatomy: carga, validación de 4 etapas y round-trip JSON (1021..1025)\n");
}

static void test_aquatic_anatomy_and_geometry(void) {
    double tBuild = 0.0, tSdf = 0.0, tSupp = 0.0;
    const float ages[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};

    for (int i = 0; i < 5; ++i) {
        CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
        Monster* m = calloc(1, sizeof(*m));
        MonsterSDF* sdf = calloc(1, sizeof(*sdf));
        CHECK(doc && m && sdf);
        CHECK(LoadDoc(AQUATIC_PATHS[i], doc));

        for (size_t ai = 0; ai < sizeof(ages) / sizeof(ages[0]); ++ai) {
            CreaturePhenotype p = CreatureRecipe_EvaluateDevelopment(&doc->recipe, ages[ai]);
            *m = Monster_Create();
            double t0 = NowMs();
            CHECK(Creature_BuildMonster(m, &doc->recipe, &p));
            tBuild += NowMs() - t0;

            CHECK(m->hasAnatomyGraph && AnatomyGraph_Validate(&m->anatomyGraph));
            CHECK(m->hasHead);
            for (size_t ni = 0; ni < m->anatomyGraph.nodeCount; ++ni) {
                Vector3 c = m->anatomyGraph.nodes[ni].center;
                CHECK(isfinite(c.x) && isfinite(c.y) && isfinite(c.z));
                CHECK(m->anatomyGraph.nodes[ni].widthRadius > 0.0f);
                CHECK(m->anatomyGraph.nodes[ni].heightRadius > 0.0f);
            }

            *sdf = MonsterSDF_Create();
            t0 = NowMs();
            CHECK(MonsterSDF_Build(sdf, m, MonsterSDF_DefaultConfig()));
            tSdf += NowMs() - t0;
            Vector3 sz = AABB_Size(sdf->bounds);
            CHECK(sz.x > 0.05f && sz.y > 0.05f && sz.z > 0.05f);

            Mesh op = Mesh_Create(), tr = Mesh_Create();
            t0 = NowMs();
            CHECK(SupplementaryMesh_Build(m, &op, &tr));
            tSupp += NowMs() - t0;
            CHECK(op.vertexCount > 100 && op.indexCount >= 300);
            CheckMeshValid(&op);
            CheckMeshValid(&tr);

            if (m->animation) {
                AnatomyDeformer* defOp = AnatomyDeformer_Create();
                AnatomyDeformer* defTr = AnatomyDeformer_Create();
                CHECK(defOp && defTr);
                CHECK(SupplementaryMesh_BindDeformers(m, &m->animation->rig.skeleton,
                                                      &op, defOp, &tr, defTr));
                AnatomyDeformer_Free(defOp);
                AnatomyDeformer_Free(defTr);
            }
            Mesh_Free(&op);
            Mesh_Free(&tr);
            MonsterSDF_Free(sdf);
            Monster_Free(m);
        }

        const CreaturePhenotype* ad = &doc->recipe.adult;
        if (AQUATIC_IDS[i] == 1021u) {
            CHECK(ad->axial.archetype == AXIAL_ARCHETYPE_AQUATIC_FUSIFORM);
            CHECK(ad->head.archetype == HEAD_ARCHETYPE_TELEOST);
            CHECK(ad->finCount == 9);
            CHECK(ad->tails[0].caudalFinType == CAUDAL_FIN_HOMOCERCAL_LUNATE);
        } else if (AQUATIC_IDS[i] == 1022u) {
            CHECK(ad->axial.archetype == AXIAL_ARCHETYPE_AQUATIC_FUSIFORM);
            CHECK(ad->head.archetype == HEAD_ARCHETYPE_ELASMOBRANCH);
            CHECK(ad->finCount == 7);
            CHECK(ad->tails[0].caudalFinType == CAUDAL_FIN_HETEROCERCAL);
            CHECK(ad->tails[0].caudalUpperLobeRatio > 1.3f);
        } else if (AQUATIC_IDS[i] == 1023u) {
            CHECK(ad->axial.archetype == AXIAL_ARCHETYPE_CURVED_VERTICAL);
            CHECK(ad->head.archetype == HEAD_ARCHETYPE_SYNGNATHID);
            CHECK(ad->axial.abdomenSegments == 11);
            CHECK(ad->tails[0].archetype == TAIL_ARCHETYPE_PREHENSILE);
            CHECK(ad->tails[0].caudalFinType == CAUDAL_FIN_NONE);
        } else if (AQUATIC_IDS[i] == 1024u) {
            CHECK(ad->axial.archetype == AXIAL_ARCHETYPE_SPRAWLING_TETRAPOD);
            CHECK(ad->head.archetype == HEAD_ARCHETYPE_TESTUDINE);
            CHECK(ad->shell.development > 0.9f);
            CHECK(ad->shell.costalScutePairs == 4);
            CHECK(ad->limbCount == 4);
            CHECK(ad->limbs[0].anatomyProfile == LIMB_PROFILE_MARINE_FLIPPER);
        } else if (AQUATIC_IDS[i] == 1025u) {
            CHECK(ad->axial.archetype == AXIAL_ARCHETYPE_AQUATIC_FUSIFORM);
            CHECK(ad->head.archetype == HEAD_ARCHETYPE_CETACEAN);
            CHECK(ad->limbCount == 2);
            CHECK(ad->finCount == 1);
            CHECK(ad->tails[0].caudalFinType == CAUDAL_FIN_HORIZONTAL_FLUKES);
        }
        free(sdf);
        free(m);
        free(doc);
    }
    printf("[PASS] test_aquatic_anatomy: 5 especies x 5 edades (build=%.1fms sdf=%.1fms supp=%.1fms)\n",
           tBuild, tSdf, tSupp);
}

static void test_aquatic_swimming_animation(void) {
    for (int i = 0; i < 5; ++i) {
        CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
        Monster* m = calloc(1, sizeof(*m));
        CHECK(doc && m && LoadDoc(AQUATIC_PATHS[i], doc));
        *m = Monster_Create();
        CHECK(Creature_BuildMonster(m, &doc->recipe, &doc->recipe.adult));
        CHECK(m->animation != NULL);

        m->animation->animator.desiredVelocity = Vec3_Create(0.0f, 0.0f, 1.8f);
        float maxTailDx = 0.0f, maxTailDy = 0.0f;
        int tailTipJoint = -1;
        if (m->animation->rig.tailCount > 0 && m->animation->rig.tails[0].jointCount > 0) {
            const TailRig* tr = &m->animation->rig.tails[0];
            tailTipJoint = tr->joints[tr->jointCount - 1];
        }
        Vector3 baseTailRel = Vec3_Zero();
        if (tailTipJoint >= 0) {
            baseTailRel = Vec3_Sub(m->animation->pose.joints[tailTipJoint].worldPosition,
                                   m->animation->pose.joints[0].worldPosition);
        }

        for (int step = 0; step < 30; ++step) {
            CHECK(MonsterAnimation_Update(m, 1.0f / 30.0f));
            if (tailTipJoint >= 0) {
                Vector3 rel = Vec3_Sub(m->animation->pose.joints[tailTipJoint].worldPosition,
                                       m->animation->pose.joints[0].worldPosition);
                maxTailDx = fmaxf(maxTailDx, fabsf(rel.x - baseTailRel.x));
                maxTailDy = fmaxf(maxTailDy, fabsf(rel.y - baseTailRel.y));
            }
        }
        if (AQUATIC_IDS[i] == 1025u) {
            CHECK(maxTailDy > maxTailDx * 1.2f);
        } else if (AQUATIC_IDS[i] == 1021u || AQUATIC_IDS[i] == 1022u) {
            CHECK(maxTailDx > maxTailDy * 1.2f);
        }

        Monster_Free(m);
        free(m);
        free(doc);
    }
    printf("[PASS] test_aquatic_anatomy: 5 modos de nado acuático (lateral vs dorsoventral vs aletas)\n");
}

static void test_aquatic_morph_matrix(void) {
    const char* pairs[][2] = {
        {"recipes/atun_rojo.creature.json", "recipes/tiburon_martillo.creature.json"},
        {"recipes/atun_rojo.creature.json", "recipes/delfin_mular.creature.json"},
        {"recipes/tiburon_martillo.creature.json", "recipes/mantarraya.creature.json"},
        {"recipes/caballito_mar.creature.json", "recipes/atun_rojo.creature.json"},
        {"recipes/tortuga_marina.creature.json", "recipes/ornitorrinco.creature.json"},
        {"recipes/delfin_mular.creature.json", "recipes/tiburon_martillo.creature.json"},
        {"recipes/tortuga_marina.creature.json", "recipes/delfin_mular.creature.json"},
    };
    const float ts[] = {0.0f, 0.10f, 0.25f, 0.50f, 0.75f, 0.90f, 1.0f};
    double tMorph = 0.0;
    size_t evalCount = 0;

    for (size_t pi = 0; pi < sizeof(pairs) / sizeof(pairs[0]); ++pi) {
        CreatureRecipeDocument *da = calloc(1, sizeof(*da)), *db = calloc(1, sizeof(*db));
        Monster *ma = calloc(1, sizeof(*ma)), *mb = calloc(1, sizeof(*mb)), *mid = calloc(1, sizeof(*mid));
        MonsterSDF *sdf = calloc(1, sizeof(*sdf));
        CHECK(da && db && ma && mb && mid && sdf);
        CHECK(LoadDoc(pairs[pi][0], da) && LoadDoc(pairs[pi][1], db));
        *ma = Monster_Create(); *mb = Monster_Create(); *mid = Monster_Create();
        CHECK(Creature_BuildMonster(ma, &da->recipe, &da->recipe.adult));
        CHECK(Creature_BuildMonster(mb, &db->recipe, &db->recipe.adult));

        for (int dir = 0; dir < 2; ++dir) {
            const Monster* src = dir == 0 ? ma : mb;
            const Monster* dst = dir == 0 ? mb : ma;
            for (size_t ti = 0; ti < sizeof(ts) / sizeof(ts[0]); ++ti) {
                *mid = Monster_Create();
                double t0 = NowMs();
                CHECK(CreatureMorph_Interpolate(src, dst, ts[ti], mid));
                tMorph += NowMs() - t0;
                ++evalCount;
                CHECK(mid->hasAnatomyGraph && AnatomyGraph_Validate(&mid->anatomyGraph));
                if (ti == 0 || ti == 3 || ti == 6) {
                    *sdf = MonsterSDF_Create();
                    CHECK(MonsterSDF_Build(sdf, mid, MonsterSDF_DefaultConfig()));
                    Mesh op = Mesh_Create(), tr = Mesh_Create();
                    CHECK(SupplementaryMesh_Build(mid, &op, &tr));
                    CheckMeshValid(&op);
                    CheckMeshValid(&tr);
                    Mesh_Free(&op);
                    Mesh_Free(&tr);
                    MonsterSDF_Free(sdf);
                }
                Monster_Free(mid);
            }
        }
        Monster_Free(ma); Monster_Free(mb);
        free(sdf); free(ma); free(mb); free(mid); free(da); free(db);
    }
    printf("[PASS] test_aquatic_anatomy: matriz morphing (7 pares x ida/vuelta x 7 fases, %.2f ms/eval)\n",
           tMorph / (double)(evalCount ? evalCount : 1));
}

static void test_aquatic_offspring_v4_matrix(void) {
    const char* pairs[][2] = {
        {"recipes/atun_rojo.creature.json", "recipes/tiburon_martillo.creature.json"},
        {"recipes/atun_rojo.creature.json", "recipes/delfin_mular.creature.json"},
        {"recipes/tiburon_martillo.creature.json", "recipes/mantarraya.creature.json"},
        {"recipes/caballito_mar.creature.json", "recipes/atun_rojo.creature.json"},
        {"recipes/caballito_mar.creature.json", "recipes/pulpo_gigante.creature.json"},
        {"recipes/tortuga_marina.creature.json", "recipes/ornitorrinco.creature.json"},
        {"recipes/tortuga_marina.creature.json", "recipes/pangolin.creature.json"},
        {"recipes/delfin_mular.creature.json", "recipes/explorador.creature.json"},
        {"recipes/delfin_mular.creature.json", "recipes/mantarraya.creature.json"},
        {"recipes/tiburon_martillo.creature.json", "recipes/halcon_peregrino.creature.json"},
        {"recipes/elefante.creature.json", "recipes/halcon_peregrino.creature.json"},
    };
    double tOff = 0.0;
    size_t totalGenerated = 0;

    for (size_t pi = 0; pi < sizeof(pairs) / sizeof(pairs[0]); ++pi) {
        CreatureRecipeDocument *da = calloc(1, sizeof(*da)), *db = calloc(1, sizeof(*db));
        CHECK(da && db && LoadDoc(pairs[pi][0], da) && LoadDoc(pairs[pi][1], db));

        for (int order = 0; order < 2; ++order) {
            const CreatureRecipe* pA = order == 0 ? &da->recipe : &db->recipe;
            const CreatureRecipe* pB = order == 0 ? &db->recipe : &da->recipe;
            char err[256] = {0};
            CreatureOffspringConfig cfg = CreatureOffspring_Default(2, 42u + (uint32_t)pi);
            double t0 = NowMs();
            CreatureOffspringBatch* batch = CreatureOffspring_Generate(pA, pB, cfg, NULL, NULL, err, sizeof(err));
            tOff += NowMs() - t0;
            if (!batch) fprintf(stderr, "Offspring fail pair %zu order %d: %s\n", pi, order, err);
            CHECK(batch != NULL && CreatureOffspring_Count(batch) == 2);
            totalGenerated += CreatureOffspring_Count(batch);

            for (size_t ci = 0; ci < CreatureOffspring_Count(batch); ++ci) {
                const OffspringReport* rep = NULL;
                const CreatureRecipe* child = CreatureOffspring_Get(batch, ci, &rep);
                CHECK(child != NULL && rep != NULL);
                CHECK(rep->effectiveDonorContribution > 0.01f);
                for (int s = 0; s < 4; ++s) {
                    const CreaturePhenotype* st = CreatureRecipe_GetStage(child, (CreatureStage)s);
                    CHECK(CreatureRecipe_Validate(child, st));
                }
                if (ci == 0) {
                    Monster* m = calloc(1, sizeof(*m));
                    CHECK(m != NULL);
                    *m = Monster_Create();
                    CHECK(Creature_BuildMonster(m, child, &child->adult));
                    CHECK(AnatomyGraph_Validate(&m->anatomyGraph));
                    Monster_Free(m);
                    free(m);

                    char tmpPath[128];
                    snprintf(tmpPath, sizeof(tmpPath), "/tmp/test_aquatic_off_%zu_%d.creature.json", pi, order);
                    CHECK(SaveRecipeFile(child, tmpPath));
                    CreatureRecipeDocument* reload = calloc(1, sizeof(*reload));
                    CHECK(reload && LoadDoc(tmpPath, reload));
                    free(reload);
                    remove(tmpPath);
                }
            }
            CreatureOffspring_Destroy(batch);
        }
        free(da);
        free(db);
    }
    printf("[PASS] test_aquatic_anatomy: CreatureOffspring V4 (11 pares x 2 órdenes = %zu híbridos en %.1f ms)\n",
           totalGenerated, tOff);
}

void run_aquatic_anatomy_tests(void) {
    printf("\n--- Suite: Expansión Anatómica Acuática (1021..1025) ---\n");
    test_aquatic_recipes_and_roundtrip();
    test_aquatic_anatomy_and_geometry();
    test_aquatic_swimming_animation();
    test_aquatic_morph_matrix();
    test_aquatic_offspring_v4_matrix();
    printf("[PASS] run_aquatic_anatomy_tests: TODAS LAS PRUEBAS PASARON\n");
}
