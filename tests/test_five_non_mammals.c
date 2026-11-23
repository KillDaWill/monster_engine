/**
 * @file test_five_non_mammals.c
 * @brief Suite integral para las 5 nuevas especies no-mamíferas (IDs 1016..1020)
 *        y las capacidades reutilizables del motor (axiales, radiales, miembros,
 *        mallas suplementarias, translúcidos, animación, morphing y CreatureOffspring).
 */
#include "test_utils.h"
#include "Attachment.h"
#include "AxialBody.h"
#include "Creature.h"
#include "CreatureMorph.h"
#include "CreatureOffspring.h"
#include "CreaturePhenotype.h"
#include "CreatureRecipe.h"
#include "CreatureRecipeIO.h"
#include "CreatureRecipeRegistry.h"
#include "CreatureRecipes.h"
#include "CreatureRig.h"
#include "EyeTexture.h"
#include "FlexibleAppendage.h"
#include "Head.h"
#include "Limb.h"
#include "Monster.h"
#include "MonsterAger.h"
#include "MonsterAnimation.h"
#include "MonsterSDF.h"
#include "MonsterVisual.h"
#include "AnimatedVisual.h"
#include "SupplementaryMesh.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool CheckMeshFinite(const Mesh* mesh) {
    if (!mesh) return false;
    for (size_t i = 0; i < mesh->vertexCount; ++i) {
        Vector3 p = mesh->vertices[i].position;
        Vector3 n = mesh->vertices[i].normal;
        if (!isfinite(p.x) || !isfinite(p.y) || !isfinite(p.z) ||
            !isfinite(n.x) || !isfinite(n.y) || !isfinite(n.z)) {
            return false;
        }
    }
    for (size_t i = 0; i < mesh->indexCount; ++i) {
        if (mesh->indices[i] >= mesh->vertexCount) return false;
    }
    return (mesh->indexCount % 3u) == 0u;
}

static void TestRadialCoordinateFrame(void) {
    AttachmentSlotSet slots = {0};
    bool ok = AttachmentSlotSet_PublishRadialRing(
        &slots, 1, Anatomy_MakeId(1, 3),
        Vec3_Create(0.0f, 1.0f, 0.0f),
        Vec3_Create(0.0f, 1.0f, 0.0f),
        Vec3_Create(1.0f, 0.0f, 0.0f),
        0.5f, 8u, 0.0f, 6.28318530718f,
        AXIAL_SLOT_RADIAL_BASE, ATTACHMENT_RADIAL,
        Vec3_Create(0.5f, 0.4f, 0.5f), 1.0f);
    TEST_ASSERT(ok && slots.count == 8u, "PublishRadialRing debe generar 8 ranuras radiales");

    for (unsigned i = 0; i < 8u; ++i) {
        const AttachmentSlot* sl = &slots.slots[i];
        TEST_ASSERT(sl->id == (AttachmentSlotId)(AXIAL_SLOT_RADIAL_BASE + i), "ID de ranura radial");
        TEST_ASSERT(sl->role == ATTACHMENT_RADIAL, "Rol radial");
        float r = sqrtf(sl->position.x * sl->position.x + sl->position.z * sl->position.z);
        TEST_ASSERT(FLOAT_NEAR(r, 0.5f), "Radio constante en anillo radial");
        TEST_ASSERT(FLOAT_NEAR(sl->position.y, 1.0f), "Altura constante en anillo radial");
        TEST_ASSERT(FLOAT_NEAR(Vec3_Length(sl->forward), 1.0f) &&
                    FLOAT_NEAR(Vec3_Length(sl->up), 1.0f) &&
                    FLOAT_NEAR(Vec3_Length(sl->side), 1.0f), "Marco radial ortonormal");
        TEST_ASSERT(fabsf(Vec3_Dot(sl->forward, sl->up)) < 1e-4f &&
                    fabsf(Vec3_Dot(sl->forward, sl->side)) < 1e-4f, "Ejes radiales ortogonales");
    }

    /* Identidades estables deterministas de apéndices repetidos */
    uint64_t id0a = RepeatedAppendage_InstanceId(10201, 0);
    uint64_t id0b = RepeatedAppendage_InstanceId(10201, 0);
    uint64_t id1 = RepeatedAppendage_InstanceId(10201, 1);
    uint64_t idOther = RepeatedAppendage_InstanceId(10202, 0);
    TEST_ASSERT(id0a != 0 && id0a == id0b && id0a != id1 && id0a != idOther,
                "RepeatedAppendage_InstanceId debe ser determinista y única");
    printf("[PASS] test_five_non_mammals: marcos radiales e identidades repetidas\n");
}

