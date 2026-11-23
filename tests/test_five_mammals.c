/**
 * @file test_five_mammals.c
 * @brief Pruebas de regresión para la expansión de cinco mamíferos y sus sistemas anatómicos genéricos.
 */
#include "Creature.h"
#include "CreatureRecipeIO.h"
#include "CreatureRecipeRegistry.h"
#include "CreatureRecipes.h"
#include "CreatureOffspring.h"
#include "MonsterAger.h"
#include "MonsterSDF.h"
#include "MonsterVisual.h"
#include "MonsterAnimation.h"
#include "AnimatedVisual.h"
#include "Membrane.h"
#include "ArmorScales.h"
#include "PigmentPattern.h"
#include "Limb.h"
#include "MathUtils.h"
#include "Transform3D.h"
#include "AxialBody.h"
#include "Ornament.h"
#include "test_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static AABB3D MeshBounds(const Mesh* mesh) {
    AABB3D b = AABB_Empty();
    for (size_t i = 0; mesh && i < mesh->vertexCount; ++i)
        AABB_ExpandPoint(&b, mesh->vertices[i].position);
    return b;
}

static bool LoadSpecDoc(const char* path, CreatureRecipeDocument* doc) {
    char err[256] = {0};
    return CreatureRecipeIO_LoadFile(doc, path, err, sizeof(err));
}

static void AssertSolidSegment(const MonsterSDF* sdf, Vector3 a, Vector3 b) {
    for (int i = 0; i <= 8; ++i)
        TEST_ASSERT(MonsterSDF_EvaluateDistance(sdf, Vec3_Lerp(a, b, (float)i / 8)) < 0,
                    "El eje de la unión anatómica debe permanecer dentro del tejido");
}

static void TestFacialFrames(void) {
    Vector3 envelope = Vec3_Create(.32f, .24f, .36f);
    for (int archetype = HEAD_ARCHETYPE_LIZARD; archetype <= HEAD_ARCHETYPE_PHOLIDOTE; ++archetype) {
        HeadPhenotype phenotype = HeadPhenotype_RandomValid((HeadArchetype)archetype, 489u);
        HeadAnatomy anatomy;
        TEST_ASSERT(HeadAnatomy_Resolve(&phenotype, 0, envelope, &anatomy),
                    "Resolver todos los arquetipos cefálicos");
        const HeadSurfaceRecipe* surface = &anatomy.surface;
        TEST_ASSERT(surface->faceTipRadii.y > 0 && surface->faceTip.z > surface->faceRoot.z,
                    "Volumen facial y estaciones en orden");
        if (archetype == HEAD_ARCHETYPE_MONOTREME || archetype == HEAD_ARCHETYPE_PHOLIDOTE) {
            float scale = archetype == HEAD_ARCHETYPE_MONOTREME ? .88f : .84f;
            const float cranialHeight[] = {1, 1.14f, 1.18f, .96f, .90f};
            HeadPhenotype_Normalize(&phenotype);
            float expected = envelope.y * (.55f + .48f * phenotype.skullHeight)
                * cranialHeight[phenotype.cranialForm] * scale;
            TEST_ASSERT(fabsf(expected - surface->craniumRadii.y) < .00001f,
                        "Escala craneal aplicada una sola vez");
            TEST_ASSERT(fabsf(surface->cranialCenters[0].z - surface->faceTip.z) < .00001f &&
                        surface->mandibularCenters[0].z < surface->faceTip.z,
                        "Cara, barrido y mandíbula derivados de la misma punta");
        }
    }
}

