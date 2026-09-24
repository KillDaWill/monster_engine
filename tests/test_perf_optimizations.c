#include "Creature.h"
#include "Limb.h"
/**
 * @file test_perf_optimizations.c
 * @brief Pruebas unitarias y de regresión para las optimizaciones de rendimiento:
 *        - Equivalencia de distancia con poda conservadora de conectores.
 *        - Determinismo exacto entre muestreo paralelo y muestreo serie.
 *        - Integridad y cobertura de cajas AABB por componente y máscara de celdas candidatas.
 *        - Reutilización sin fugas ni corrupción con Monster_CopyInto.
 *        - Selección correcta de tiers (MORPH vs SETTLED) en MonsterVisualAsync.
 */

#include "test_utils.h"
#include "Monster.h"
#include "Creature.h"
#include "MonsterSDF.h"
#include "SDFMesher.h"
#include "SDFSamplingPool.h"
#include "MonsterVisualAsync.h"
#include "MonsterAger.h"
#include "AABB.h"
#include <string.h>
#include <unistd.h>

static Monster CreateTestLizard(bool adult) {
    Monster lizard = Monster_Create();
    CreaturePhenotype phenotype = adult ? CreatureRecipes_Lizard()->adult : CreatureRecipes_Lizard()->juvenile;
    Creature_BuildMonster(&lizard,CreatureRecipes_Lizard(), &phenotype);
    Monster_SetHeadOpenFactor(&lizard, 0.10f);
    return lizard;
}

/**
 * Prueba 1: Equivalencia numérica de evaluación de campo SDF entre
 * poda conservadora activa y desactivada.
 */
static void test_connector_pruning_equivalence(void) {
    printf("  [TEST] test_connector_pruning_equivalence...\n");
    Monster lizard = CreateTestLizard(true);

    MonsterSDFConfig cfgUnpruned = MonsterSDF_DefaultConfig();
    cfgUnpruned.enableConnectorPruning = false;
    MonsterSDF sdfUnpruned = MonsterSDF_Create();
    MonsterSDF_Build(&sdfUnpruned, &lizard, cfgUnpruned);

    MonsterSDFConfig cfgPruned = MonsterSDF_DefaultConfig();
    cfgPruned.enableConnectorPruning = true;
    MonsterSDF sdfPruned = MonsterSDF_Create();
    MonsterSDF_Build(&sdfPruned, &lizard, cfgPruned);

    AABB3D bounds = MonsterSDF_GetBounds(&sdfPruned);
    /* Evaluar una rejilla de puntos dentro y fuera del volumen */
    int samplesPerAxis = 12;
    for (int ix = 0; ix < samplesPerAxis; ++ix) {
        float tx = (float)ix / (float)(samplesPerAxis - 1);
        float x = bounds.start.x + tx * (bounds.end.x - bounds.start.x);
        for (int iy = 0; iy < samplesPerAxis; ++iy) {
            float ty = (float)iy / (float)(samplesPerAxis - 1);
            float y = bounds.start.y + ty * (bounds.end.y - bounds.start.y);
            for (int iz = 0; iz < samplesPerAxis; ++iz) {
                float tz = (float)iz / (float)(samplesPerAxis - 1);
                float z = bounds.start.z + tz * (bounds.end.z - bounds.start.z);

                Vector3 p = Vec3_Create(x, y, z);
                float d1 = MonsterSDF_EvaluateDistance(&sdfUnpruned, p);
                float d2 = MonsterSDF_EvaluateDistance(&sdfPruned, p);

                /* En la banda inmediata de la isosuperficie (|d| < 0.05), donde
                 * Marching Cubes genera vértices, la poda no debe alterar el campo. */
                if (fabsf(d1) < 0.05f || fabsf(d2) < 0.05f) {
                    if (!FLOAT_NEAR(d1, d2)) {
                        printf("  [DEBUG ISOSUPERFICIE] ix=%d iy=%d iz=%d p=(%.3f,%.3f,%.3f) d1=%.5f d2=%.5f diff=%.5f\n",
                               ix, iy, iz, p.x, p.y, p.z, d1, d2, fabsf(d1 - d2));
                    }
                    TEST_ASSERT(FLOAT_NEAR(d1, d2), "La poda conservadora no debe alterar la isosuperficie");
                }
            }
        }
    }

    /* Cobertura determinista de todo el dominio, no sólo de la banda superficial. */
    uint32_t seed=12345;
    for(unsigned i=0;i<20000;++i) {
        float t[3];
        for(unsigned k=0;k<3;++k){seed=1664525u*seed+1013904223u;t[k]=(seed>>8)/16777216.0f;}
        Vector3 p=Vec3_Create(bounds.start.x+t[0]*(bounds.end.x-bounds.start.x),
            bounds.start.y+t[1]*(bounds.end.y-bounds.start.y),
            bounds.start.z+t[2]*(bounds.end.z-bounds.start.z));
        float full=MonsterSDF_EvaluateDistance(&sdfUnpruned,p);
        float pruned=MonsterSDF_EvaluateDistance(&sdfPruned,p);
        if(fabsf(full-pruned)>=1e-6f)printf("poda: p=%g,%g,%g full=%g pruned=%g diff=%g\n",p.x,p.y,p.z,full,pruned,fabsf(full-pruned));
        TEST_ASSERT(fabsf(full-pruned)<1e-6f,"La poda escalar debe conservar todo el campo");
    }

    MonsterSDF_Free(&sdfUnpruned);
    MonsterSDF_Free(&sdfPruned);
    Monster_Free(&lizard);
}