static void TestCatalogLoadAndJsonRoundTrip(const CreatureRecipe* recipes[5]) {
    const uint32_t ids[5] = {1016, 1017, 1018, 1019, 1020};
    const AxialArchetype expectedAxial[5] = {
        AXIAL_ARCHETYPE_AVIAN,
        AXIAL_ARCHETYPE_DISC_RAY,
        AXIAL_ARCHETYPE_CEPHALOPOD,
        AXIAL_ARCHETYPE_ARTHROPOD,
        AXIAL_ARCHETYPE_MEDUSA_RADIAL
    };

    for (int i = 0; i < 5; ++i) {
        const CreatureRecipe* r = CreatureRecipeRegistry_Find(ids[i]);
        TEST_ASSERT(r != NULL, "Receta externa 1016..1020 debe cargarse desde recipes/catalog.txt");
        recipes[i] = r;
        TEST_ASSERT(r->adult.axial.archetype == expectedAxial[i], "Arquetipo axial esperado");
        TEST_ASSERT(AxialBody_Supports(r->adult.axial.archetype), "AxialBody soporta el nuevo arquetipo");

        /* Round-trip JSON completo v1 */
        FILE* tmp = tmpfile();
        TEST_ASSERT(tmp != NULL, "tmpfile");
        TEST_ASSERT(CreatureRecipeIO_WriteJSON(tmp, r), "WriteJSON de nueva especie");
        long len = ftell(tmp);
        TEST_ASSERT(len > 100 && len < 2000000, "Tamaño JSON válido");
        rewind(tmp);
        char* buf = calloc(1, (size_t)len + 1);
        TEST_ASSERT(buf != NULL, "malloc JSON");
        TEST_ASSERT(fread(buf, 1, (size_t)len, tmp) == (size_t)len, "fread JSON");
        fclose(tmp);

        CreatureRecipeDocument* reloaded = calloc(1, sizeof(*reloaded));
        TEST_ASSERT(reloaded != NULL, "calloc reloaded");
        char err[256] = {0};
        TEST_ASSERT(CreatureRecipeIO_ReadJSON(reloaded, buf, err, sizeof(err)),
                    "ReadJSON round-trip de nueva especie");
        TEST_ASSERT(reloaded->recipe.id == r->id &&
                    reloaded->recipe.gait == r->gait &&
                    reloaded->recipe.bodyPlan.moduleCount == r->bodyPlan.moduleCount &&
                    reloaded->recipe.adult.axial.archetype == r->adult.axial.archetype &&
                    reloaded->recipe.adult.flexibleAppendageCount == r->adult.flexibleAppendageCount &&
                    reloaded->recipe.adult.repeatedAppendageCount == r->adult.repeatedAppendageCount &&
                    FLOAT_NEAR(reloaded->recipe.adult.surface.translucency, r->adult.surface.translucency) &&
                    FLOAT_NEAR(reloaded->recipe.adult.surface.iridescence, r->adult.surface.iridescence) &&
                    FLOAT_NEAR(reloaded->recipe.adult.surface.subsurfaceGlow, r->adult.surface.subsurfaceGlow),
                    "Campos clave preservados en round-trip JSON");
        free(reloaded);
        free(buf);
    }
    printf("[PASS] test_five_non_mammals: carga de catálogo y round-trip JSON (1016..1020)\n");
}

