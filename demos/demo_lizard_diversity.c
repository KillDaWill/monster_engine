/**
 * @file demo_lizard_diversity.c
 * @brief Demo interactivo 3D que exhibe la diversidad morfológica de 10 lagartos procedimentales.
 * @author Monster Engine Team
 * @date 2026
 *
 * Muestra 10 variantes fenotípicas radicalmente distintas generadas a partir
 * de la receta ontogenética canónica mediante el sistema modular CreatureVariation.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

#include "CreatureVariation.h"
#include "LizardVariations.h"
#include "CreatureRecipes.h"
#include "Creature.h"
#include "Monster.h"
#include "MonsterVisual.h"
#include "RenderInterfaces.h"
#include "OpenGLRenderer.h"
#include "Vector.h"
#include "Color.h"

#define NUM_SPECIMENS 10

typedef struct Specimen {
    size_t index;
    const char* name;
    uint32_t seed;
    CreatureVariation variation;
    CreatureVariant variant;
    Monster monster;
    MonsterVisual visual;
    size_t totalVertices;
    size_t totalTriangles;
    double buildTimeMs;
    bool ready;
    AABB3D bounds;
} Specimen;

static void Specimen_PrintDetails(const Specimen* s) {
    if (!s || !s->ready) return;
    printf("\n------------------------------------------------------------\n");
    printf(" [%zu] %s (Semilla: %u)\n", s->index + 1, s->name, s->seed);
    printf("   Geometría : %zu vértices, %zu triángulos | Tiempo: %.2f ms\n",
           s->totalVertices, s->totalTriangles, s->buildTimeMs);
    printf("   Rasgos    : %zu configurados en variación\n", s->variation.traitCount);

    const CreaturePhenotype* p = &s->variant.phenotype;
    const char* tailNames[] = {"Tapered", "Whip", "Heavy", "Prehensile", "Finned", "Custom"};
    const char* tailStr = (p->tailCount > 0 && p->tails[0].archetype <= TAIL_ARCHETYPE_CUSTOM) ?
        tailNames[p->tails[0].archetype] : "Ninguna";
    const char* pupilNames[] = {"Redonda", "Vertical", "Horizontal", "Diamante"};
    const char* pupilStr = (p->eyes.pupilShape <= PUPIL_DIAMOND) ?
        pupilNames[p->eyes.pupilShape] : "Especial";
    const char* integumentNames[] = {"Escamas", "Placas Dérmicas", "Piel Lisa"};
    const char* integumentStr = (p->surface.integument.type <= INTEGUMENT_SMOOTH_SKIN) ?
        integumentNames[p->surface.integument.type] : "Personalizado";

    printf("   Anatomía  : Escala total: %.2f | Cuello: L=%.2f W=%.2f | Tórax W=%.2f\n",
           p->axial.totalScale, p->axial.neckLength, p->axial.neckWidth, p->axial.thoraxWidth);
    printf("   Apéndices : Cola: %s (L=%.2f) | Extremidades: %zu (grosor: %.2f)\n",
           tailStr, p->tailCount > 0 ? p->tails[0].length : 0.0f,
           p->limbCount, p->limbCount > 0 ? p->limbs[0].thickness : 0.0f);
    printf("   Cefálica  : Ojos: tamaño=%.2f pupila=%s | Ornamentos: %zu\n",
           p->eyes.size, pupilStr, p->ornamentCount);
    printf("   Superficie: %s | Capas pigmento: %zu\n",
           integumentStr, p->surface.pigment.layerCount);
    printf("------------------------------------------------------------\n");
}

static bool Specimen_Build(
    Specimen* s,
    size_t index,
    uint32_t seed,
    float voxelSize,
    const CreatureRecipe* baseRecipe,
    const CreaturePhenotype* basePheno) {
    if (!s || !baseRecipe || !basePheno) return false;

    if (s->ready) {
        MonsterVisual_Free(&s->visual);
        Monster_Free(&s->monster);
        s->ready = false;
    }

    s->index = index;
    s->name = LizardVariations_GetName(index);
    s->seed = seed;
    s->variation = LizardVariations_Get(index, seed);

    if (!CreatureVariation_Apply(baseRecipe, basePheno, &s->variation, &s->variant)) {
        fprintf(stderr, "[ERROR] Falló CreatureVariation_Apply para espécimen %zu (%s)\n",
                index, s->name);
        return false;
    }

    s->monster = Monster_Create();
    if (!Creature_BuildMonster(&s->monster, &s->variant.recipe, &s->variant.phenotype)) {
        fprintf(stderr, "[ERROR] Falló Creature_BuildMonster para espécimen %zu (%s)\n",
                index, s->name);
        return false;
    }

    Monster_SetHeadOpenFactor(&s->monster, 0.0f);

    SDFMesherConfig mesherCfg = SDFMesher_DefaultConfig();
    mesherCfg.voxelSize = voxelSize;
    mesherCfg.maxCells = 350000;
    mesherCfg.maxResolution = 256;

    s->visual = MonsterVisual_Create(mesherCfg);

    double t0 = OpenGLDemoWindow_Time();
    MonsterSDFConfig sdfCfg = MonsterSDF_DefaultConfig();
    if (!MonsterVisual_RebuildNow(&s->visual, &s->monster, sdfCfg)) {
        fprintf(stderr, "[ERROR] Falló MonsterVisual_RebuildNow para espécimen %zu (%s)\n",
                index, s->name);
        return false;
    }
    s->buildTimeMs = (OpenGLDemoWindow_Time() - t0) * 1000.0;

    /* Contabilizar geometría total generada */
    s->totalVertices = s->visual.mesh.vertexCount + s->visual.headMesh.vertexCount;
    s->totalTriangles = (s->visual.mesh.indexCount + s->visual.headMesh.indexCount) / 3;

    for (size_t m = 0; m < s->visual.mouthCount; ++m) {
        s->totalVertices += s->visual.mouths[m].jaw.vertexCount + s->visual.mouths[m].hinge.vertexCount;
        s->totalTriangles += (s->visual.mouths[m].jaw.indexCount + s->visual.mouths[m].hinge.indexCount) / 3;
    }
    for (size_t e = 0; e < s->visual.eyeCount; ++e) {
        s->totalVertices += s->visual.eyes[e].globe.vertexCount;
        s->totalTriangles += s->visual.eyes[e].globe.indexCount / 3;
    }

    printf("    partes cuerpo=%zu cabeza=%zu axial=%d celdas=%zu minimo=%.5f ajustado=%d\n",
        s->visual.mesh.vertexCount, s->visual.headMesh.vertexCount,
        s->visual.sdf.axialStationCount, s->visual.mesher.lastStats.cellCount,
        s->visual.mesher.lastStats.minimumVoxelSize, s->visual.mesher.lastStats.cellBudgetAdjusted);
    s->bounds = AABB_Empty();
    for(size_t v=0;v<s->visual.mesh.vertexCount;++v)AABB_ExpandPoint(&s->bounds,s->visual.mesh.vertices[v].position);
    for(size_t e=0;e<s->visual.eyeCount;++e)
        for(size_t v=0;v<s->visual.eyes[e].globe.vertexCount;++v)AABB_ExpandPoint(&s->bounds,s->visual.eyes[e].globe.vertices[v].position);
    for(size_t j=0;j<s->visual.mouthCount;++j)
        for(size_t v=0;v<s->visual.mouths[j].jaw.vertexCount;++v)AABB_ExpandPoint(&s->bounds,s->visual.mouths[j].jaw.vertices[v].position);
    MorphologicalSignature signature=Creature_MorphologicalSignature(&s->monster);
    size_t components=0,largest=0,second=0;
    if(!Mesh_ComponentStatistics(&s->visual.mesh,&components,&largest,&second))return false;
    printf("    firma L/W=%.3f H/W=%.3f cabeza=%.3f hocico=%.3f fore=%.3f hind=%.3f H/F=%.3f pie=%.3f colaL=%.3f colaW=%.3f ojo=%.3f orn=%.3f curva=%.3f grosorFore=%.3f\n",
        signature.bodyLengthWidthRatio,signature.bodyHeightWidthRatio,signature.headBodyScale,signature.headLengthWidthRatio,
        signature.forelimbBodyRatio,signature.hindlimbBodyRatio,signature.hindForeRatio,signature.footBodyRatio,
        signature.tailBodyLengthRatio,signature.tailBaseBodyRatio,signature.eyeHeadRatio,signature.maxOrnamentHeightBodyRatio,
        signature.distalTailCurvature,signature.forelimbThicknessBodyRatio);
    printf("    componentes=%zu principal=%zu segundo=%zu geometria=%u bounds=(%.2f %.2f %.2f)-(%.2f %.2f %.2f)\n",
        components,largest,second,Creature_ValidateGeometry(&s->monster),s->bounds.start.x,s->bounds.start.y,s->bounds.start.z,
        s->bounds.end.x,s->bounds.end.y,s->bounds.end.z);
    s->ready = true;
    return true;
}

