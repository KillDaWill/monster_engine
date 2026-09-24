/**
 * @file test_fur.c
 * @brief Pruebas unitarias para el sistema de pelaje procedural (FurPhenotype, flujo y máscara).
 * @author Monster Engine Team
 * @date 2026
 */

#include "Creature.h"
#include "Fur.h"
#include "CreatureRecipes.h"
#include "Limb.h"
#include "test_utils.h"
#include "SurfacePresets.h"
#include "SurfaceMapper.h"
#include "Monster.h"
#include "MonsterVisual.h"
#include "MonsterAnimation.h"
#include "AnatomyDeformer.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>

static void TestFurPhenotypeAndPreset(void) {
    /* 1. Determinismo con semilla idéntica y diferenciación con semilla distinta */
    for (uint32_t seed = 0; seed < 50; ++seed) {
        SurfacePhenotype a = SurfacePreset_CanidShortDoubleCoat(seed, 1.0f);
        SurfacePhenotype b = SurfacePreset_CanidShortDoubleCoat(seed, 1.0f);
        SurfaceRecipe ra = SurfaceRecipe_Compile(&a);
        SurfaceRecipe rb = SurfaceRecipe_Compile(&b);
        TEST_ASSERT(memcmp(&ra, &rb, sizeof(ra)) == 0, "Recetas de pelaje idénticas con misma semilla");

        SurfacePhenotype c = SurfacePreset_CanidShortDoubleCoat(seed + 1, 1.0f);
        SurfaceRecipe rc = SurfaceRecipe_Compile(&c);
        TEST_ASSERT(memcmp(&ra, &rc, sizeof(ra)) != 0, "Semillas diferentes generan recetas distintas");

        /* 2. Resistencia a valores inválidos (NaN, Inf, negativos) */
        a.integument.fur.length = NAN;
        a.integument.fur.undercoatLength = -1.0f;
        a.integument.fur.density = INFINITY;
        a.integument.fur.roughness = NAN;
        ra = SurfaceRecipe_Compile(&a);
        for (int i = 0; i < SURFACE_RECIPE_ROWS; ++i) {
            for (int j = 0; j < 4; ++j) {
                TEST_ASSERT(isfinite(ra.data[i][j]), "Receta de pelaje sin NaN ni Inf");
            }
        }
        /* length acotado a rango seguro [0.005, 2.0] */
        TEST_ASSERT(ra.data[8][0] >= 0.005f && ra.data[8][0] <= 2.0f, "Longitud de pelo en rango seguro");
        /* densidad acotada a rango [0.05, 4.0] */
        TEST_ASSERT(ra.data[8][1] >= 0.05f && ra.data[8][1] <= 4.0f, "Densidad de pelo en rango seguro");
    }

    /* 3. Coberturas y exclusión mutua de escamas vs pelaje */
    SurfacePhenotype dogCoat = SurfacePreset_CanidShortDoubleCoat(123, 1.0f);
    SurfaceRecipe dogRecipe = SurfaceRecipe_Compile(&dogCoat);
    TEST_ASSERT((int)(dogRecipe.data[7][0] + 0.5f) == 2, "Tipo de tegumento INTEGUMENT_FUR (2)");
    TEST_ASSERT(dogRecipe.data[11][3] > 0.0f, "Cobertura de pelaje positiva");
    TEST_ASSERT(dogRecipe.data[6][2] == 0.0f, "Cobertura de escamas estrictamente cero en pelaje");

    SurfacePhenotype reptileCoat = SurfacePreset_ScaledReptile(123, 1.0f);
    SurfaceRecipe reptileRecipe = SurfaceRecipe_Compile(&reptileCoat);
    TEST_ASSERT((int)(reptileRecipe.data[7][0] + 0.5f) == 1, "Tipo de tegumento INTEGUMENT_SCALES (1)");
    TEST_ASSERT(reptileRecipe.data[6][2] > 0.0f, "Cobertura de escamas positiva");
    TEST_ASSERT(reptileRecipe.data[11][3] == 0.0f, "Cobertura de pelaje estrictamente cero en reptil");

    /* 4. Estabilidad ontogenética de identidad celular */
    const CreatureRecipe* canid = CreatureRecipes_Dog();
    uint32_t adultFurSeed = canid->adult.surface.integument.fur.seed;
    for (int i = 0; i <= 20; ++i) {
        CreaturePhenotype p = CreaturePhenotype_Interpolate(&canid->juvenile, &canid->adult, (float)i * 0.05f);
        SurfaceRecipe r = SurfaceRecipe_Compile(&p.surface);
        TEST_ASSERT(r.furSeed == adultFurSeed, "Maduración no altera la semilla celular de pelaje");
        TEST_ASSERT(r.data[8][0] > 0.0f, "Longitud de pelo ontogenética finita y positiva");
    }

    /* 5. Perfiles regionales caninos */
    TEST_ASSERT(dogCoat.furRegions[SURFACE_REGION_NECK].lengthMultiplier > dogCoat.furRegions[SURFACE_REGION_HEAD].lengthMultiplier,
                "Multiplicador regional de cuello mayor que cabeza");
    TEST_ASSERT(dogCoat.furRegions[SURFACE_REGION_TAIL].lengthMultiplier > dogCoat.furRegions[SURFACE_REGION_DORSAL_TRUNK].lengthMultiplier,
                "Multiplicador regional de cola mayor que tronco dorsal");
    TEST_ASSERT(dogCoat.furRegions[SURFACE_REGION_DIGIT].lengthMultiplier < dogCoat.furRegions[SURFACE_REGION_DORSAL_TRUNK].lengthMultiplier,
                "Multiplicador regional de dígitos menor que tronco");
}