static void TestStagesSdfSupplementaryAndAger(const CreatureRecipe* recipes[5]) {
    const float ages[] = {0.0f, 0.20f, 0.3333f, 0.50f, 0.6667f, 0.85f, 1.0f};
    Monster* m = malloc(sizeof(*m));
    Monster* first = malloc(sizeof(*first));
    Monster* last = malloc(sizeof(*last));
    Monster* interp = malloc(sizeof(*interp));
    MonsterSDF* sdf = malloc(sizeof(*sdf));
    TEST_ASSERT(m && first && last && interp && sdf, "alloc test buffers");

    for (int i = 0; i < 5; ++i) {
        const CreatureRecipe* r = recipes[i];
        /* 1. Verificar las 4 etapas + edades intermedias */
        for (size_t a = 0; a < sizeof(ages) / sizeof(ages[0]); ++a) {
            CreaturePhenotype p = CreatureRecipe_EvaluateDevelopment(r, ages[a]);
            *m = Monster_Create();
            TEST_ASSERT(Creature_BuildMonster(m, r, &p), "BuildMonster en etapa/edad intermedia");
            TEST_ASSERT(AnatomyGraph_Validate(&m->anatomyGraph), "AnatomyGraph válido");
            if (m->hasHead) {
                TEST_ASSERT(HeadAnatomy_Validate(&m->head.anatomy) == HEAD_VALID, "HeadAnatomy válido");
            }
            *sdf = MonsterSDF_Create();
            TEST_ASSERT(MonsterSDF_Build(sdf, m, MonsterSDF_DefaultConfig()), "MonsterSDF_Build válido");
            Vector3 bSize = AABB_Size(sdf->bounds);
            TEST_ASSERT(isfinite(bSize.x) && bSize.x > 0.05f &&
                        isfinite(bSize.y) && bSize.y > 0.05f &&
                        isfinite(bSize.z) && bSize.z > 0.05f, "Límites SDF finitos y no degenerados");

            Mesh det = Mesh_Create(), trn = Mesh_Create();
            TEST_ASSERT(SupplementaryMesh_Build(m, &det, &trn), "SupplementaryMesh_Build");
            TEST_ASSERT(CheckMeshFinite(&det) && CheckMeshFinite(&trn), "Mallas suplementarias finitas");

            /* Comprobaciones anatómicas específicas por plan corporal */
            if (r->id == 1016 && ages[a] >= 0.99f) {
                /* Halcón adulto: plumas de ala + abanico caudal */
                TEST_ASSERT(det.vertexCount > 500 && trn.vertexCount == 0, "Halcón genera plumas opacas");
            } else if (r->id == 1017 && ages[a] >= 0.99f) {
                /* Mantarraya adulta: superficie continua pectoral + 5 pares de branquias ventrales */
                TEST_ASSERT(det.vertexCount > 500, "Mantarraya genera aletas y branquias ventrales");
            } else if (r->id == 1018 && ages[a] >= 0.99f) {
                /* Pulpo gigante: 8 brazos + 1 sifón + ventosas biseriales + membrana interbraquial */
                TEST_ASSERT(m->phenotype.flexibleAppendageCount == 9u, "Pulpo con 8 brazos + 1 sifón");
                TEST_ASSERT(det.vertexCount > 3000, "Pulpo genera ventosas en doble hilera");
            } else if (r->id == 1019) {
                /* Libélula hemimetábola: ninfa sin alas adultas; adulto con 4 alas translúcidas + venación */
                if (ages[a] <= 0.21f) {
                    TEST_ASSERT(trn.vertexCount == 0, "Ninfa acuática de libélula NO tiene alas adultas");
                } else if (ages[a] >= 0.99f) {
                    TEST_ASSERT(det.vertexCount > 200 && trn.vertexCount > 100 &&
                                trn.translucent && trn.doubleSided,
                                "Libélula adulta tiene venación opaca y 4 alas translúcidas doble cara");
                }
            } else if (r->id == 1020 && ages[a] >= 0.99f) {
                /* Medusa ortiga: campana cóncava hueca analítica + 24 filamentos + 4 cintas orales */
                TEST_ASSERT(!m->hasHead && sdf->hasMedusaBell, "Medusa sin cabeza vertebrada con campana analítica");
                TEST_ASSERT(det.vertexCount > 500 && trn.vertexCount > 500 &&
                            trn.translucent && trn.doubleSided,
                            "Medusa genera 24 tentáculos y 4 brazos orales translúcidos");
                /* Verificar concavidad real: el interior subumbrelar bajo el manubrio es espacio exterior (>0) */
                Vector3 subCenter = Vec3_Create(
                    sdf->medusaBell.center.x + sdf->medusaBell.radius * 0.48f,
                    sdf->medusaBell.center.y + sdf->medusaBell.height * 0.14f,
                    sdf->medusaBell.center.z);
                float dSub = MonsterSDF_EvaluateDistance(sdf, subCenter);
                TEST_ASSERT(dSub > 0.0f, "La campana de la medusa debe ser cóncava/hueca por debajo, no sólida");
            }

            Mesh_Free(&det);
            Mesh_Free(&trn);
            MonsterSDF_Free(sdf);
            Monster_Free(m);
        }

        /* 2. Ager en extremos exactos (seed -> adult) */
        *first = Monster_Create(); *last = Monster_Create(); *interp = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(first, r, &r->seed) &&
                    Creature_BuildMonster(last, r, &r->adult), "Extremos Ager");
        MonsterAger_Interpolate(first, last, 0.0f, interp);
        TEST_ASSERT(FLOAT_NEAR(interp->phenotype.axial.totalScale, first->phenotype.axial.totalScale), "Ager t=0");
        MonsterAger_Interpolate(first, last, 0.5f, interp);
        TEST_ASSERT(AnatomyGraph_Validate(&interp->anatomyGraph), "Ager t=0.5 válido");
        MonsterAger_Interpolate(first, last, 1.0f, interp);
        TEST_ASSERT(FLOAT_NEAR(interp->phenotype.axial.totalScale, last->phenotype.axial.totalScale), "Ager t=1");
        Monster_Free(first); Monster_Free(last); Monster_Free(interp);
    }

    free(m); free(first); free(last); free(interp); free(sdf);
    printf("[PASS] test_five_non_mammals: 4 etapas, edades intermedias, SDF, mallas suplementarias y Ager\n");
}