static void Specimen_Free(Specimen* s) {
    if (s && s->ready) {
        MonsterVisual_Free(&s->visual);
        Monster_Free(&s->monster);
        s->ready = false;
    }
}

static void Specimen_Debug(const Specimen* s) {
    const AnatomyGraph* g = &s->monster.anatomyGraph;
    for (size_t i=0; i<g->connectionCount; ++i) {
        const AnatomyNode* a=AnatomyGraph_FindNode(g,g->connections[i].fromId);
        const AnatomyNode* b=AnatomyGraph_FindNode(g,g->connections[i].toId);
        if(a && b) OpenGLRenderer_DebugLine(a->center,b->center,Color_FromRGB(250,180,60));
    }
    for(size_t i=0;i<g->nodeCount;++i) {
        Vector3 p=g->nodes[i].center;
        OpenGLRenderer_DebugLine(Vec3_Sub(p,Vec3_Create(.05f,0,0)),Vec3_Add(p,Vec3_Create(.05f,0,0)),COLOR_WHITE);
        OpenGLRenderer_DebugLine(Vec3_Sub(p,Vec3_Create(0,.05f,0)),Vec3_Add(p,Vec3_Create(0,.05f,0)),COLOR_WHITE);
    }
    const HeadLandmarks* h=&s->monster.head.anatomy.landmarks;
    Vector3 origin=s->monster.bodyParts[0].positionRender;
    OpenGLRenderer_DebugLine(Vec3_Add(origin,h->leftOrbit),Vec3_Add(origin,h->rightOrbit),Color_FromRGB(80,200,255));
    OpenGLRenderer_DebugLine(Vec3_Add(origin,h->leftJawHinge),Vec3_Add(origin,h->rightJawHinge),Color_FromRGB(255,60,100));
}
static bool headCloseup = false;
static float inspectionZoom = 1.0f;