static void TestFurMappingFlowAndMasking(void) {
    Monster m = Monster_Create();
    const CreatureRecipe* recipe = CreatureRecipes_Dog();
    TEST_ASSERT(Creature_BuildMonster(&m, recipe, &recipe->adult), "Construir perro adulto para mapeo");

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.06f;
    MonsterVisual visual = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, &m, MonsterSDF_DefaultConfig()), "Mallado de perro para mapeo de pelaje");

    const Mesh* mesh = &visual.mesh;
    TEST_ASSERT(mesh->vertexCount > 0, "Malla del cuerpo no vacía");

    size_t validFlowCount = 0;
    size_t caudalFlowCount = 0;
    size_t distalLimbFlowCount = 0;
    size_t digitSoleMaskCount = 0;
    size_t dorsalMaskCount = 0;

    for (size_t i = 0; i < mesh->vertexCount; ++i) {
        const MeshVertex* v = &mesh->vertices[i];
        Vector3 flow = v->surface.flowDirection;
        Vector3 normal = v->surface.normal;
        float mask = v->surface.integumentMask;

        TEST_ASSERT(isfinite(flow.x) && isfinite(flow.y) && isfinite(flow.z), "Vector de flujo finito");
        TEST_ASSERT(isfinite(mask) && mask >= 0.0f && mask <= 1.001f, "Máscara de tegumento acotada [0, 1]");

        float flowLen = Vec3_Length(flow);
        if (flowLen > 0.1f) {
            /* Flujo normalizado */
            TEST_ASSERT(fabsf(flowLen - 1.0f) < 0.05f, "Dirección de flujo normalizada");
            /* Flujo tangente a la normal de reposo */
            float dotN = fabsf(Vec3_Dot(flow, normal));
            TEST_ASSERT(dotN < 0.05f, "Flujo de pelo ortogonal a la normal del tegumento");
            validFlowCount++;
        }

        /* Verificación de flujo caudal en tronco y cola (+Z es caudal en el perro) */
        if (v->surface.region == SURFACE_REGION_DORSAL_TRUNK ||
            v->surface.region == SURFACE_REGION_VENTRAL_TRUNK ||
            v->surface.region == SURFACE_REGION_TAIL) {
            if (flow.z > 0.05f) caudalFlowCount++;
        }

        /* Verificación de flujo distal en extremidades (-Y es hacia las patas en posición erecta) */
        if (v->surface.region == SURFACE_REGION_FORELIMB || v->surface.region == SURFACE_REGION_HINDLIMB) {
            if (flow.y < 0.0f) distalLimbFlowCount++;
        }

        /* Máscara en dígitos: zona ventral/sole debe anularse hacia la base de apoyo */
        if (v->surface.region == SURFACE_REGION_DIGIT && v->surface.ventral > 0.8f && normal.y < -0.6f) {
            if (mask < 0.1f) digitSoleMaskCount++;
        }

        /* Máscara dorsal de tronco completamente activa */
        if (v->surface.region == SURFACE_REGION_DORSAL_TRUNK && v->surface.ventral < 0.2f && normal.y > 0.5f) {
            if (mask > 0.95f) dorsalMaskCount++;
        }
    }

    TEST_ASSERT(validFlowCount > mesh->vertexCount * 9 / 10, "La inmensa mayoría de vértices tiene flujo definido");
    TEST_ASSERT(caudalFlowCount > 0, "Existe flujo caudal a lo largo de tronco y cola");
    TEST_ASSERT(distalLimbFlowCount > 0, "Existe flujo distal en extremidades hacia las garras");
    TEST_ASSERT(dorsalMaskCount > 0, "Tegumento de pelaje activo en dorso");

    Mesh boundMesh=Mesh_Create();
    Mesh_ReserveVertices(&boundMesh,mesh->vertexCount);
    memcpy(boundMesh.vertices,mesh->vertices,mesh->vertexCount*sizeof(MeshVertex));boundMesh.vertexCount=mesh->vertexCount;
    AnatomyDeformer* binding=AnatomyDeformer_Create();
    TEST_ASSERT(AnatomyDeformer_Bind(binding,&boundMesh,&m.anatomyGraph),"Vincular dominio para comparar máscaras");
    TEST_ASSERT(SurfaceMapper_MapBoundMesh(&boundMesh,binding,&m.anatomyGraph,&m.surfaceMapping,NULL),"Mapear dominio ligado");
    for(size_t i=0;i<mesh->vertexCount;++i) {
        TEST_ASSERT(fabsf(mesh->vertices[i].surface.integumentMask-boundMesh.vertices[i].surface.integumentMask)<.002f,"Máscara estática y ligada equivalente");
        TEST_ASSERT(Vec3_Distance(mesh->vertices[i].surface.position,boundMesh.vertices[i].surface.position)<.002f,"Mismo dominio estático y ligado");
    }
    AnatomyDeformer_Free(binding);Mesh_Free(&boundMesh);
    /* Verificación de enmascaramiento en tejidos no tegumentarios */
    SurfaceCoordinate oralCoord = SurfaceMapper_MapPoint(Vec3_Zero(), Vec3_Create(0, 1, 0), SDF_MATERIAL_MOUTH, &m.anatomyGraph, &m.surfaceMapping);
    TEST_ASSERT(oralCoord.integumentMask == 0.0f, "Tejido oral completamente excluido de pelaje (máscara 0)");

    SurfaceCoordinate nasalCoord = SurfaceMapper_MapPoint(Vec3_Zero(), Vec3_Create(0, 0, 1), SDF_MATERIAL_NASAL_PAD, &m.anatomyGraph, &m.surfaceMapping);
    TEST_ASSERT(nasalCoord.integumentMask == 0.0f, "Trufa canina (nasal pad) completamente excluida de pelaje");

    SurfaceCoordinate socketCoord = SurfaceMapper_MapPoint(Vec3_Zero(), Vec3_Create(1, 0, 0), SDF_MATERIAL_EYE_SOCKET, &m.anatomyGraph, &m.surfaceMapping);
    TEST_ASSERT(socketCoord.integumentMask == 0.0f, "Cuencas oculares excluidas de pelaje");

    MonsterVisual_Free(&visual);
    Monster_Free(&m);
}