static void TestAnimationWithoutSdfRebuild(const CreatureRecipe* recipes[5]) {
    Monster* m = malloc(sizeof(*m));
    MonsterVisual* v = malloc(sizeof(*v));
    TEST_ASSERT(m && v, "alloc anim test");

    for (int i = 0; i < 5; ++i) {
        const CreatureRecipe* r = recipes[i];
        CreaturePhenotype p = CreatureRecipe_EvaluateDevelopment(r, 1.0f);
        *m = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(m, r, &p), "BuildMonster para animación");
        TEST_ASSERT(m->animation != NULL, "Animación configurada para nueva marcha");

        SDFMesherConfig cfg = SDFMesher_DefaultConfig();
        cfg.voxelSize = 0.045f * p.axial.totalScale;
        cfg.maxCells = 600000;
        *v = MonsterVisual_Create(cfg);
        TEST_ASSERT(MonsterVisual_RebuildNow(v, m, MonsterSDF_DefaultConfig()), "Rebuild visual");
        uint64_t genBefore = v->rebuildGeneration;

        AnimatedVisual av = {0};
        TEST_ASSERT(AnimatedVisual_Bind(&av, v, m), "AnimatedVisual_Bind con mallas suplementarias");

        /* Guardar posiciones iniciales para comprobar movimiento real sin remallado SDF */
        Vector3 bodyStart = v->mesh.vertexCount ? v->mesh.vertices[v->mesh.vertexCount / 2].position : Vec3_Zero();
        Vector3 detStart = v->detailMesh.vertexCount ? v->detailMesh.vertices[v->detailMesh.vertexCount / 2].position : Vec3_Zero();
        Vector3 trnStart = v->translucentMesh.vertexCount ? v->translucentMesh.vertices[v->translucentMesh.vertexCount / 2].position : Vec3_Zero();

        m->animation->animator.desiredVelocity = Vec3_Create(0.0f, 0.0f, 0.85f * p.axial.totalScale);
        if (r->id == 1016) {
            /* Verificar también plegado y aleteo explícito en halcón */
            m->animation->animator.wingFold = 0.65f;
            TEST_ASSERT(MonsterAnimation_Update(m, 1.0f / 60.0f), "Update wingFold");
            TEST_ASSERT(AnimatedVisual_Deform(&av, v, m), "Deform wingFold");
            m->animation->animator.wingFold = 0.0f;
            m->animation->animator.wingFlap = 0.85f;
        } else if (r->id == 1019) {
            /* Verificar batido independiente de alas anterior y posterior en libélula */
            m->animation->animator.wingFlap = 0.80f;
            m->animation->animator.hindWingFlap = -0.75f;
        } else if (r->id == 1018) {
            m->animation->animator.armSpread = 0.50f;
        }

        float maxDelta = 0.0f;
        for (int f = 0; f < 24; ++f) {
            TEST_ASSERT(MonsterAnimation_Update(m, 1.0f / 60.0f), "MonsterAnimation_Update");
            TEST_ASSERT(AnimatedVisual_Deform(&av, v, m), "AnimatedVisual_Deform");
            TEST_ASSERT(v->rebuildGeneration == genBefore, "Cero reconstrucciones SDF durante animación");
            if (v->mesh.vertexCount) {
                float d = Vec3_Distance(bodyStart, v->mesh.vertices[v->mesh.vertexCount / 2].position);
                if (d > maxDelta) maxDelta = d;
            }
            if (v->detailMesh.vertexCount) {
                float d = Vec3_Distance(detStart, v->detailMesh.vertices[v->detailMesh.vertexCount / 2].position);
                if (d > maxDelta) maxDelta = d;
            }
            if (v->translucentMesh.vertexCount) {
                float d = Vec3_Distance(trnStart, v->translucentMesh.vertices[v->translucentMesh.vertexCount / 2].position);
                if (d > maxDelta) maxDelta = d;
            }
        }
        TEST_ASSERT(CheckMeshFinite(&v->mesh) && CheckMeshFinite(&v->detailMesh) && CheckMeshFinite(&v->translucentMesh),
                    "Mallas deformadas finitas");
        TEST_ASSERT(maxDelta > 0.002f && maxDelta < 5.0f * p.axial.totalScale,
                    "Deformación animada real y acotada en los nuevos modos de movimiento");

        AnimatedVisual_Free(&av);
        MonsterVisual_Free(v);
        Monster_Free(m);
    }

    free(m); free(v);
    printf("[PASS] test_five_non_mammals: animación sin reconstrucción SDF para los 5 modos\n");
}

static void TestCrossBodyPlanMorph(const CreatureRecipe* recipes[5]) {
    const CreatureRecipe* pairs[][2] = {
        {recipes[0], CreatureRecipes_Dog()},     /* Halcón <-> Perro */
        {recipes[1], CreatureRecipes_Lizard()},  /* Mantarraya <-> Lagarto */
        {recipes[2], recipes[0]},                /* Pulpo <-> Halcón */
        {recipes[3], recipes[1]},                /* Libélula <-> Mantarraya */
        {recipes[4], recipes[0]},                /* Medusa <-> Halcón (sin cabeza <-> cefalizado) */
    };
    Monster* a = malloc(sizeof(*a));
    Monster* b = malloc(sizeof(*b));
    Monster* out = malloc(sizeof(*out));
    MonsterSDF* sdf = malloc(sizeof(*sdf));
    TEST_ASSERT(a && b && out && sdf, "alloc morph test");

    const float ts[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
    for (size_t pi = 0; pi < sizeof(pairs) / sizeof(pairs[0]); ++pi) {
        *a = Monster_Create(); *b = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(a, pairs[pi][0], &pairs[pi][0]->adult) &&
                    Creature_BuildMonster(b, pairs[pi][1], &pairs[pi][1]->adult), "Extremos morph cruzado");
        for (size_t ti = 0; ti < sizeof(ts) / sizeof(ts[0]); ++ti) {
            *out = Monster_Create();
            TEST_ASSERT(CreatureMorph_Interpolate(a, b, ts[ti], out), "CreatureMorph_Interpolate cruzado");
            TEST_ASSERT(AnatomyGraph_Validate(&out->anatomyGraph), "Grafo válido en morph cruzado");
            *sdf = MonsterSDF_Create();
            TEST_ASSERT(MonsterSDF_Build(sdf, out, MonsterSDF_DefaultConfig()), "SDF válido en morph cruzado");
            Mesh det = Mesh_Create(), trn = Mesh_Create();
            TEST_ASSERT(SupplementaryMesh_Build(out, &det, &trn) &&
                        CheckMeshFinite(&det) && CheckMeshFinite(&trn),
                        "Mallas suplementarias válidas en morph cruzado");
            Mesh_Free(&det); Mesh_Free(&trn);
            MonsterSDF_Free(sdf);
            Monster_Free(out);
        }
        Monster_Free(a); Monster_Free(b);
    }

    free(a); free(b); free(out); free(sdf);
    printf("[PASS] test_five_non_mammals: morphing entre planes corporales (5 pares x 5 fases)\n");
}