static void AssertGiraffeTuft(Monster* m, const MonsterSDF* sdf) {
    TEST_ASSERT(m->animation && m->animation->rig.tailCount == 1, "Rig caudal de jirafa");
    const TailRig* tail = &m->animation->rig.tails[0];
    TEST_ASSERT(tail->jointCount >= 2 && m->surface.longHairCount == 1, "Pincel caudal con dos estaciones");
    const Skeleton* skeleton = &m->animation->rig.skeleton;
    int tip = tail->joints[tail->jointCount-1];
    int beforeTip = tail->joints[tail->jointCount-2];
    const AnatomyNode* tipNode = AnatomyGraph_FindNode(&m->anatomyGraph, skeleton->joints[tip].anatomyId);
    const AnatomyNode* previous = AnatomyGraph_FindNode(&m->anatomyGraph, skeleton->joints[beforeTip].anatomyId);
    TEST_ASSERT(tipNode && previous, "El rig publica las estaciones terminales");

    LongHairSet hair = {0};
    TEST_ASSERT(LongHairSet_Build(&hair, m->surface.longHair, m->surface.longHairCount, &m->anatomyGraph,
                m->phenotype.axial.totalScale, MonsterSDF_EvaluateDistanceWrapper, sdf),
                "Proyectar el pincel sobre la cola");
    m->animation->animator.desiredVelocity = Vec3_Create(0, 0, m->phenotype.axial.totalScale);
    for (int frame = 0; frame < 18; ++frame)
        TEST_ASSERT(MonsterAnimation_Update(m, 1.0f/60), "Animar inserción caudal");
    TEST_ASSERT(LongHairSet_Pose(&hair, skeleton, &m->animation->pose), "Transportar raíces del pincel");
    unsigned visible = 0;
    for (size_t i = 0; i < hair.count; ++i) {
        const LongHairGuide* guide = &hair.guides[i];
        if (!guide->visible) continue;
        ++visible;
        TEST_ASSERT(guide->root.fromId == previous->id && guide->root.toId == tipNode->id,
                    "Cada mechón pertenece al último tramo caudal");
        float t = guide->root.segmentParameter;
        Vector3 axis = Vec3_Lerp(m->animation->pose.joints[beforeTip].worldPosition,
                                m->animation->pose.joints[tip].worldPosition, t);
        float radius = Math_Lerp(previous->widthRadius, tipNode->widthRadius, t);
        TEST_ASSERT(Vec3_Distance(guide->points[0], axis) < radius * 1.45f,
                    "El folículo conserva su inserción al flexionar la cola");
    }
    TEST_ASSERT(visible >= hair.count/2 && visible > 0, "El pincel conserva cobertura visible");
    LongHairSet_Free(&hair);
}

static void TestAllFiveRecipesFourStagesAndAger(void) {
    const struct {
        const char* path;
        CreatureRecipeId id;
    } specs[] = {
        {"recipes/jirafa.creature.json", 1011u},
        {"recipes/ornitorrinco.creature.json", 1012u},
        {"recipes/pangolin.creature.json", 1013u},
        {"recipes/canguro.creature.json", 1014u},
        {"recipes/murcielago.creature.json", 1015u},
    };
    char err[256] = {0};
    TEST_ASSERT(CreatureRecipeRegistry_LoadDirectory("recipes", err, sizeof(err)),
                "El catálogo externo completo debe cargar las 5 especies nuevas");

    for (size_t s = 0; s < sizeof(specs) / sizeof(specs[0]); ++s) {
        CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
        TEST_ASSERT(doc != NULL && LoadSpecDoc(specs[s].path, doc), "Carga de receta nueva");
        TEST_ASSERT(doc->recipe.id == specs[s].id, "ID de receta consistente");
        const CreatureRecipe* reg = CreatureRecipes_Find(specs[s].id);
        TEST_ASSERT(reg != NULL && reg->id == specs[s].id, "Receta registrada en catálogo");

        Monster* m = malloc(sizeof(*m));
        Monster* juvenile = malloc(sizeof(*juvenile));
        Monster* adult = malloc(sizeof(*adult));
        Monster* agerOut = malloc(sizeof(*agerOut));
        TEST_ASSERT(m && juvenile && adult && agerOut, "Memoria de pruebas de cinco mamíferos");

        for (unsigned st = 0; st < 4; ++st) {
            const CreaturePhenotype* p = CreatureRecipe_GetStage(&doc->recipe, (CreatureStage)st);
            *m = Monster_Create();
            TEST_ASSERT(Creature_BuildMonster(m, &doc->recipe, p), "Construcción válida en cada etapa");
            TEST_ASSERT(AnatomyGraph_Validate(&m->anatomyGraph), "Grafo anatómico válido en las 4 etapas");
            TEST_ASSERT(m->animation != NULL, "Animación y rig válidos");
            MonsterSDF sdf = MonsterSDF_Create();
            TEST_ASSERT(MonsterSDF_Build(&sdf, m, MonsterSDF_DefaultConfig()), "SDF válido");
            Vector3 sz = AABB_Size(sdf.bounds);
            TEST_ASSERT(isfinite(sz.x) && sz.x > 0 && isfinite(sz.y) && sz.y > 0 && isfinite(sz.z) && sz.z > 0,
                        "Límites SDF finitos y positivos");
            if (specs[s].id == 1011u) AssertGiraffeTuft(m, &sdf);
            MonsterSDF_Free(&sdf);
            Monster_Free(m);
        }

        *juvenile = Monster_Create();
        *adult = Monster_Create();
        *agerOut = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(juvenile, &doc->recipe, &doc->recipe.seed) &&
                    Creature_BuildMonster(adult, &doc->recipe, &doc->recipe.adult), "Extremos Ager válidos");
        const float ages[] = {0.0f, 0.25f, 0.50f, 0.75f, 1.0f};
        for (size_t a = 0; a < 5; ++a) {
            MonsterAger_Interpolate(juvenile, adult, ages[a], agerOut);
            TEST_ASSERT(agerOut->hasAnatomyGraph && AnatomyGraph_Validate(&agerOut->anatomyGraph),
                        "Topología coherente en edades intermedias");
            if (a == 0 || a == 4) {
                const Monster* endpoint = a == 0 ? juvenile : adult;
                TEST_ASSERT(AnatomyGraph_Fingerprint(&agerOut->anatomyGraph) ==
                            AnatomyGraph_Fingerprint(&endpoint->anatomyGraph),
                            "Ager conserva exactamente la geometría en cada extremo");
            }
            if (specs[s].id == 1012u || specs[s].id == 1015u) {
                Mesh membranes = Mesh_Create();
                TEST_ASSERT(Membrane_BuildMesh(agerOut, &membranes) && membranes.indexCount > 0,
                            "Las membranas se conservan en los extremos y edades intermedias");
                Mesh_Free(&membranes);
            }
            if (specs[s].id == 1011u && a > 0 && a < 4) {
                TEST_ASSERT(Creature_ConfigureAnimation(agerOut), "Rig actualizado a la edad intermedia");
                MonsterSDF sdf = MonsterSDF_Create();
                TEST_ASSERT(MonsterSDF_Build(&sdf, agerOut, MonsterSDF_DefaultConfig()), "Campo Ager de jirafa");
                AssertGiraffeTuft(agerOut, &sdf);
                MonsterSDF_Free(&sdf);
            }
        }

        Monster_Free(juvenile);
        Monster_Free(adult);
        Monster_Free(agerOut);
        free(m); free(juvenile); free(adult); free(agerOut);
        free(doc);
    }
    printf("[PASS] test_five_mammals: carga, 4 etapas, SDF y Ager para 1011..1015\n");
}