static void TestFurAppearanceNoRebuild(void) {
    Monster m = Monster_Create();
    const CreatureRecipe* recipe = CreatureRecipes_Dog();
    TEST_ASSERT(Creature_BuildMonster(&m, recipe, &recipe->adult), "Construir perro para prueba de no reconstrucción");

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.06f;
    MonsterVisual visual = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, &m, MonsterSDF_DefaultConfig()), "Construcción inicial");

    uint64_t generation = visual.rebuildGeneration;
    uint64_t fingerprint = visual.geometryFingerprint;
    const MeshVertex* vPtr = visual.mesh.vertices;
    const MeshIndex* iPtr = visual.mesh.indices;
    SurfaceRecipe oldRecipe = visual.mesh.surfaceRecipe;

    /* Modificar parámetros de pelaje y color en la criatura */
    m.surface.integument.fur.length = 0.08f;
    m.surface.integument.fur.density = 450.0f;
    m.surface.integument.fur.undercoatLength = 0.035f;
    m.surface.pigment.baseColor = Color_FromRGB(220, 140, 70);
    Monster_SetSurface(&m, &m.surface);

    /* Actualización del visual: NUNCA debe regenerar la geometría */
    TEST_ASSERT(!MonsterVisual_Update(&visual, &m, 1.0f, 0, MonsterSDF_DefaultConfig()),
                "Edición de pelaje no reporta cambio geométrico");
    TEST_ASSERT(visual.rebuildGeneration == generation, "Generación visual inalterada");
    TEST_ASSERT(visual.geometryFingerprint == fingerprint, "Huella geométrica idéntica");
    TEST_ASSERT(vPtr == visual.mesh.vertices && iPtr == visual.mesh.indices, "Punteros de buffers idénticos");
    TEST_ASSERT(memcmp(&oldRecipe, &visual.mesh.surfaceRecipe, sizeof(oldRecipe)) != 0,
                "La receta GPU refleja los nuevos parámetros de pelaje");
    TEST_ASSERT(visual.mesh.surfaceRecipe.data[8][0] == 0.08f, "Nueva longitud compilada correctamente");

    /* Alternar a piel lisa sin remallar */
    SurfacePhenotype smooth = m.surface;
    smooth.integument.type = INTEGUMENT_SMOOTH_SKIN;
    smooth.integument.coverage = 0.0f;
    Monster_SetSurface(&m, &smooth);
    TEST_ASSERT(!MonsterVisual_Update(&visual, &m, 1.0f, 0, MonsterSDF_DefaultConfig()),
                "Desactivar pelaje no remalla la superficie");
    TEST_ASSERT(visual.mesh.surfaceRecipe.data[7][0] == 0.0f, "Receta compilada como piel lisa");
    TEST_ASSERT(visual.rebuildGeneration == generation, "Sin reconstrucción tras desactivar pelaje");

    MonsterVisual_Free(&visual);
    Monster_Free(&m);
}