/**
 * Prueba 2: Determinismo estricto bit a bit / float idéntico entre
 * muestreo serie (1 hilo) y paralelo (4 hilos).
 */
static void test_parallel_serial_determinism(void) {
    printf("  [TEST] test_parallel_serial_determinism...\n");
    Monster lizard = CreateTestLizard(false);

    MonsterSDF sdf = MonsterSDF_Create();
    MonsterSDF_Build(&sdf, &lizard, MonsterSDF_DefaultConfig());

    SDFField field = MonsterSDF_GetField(&sdf);
    AABB3D bounds = MonsterSDF_GetBounds(&sdf);
    AABB_Pad(&bounds, 0.25f);

    SDFMesherConfig cfgSerial = SDFMesher_DefaultConfig();
    cfgSerial.voxelSize = 0.12f;
    cfgSerial.useAutoBounds = false;
    cfgSerial.bounds = bounds;
    cfgSerial.samplingThreadCount = 1;

    /* 1. Malla en serie */
    SDFMesher mesherSerial = SDFMesher_Create(cfgSerial);
    Mesh meshSerial = Mesh_Create();
    bool okSerial = SDFMesher_GenerateMeshDetailed(&mesherSerial, &field, NULL, 0, &meshSerial);
    TEST_ASSERT(okSerial, "Generación en serie debe ser exitosa");

    /* 2. Malla en paralelo con pool de hilos */
    SDFMesherConfig cfgParallel = cfgSerial;
    cfgParallel.samplingThreadCount = 4;
    SDFSamplingPool* pool = SDFSamplingPool_Create(4);
    SDFMesher mesherParallel = SDFMesher_Create(cfgParallel);
    SDFMesher_SetSamplingPool(&mesherParallel, pool);
    Mesh meshParallel = Mesh_Create();
    bool okParallel = SDFMesher_GenerateMeshDetailed(&mesherParallel, &field, NULL, 0, &meshParallel);
    TEST_ASSERT(okParallel, "Generación en paralelo debe ser exitosa");

    TEST_ASSERT(meshSerial.vertexCount > 0, "El mallador en serie debe generar vértices");
    TEST_ASSERT(meshSerial.vertexCount == meshParallel.vertexCount,
                "El conteo de vértices en paralelo debe ser exactamente igual al serie");
    TEST_ASSERT(meshSerial.indexCount == meshParallel.indexCount,
                "El conteo de índices en paralelo debe ser exactamente igual al serie");

    for (size_t i = 0; i < meshSerial.vertexCount; ++i) {
        TEST_ASSERT(FLOAT_NEAR(meshSerial.vertices[i].position.x, meshParallel.vertices[i].position.x),
                    "Posición X de vértice debe coincidir");
        TEST_ASSERT(FLOAT_NEAR(meshSerial.vertices[i].position.y, meshParallel.vertices[i].position.y),
                    "Posición Y de vértice debe coincidir");
        TEST_ASSERT(FLOAT_NEAR(meshSerial.vertices[i].position.z, meshParallel.vertices[i].position.z),
                    "Posición Z de vértice debe coincidir");

        TEST_ASSERT(FLOAT_NEAR(meshSerial.vertices[i].normal.x, meshParallel.vertices[i].normal.x),
                    "Normal X de vértice debe coincidir");
        TEST_ASSERT(FLOAT_NEAR(meshSerial.vertices[i].normal.y, meshParallel.vertices[i].normal.y),
                    "Normal Y de vértice debe coincidir");
        TEST_ASSERT(FLOAT_NEAR(meshSerial.vertices[i].normal.z, meshParallel.vertices[i].normal.z),
                    "Normal Z de vértice debe coincidir");
    }

    for (size_t i = 0; i < meshSerial.indexCount; ++i) {
        TEST_ASSERT(meshSerial.indices[i] == meshParallel.indices[i],
                    "Los índices generados deben ser idénticos");
    }

    Mesh_Free(&meshSerial);
    Mesh_Free(&meshParallel);
    SDFMesher_Free(&mesherSerial);
    SDFMesher_Free(&mesherParallel);
    SDFSamplingPool_Free(pool);
    MonsterSDF_Free(&sdf);
    Monster_Free(&lizard);
}