static void TestGiraffeCapabilities(void) {
    CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
    TEST_ASSERT(doc && LoadSpecDoc("recipes/jirafa.creature.json", doc), "Cargar jirafa");
    Monster* m = malloc(sizeof(*m));
    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, &doc->recipe, &doc->recipe.adult), "Construir jirafa");
    TEST_ASSERT(m->animation->rig.cervicalChainCount >= 4, "Cadena cervical multi-segmento activa");

    /* Patrón reticulado no trivial y determinista */
    float minR = 1e6f, maxR = -1e6f;
    for (int k = 0; k < 40; ++k) {
        float val = PigmentPattern_SampleSurface(PIGMENT_PATTERN_RETICULATED,
            Vec3_Create(0.05f * (float)k, 0.9f, 0.3f), 6.0f, 0.7f, Vec3_Create(0, 1, 0), 42u);
        TEST_ASSERT(isfinite(val) && val >= 0.0f && val <= 1.0f, "Patrón reticulado acotado");
        if (val < minR) minR = val;
        if (val > maxR) maxR = val;
    }
    TEST_ASSERT((maxR - minR) > 0.25f, "Patrón reticulado varía entre polígonos y suturas");

    /* Osiconos craneales presentes en grafo */
    size_t ossicones = 0;
    for (size_t i = 0; i < m->anatomyGraph.nodeCount; ++i) {
        if (m->anatomyGraph.nodes[i].region == ANATOMY_REGION_ORNAMENT) ++ossicones;
    }
    TEST_ASSERT(ossicones >= 4, "Osiconos craneales resueltos en anatomía");
    for (uint32_t module = 8; module <= 9; ++module) {
        const AnatomyNode* base = AnatomyGraph_FindModuleNode(&m->anatomyGraph, module, ORNAMENT_NODE_BASE);
        const AnatomyNode* tip = AnatomyGraph_FindModuleNode(&m->anatomyGraph, module, ORNAMENT_NODE_TIP);
        TEST_ASSERT(base && tip && tip->center.y - base->center.y > fabsf(tip->center.z - base->center.z) * 2,
                    "Osicono corto crece desde la bóveda craneal");
        const AnatomyNode* headNode = AnatomyGraph_FindFirstRegion(&m->anatomyGraph, ANATOMY_REGION_HEAD);
        TEST_ASSERT(headNode != NULL, "Estación cefálica de jirafa");
        Vector3 earBase = Vec3_Add(headNode->center, m->head.anatomy.landmarks.leftEarBase);
        TEST_ASSERT(fabsf(base->center.x) < fabsf(earBase.x) && base->center.y > earBase.y,
                    "Osiconos mediales y superiores respecto a las orejas laterales");
    }
    const Rig* rig = &m->animation->rig;
    float previousWidth = INFINITY;
    for (size_t i = 1; i < rig->cervicalChainCount; ++i) {
        const AnatomyNode* node = AnatomyGraph_FindNode(&m->anatomyGraph,
            rig->skeleton.joints[rig->cervicalChain[i]].anatomyId);
        TEST_ASSERT(node && node->widthRadius < previousWidth, "Cuello afinado de hombro a cráneo");
        previousWidth = node->widthRadius;
    }
    const AnatomyNode* shoulders = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 1, AXIAL_NODE_PECTORAL);
    const AnatomyNode* neck = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 1, AXIAL_NODE_NECK);
    const AnatomyNode* mid = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 1, 12);
    TEST_ASSERT(shoulders && neck && mid && neck->widthRadius < shoulders->widthRadius*.38f &&
                mid->heightRadius < shoulders->heightRadius*.50f, "Nuca fina y musculatura escapular conservada");
    const AnatomyNode* tailRoot = AnatomyGraph_FindFirstRegion(&m->anatomyGraph, ANATOMY_REGION_TAIL);
    const AnatomyNode* tailEnd = tailRoot ? AnatomyGraph_FindModuleNode(&m->anatomyGraph, tailRoot->moduleInstanceId,
        m->phenotype.tails[0].segmentCount) : NULL;
    TEST_ASSERT(tailRoot && tailEnd && tailRoot->widthRadius < shoulders->widthRadius*.22f &&
                tailEnd->center.y < tailRoot->center.y - fabsf(tailEnd->center.z-tailRoot->center.z),
                "Cola fina que cae junto a la grupa");
    Monster_Free(m);
    free(m);
    free(doc);
    printf("[PASS] test_five_mammals: jirafa (cadena cervical, osiconos y patrón reticulado)\n");
}