static void TestFurPoseStability(void) {
    Monster m = Monster_Create();
    CreaturePhenotype p = CreatureRecipes_Lizard()->adult;
    p.surface = SurfacePreset_CanidShortDoubleCoat(42, 1.0f);
    TEST_ASSERT(Creature_BuildMonster(&m, CreatureRecipes_Lizard(), &p), "Construir espécimen para prueba de estabilidad");

    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.06f;
    MonsterVisual visual = MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual, &m, MonsterSDF_DefaultConfig()), "Mallado base");

    Mesh mesh = Mesh_Create();
    for (size_t i = 0; i < visual.mesh.vertexCount; ++i) {
        Mesh_AddVertex(&mesh, visual.mesh.vertices[i], NULL);
    }
    for (size_t i = 0; i + 2 < visual.mesh.indexCount; i += 3) {
        Mesh_AddTriangle(&mesh, visual.mesh.indices[i], visual.mesh.indices[i+1], visual.mesh.indices[i+2]);
    }

    SurfaceCoordinate* saved = (SurfaceCoordinate*)malloc(mesh.vertexCount * sizeof(*saved));
    TEST_ASSERT(saved != NULL, "Reserva de coordenadas de reposo");
    for (size_t i = 0; i < mesh.vertexCount; ++i) {
        saved[i] = mesh.vertices[i].surface;
    }

    AnatomyDeformer* deformer = AnatomyDeformer_Create();
    TEST_ASSERT(AnatomyDeformer_BindSkeleton(deformer, &mesh, &m.anatomyGraph, &m.animation->rig.skeleton),
                "Bind del esqueleto canino");

    Vector3 initialPos = mesh.vertices[0].position;
    m.animation->animator.desiredVelocity = Vec3_Create(0, 0, 0.4f);

    /* Ejecutar simulación de marcha durante varios fotogramas */
    for (int frame = 0; frame < 30; ++frame) {
        TEST_ASSERT(MonsterAnimation_Update(&m, 1.0f / 60.0f), "Actualización cinemática");
        TEST_ASSERT(AnatomyDeformer_DeformPose(deformer, &m.animation->rig.skeleton, &m.animation->pose, &mesh),
                    "Deformar pose");
        for (size_t i = 0; i < mesh.vertexCount; ++i) {
            /* Las coordenadas de superficie, flujo y máscara de reposo son estrictamente invariantes a la pose */
            TEST_ASSERT(FLOAT_NEAR(mesh.vertices[i].surface.flowDirection.x, saved[i].flowDirection.x),
                        "Flujo de reposo X invariante a la pose");
            TEST_ASSERT(FLOAT_NEAR(mesh.vertices[i].surface.flowDirection.y, saved[i].flowDirection.y),
                        "Flujo de reposo Y invariante a la pose");
            TEST_ASSERT(FLOAT_NEAR(mesh.vertices[i].surface.flowDirection.z, saved[i].flowDirection.z),
                        "Flujo de reposo Z invariante a la pose");
            TEST_ASSERT(FLOAT_NEAR(mesh.vertices[i].surface.integumentMask, saved[i].integumentMask),
                        "Máscara de reposo invariante a la pose");
        }
    }

    TEST_ASSERT(Vec3_Distance(initialPos, mesh.vertices[0].position) > 0.01f,
                "La animación deforma efectivamente la geometría");

    free(saved);
    AnatomyDeformer_Free(deformer);
    Mesh_Free(&mesh);
    MonsterVisual_Free(&visual);
    Monster_Free(&m);
}