/**
 * Prueba 3: Cobertura de cajas AABB por componente y omisión de celdas vacías.
 */
static void test_component_bounds_and_cell_skipping(void) {
    printf("  [TEST] test_component_bounds_and_cell_skipping...\n");
    Monster lizard = CreateTestLizard(true);

    MonsterSDF sdf = MonsterSDF_Create();
    MonsterSDF_Build(&sdf, &lizard, MonsterSDF_DefaultConfig());

    AABB3D compBoxes[ANATOMY_MAX_CONNECTIONS+32];
    size_t compCount = MonsterSDF_GetComponentBounds(&sdf, compBoxes, ANATOMY_MAX_CONNECTIONS+32);
    TEST_ASSERT(compCount > 0, "Debe reportar cajas de componentes geométricos");

    /* Verificar que las cajas de componentes cubran las partes anatómicas */
    AABB3D totalUnion = compBoxes[0];
    for (size_t i = 1; i < compCount; ++i) {
        AABB_ExpandPoint(&totalUnion, compBoxes[i].start);
        AABB_ExpandPoint(&totalUnion, compBoxes[i].end);
    }

    AABB3D sdfBox = MonsterSDF_GetBounds(&sdf);
    /* La unión de componentes debe tener extensión geométrica y estar contenida en el dominio */
    TEST_ASSERT(totalUnion.end.x > totalUnion.start.x + 1.0f, "La unión de componentes debe tener extensión física en X");
    TEST_ASSERT(totalUnion.end.z > totalUnion.start.z + 5.0f, "La unión de componentes debe tener extensión física en Z");
    TEST_ASSERT(totalUnion.start.x >= sdfBox.start.x - 0.1f, "La unión de componentes debe estar contenida en sdfBox min X");
    TEST_ASSERT(totalUnion.end.x <= sdfBox.end.x + 0.1f, "La unión de componentes debe estar contenida en sdfBox max X");
    TEST_ASSERT(totalUnion.start.z >= sdfBox.start.z - 0.1f, "La unión de componentes debe estar contenida en sdfBox min Z");
    TEST_ASSERT(totalUnion.end.z <= sdfBox.end.z + 0.1f, "La unión de componentes debe estar contenida en sdfBox max Z");

    /* Ejecutar mallador y verificar que omite celdas vacías */
    SDFField field = MonsterSDF_GetField(&sdf);
    SDFMesherConfig cfg = SDFMesher_DefaultConfig();
    cfg.voxelSize = 0.08f;
    cfg.useAutoBounds = true;

    SDFMesher mesher = SDFMesher_Create(cfg);
    Mesh mesh = Mesh_Create();
    bool ok = SDFMesher_GenerateMeshDetailed(&mesher, &field, NULL, 0, &mesh);
    TEST_ASSERT(ok, "Generación de malla debe ser exitosa");

    const SDFMesherStats* stats = SDFMesher_GetLastStats(&mesher);
    TEST_ASSERT(stats->cellCount > 0, "Debe haber celdas totales en el dominio");
    TEST_ASSERT(stats->activeCellCount < stats->cellCount,
                "El filtrado de celdas candidatas debe omitir celdas vacías");
    TEST_ASSERT(stats->distanceEvaluationCount < (size_t)(stats->resolutionX * stats->resolutionY * stats->resolutionZ),
                "El muestreo disperso debe evaluar menos nodos que el volumen denso");
    TEST_ASSERT(mesh.indexCount > 0, "La malla generada debe contener triángulos válidos");

    Mesh_Free(&mesh);
    SDFMesher_Free(&mesher);
    MonsterSDF_Free(&sdf);
    Monster_Free(&lizard);
}