static void TestPlatypusCapabilities(void) {
    CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
    TEST_ASSERT(doc && LoadSpecDoc("recipes/ornitorrinco.creature.json", doc), "Cargar ornitorrinco");
    Monster* m = malloc(sizeof(*m));
    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, &doc->recipe, &doc->recipe.adult), "Construir ornitorrinco");
    TEST_ASSERT(m->hasHead && m->head.anatomy.surface.isBill && !m->head.anatomy.surface.hasEars,
                "Pico anatómico de monotrema activo y sin orejas externas");
    const AnatomyNode* tailTip = AnatomyGraph_FindFirstRegion(&m->anatomyGraph, ANATOMY_REGION_TAIL);
    TEST_ASSERT(tailTip != NULL && doc->recipe.adult.tails[0].archetype == TAIL_ARCHETYPE_PADDLE,
                "Cola plana tipo remo activa");
    const HeadSurfaceRecipe* head = &m->head.anatomy.surface;
    TEST_ASSERT(head->cranialRadii[1].x > head->cranialRadii[1].y * 3.0f &&
                head->cranialRadii[4].y > head->cranialRadii[1].y * 2.5f &&
                head->cranialRadii[3].y > head->cranialRadii[1].y &&
                head->cranialRadii[3].y < head->cranialRadii[4].y,
                "Pico ancho y plano con bóveda craneal convexa y transición frontal progresiva");
    const AnatomyNode* paddle = AnatomyGraph_FindModuleNode(&m->anatomyGraph, tailTip->moduleInstanceId, 8);
    const AnatomyNode* paddleEnd = AnatomyGraph_FindModuleNode(&m->anatomyGraph, tailTip->moduleInstanceId, 12);
    TEST_ASSERT(paddle && paddleEnd && paddle->widthRadius > paddle->heightRadius * 3.0f &&
                paddle->widthRadius > tailTip->widthRadius && paddleEnd->widthRadius > paddle->widthRadius * .35f &&
                paddleEnd->widthRadius < paddle->widthRadius *.70f, "Pedículo, pala aplanada y cierre redondeado");
    MonsterSDF sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf, m, MonsterSDF_DefaultConfig()), "Campo de pala");
    const AnatomyNode* pelvis = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 1, AXIAL_NODE_PELVIS);
    TEST_ASSERT(pelvis, "Pelvis anfitriona");
    AssertSolidSegment(&sdf, pelvis->center, tailTip->center);
    MonsterSDF_Free(&sdf);

    Mesh openWeb = Mesh_Create();
    TEST_ASSERT(Membrane_BuildMesh(m, &openWeb) && openWeb.vertexCount > 0 && openWeb.indexCount > 0,
                "Malla de membrana interdigital generada");
    CreaturePhenotype foldedPheno = doc->recipe.adult;
    foldedPheno.limbs[0].webbingFold = 0.90f;
    foldedPheno.limbs[1].webbingFold = 0.90f;
    Monster_Free(m);
    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, &doc->recipe, &foldedPheno), "Construir ornitorrinco plegado");
    Mesh foldedWeb = Mesh_Create();
    TEST_ASSERT(Membrane_BuildMesh(m, &foldedWeb), "Membrana plegada generada");
    AABB3D bOpen = MeshBounds(&openWeb), bFold = MeshBounds(&foldedWeb);
    TEST_ASSERT(AABB_Size(bFold).x <= AABB_Size(bOpen).x + 1e-4f,
                "El plegamiento terrestre no ensancha la membrana interdigital");

    Mesh_Free(&openWeb);
    Mesh_Free(&foldedWeb);
    Monster_Free(m);
    free(m);
    free(doc);
    printf("[PASS] test_five_mammals: ornitorrinco (pico, cola paddle y membrana retráctil)\n");
}