static Mesh FurTestPlane(unsigned divisions) {
    Mesh m=Mesh_Create();
    size_t n=(divisions+1)*(divisions+1);
    Mesh_ReserveVertices(&m,n);Mesh_ReserveIndices(&m,6*divisions*divisions);
    for(unsigned y=0;y<=divisions;++y)for(unsigned x=0;x<=divisions;++x) {
        MeshVertex v={0};v.position=Vec3_Create((float)x/divisions,(float)y/divisions,0);
        v.normal=Vec3_Create(0,0,1);v.surface.position=v.position;v.surface.normal=v.normal;
        v.surface.flowDirection=Vec3_Create(0,1,0);v.surface.integumentMask=1;v.material=SDF_MATERIAL_SKIN;
        m.vertices[m.vertexCount++]=v;
    }
    for(unsigned y=0;y<divisions;++y)for(unsigned x=0;x<divisions;++x) {
        unsigned a=y*(divisions+1)+x,b=a+1,c=a+divisions+1,d=c+1;
        unsigned ids[6]={a,b,c,b,d,c};for(unsigned k=0;k<6;++k)m.indices[m.indexCount++]=ids[k];
    }
    return m;
}
static void TestFurFieldAndRoots(void) {
    SurfacePhenotype s=SurfacePhenotype_Default();FurPhenotype f=s.integument.fur;
    Vector3 n={0,0,1},flow={0,1,0};
    for(int i=1;i<99;++i) {
        float h=i*.01f;FurCurve c=Fur_EvaluateCurve(&f,n,flow,.72f,h);
        FurCurve a=Fur_EvaluateCurve(&f,n,flow,.72f,h-.0001f),b=Fur_EvaluateCurve(&f,n,flow,.72f,h+.0001f);
        Vector3 derivative=Vec3_Scale(Vec3_Sub(b.offset,a.offset),5000);
        TEST_ASSERT(Vec3_Distance(derivative,c.derivative)<.0003f,"Tangente coincide con derivada central");
        TEST_ASSERT(b.radius<a.radius,"Fibra se afina hacia la punta");
    }
    f.stiffness=0;FurCurve soft=Fur_EvaluateCurve(&f,n,flow,.5f,1);
    f.stiffness=1;FurCurve stiff=Fur_EvaluateCurve(&f,n,flow,.5f,1);
    TEST_ASSERT(soft.offset.y>stiff.offset.y*4,"Rigidez reduce flexión geométrica");
    Mesh a=FurTestPlane(1),b=FurTestPlane(12);FurRootSet ra={0},rb={0},repeat={0},other={0};
    TEST_ASSERT(FurRootSet_Build(&ra,&a,32,5000,6000)&&FurRootSet_Build(&rb,&b,32,5000,6000),"Muestreo por área");
    TEST_ASSERT(ra.count==rb.count&&fabsf(ra.area-rb.area)<.00001f,"Teselación no altera densidad");
    TEST_ASSERT(FurRootSet_Build(&repeat,&a,32,5000,6000)&&FurRootSet_Build(&other,&a,33,5000,6000),"Semillas reproducibles");
    TEST_ASSERT(!memcmp(ra.roots,repeat.roots,ra.count*sizeof(FurRoot)),"Mismas raíces para misma semilla");
    TEST_ASSERT(memcmp(ra.roots,other.roots,ra.count*sizeof(FurRoot)),"Semillas distintas cambian raíces");
    size_t left=0;
    for(size_t i=0;i<ra.count;++i) {
        FurRoot r=ra.roots[i];float sum=r.barycentric.x+r.barycentric.y+r.barycentric.z;
        TEST_ASSERT(r.triangleIndex<2&&r.barycentric.x>=0&&r.barycentric.y>=0&&r.barycentric.z>=0&&fabsf(sum-1)<1e-6f,"Baricéntricas válidas");
        Vector3 p=Vec3_Zero();float weights[3]={r.barycentric.x,r.barycentric.y,r.barycentric.z};
        for(int k=0;k<3;++k)p=Vec3_Add(p,Vec3_Scale(a.vertices[a.indices[r.triangleIndex*3+k]].position,weights[k]));
        left+=p.x<.25f;
        float hash=Fur_Random(r.randomSeed);TEST_ASSERT(!(hash<.15f)||hash<.5f,"Subconjuntos LOD anidados");
    }
    TEST_ASSERT(left>ra.count*.22f&&left<ra.count*.28f,"Distribución espacial proporcional al área");
    for(size_t i=0;i<a.vertexCount;++i)a.vertices[i].surface.integumentMask=0;
    TEST_ASSERT(FurRootSet_Build(&repeat,&a,32,5000,6000)&&repeat.count==0,"Regiones enmascaradas sin raíces");
    FurRootSet_Free(&ra);FurRootSet_Free(&rb);FurRootSet_Free(&repeat);FurRootSet_Free(&other);Mesh_Free(&a);Mesh_Free(&b);
}
static void TestFurOpticsAndLOD(void) {
    float reference=0;
    for(int count=8;count<=32;count*=2) {
        float samples[32][2],trans=1;int used=Fur_ShellSamples((float)count,samples);
        for(int i=0;i<used;++i) {
            float h=samples[i][0];float occupancy=.2f*(1-h)*(1-h);
            trans*=1-Fur_OpticalAlpha(5,occupancy,samples[i][1],.65f);
        }
        if(count==8)reference=trans;
        TEST_ASSERT(fabsf(reference-trans)<.06f,"Opacidad integrada 8/16/32 estable");
    }
    for(int i=40;i<=320;++i) {
        float samples[32][2];int count=Fur_ShellSamples(i*.1f,samples);float sum=0;
        TEST_ASSERT(count<=32,"Número de conchas acotado");
        for(int j=0;j<count;++j){sum+=samples[j][1];TEST_ASSERT(samples[j][0]>0&&samples[j][0]<=1,"Altura de concha válida");}
        TEST_ASSERT(fabsf(sum-1)<1e-6f,"LOD conserva espesor óptico total");
    }
    TEST_ASSERT(Fur_ShellResolution(100,.5f,1)==32&&Fur_ShellResolution(.1f,1,1)==0,"LOD proyectado acotado");
    TEST_ASSERT(Fur_ShellResolution(8,1,1)>Fur_ShellResolution(8,0,1),"Rasante aumenta resolución");
}

void run_fur_tests(void) {
    TestFurFieldAndRoots();TestFurOpticsAndLOD();
    TestFurPhenotypeAndPreset();
    TestFurMappingFlowAndMasking();
    TestFurAppearanceNoRebuild();
    TestFurPoseStability();
    printf("[PASS] pelaje: fenotipo, determinismo, flujo anatómico, máscara, desacoplo de malla y pose\n");
}