static void Specimen_Draw(const Specimen* s,Renderer3D* renderer,Vector3 placement,bool grid,bool debug) {
    Vector3 size=AABB_Size(s->bounds),center=Vec3_Scale(Vec3_Add(s->bounds.start,s->bounds.end),.5f);
    float longest=fmaxf(size.x,fmaxf(size.y,size.z));
    float scale=fminf(1.0f,(grid?6.3f:8.0f)/longest);
    if (headCloseup && !grid) {
        const HeadAnatomy* head = &s->monster.head.anatomy;
        center = Vec3_Add(s->monster.bodyParts[head->attachmentBodyPartIndex].positionRender,
                         Vec3_Lerp(head->surface.craniumCenter, head->surface.faceTip, .25f));
        scale = 2.5f;
    }
    if (!grid) scale *= inspectionZoom;
    OpenGLRenderer_ModelTransform(placement,55.0f,scale,center);
    MonsterVisual_Render(&s->visual,renderer);
    if(debug) Specimen_Debug(s);
    OpenGLRenderer_PopModelMatrix();
}

static void Specimen_DumpAnatomy(const Specimen* s) {
    if (!s || !s->ready) return;
    const AnatomyGraph* g = &s->monster.anatomyGraph;

    printf("\n======================================================================\n");
    printf(" ANATOMICAL DIAGNOSTIC DUMP: [%zu] %s (Seed: %u)\n", s->index + 1, s->name, s->seed);
    printf("======================================================================\n");

    const HeadSurfaceRecipe* hr = &s->monster.head.anatomy.surface;
    const HeadLandmarks* hl = &s->monster.head.anatomy.landmarks;
    printf("\nHEAD: envelope W/H = %.4f / %.4f; cranium radii W/H/L = %.4f / %.4f / %.4f; H/W = %.4f\n",
        s->variant.phenotype.headEnvelope.widthScale, s->variant.phenotype.headEnvelope.heightScale,
        hr->craniumRadii.x, hr->craniumRadii.y, hr->craniumRadii.z, hr->craniumRadii.y/hr->craniumRadii.x);
    printf("HEAD: face root/mid/tip widths = %.4f / %.4f / %.4f; orbit X = %.4f (%.4f skull); cheek/jaw = %.4f / %.4f\n",
        hr->faceRootRadii.x, hr->faceMidRadii.x, hr->faceTipRadii.x,
        hl->leftOrbit.x, hl->leftOrbit.x/hr->craniumRadii.x,
        s->variant.phenotype.head.cheekMass, s->variant.phenotype.head.jawStrength);

    /* 1. Estaciones axiales del cuerpo (Módulo 1) */
    printf("\n--- 1. AXIAL BODY STATIONS (Module 1) ---\n");
    printf(" %-4s | %-18s | %-24s | %-8s | %-8s | %-8s\n",
           "ID", "Region/Role", "Center (X, Y, Z)", "Width", "Height", "Aspect H/W");
    printf("------+--------------------+--------------------------+----------+----------+----------\n");
    const char* axialNames[] = {"?", "Neck", "Pectoral", "Thorax Ant", "Thorax Post", "Abdomen", "Pelvis"};
    const AnatomyNode* pelvisNode = NULL;
    for (uint16_t local = 1; local <= 6; ++local) {
        const AnatomyNode* n = AnatomyGraph_FindModuleNode(g, 1, local);
        if (n) {
            float aspect = (n->widthRadius > 1e-5f) ? (n->heightRadius / n->widthRadius) : 0.0f;
            printf(" %-4u | %-18s | (%6.3f, %6.3f, %6.3f) | %8.4f | %8.4f | %8.4f\n",
                   local, (local <= 6) ? axialNames[local] : "Unknown",
                   n->center.x, n->center.y, n->center.z,
                   n->widthRadius, n->heightRadius, aspect);
            if (local == 6) pelvisNode = n;
        }
    }

    /* 2. Estaciones caudales (Módulo 20) y continuidad con pelvis */
    printf("\n--- 2. CAUDAL STATIONS (Module 20) ---\n");
    if (pelvisNode) {
        printf(" Pelvis Ref: W=%.4f, H=%.4f (Aspect H/W=%.4f)\n",
               pelvisNode->widthRadius, pelvisNode->heightRadius,
               pelvisNode->heightRadius / pelvisNode->widthRadius);
    }
    printf(" %-4s | %-24s | %-8s | %-8s | %-12s\n",
           "Node", "Center (X, Y, Z)", "Width", "Height", "Match/Taper");
    printf("------+--------------------------+----------+----------+-------------\n");
    for (uint16_t local = 1; local <= 16; ++local) {
        const AnatomyNode* n = AnatomyGraph_FindModuleNode(g, 20, local);
        if (n) {
            float matchW = pelvisNode ? (n->widthRadius / pelvisNode->widthRadius * 100.0f) : 0.0f;
            printf(" %-4u | (%6.3f, %6.3f, %6.3f) | %8.4f | %8.4f | %6.1f%% pelvis\n",
                   local, n->center.x, n->center.y, n->center.z,
                   n->widthRadius, n->heightRadius, matchW);
        }
    }

    /* 3. Extremidades anteriores (Módulo 10) y posteriores (Módulo 12) */
    printf("\n--- 3. LIMB STATIONS (Fore: Mod 10, Hind: Mod 12) ---\n");
    const char* limbNodeNames[] = {"?", "Root", "Middle", "Distal", "Autopod"};
    printf(" %-4s | %-8s | %-16s | %-8s | %-8s | %-24s\n",
           "Mod", "Local", "Segment", "Width", "Height", "Center");
    printf("------+----------+------------------+----------+----------+--------------------------\n");
    uint32_t limbMods[2] = {10, 12};
    for (int m = 0; m < 2; ++m) {
        uint32_t mod = limbMods[m];
        for (uint16_t local = 1; local <= 4; ++local) {
            const AnatomyNode* n = AnatomyGraph_FindModuleNode(g, mod, local);
            if (n) {
                printf(" %-4u | %-8u | %-16s | %8.4f | %8.4f | (%6.3f, %6.3f, %6.3f)\n",
                       mod, local, (local <= 4) ? limbNodeNames[local] : "Digit",
                       n->widthRadius, n->heightRadius,
                       n->center.x, n->center.y, n->center.z);
            }
        }
    }

    /* 4. Conexiones anatómicas y solapamiento de soporte */
    printf("\n--- 4. LIMB ATTACHMENT CONNECTIONS ---\n");
    for (size_t c = 0; c < g->connectionCount; ++c) {
        const BodyConnection* conn = &g->connections[c];
        const AnatomyNode* from = AnatomyGraph_FindNode(g, conn->fromId);
        const AnatomyNode* to = AnatomyGraph_FindNode(g, conn->toId);
        if (from && to && from->role == ANATOMY_ROLE_AXIAL && to->role == ANATOMY_ROLE_JOINT && to->localNodeId == 1) {
            const char* kindStr = (conn->kind == BODY_CONNECTION_SUPPORT) ? "SUPPORT (No SDF volume)" :
                                  (conn->kind == BODY_CONNECTION_LIMB_SEGMENT) ? "LIMB_SEGMENT" : "OTHER";
            float lateralDist = fabsf(to->center.x - from->center.x);
            float overlap = (from->widthRadius + to->widthRadius) - lateralDist;
            printf(" Mod %u: Axial Node %u -> Limb Node %u | Kind: %s | LatDist: %.4f | HostRadius: %.4f | LimbRootR: %.4f | SDF Overlap: %.4f\n",
                   conn->moduleInstanceId, from->localNodeId, to->localNodeId, kindStr,
                   lateralDist, from->widthRadius, to->widthRadius, overlap);
        }
    }
    printf("======================================================================\n\n");
}