static void TestPangolinCapabilities(void) {
    CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
    TEST_ASSERT(doc && LoadSpecDoc("recipes/pangolin.creature.json", doc), "Cargar pangolín");
    Monster* m = malloc(sizeof(*m));
    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, &doc->recipe, &doc->recipe.adult), "Construir pangolín");
    TEST_ASSERT(m->phenotype.head.archetype == HEAD_ARCHETYPE_PHOLIDOTE, "Cabeza cónica de pangolín");
    TEST_ASSERT(m->head.anatomy.surface.hasEars, "El pangolín chino tiene pabellones");
    const HeadSurfaceRecipe* head = &m->head.anatomy.surface;
    TEST_ASSERT(head->craniumRadii.y > head->craniumRadii.z*.36f &&
                head->cranialRadii[1].y > head->cranialRadii[0].y &&
                head->cranialRadii[2].y > head->cranialRadii[1].y &&
                head->cranialRadii[4].y > head->cranialRadii[2].y &&
                head->noseRadii.y > head->cranialRadii[0].y*.5f, "Bóveda y rostro afilado con punta volumétrica");

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.04f; cfg.maxCells = 250000; cfg.maxResolution = 128;
    MonsterVisual vis = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&vis, m, MonsterSDF_DefaultConfig()), "Visual de pangolín construido");
    TEST_ASSERT(vis.armorMesh.vertexCount > 100 && vis.armorMesh.indexCount % 3 == 0,
                "Coraza geométrica de escamas queratínicas generada");
    const MonsterSDFMouth* face = &vis.sdf.mouths[0];
    for (size_t i = 0; i < vis.armorMesh.vertexCount; ++i) {
        Vector3 p = Transform3D_ApplyRotationBasis(face->inverseRotation,
            Vec3_Sub(vis.armorMesh.vertices[i].position, face->center));
        TEST_ASSERT(p.z < face->faceMidLocal.z, "Las escamas dejan libre el extremo del rostro");
    }
    /* Cada arista de una placa cerrada aparece una vez en ambos sentidos. */
    for (size_t t = 0; t < 72; ++t) {
        MeshIndex a = vis.armorMesh.indices[t];
        MeshIndex b = vis.armorMesh.indices[t - t % 3 + (t + 1) % 3];
        unsigned opposite = 0;
        for (size_t e = 0; e < 72; ++e)
            if (vis.armorMesh.indices[e] == b &&
                vis.armorMesh.indices[e - e % 3 + (e + 1) % 3] == a) ++opposite;
        TEST_ASSERT(a != b && opposite == 1, "Escama cerrada sin caras duplicadas");
    }

    AnimatedVisual bind = {0};
    TEST_ASSERT(AnimatedVisual_Bind(&bind, &vis, m) && bind.armor != NULL, "Deformer de coraza vinculado");
    AABB3D restBox = MeshBounds(&vis.armorMesh);
    float restLen = AABB_Size(restBox).z;
    for (int step = 0; step < 30; ++step) {
        m->animation->animator.defensiveCurl = 1.0f;
        MonsterAnimation_Update(m, 1.0f / 30.0f);
    }
    TEST_ASSERT(AnimatedVisual_Deform(&bind, &vis, m), "Deformación de coraza en enrollamiento");
    AABB3D curlBox = MeshBounds(&vis.armorMesh);
    float curlLen = AABB_Size(curlBox).z;
    TEST_ASSERT(isfinite(curlLen) && curlLen < restLen * 0.90f,
                "Enrollamiento defensivo reduce extensión longitudinal sin explotar malla");

    AnimatedVisual_Free(&bind);
    MonsterVisual_Free(&vis);
    Monster_Free(m);
    free(m);
    free(doc);
    printf("[PASS] test_five_mammals: pangolín (escamas geométricas imbricadas y enrollamiento)\n");
}