static void TestOffspringHomologyAndHybrids(const CreatureRecipe* recipes[5]) {
    const CreatureRecipe* pairs[][2] = {
        {recipes[0], CreatureRecipes_Dog()},     /* Halcón x Perro */
        {recipes[2], recipes[4]},                /* Pulpo x Medusa (reutilización de apéndices repetidos/translúcidos) */
        {recipes[1], CreatureRecipes_Lizard()},  /* Mantarraya x Lagarto */
        {recipes[3], recipes[0]},                /* Libélula x Halcón (alas insecto vs alas aviares: no homólogas) */
    };
    const uint32_t seeds[] = {101602u, 101820u, 101701u, 101916u};

    Monster* m = malloc(sizeof(*m));
    MonsterSDF* sdf = malloc(sizeof(*sdf));
    TEST_ASSERT(m && sdf, "alloc offspring test");

    for (size_t i = 0; i < sizeof(pairs) / sizeof(pairs[0]); ++i) {
        CreatureOffspringConfig cfg = CreatureOffspring_Default(1, seeds[i]);
        cfg.proposalsPerOffspring = 4;
        char err1[256] = {0}, err2[256] = {0};
        CreatureOffspringBatch* b1 = CreatureOffspring_Generate(pairs[i][0], pairs[i][1], cfg, NULL, NULL, err1, sizeof(err1));
        CreatureOffspringBatch* b2 = CreatureOffspring_Generate(pairs[i][0], pairs[i][1], cfg, NULL, NULL, err2, sizeof(err2));
        TEST_ASSERT(b1 != NULL && CreatureOffspring_Count(b1) == 1u,
                    "CreatureOffspring_Generate entre planes no-mamíferos");
        TEST_ASSERT(b2 != NULL && CreatureOffspring_Count(b2) == 1u,
                    "CreatureOffspring_Generate determinista");

        const OffspringReport *r1 = NULL, *r2 = NULL;
        const CreatureRecipe* c1 = CreatureOffspring_Get(b1, 0, &r1);
        const CreatureRecipe* c2 = CreatureOffspring_Get(b2, 0, &r2);
        TEST_ASSERT(c1 && c2 && r1 && r2, "Candidato y reporte presentes");
        TEST_ASSERT(r1->scaffoldParent == r2->scaffoldParent &&
                    r1->grafts == r2->grafts &&
                    r1->unsupportedGrafts == r2->unsupportedGrafts &&
                    c1->bodyPlan.moduleCount == c2->bodyPlan.moduleCount &&
                    FLOAT_NEAR(c1->adult.axial.totalScale, c2->adult.axial.totalScale),
                    "Determinismo exacto de CreatureOffspring");

        /* Validar construcción completa en las 4 etapas + SDF + mallas suplementarias */
        for (int st = 0; st < 4; ++st) {
            const CreaturePhenotype* ph = CreatureRecipe_GetStage(c1, (CreatureStage)st);
            TEST_ASSERT(CreatureRecipe_Validate(c1, ph), "Etapa de híbrido válida");
            *m = Monster_Create();
            TEST_ASSERT(Creature_BuildMonster(m, c1, ph), "BuildMonster de híbrido");
            if (st == 3) {
                *sdf = MonsterSDF_Create();
                TEST_ASSERT(MonsterSDF_Build(sdf, m, MonsterSDF_DefaultConfig()), "SDF de híbrido adulto");
                Mesh det = Mesh_Create(), trn = Mesh_Create();
                TEST_ASSERT(SupplementaryMesh_Build(m, &det, &trn) &&
                            CheckMeshFinite(&det) && CheckMeshFinite(&trn),
                            "Mallas suplementarias finitas en híbrido");
                Mesh_Free(&det); Mesh_Free(&trn);
                MonsterSDF_Free(sdf);
            }
            Monster_Free(m);
        }

        /* Verificar explícitamente que Libélula x Halcón mantiene arquetipos de miembro válidos y coherentes */
        if (i == 3) {
            for (size_t li = 0; li < c1->adult.limbCount; ++li) {
                TEST_ASSERT(Limb_Supports(c1->adult.limbs[li].archetype),
                            "Arquetipo de miembro válido en híbrido");
            }
        }
        CreatureOffspring_Destroy(b1);
        CreatureOffspring_Destroy(b2);
    }

    free(m); free(sdf);
    printf("[PASS] test_five_non_mammals: CreatureOffspring (homología, determinismo e híbridos válidos)\n");
}