/**
 * Prueba 4: Verificación de copia profunda y reutilización de buffers con Monster_CopyInto.
 */
static void test_monster_copy_into_reuse(void) {
    printf("  [TEST] test_monster_copy_into_reuse...\n");
    Monster young = CreateTestLizard(false);
    Monster adult = CreateTestLizard(true);

    Monster copy = Monster_Create();

    /* 1. Primera copia */
    bool ok1 = Monster_CopyInto(&copy, &young);
    TEST_ASSERT(ok1, "Monster_CopyInto debe tener éxito al copiar joven");
    TEST_ASSERT(copy.bodyPartCount == young.bodyPartCount, "Conteo de partes debe coincidir");
    TEST_ASSERT(copy.eyeCount == young.eyeCount, "Conteo de ojos debe coincidir");
    TEST_ASSERT(copy.mouthCount == young.mouthCount, "Conteo de bocas debe coincidir");
    TEST_ASSERT(copy.hasCreaturePhenotype == young.hasCreaturePhenotype, "Flag de fenotipo debe coincidir");
    TEST_ASSERT(FLOAT_NEAR(copy.phenotype.axial.totalScale, young.phenotype.axial.totalScale),
                "Escala total debe coincidir con joven");

    /* Guardar punteros de buffer preasignado */
    BodyPart* initialParts = copy.bodyParts;
    Eye* initialEyes = copy.eyes;
    Mouth* initialMouths = copy.mouths;

    /* 2. Sobrescribir copiando el adulto en el mismo contenedor */
    bool ok2 = Monster_CopyInto(&copy, &adult);
    TEST_ASSERT(ok2, "Monster_CopyInto debe tener éxito al copiar adulto");
    TEST_ASSERT(FLOAT_NEAR(copy.phenotype.axial.totalScale, adult.phenotype.axial.totalScale),
                "Escala total debe actualizarse a la del adulto");

    /* Reutilización de memoria: si la capacidad era suficiente, no debe reasignar innecesariamente */
    TEST_ASSERT(copy.bodyParts == initialParts, "Debe reutilizar el buffer existente de bodyParts");
    TEST_ASSERT(copy.eyes == initialEyes, "Debe reutilizar el buffer existente de eyes");
    TEST_ASSERT(copy.mouths == initialMouths, "Debe reutilizar el buffer existente de mouths");

    Monster_Free(&copy);
    Monster_Free(&young);
    Monster_Free(&adult);
}

/**
 * Prueba 5: Selección de calidad MORPH vs SETTLED en MonsterVisualAsync.
 */