static void TestKangarooCapabilities(void) {
    CreatureRecipeDocument* doc = calloc(1, sizeof(*doc));
    TEST_ASSERT(doc && LoadSpecDoc("recipes/canguro.creature.json", doc), "Cargar canguro");
    Monster* m = malloc(sizeof(*m));
    *m = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(m, &doc->recipe, &doc->recipe.adult), "Construir canguro");
    TEST_ASSERT(m->recipe.gait == CREATURE_GAIT_BIPEDAL_HOP, "Marcha BIPEDAL_HOP");
    TEST_ASSERT(m->animation->rig.limbCount == 4, "4 miembros en rig");
    TEST_ASSERT(!m->animation->rig.limbs[0].walking && !m->animation->rig.limbs[1].walking,
                "Miembros anteriores no caminan en salto bípedo");
    TEST_ASSERT(m->animation->rig.limbs[2].walking && m->animation->rig.limbs[3].walking,
                "Miembros posteriores sustentan salto bípedo");
    const Skeleton* skeleton = &m->animation->rig.skeleton;
    AnatomyId pawId = skeleton->joints[m->animation->rig.limbs[2].endEffectorJoint].anatomyId;
    const AnatomyNode* paw = AnatomyGraph_FindNode(&m->anatomyGraph, pawId);
    TEST_ASSERT(paw && fabsf(paw->center.y - paw->heightRadius) < .006f,
                "La suela del metatarso en reposo coincide con el suelo");
    uint32_t module = pawId >> 16;
    const AnatomyNode* ankle = AnatomyGraph_FindModuleNode(&m->anatomyGraph, module, LIMB_NODE_DISTAL);
    const AnatomyNode* toe = AnatomyGraph_FindModuleNode(&m->anatomyGraph, module, Limb_DigitLocalId(1, 4));
    TEST_ASSERT(ankle && toe && toe->center.z - ankle->center.z > doc->recipe.adult.limbs[2].length *
                doc->recipe.adult.axial.totalScale * .80f, "Metatarso largo y cuarto dedo dominante");

    MonsterSDF sdf = MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf, m, MonsterSDF_DefaultConfig()), "Campo de tronco y miembros macrópodos");
    const AnatomyNode* pelvis = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 1, AXIAL_NODE_PELVIS);
    const AnatomyNode* thorax = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 1, AXIAL_NODE_THORAX_POSTERIOR);
    const AnatomyNode* abdomen = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 1, AXIAL_NODE_ABDOMEN);
    const AnatomyNode* chest = AnatomyGraph_FindModuleNode(&m->anatomyGraph, 1, AXIAL_NODE_PECTORAL);
    TEST_ASSERT(pelvis && abdomen && thorax && chest &&
                chest->widthRadius < thorax->widthRadius && thorax->widthRadius < abdomen->widthRadius &&
                abdomen->widthRadius < pelvis->widthRadius && abdomen->heightRadius < thorax->heightRadius * 1.32f,
                "El tronco gana masa progresivamente hacia la pelvis");
    for (int i = 0; i < 4; ++i) {
        const LimbRig* limb = &m->animation->rig.limbs[i];
        AnatomyId rootId = skeleton->joints[limb->rootJoint].anatomyId;
        const AnatomyNode* root = AnatomyGraph_FindNode(&m->anatomyGraph, rootId);
        TEST_ASSERT(root, "Raíz de miembro resuelta");
        AssertSolidSegment(&sdf, (i < 2 ? chest : pelvis)->center, root->center);
        if (i < 2) {
            const AnatomyNode* wrist = AnatomyGraph_FindModuleNode(&m->anatomyGraph, root->moduleInstanceId, LIMB_NODE_DISTAL);
            const AnatomyNode* autopod = AnatomyGraph_FindModuleNode(&m->anatomyGraph, root->moduleInstanceId, LIMB_NODE_AUTOPOD);
            const AnatomyNode* palm = AnatomyGraph_FindModuleNode(&m->anatomyGraph, root->moduleInstanceId, LIMB_NODE_PALM_BALL);
            TEST_ASSERT(wrist && autopod && palm && autopod->center.z > wrist->center.z,
                        "La mano continúa hacia delante desde la muñeca y soporta los dedos");
            for (unsigned d = 0; d < 5; ++d) {
                const AnatomyNode* base = AnatomyGraph_FindModuleNode(&m->anatomyGraph, root->moduleInstanceId, Limb_DigitLocalId(d,0));
                TEST_ASSERT(base, "Cinco dedos con raíces presentes");
                AssertSolidSegment(&sdf, palm->center, base->center);
            }
        } else {
            const AnatomyNode* midFoot = AnatomyGraph_FindModuleNode(&m->anatomyGraph, root->moduleInstanceId, 212);
            TEST_ASSERT(midFoot != NULL, "Estación intermedia del metatarso macrópodo");
            const AnatomyNode* medial = AnatomyGraph_FindModuleNode(&m->anatomyGraph, root->moduleInstanceId, Limb_DigitLocalId(0,1));
            const AnatomyNode* main = AnatomyGraph_FindModuleNode(&m->anatomyGraph, root->moduleInstanceId, Limb_DigitLocalId(1,1));
            const AnatomyNode* lateral = AnatomyGraph_FindModuleNode(&m->anatomyGraph, root->moduleInstanceId, Limb_DigitLocalId(2,1));
            TEST_ASSERT(medial && main && lateral && fabsf(medial->center.x) < fabsf(main->center.x) &&
                        fabsf(main->center.x) < fabsf(lateral->center.x) && main->widthRadius > lateral->widthRadius*1.3f &&
                        lateral->widthRadius > medial->widthRadius, "Pie especular con IV dominante y II–III medial");
        }
    }
    AssertSolidSegment(&sdf, ankle->center, paw->center);
    const TailRig* tail = &m->animation->rig.tails[0];
    const AnatomyNode* tailRoot = AnatomyGraph_FindModuleNode(&m->anatomyGraph, tail->moduleInstanceId, 1);
    const AnatomyNode* tailTip = AnatomyGraph_FindModuleNode(&m->anatomyGraph, tail->moduleInstanceId,
        m->phenotype.tails[0].segmentCount);
    TEST_ASSERT(tailRoot && tailTip && tailTip->center.y >= tailTip->heightRadius - .005f &&
                tailTip->center.y < .09f*m->phenotype.axial.totalScale,
                "La punta caudal queda cerca del suelo sin atravesarlo en reposo");
    AssertSolidSegment(&sdf, pelvis->center, tailRoot->center);
    MonsterSDF_Free(&sdf);

    float minHipY = 1e6f, maxHipY = -1e6f;
    bool sawFlight = false;
    m->animation->animator.desiredVelocity = Vec3_Create(0, 0, 1.5f);
    for (int step = 0; step < 90; ++step) {
        TEST_ASSERT(MonsterAnimation_Update(m, 1.0f / 30.0f), "Actualización locomotora de salto");
        int hlRoot = m->animation->rig.limbs[2].rootJoint;
        float hy = m->animation->pose.joints[hlRoot].worldPosition.y;
        if (hy < minHipY) minHipY = hy;
        if (hy > maxHipY) maxHipY = hy;
        const FootState* feet = m->animation->animator.locomotion.feet;
        TEST_ASSERT(feet[2].phase == feet[3].phase, "El salto conserva apoyos y vuelo sincronizados");
        if (feet[2].phase == FOOT_SWING) {
            sawFlight = true;
        } else {
            for (int i = 2; i < 4; ++i) {
                const LimbRig* limb = &m->animation->rig.limbs[i];
                Vector3 foot = m->animation->pose.joints[limb->endEffectorJoint].worldPosition;
                float scale = m->phenotype.axial.totalScale;
                TEST_ASSERT(fabsf(foot.y-limb->soleHeight) < .006f*scale &&
                            hypotf(foot.x-feet[i].targetPosition.x,foot.z-feet[i].targetPosition.z) < .006f*scale,
                            "El apoyo posterior se conserva cerca del suelo y de su objetivo");
            }
        }
    }
    TEST_ASSERT(sawFlight && (maxHipY - minHipY) > 0.04f,
                "Salto bípedo sincronizado produce fase aérea y arco vertical");

    Monster_Free(m);
    free(m);
    free(doc);
    printf("[PASS] test_five_mammals: canguro (macrópodo, brazos libres y salto bípedo aéreo)\n");
}