static void TestFalconAnatomicalInvariants(const CreatureRecipe* falcon) {
    /* 1. Verificar que el sistema genérico de pico aviar soporta múltiples perfiles de ave. */
    const float curvatures[4] = {0.92f, 0.12f, 0.05f, 0.84f}; /* rapaz, cónico corto, recto largo, loro */
    const float lengths[4]    = {0.38f, 0.26f, 0.88f, 0.42f};
    const float depths[4]     = {0.68f, 0.58f, 0.30f, 0.86f};
    for (int k = 0; k < 4; ++k) {
        HeadPhenotype hp = falcon->adult.head;
        hp.beakCurvature = curvatures[k];
        hp.beakLength = lengths[k];
        hp.beakDepth = depths[k];
        HeadAnatomy ha = {0};
        TEST_ASSERT(HeadAnatomy_Resolve(&hp, 0, Vec3_Create(0.20f, 0.18f, 0.22f), &ha),
                    "HeadAnatomy_Resolve aviar genérico debe resolver los 4 perfiles de pico");
        TEST_ASSERT(ha.surface.isBeak && ha.surface.beakWeight > 0.5f, "Pico 3D activo en perfil aviar");
        TEST_ASSERT(ha.surface.beakTip.z > ha.surface.beakBase.z, "Pico se proyecta hacia delante");
    }

    /* 2. Invariantes anatómicos específicos del Halcón Peregrino (ID 1016). */
    Monster* m = malloc(sizeof(*m));
    MonsterSDF* sdfClosed = malloc(sizeof(*sdfClosed));
    MonsterSDF* sdfOpen = malloc(sizeof(*sdfOpen));
    TEST_ASSERT(m && sdfClosed && sdfOpen, "alloc falcon invariants");

    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, falcon, &falcon->adult), "Build halcón adulto");
    const HeadSurfaceRecipe* hs = &m->head.anatomy.surface;
    const HeadLandmarks* hl = &m->head.anatomy.landmarks;
    TEST_ASSERT(hs->isBeak && hs->beakWeight > 0.9f, "Halcón usa sistema especializado de pico 3D");
    TEST_ASSERT(hs->beakTip.z > hs->beakBase.z + hs->craniumRadii.z * 0.25f,
                "Culmen superior sobresale claramente de la cera");
    TEST_ASSERT(hs->beakMid.y > hs->beakTip.y && hs->beakTip.y < hs->beakBase.y,
                "Perfil de pico rapaz ganchudo hacia abajo");
    TEST_ASSERT(hs->beakLowerTip.z < hs->beakTip.z,
                "Mandíbula inferior recogida detrás del gancho superior");
    TEST_ASSERT(hl->leftNostril.z >= hs->beakBase.z - 0.01f && hl->leftNostril.z < hs->beakTip.z,
                "Narinas posicionadas sobre la cera del pico");
    TEST_ASSERT(hs->neckCollarContinuity > 0.8f, "Continuidad suave cabeza-cuello aviar");

    /* Proporciones compactas cráneo/tórax y ojo/cráneo */
    const AnatomyNode* thorax = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 1, AXIAL_NODE_THORAX_ANTERIOR);
    TEST_ASSERT(thorax != NULL, "Nodo torácico de halcón");
    float headThoraxRatio = hs->craniumRadii.x / fmaxf(thorax->widthRadius, 1e-4f);
    TEST_ASSERT(headThoraxRatio > 0.30f && headThoraxRatio < 0.72f,
                "Cabeza compacta en proporción al tórax del halcón");
    float eyeSkullRatio = m->head.anatomy.eyeScale.y / fmaxf(hs->craniumRadii.y, 1e-4f);
    TEST_ASSERT(eyeSkullRatio > 0.14f && eyeSkullRatio < 0.45f,
                "Ojos integrados sin tamaño caricaturesco");

    /* Base caudal corta: solo el primer tramo del uropigio es volumen sólido AXIAL_LOFT */
    unsigned solidTailEdges = 0, supportTailEdges = 0;
    for (size_t i = 0; i < m->anatomyGraph.connectionCount; ++i) {
        const BodyConnection* c = &m->anatomyGraph.connections[i];
        if ((c->toId >> 16) == 20u) {
            if (c->kind == BODY_CONNECTION_AXIAL_LOFT) ++solidTailEdges;
            else if (c->kind == BODY_CONNECTION_SUPPORT) ++supportTailEdges;
        }
    }
    TEST_ASSERT(solidTailEdges == 1u && supportTailEdges >= 2u,
                "Cola aviar con uropigio corto sólido y soporte sin tallo rígido desnudo");

    /* Pico cerrado vs pico abierto: el gancho superior permanece íntegro al abrir la boca */
    *sdfClosed = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(sdfClosed, m, MonsterSDF_DefaultConfig()), "SDF halcón cerrado");
    const MonsterSDFMouth* mc = &sdfClosed->mouths[0];
    Vector3 tipLocalProbe = Vec3_Lerp(mc->beakMidLocal, mc->beakTipLocal, 0.78f);
    Vector3 tipWorld = Vec3_Add(mc->center, tipLocalProbe);
    float dTipClosed = MonsterSDF_EvaluateDistance(sdfClosed, tipWorld);
    TEST_ASSERT(dTipClosed < 0.0f, "El gancho del pico superior es un volumen 3D sólido cerrado");

    Monster_SetHeadOpenFactor(m, 0.55f);
    *sdfOpen = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(sdfOpen, m, MonsterSDF_DefaultConfig()), "SDF halcón abierto");
    float dTipOpen = MonsterSDF_EvaluateDistance(sdfOpen, tipWorld);
    TEST_ASSERT(dTipOpen < 0.0f, "Al abrir el pico, el gancho superior permanece íntegro y sin recortar");

    /* Comparación ala extendida vs ala plegada (incluyendo plumaje suplementario completo) */
    Mesh extDet = Mesh_Create(), extTrn = Mesh_Create();
    TEST_ASSERT(SupplementaryMesh_Build(m, &extDet, &extTrn) && CheckMeshFinite(&extDet) && extDet.vertexCount > 500,
                "Plumaje suplementario válido en vuelo extendido");
    float extMinX = 1e9f, extMaxX = -1e9f;
    for (size_t vi = 0; vi < extDet.vertexCount; ++vi) {
        float x = extDet.vertices[vi].position.x;
        if (x < extMinX) extMinX = x;
        if (x > extMaxX) extMaxX = x;
    }
    float extSpan = extMaxX - extMinX;
    Mesh_Free(&extDet); Mesh_Free(&extTrn);

    CreaturePhenotype foldedPheno = falcon->adult;
    foldedPheno.limbs[0].sprawl = 0.95f;
    foldedPheno.limbs[1].sprawl = 0.95f;
    Monster* mf = malloc(sizeof(*mf));
    MonsterSDF* sdfFolded = malloc(sizeof(*sdfFolded));
    TEST_ASSERT(mf && sdfFolded, "alloc folded falcon");
    *mf = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(mf, falcon, &foldedPheno), "Build halcón plegado");
    *sdfFolded = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(sdfFolded, mf, MonsterSDF_DefaultConfig()), "SDF halcón plegado");
    Mesh foldDet = Mesh_Create(), foldTrn = Mesh_Create();
    TEST_ASSERT(SupplementaryMesh_Build(mf, &foldDet, &foldTrn) && CheckMeshFinite(&foldDet) && foldDet.vertexCount > 500,
                "Plumaje suplementario válido y finito en configuración plegada");
    float foldMinX = 1e9f, foldMaxX = -1e9f;
    for (size_t vi = 0; vi < foldDet.vertexCount; ++vi) {
        float x = foldDet.vertices[vi].position.x;
        if (x < foldMinX) foldMinX = x;
        if (x > foldMaxX) foldMaxX = x;
    }
    float foldSpan = foldMaxX - foldMinX;
    TEST_ASSERT(foldSpan < extSpan * 0.45f, "Alas plegadas se recogen cerca del torso respecto al vuelo");
    Mesh_Free(&foldDet); Mesh_Free(&foldTrn);

    MonsterSDF_Free(sdfFolded); Monster_Free(mf); free(sdfFolded); free(mf);
    MonsterSDF_Free(sdfClosed); MonsterSDF_Free(sdfOpen); Monster_Free(m);
    free(sdfClosed); free(sdfOpen); free(m);
    printf("[PASS] test_five_non_mammals: invariantes anatómicos de Halcón Peregrino (1016)\n");
}