static void test_visual_async_morph_settled_tiers(void) {
    printf("  [TEST] test_visual_async_morph_settled_tiers...\n");
    Monster lizard = CreateTestLizard(false);

    MonsterVisualAsyncConfig cfg = MonsterVisualAsync_DefaultConfig();
    cfg.settledDelaySec = 0.05f;
    MonsterVisualAsync* visual = MonsterVisualAsync_Create(cfg);
    TEST_ASSERT(visual != NULL, "Debe crear el gestor asíncrono");

    /* 1. Activar modo MORPH y actualizar */
    MonsterVisualAsync_SetMorphMode(visual, true);
    MonsterVisualAsync_Update(visual, &lizard, 0.016f);

    /* Esta prueba comprueba selección de calidad, no una latencia de máquina.
     * La barrera espera publicación real incluso bajo sanitizadores. */
    MonsterVisualAsync_Flush(visual);
    TEST_ASSERT(MonsterVisualAsync_GetDisplayGeneration(visual) > 0,
                "El worker debe completar al menos una malla");

    MonsterVisualAsyncStats stats = MonsterVisualAsync_GetStats(visual);
    TEST_ASSERT(stats.activeQualityTier == MONSTER_VISUAL_QUALITY_MORPH,
                "En modo morph el tier activo debe ser MONSTER_VISUAL_QUALITY_MORPH");

    /* 2. Desactivar modo MORPH y simular paso del tiempo para transición a SETTLED */
    MonsterVisualAsync_SetMorphMode(visual, false);
    /* Avanzar tiempo más allá de settledDelaySec */
    MonsterVisualAsync_Update(visual, &lizard, 0.10f);

    MonsterVisualAsync_Flush(visual);
    stats = MonsterVisualAsync_GetStats(visual);
    TEST_ASSERT(stats.displayedFingerprint==stats.requestedFingerprint,
                "La calidad comprobada pertenece al snapshot solicitado");

    TEST_ASSERT(stats.activeQualityTier == MONSTER_VISUAL_QUALITY_SETTLED,
                "Tras el retraso de reposo debe transicionar a MONSTER_VISUAL_QUALITY_SETTLED");

    MonsterVisualAsync_Free(visual);
    Monster_Free(&lizard);
}


static bool contains_point(AABB3D b,Vector3 p) {
    return p.x>=b.start.x&&p.x<=b.end.x&&p.y>=b.start.y&&p.y<=b.end.y&&p.z>=b.start.z&&p.z<=b.end.z;
}

static void test_juvenile_connector_bounds(void) {
    Monster m=CreateTestLizard(false);MonsterSDF sdf=MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf,&m,MonsterSDF_DefaultConfig()),"SDF juvenil inválido");
    AABB3D boxes[ANATOMY_MAX_CONNECTIONS+32];
    size_t count=MonsterSDF_GetComponentBounds(&sdf,boxes,ANATOMY_MAX_CONNECTIONS+32);
    TEST_ASSERT(count>sdf.connectorCount&&sdf.connectorCount>96,"No se ejercita la capacidad ampliada");
    size_t probes=0;
    for(size_t i=0;i<sdf.connectorCount;++i) {
        const MonsterSDFConnector* c=&sdf.connectors[i];
        if(c->kind!=BODY_CONNECTION_DIGIT_SEGMENT)continue;
        /* Banda alrededor de la superficie de cada conector, incluidos extremos. */
        for(unsigned j=0;j<5;++j)for(unsigned a=0;a<24;++a)for(unsigned k=0;k<5;++k) {
            float t=j*.25f,angle=a*6.28318530718f/24,scale=.8f+k*.1f;
            float w=c->widthA+t*(c->widthB-c->widthA),h=c->heightA+t*(c->heightB-c->heightA);
            Vector3 p=Vec3_Add(Vec3_Lerp(c->a,c->b,t),Vec3_Add(
                Vec3_Scale(c->side,w*cosf(angle)*scale),Vec3_Scale(c->up,h*sinf(angle)*scale)));
            sdf.config.enableConnectorPruning=false;float full=MonsterSDF_EvaluateDistance(&sdf,p);
            sdf.config.enableConnectorPruning=true;float pruned=MonsterSDF_EvaluateDistance(&sdf,p);
            TEST_ASSERT(isfinite(full)&&isfinite(pruned),"Distancia digital no finita");
            if(fabsf(full)<h*.25f) {
                TEST_ASSERT(fabsf(full-pruned)<1e-6f,"La poda altera el campo junto a una falange juvenil");
                TEST_ASSERT(fabsf(full)<1e-6f||(full<0)==(pruned<0),"La poda invierte el signo digital");
                bool covered=false;
                for(size_t b=0;b<count;++b)covered|=contains_point(boxes[b],p);
                TEST_ASSERT(covered,"Las cajas no cubren la banda superficial digital");
                ++probes;
            }
            if(scale<=1.0f)TEST_ASSERT(contains_point(c->bounds,p),"La caja local no contiene el conector");
        }
        /* También inspecciona el entorno mundial de su AABB; no sólo el eje. */
        for(unsigned x=0;x<5;++x)for(unsigned y=0;y<5;++y)for(unsigned z=0;z<5;++z) {
            Vector3 p=Vec3_Create(c->bounds.start.x+(c->bounds.end.x-c->bounds.start.x)*x*.25f,
                c->bounds.start.y+(c->bounds.end.y-c->bounds.start.y)*y*.25f,
                c->bounds.start.z+(c->bounds.end.z-c->bounds.start.z)*z*.25f);
            sdf.config.enableConnectorPruning=false;float full=MonsterSDF_EvaluateDistance(&sdf,p);
            sdf.config.enableConnectorPruning=true;float pruned=MonsterSDF_EvaluateDistance(&sdf,p);
            if(fabsf(full)<.01f)TEST_ASSERT(fabsf(full-pruned)<1e-6f,"Poda incorrecta cerca de caja digital");
        }
    }
    TEST_ASSERT(probes>1000,"Cobertura insuficiente de falanges finas");
    MonsterSDF_Free(&sdf);Monster_Free(&m);
    printf("[PASS] test_juvenile_connector_bounds (%zu muestras superficiales)\n",probes);
}