int main(int argc, char* argv[]) {
    bool headless = false, monochrome = false, anatomyDebug = false;
    bool sideView = false;
    bool topView = false;
    bool dumpAnatomy = false;
    int maxFrames = 0;
    const char* savePpmPath = NULL;
    const char* captureDirectory = NULL;
    char capturePath[1024];
    int initialSpecimen = -1; /* -1 = modo cuadrícula */
    uint32_t baseSeed = 54321u;
    float voxelSize = 0.11f;
    float orbitAngle = 0.45f;

    for (int i = 1; i < argc; ++i) {
        if (strncmp(argv[i], "--zoom=", 7) == 0) { inspectionZoom=fmaxf(.25f,fminf(3.0f,(float)atof(argv[i]+7)));
        } else if (strcmp(argv[i], "--head-closeup") == 0) { headCloseup=true;
        } else if (strncmp(argv[i], "--capture-dir=", 14) == 0) { captureDirectory=argv[i]+14;
        } else if (strcmp(argv[i], "--monochrome") == 0) { monochrome=true;
        } else if (strcmp(argv[i], "--anatomy") == 0) { anatomyDebug=true;
        } else if (strcmp(argv[i], "--dump-anatomy") == 0) { dumpAnatomy=true;
        } else if (strcmp(argv[i], "--side") == 0) { sideView=true;
        } else if (strcmp(argv[i], "--top") == 0) { topView=true;
        } else if (strcmp(argv[i], "--headless") == 0) {
            headless = true;
        } else if (strncmp(argv[i], "--frames=", 9) == 0) {
            maxFrames = atoi(argv[i] + 9);
        } else if (strncmp(argv[i], "--save-ppm=", 11) == 0) {
            savePpmPath = argv[i] + 11;
        } else if (strncmp(argv[i], "--specimen=", 11) == 0) {
            initialSpecimen = atoi(argv[i] + 11) - 1;
            if (initialSpecimen < -1) initialSpecimen = -1;
            if (initialSpecimen >= NUM_SPECIMENS) initialSpecimen = NUM_SPECIMENS - 1;
        } else if (strncmp(argv[i], "--orbit=", 8) == 0) {
            orbitAngle = (float)atof(argv[i] + 8);
        } else if (strncmp(argv[i], "--seed=", 7) == 0) {
            baseSeed = (uint32_t)strtoul(argv[i] + 7, NULL, 10);
        } else if (strncmp(argv[i], "--voxel=", 8) == 0) {
            voxelSize = (float)atof(argv[i] + 8);
            if (voxelSize < 0.05f) voxelSize = 0.05f;
            if (voxelSize > 0.25f) voxelSize = 0.25f;
        }
    }

    if(captureDirectory) maxFrames=8;
    if (headless && maxFrames <= 0) {
        maxFrames = 30;
    }

    printf("======================================================================\n");
    printf("   MONSTER ENGINE: Demo de Diversidad Morfológica (10 Lagartos)       \n");
    printf("======================================================================\n");
    printf(" Controles interactivos:\n");
    printf("  [1..9, 0] : Modo inspección del espécimen 1 a 10 (cámara en órbita)\n");
    printf("  [G]       : Modo cuadrícula general (muestra los 10 especímenes)\n");
    printf("  [M] Gris | [A] Anatomía\n");
    printf("  [R]       : Re-tirar semilla estocástica y regenerar el espécimen activo\n");
    printf("  [ESPACIO] : Pausar / reanudar rotación de la cámara\n");
    printf("  [D]       : Alternar cuadrícula de referencia en el suelo\n");
    printf("  [ESC]     : Salir\n");
    printf("======================================================================\n");

    const CreatureRecipe* baseRecipe = CreatureRecipes_Lizard();
    const CreaturePhenotype* basePheno = CreatureRecipe_GetStage(baseRecipe, CREATURE_STAGE_ADULT);
    if (!baseRecipe || !basePheno) {
        fprintf(stderr, "[ERROR FATAL] Receta canónica de lagarto no disponible.\n");
        return 1;
    }

    Specimen specimens[NUM_SPECIMENS];
    memset(specimens, 0, sizeof(specimens));

    printf("\n[*] Construyendo los 10 especímenes morfológicos...\n");
    double tTotalStart = OpenGLDemoWindow_Time();
    size_t totalWorldVertices = 0;
    size_t totalWorldTriangles = 0;

    for (size_t i = 0; i < NUM_SPECIMENS; ++i) {
        if (savePpmPath && initialSpecimen >= 0 && (int)i != initialSpecimen) continue;
        uint32_t seed = (initialSpecimen >= 0 && (int)i == initialSpecimen && baseSeed != 54321u) ?
                        baseSeed : (baseSeed + (uint32_t)(i * 1337u));
        if (!Specimen_Build(&specimens[i], i, seed, voxelSize, baseRecipe, basePheno)) {
            fprintf(stderr, "[ERROR FATAL] Falló la construcción del espécimen %zu.\n", i + 1);
            for (size_t k = 0; k <= i; ++k) Specimen_Free(&specimens[k]);
            return 1;
        }
        totalWorldVertices += specimens[i].totalVertices;
        totalWorldTriangles += specimens[i].totalTriangles;
        printf("  [%2zu/10] %-22s : %6zu vértices, %6zu tris en %6.2f ms\n",
               i + 1, specimens[i].name, specimens[i].totalVertices,
               specimens[i].totalTriangles, specimens[i].buildTimeMs);
    }
    double tTotalMs = (OpenGLDemoWindow_Time() - tTotalStart) * 1000.0;
    printf("\n[*] 10 Especímenes listos: %zu vértices totales generados en %.2f ms (media %.2f ms/criatura)\n",
           totalWorldVertices, tTotalMs, tTotalMs / (double)NUM_SPECIMENS);

    if (dumpAnatomy) {
        int target = (initialSpecimen >= 0) ? initialSpecimen : 1;
        Specimen_DumpAnatomy(&specimens[target]);
    }

    /* En modo puramente headless sin ventana OpenGL */
    if (headless && !savePpmPath && !captureDirectory) {
        printf("\n[INFO] Ejecución en modo headless finalizada con éxito (%d frames simulados).\n", maxFrames);
        for (size_t i = 0; i < NUM_SPECIMENS; ++i) Specimen_Free(&specimens[i]);
        return 0;
    }

    /* Crear ventana OpenGL para visualización o captura */
    int windowWidth = 1280;
    int windowHeight = 800;
    OpenGLDemoWindow* window = OpenGLDemoWindow_Create(
        "Monster Engine | Diversidad de Lagartos (10 Variantes)",
        windowWidth, windowHeight
    );

    if (!window) {
        fprintf(stderr, "[WARN] No se pudo abrir ventana SDL2/OpenGL (¿entorno sin display?).\n");
        if (headless) {
            printf("[INFO] Modo headless completado sin renderizado visual.\n");
            for (size_t i = 0; i < NUM_SPECIMENS; ++i) Specimen_Free(&specimens[i]);
            return 0;
        }
        for (size_t i = 0; i < NUM_SPECIMENS; ++i) Specimen_Free(&specimens[i]);
        return 1;
    }

    ICamera camera;
    camera.fov = 45.0f;
    camera.nearPlane = 0.2f;
    camera.farPlane = 120.0f;
    camera.up = Vec3_Create(0.0f, 1.0f, 0.0f);

    Renderer3D renderer = OpenGLRenderer_Create(&camera);

    int selectedSpecimen = initialSpecimen;
    bool paused = false;
    bool showGrid = true;
    int frameCount = 0;
    double lastTime = OpenGLDemoWindow_Time();

    if (selectedSpecimen >= 0) {
        Specimen_PrintDetails(&specimens[selectedSpecimen]);
    } else {
        printf("\n[MODO] Vista Cuadrícula activa (5 columnas x 2 filas).\n");
    }

    while (!maxFrames || frameCount < maxFrames) {
        double now = OpenGLDemoWindow_Time();
        float dt = (float)(now - lastTime);
        lastTime = now;
        if (dt > 0.1f) dt = 0.1f;

        OpenGLDemoInput input = OpenGLDemoWindow_Poll(window);
        if (input.quit) break;
        if(input.keyM) monochrome=!monochrome;
        if(input.keyA) anatomyDebug=!anatomyDebug;
        if (input.togglePause) paused = !paused;
        if (input.toggleDebug) showGrid = !showGrid;

        /* Tecla 'G': regresar a cuadrícula */
        if (input.keyG) {
            selectedSpecimen = -1;
            printf("\n[MODO] Regresando a Vista Cuadrícula general.\n");
        }

        /* Teclas numéricas: 1..9 (índices 0..8) y 0 (índice 9) */
        if (input.digitKey >= 0) {
            int targetIdx = input.digitKey;
            if (targetIdx >= 0 && targetIdx < NUM_SPECIMENS) {
                selectedSpecimen = targetIdx;
                Specimen_PrintDetails(&specimens[selectedSpecimen]);
            }
        }

        /* Tecla 'R': re-tirar semilla */
        if (input.keyR) {
            if (selectedSpecimen >= 0) {
                Specimen* s = &specimens[selectedSpecimen];
                uint32_t newSeed = s->seed * 1664525u + 1013904223u;
                printf("\n[*] Re-generando %s con nueva semilla: %u...\n", s->name, newSeed);
                Specimen_Build(s, s->index, newSeed, voxelSize, baseRecipe, basePheno);
                Specimen_PrintDetails(s);
            } else {
                printf("\n[*] Re-generando los 10 especímenes con nuevas semillas estocásticas...\n");
                for (size_t i = 0; i < NUM_SPECIMENS; ++i) {
                    uint32_t newSeed = specimens[i].seed * 1664525u + 1013904223u;
                    Specimen_Build(&specimens[i], i, newSeed, voxelSize, baseRecipe, basePheno);
                }
                printf("[OK] Cuadrícula actualizada.\n");
            }
        }

        if (!paused) {
            orbitAngle += dt * 0.40f;
            if (orbitAngle > 6.2831853f) orbitAngle -= 6.2831853f;
        }

        if(captureDirectory) {
            const int specimensToCapture[]={-1,-1,-1,3,5,6,7,8};
            const char* names[]={"grid-color","grid-gray","grid-side","gecko","burrower","armored","sailback","chameleon"};
            selectedSpecimen=specimensToCapture[frameCount];
            monochrome=frameCount==1;sideView=frameCount==2;
            orbitAngle=.70f;
            snprintf(capturePath,sizeof(capturePath),"%s/%s.ppm",captureDirectory,names[frameCount]);
            savePpmPath=capturePath;
        }
        /* Configurar cámara según modo */
        if (selectedSpecimen < 0) {
            /* Modo Cuadrícula: Vista cenital inclinada amplia */
            camera.position = Vec3_Create(0.0f, sideView ? 7.0f : 12.0f, 29.5f);
            camera.target = Vec3_Create(0.0f, 0.0f, 0.0f);
            camera.up = Vec3_Create(0.0f, 1.0f, 0.0f);
        } else {
            /* Modo Inspección: Cámara orbital suave o cenital alrededor del espécimen */
            if (topView) {
                camera.position = Vec3_Create(0.001f, 13.0f, 0.001f);
                camera.target = Vec3_Create(0.0f, 0.0f, 0.0f);
                camera.up = Vec3_Create(0.0f, 0.0f, -1.0f);
            } else {
                float dist = 12.0f;
                camera.position = Vec3_Create(
                    dist * sinf(orbitAngle),
                    sideView ? 1.0f : 4.0f,
                    dist * cosf(orbitAngle)
                );
                camera.target = Vec3_Create(0.0f, 0.4f, 0.0f);
                camera.up = Vec3_Create(0.0f, 1.0f, 0.0f);
            }
        }

        /* Comenzar renderizado */
        OpenGLRenderer_SetSurfaceDebug(&renderer,monochrome ? 10 : 0);
        renderer.beginFrame(&renderer);
        OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);

        /* Dibujar cuadrícula de suelo */
        if (showGrid) {
            Color gridCol = Color_FromRGB(60, 68, 75);
            int extent = (selectedSpecimen < 0) ? 22 : 10;
            for (int line = -extent; line <= extent; line += 2) {
                OpenGLRenderer_DebugLine(
                    Vec3_Create((float)line, -0.05f, (float)-extent),
                    Vec3_Create((float)line, -0.05f, (float)extent),
                    gridCol
                );
                OpenGLRenderer_DebugLine(
                    Vec3_Create((float)-extent, -0.05f, (float)line),
                    Vec3_Create((float)extent, -0.05f, (float)line),
                    gridCol
                );
            }
        }

        if (selectedSpecimen < 0) {
            /* Renderizar los 10 especímenes en disposición 5 columnas x 2 filas */
            for (size_t i = 0; i < NUM_SPECIMENS; ++i) {
                int col = (int)(i % 5);
                int row = (int)(i / 5);
                float tx = (col - 2.0f) * 7.0f;
                float tz = (row == 0) ? -5.5f : 5.5f;

                Specimen_Draw(&specimens[i], &renderer, Vec3_Create(tx,0,tz), true, anatomyDebug);
            }
        } else {
            /* Renderizar únicamente el espécimen seleccionado centrado en el origen */
            Specimen_Draw(&specimens[selectedSpecimen], &renderer, Vec3_Zero(), false, anatomyDebug);
        }

        renderer.endFrame(&renderer);
        OpenGLRenderer_Finish();

        /* Guardar captura si fue solicitada */
        if (savePpmPath && (captureDirectory || frameCount == maxFrames - 1 || maxFrames == 0)) {
            if (OpenGLRenderer_SavePPM(savePpmPath, windowWidth, windowHeight)) {
                printf("[CAPTURA] Guardada imagen PPM en: %s\n", savePpmPath);
            } else {
                fprintf(stderr, "[ERROR] Falló el guardado de captura PPM en: %s\n", savePpmPath);
            }
            if (maxFrames == 0) break;
        }

        OpenGLDemoWindow_Swap(window);
        frameCount++;
    }

    printf("\n[*] Liberando recursos y cerrando visor de diversidad...\n");
    for (size_t i = 0; i < NUM_SPECIMENS; ++i) {
        Specimen_Free(&specimens[i]);
    }
    OpenGLRenderer_Destroy(&renderer);
    OpenGLDemoWindow_Free(window);

    printf("[OK] Demo de Diversidad finalizado con éxito.\n");
    return 0;
}