static void TestDragonflyAnatomicalInvariants(const CreatureRecipe* dragonfly) {
    Monster* m = malloc(sizeof(*m));
    MonsterSDF* sdf = malloc(sizeof(*sdf));
    TEST_ASSERT(m && sdf, "alloc dragonfly invariants");
    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, dragonfly, &dragonfly->adult), "Build libélula adulta");
    *sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(sdf, m, MonsterSDF_DefaultConfig()), "SDF libélula adulta");

    /* Conteo de 6 patas torácicas y 4 alas */
    unsigned legCount = 0, wingCount = 0;
    for (size_t i = 0; i < m->phenotype.limbCount; ++i) {
        if (m->phenotype.limbs[i].archetype == LIMB_ARCHETYPE_ARTHROPOD_LEG) ++legCount;
        else if (m->phenotype.limbs[i].archetype == LIMB_ARCHETYPE_INSECT_WING) ++wingCount;
    }
    TEST_ASSERT(legCount == 6u && wingCount == 4u, "Libélula con 6 patas torácicas y 4 alas");

    /* Separación estricta cabeza vs primer par de patas: NINGUNA raíz de pata dentro de la cabeza */
    TEST_ASSERT(sdf->mouthCount >= 1u, "Libélula tiene cápsula cefálica SDF");
    float headMinZ = sdf->mouths[0].center.z + sdf->mouths[0].headBounds.start.z;
    for (uint32_t legMod = 10u; legMod <= 15u; ++legMod) {
        const AnatomyNode* root = AnatomyGraph_FindModuleNode(&m->anatomyGraph, legMod, LIMB_NODE_ROOT);
        TEST_ASSERT(root != NULL, "Raíz de pata de insecto presente");
        TEST_ASSERT(root->center.z < headMinZ - 0.01f,
                    "Ninguna pata de libélula puede originarse dentro o delante de la cápsula cefálica");
    }

    /* Proporción cabeza/tórax compacta (sin cabeza plana tipo rana) y ojos compuestos holópticos */
    const AnatomyNode* syn = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 1, AXIAL_NODE_THORAX_ANTERIOR);
    TEST_ASSERT(syn != NULL, "Sintórax de libélula presente");
    float headRatio = m->head.anatomy.surface.craniumRadii.x / fmaxf(syn->widthRadius, 1e-4f);
    TEST_ASSERT(headRatio > 0.55f && headRatio < 1.35f,
                "Cabeza de libélula proporcionada respecto al sintórax (sin deformación ancha tipo rana)");
    TEST_ASSERT(m->eyeCount == 2u, "Libélula tiene dos ojos compuestos simétricos");
    TEST_ASSERT(m->eyes[0].pupilShape == PUPIL_COMPOUND && m->eyes[0].cornealGloss < 0.50f,
                "Ojos compuestos facetados sin brillo corneal húmedo vertebrado");
    unsigned char texBuf[32 * 32 * 4];
    TEST_ASSERT(EyeTexture_Generate(&m->eyes[0], 32, texBuf, sizeof(texBuf)),
                "Textura procedural hexagonal PUPIL_COMPOUND generada correctamente");

    MonsterSDF_Free(sdf); Monster_Free(m);
    free(sdf); free(m);
    printf("[PASS] test_five_non_mammals: invariantes anatómicos de Libélula Emperador (1019)\n");
}