static size_t full_domain_bounds(const void* context,AABB3D* boxes,size_t capacity) {
    if(!capacity)return 0;
    boxes[0]=MonsterSDF_GetBounds(context);return 1;
}

static void test_digit_candidate_culling(void) {
    Monster m=CreateTestLizard(false);MonsterSDF sdf=MonsterSDF_Create();
    TEST_ASSERT(MonsterSDF_Build(&sdf,&m,MonsterSDF_DefaultConfig()),"SDF juvenil inválido");
    /* Dominio pequeño y fino sobre los dedos más frágiles, tras el límite antiguo. */
    for(unsigned limb=0;limb<4;++limb) {
        SDFMesherConfig cfg=SDFMesher_DefaultConfig();cfg.useAutoBounds=false;
        cfg.bounds=AABB_Empty();cfg.voxelSize=.014f;cfg.maxResolution=160;cfg.maxCells=500000;
        for(size_t i=0;i<m.anatomyGraph.nodeCount;++i) {
            const AnatomyNode* n=&m.anatomyGraph.nodes[i];
            if(n->id>=Anatomy_MakeId(10+(limb),Limb_DigitLocalId(0,0))&&n->id<=Anatomy_MakeId(10+(limb),Limb_DigitLocalId(4,6)))
                AABB_ExpandRadius(&cfg.bounds,n->center,Vec3_Create(n->widthRadius,n->widthRadius,n->widthRadius));
        }
        AABB_Pad(&cfg.bounds,.04f);
        SDFMesher mesher=SDFMesher_Create(cfg);Mesh culled=Mesh_Create(),full=Mesh_Create();
        SDFField field=MonsterSDF_GetField(&sdf);
        TEST_ASSERT(SDFMesher_GenerateMesh(&mesher,&field,&culled),"Mallado con descarte falló");
        field.getComponentBounds=full_domain_bounds;
        TEST_ASSERT(SDFMesher_GenerateMesh(&mesher,&field,&full),"Mallado sin descarte falló");
        TEST_ASSERT(culled.vertexCount==full.vertexCount&&culled.indexCount==full.indexCount,
            "El descarte de componentes elimina superficie digital");
        for(size_t i=0;i<full.vertexCount;++i)
            TEST_ASSERT(Vec3_Distance(culled.vertices[i].position,full.vertices[i].position)<1e-6f,
                "El descarte cambió una intersección superficial");
        Mesh_Free(&culled);Mesh_Free(&full);SDFMesher_Free(&mesher);
    }
    MonsterSDF_Free(&sdf);Monster_Free(&m);
    printf("[PASS] test_digit_candidate_culling\n");
}

void run_perf_optimizations_tests(void) {
    test_connector_pruning_equivalence();
    test_juvenile_connector_bounds();
    test_digit_candidate_culling();
    test_parallel_serial_determinism();
    test_component_bounds_and_cell_skipping();
    test_monster_copy_into_reuse();
    test_visual_async_morph_settled_tiers();
}