static void TestBatCapabilitiesAndOffspring(void) {
    CreatureRecipeDocument* batDoc = calloc(1, sizeof(*batDoc));
    CreatureRecipeDocument* platDoc = calloc(1, sizeof(*platDoc));
    CreatureRecipeDocument* panDoc = calloc(1, sizeof(*panDoc));
    CreatureRecipeDocument* canDoc = calloc(1, sizeof(*canDoc));
    TEST_ASSERT(batDoc && platDoc && panDoc && canDoc &&
                LoadSpecDoc("recipes/murcielago.creature.json", batDoc) &&
                LoadSpecDoc("recipes/ornitorrinco.creature.json", platDoc) &&
                LoadSpecDoc("recipes/pangolin.creature.json", panDoc) &&
                LoadSpecDoc("recipes/canguro.creature.json", canDoc), "Cargar recetas para alas y cruces");

    Monster* bat = malloc(sizeof(*bat));
    *bat = Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(bat, &batDoc->recipe, &batDoc->recipe.adult), "Construir murciélago");
    TEST_ASSERT(!bat->animation->rig.limbs[0].walking && !bat->animation->rig.limbs[1].walking,
                "Las alas son no ambulatorias");
    TEST_ASSERT(bat->animation->rig.limbs[0].role == LIMB_WING, "Asociación explícita del controlador alar");
    const HeadSurfaceRecipe* head = &bat->head.anatomy.surface;
    TEST_ASSERT(head->hasEars && head->earConcavity > .8f && head->earShape.y > head->craniumRadii.x*2.15f &&
                head->craniumRadii.y > head->craniumRadii.x*.46f, "Cara compacta y orejas largas con concha intacta");

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.025f; cfg.maxCells = 250000; cfg.maxResolution = 128;
    MonsterVisual vis = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&vis, bat, MonsterSDF_DefaultConfig()), "Visual de murciélago");
    TEST_ASSERT(vis.membraneMesh.vertexCount > 200 && vis.membraneMesh.indexCount % 3 == 0,
                "Patagios y dactilopatagios alares generados");

    AnimatedVisual bind = {0};
    TEST_ASSERT(AnimatedVisual_Bind(&bind, &vis, bat) && bind.membrane != NULL, "Deformer de patagios vinculado");
    AABB3D extBox = MeshBounds(&vis.membraneMesh);
    float extSpan = AABB_Size(extBox).x;
    bat->animation->animator.wingFold = 0.92f;
    TEST_ASSERT(MonsterAnimation_Update(bat, 1.0f / 30.0f) && AnimatedVisual_Deform(&bind, &vis, bat),
                "Plegado articular alar aplicado a membrana");
    AABB3D foldBox = MeshBounds(&vis.membraneMesh);
    float foldSpan = AABB_Size(foldBox).x;
    TEST_ASSERT(isfinite(foldSpan) && foldSpan < extSpan * 0.88f,
                "El plegado alar recoge realmente la envergadura del patagio");
    bat->animation->animator.wingFold = 0;
    TEST_ASSERT(MonsterAnimation_Update(bat, 1.0f / 30.0f) && AnimatedVisual_Deform(&bind, &vis, bat),
                "Extensión después del plegado");
    TEST_ASSERT(FLOAT_NEAR(AABB_Size(MeshBounds(&vis.membraneMesh)).x, extSpan),
                "El ala recupera la extensión al soltar el controlador");

    /* Cruces entre especies nuevas via CreatureOffspring */
    char err[256] = {0};
    CreatureOffspringConfig ocfg = CreatureOffspring_Default(2, 77u);
    CreatureOffspringBatch* cross1 = CreatureOffspring_Generate(&platDoc->recipe, &panDoc->recipe, ocfg, NULL, NULL, err, sizeof(err));
    TEST_ASSERT(cross1 != NULL && CreatureOffspring_Count(cross1) >= 1, "Cruce Ornitorrinco x Pangolín");
    const CreatureRecipe* r1 = CreatureOffspring_Get(cross1, 0, NULL);
    TEST_ASSERT(r1 && CreatureRecipe_Validate(r1, &r1->adult), "Descendiente Ornitorrinco x Pangolín válido");

    CreatureOffspringBatch* cross2 = CreatureOffspring_Generate(&canDoc->recipe, &batDoc->recipe, ocfg, NULL, NULL, err, sizeof(err));
    TEST_ASSERT(cross2 != NULL && CreatureOffspring_Count(cross2) >= 1, "Cruce Canguro x Murciélago");
    const CreatureRecipe* r2 = CreatureOffspring_Get(cross2, 0, NULL);
    TEST_ASSERT(r2 && CreatureRecipe_Validate(r2, &r2->adult), "Descendiente Canguro x Murciélago válido");

    CreatureOffspring_Destroy(cross1);
    CreatureOffspring_Destroy(cross2);
    AnimatedVisual_Free(&bind);
    MonsterVisual_Free(&vis);
    Monster_Free(bat);
    free(bat);
    free(batDoc); free(platDoc); free(panDoc); free(canDoc);
    printf("[PASS] test_five_mammals: murciélago (alas, patagios, plegado) y CreatureOffspring\n");
}

void run_five_mammals_tests(void) {
    TestFacialFrames();
    TestAllFiveRecipesFourStagesAndAger();
    TestGiraffeCapabilities();
    TestPlatypusCapabilities();
    TestPangolinCapabilities();
    TestKangarooCapabilities();
    TestBatCapabilitiesAndOffspring();
}