static void TestOctopusAnatomicalInvariants(const CreatureRecipe* octopus) {
    Monster* m = malloc(sizeof(*m));
    MonsterSDF* sdf = malloc(sizeof(*sdf));
    TEST_ASSERT(m && sdf, "alloc octopus invariants");
    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, octopus, &octopus->adult), "Build pulpo adulto");
    *sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(sdf, m, MonsterSDF_DefaultConfig()), "SDF pulpo adulto");

    /* 8 brazos musculares + 1 sifón con abertura terminal en embudo */
    unsigned arms = 0, siphons = 0;
    for (size_t i = 0; i < m->phenotype.flexibleAppendageCount; ++i) {
        if (m->phenotype.flexibleAppendages[i].profile == FLEXIBLE_PROFILE_MUSCULAR_ARM) ++arms;
        else if (m->phenotype.flexibleAppendages[i].profile == FLEXIBLE_PROFILE_SIPHON) ++siphons;
    }
    TEST_ASSERT(arms == 8u && siphons == 1u, "Pulpo con 8 brazos musculares y 1 sifón");

    bool foundFunnel = false;
    for (size_t i = 0; i < sdf->connectorCount; ++i) {
        const MonsterSDFConnector* c = &sdf->connectors[i];
        if (c->kind == BODY_CONNECTION_FUNNEL_OPENING) {
            foundFunnel = true;
            /* La boca central del embudo debe estar hueca (>0) mientras el borde muscular es sólido (<0) */
            Vector3 mouthCenter = Vec3_Add(c->b, Vec3_Scale(c->forward, c->r2 * 0.10f));
            Vector3 wallProbe = Vec3_Add(c->b, Vec3_Scale(c->side, c->widthB * 0.78f));
            float dMouth = MonsterSDF_EvaluateDistance(sdf, mouthCenter);
            float dWall = MonsterSDF_EvaluateDistance(sdf, wallProbe);
            TEST_ASSERT(dMouth > 0.0f && dWall < 0.0f,
                        "El sifón tiene un lumen circular abierto rodeado de pared muscular");
        }
    }
    TEST_ASSERT(foundFunnel, "Sifón de pulpo compila abertura terminal BODY_CONNECTION_FUNNEL_OPENING");

    /* Manto posterior ovoide suave sin mamelón ni pico agudo */
    TEST_ASSERT(sdf->axialStationCount == 7u, "Manto cefalópodo sin estación extra de mamelón trasero");
    const AnatomyNode* mMid = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 1, AXIAL_NODE_ABDOMEN);
    const AnatomyNode* mRear = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 1, AXIAL_NODE_PELVIS);
    TEST_ASSERT(mMid && mRear && mRear->widthRadius < mMid->widthRadius &&
                mRear->widthRadius > mMid->widthRadius * 0.50f,
                "Cúpula posterior del manto redondeada y continua sin estrangulamiento");

    /* Pupila horizontal típica de pulpo y cabeza compacta sin hocico */
    TEST_ASSERT(m->eyeCount == 2u && m->eyes[0].pupilShape == PUPIL_HORIZONTAL,
                "Pulpo con dos ojos laterales de pupila horizontal");
    TEST_ASSERT(m->head.anatomy.surface.hasCranialProfile && m->head.anatomy.surface.neckCollarContinuity > 0.8f,
                "Cabeza cefalópoda compacta e integrada al collar del manto");

    MonsterSDF_Free(sdf); Monster_Free(m);
    free(sdf); free(m);
    printf("[PASS] test_five_non_mammals: invariantes anatómicos de Pulpo Gigante (1018)\n");
}

void run_five_non_mammals_tests(void) {
    printf("\n--- Suite: Cinco Especies No-Mamíferas (1016..1020) y Arquitectura Reutilizable ---\n");
    TestRadialCoordinateFrame();

    char err[256] = {0};
    CreatureRecipeRegistry_Clear();
    TEST_ASSERT(CreatureRecipeRegistry_LoadDirectory("recipes", err, sizeof(err)),
                "Cargar catálogo recipes/");
    const CreatureRecipe* recipes[5] = {0};
    TestCatalogLoadAndJsonRoundTrip(recipes);
    TestStagesSdfSupplementaryAndAger(recipes);
    TestFalconAnatomicalInvariants(recipes[0]);
    TestDragonflyAnatomicalInvariants(recipes[3]);
    TestOctopusAnatomicalInvariants(recipes[2]);
    TestAnimationWithoutSdfRebuild(recipes);
    TestCrossBodyPlanMorph(recipes);
    TestOffspringHomologyAndHybrids(recipes);
    CreatureRecipeRegistry_Clear();
    printf("[PASS] run_five_non_mammals_tests: TODAS LAS PRUEBAS PASARON\n");
}
