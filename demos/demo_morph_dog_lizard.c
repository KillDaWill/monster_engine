/**
 * @file demo_morph_dog_lizard.c
 * @brief Demo 3D interactiva para probar el MonsterAger al extremo mediante metamorfosis
 *        continua entre dos especies con planes corporales y coberturas distintas:
 *        Perro digitígrado con pelaje (Canis) -> Lagarto con escamas (Lacerta).
 * @author Monster Engine Team
 * @date 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <time.h>
#include <SDL2/SDL.h>
#include <GL/gl.h>

#include "Creature.h"
#include "CreatureMorph.h"
#include "CreatureRecipes.h"
#include "Limb.h"
#include "Monster.h"
#include "MonsterAger.h"
#include "MonsterVisualAsync.h"
#include "RenderInterfaces.h"
#include "OpenGLRenderer.h"
#include "Vector.h"
#include "MathUtils.h"

#ifndef MONSTER_BUILD_REVISION
#define MONSTER_BUILD_REVISION "unknown"
#endif

static void WriteJSONText(FILE* file,const char* value) {
    fputc('"',file);
    for(const unsigned char* p=(const unsigned char*)(value?value:"");*p;++p) {
        if(*p=='"'||*p=='\\')fputc('\\',file);
        if(*p<32)fprintf(file,"\\u%04x",*p);else fputc(*p,file);
    }
    fputc('"',file);
}

static void DrawAnatomyOverlay(const AnatomyGraph* graph) {
    if (!graph) return;
    for (size_t i = 0; i < graph->connectionCount; ++i) {
        const AnatomyNode* a = AnatomyGraph_FindNode(graph, graph->connections[i].fromId);
        const AnatomyNode* b = AnatomyGraph_FindNode(graph, graph->connections[i].toId);
        if (a && b) {
            Color linkColor = Color_FromRGB(255, 120, 20);
            if (graph->connections[i].kind == BODY_CONNECTION_LIMB_SEGMENT) {
                linkColor = Color_FromRGB(50, 220, 255);
            } else if (graph->connections[i].kind == BODY_CONNECTION_DIGIT_SEGMENT) {
                linkColor = Color_FromRGB(255, 230, 40);
            }
            OpenGLRenderer_DebugLine(a->center, b->center, linkColor);
        }
    }
}

static void DrawAABBWireframe(AABB3D box, Color color) {
    Vector3 c[8] = {
        Vec3_Create(box.start.x, box.start.y, box.start.z),
        Vec3_Create(box.end.x,   box.start.y, box.start.z),
        Vec3_Create(box.end.x,   box.end.y,   box.start.z),
        Vec3_Create(box.start.x, box.end.y,   box.start.z),
        Vec3_Create(box.start.x, box.start.y, box.end.z),
        Vec3_Create(box.end.x,   box.start.y, box.end.z),
        Vec3_Create(box.end.x,   box.end.y,   box.end.z),
        Vec3_Create(box.start.x, box.end.y,   box.end.z)
    };
    OpenGLRenderer_DebugLine(c[0], c[1], color);
    OpenGLRenderer_DebugLine(c[1], c[2], color);
    OpenGLRenderer_DebugLine(c[2], c[3], color);
    OpenGLRenderer_DebugLine(c[3], c[0], color);

    OpenGLRenderer_DebugLine(c[4], c[5], color);
    OpenGLRenderer_DebugLine(c[5], c[6], color);
    OpenGLRenderer_DebugLine(c[6], c[7], color);
    OpenGLRenderer_DebugLine(c[7], c[4], color);

    OpenGLRenderer_DebugLine(c[0], c[4], color);
    OpenGLRenderer_DebugLine(c[1], c[5], color);
    OpenGLRenderer_DebugLine(c[2], c[6], color);
    OpenGLRenderer_DebugLine(c[3], c[7], color);
}

static void DrawChunkOverlay(const MorphChunkManager* chunkMgr) {
    if (!chunkMgr) return;
    Color surfaceColor = Color_FromRGB(0, 220, 255);
    Color dirtyColor = Color_FromRGB(255, 60, 60);
    for (size_t i = 0; i < chunkMgr->chunkCount; ++i) {
        const MorphChunk* chunk = &chunkMgr->chunks[i];
        if (chunk->hasSurface || chunk->isDirty) {
            Color c = chunk->isDirty ? dirtyColor : surfaceColor;
            DrawAABBWireframe(chunk->bounds, c);
        }
    }
}

static void DrawFallbackOverlay(AABB3D bounds) {
    Color fallbackColor = Color_FromRGB(255, 0, 255);
    DrawAABBWireframe(bounds, fallbackColor);
}

static float SampleMeshResidual(const Mesh* mesh,const MonsterSDF* sdf) {
    if(!mesh||!sdf||!mesh->vertexCount)return NAN;
    float maximum=0.0f;
    size_t stride=mesh->vertexCount/512+1;
    for(size_t v=0;v<mesh->vertexCount;v+=stride)
        maximum=fmaxf(maximum,fabsf(MonsterSDF_EvaluateDistance(sdf,
            mesh->vertices[v].position)));
    return maximum;
}

/* La cámara y las regiones se guardan con la captura; no se usan máscaras
 * rectangulares fijadas para una especie o una resolución de pantalla. */
static AABB3D FeatureWorldBox(const MonsterSDFMouth* m,Vector3 center,Vector3 radii) {
    AABB3D result=AABB_Empty();
    for(unsigned i=0;i<8;++i) {
        Vector3 p=Vec3_Add(center,Vec3_Create(i&1?radii.x:-radii.x,i&2?radii.y:-radii.y,i&4?radii.z:-radii.z));
        Vector3 q=Vec3_Add(m->center,Vec3_Add(Vec3_Scale(m->inverseRotation.row0,p.x),
            Vec3_Add(Vec3_Scale(m->inverseRotation.row1,p.y),Vec3_Scale(m->inverseRotation.row2,p.z))));
        AABB_ExpandPoint(&result,q);
    }
    return result;
}
static void WriteFeature(FILE* out,const char* name,AABB3D box,bool* first) {
    if(!*first)fputc(',',out);
    *first=false;
    fprintf(out,"{\"name\":\"%s\",\"min\":[%.9g,%.9g,%.9g],\"max\":[%.9g,%.9g,%.9g]}",name,
        box.start.x,box.start.y,box.start.z,box.end.x,box.end.y,box.end.z);
}
static bool SaveCaptureMetadata(const char* prefix,const Monster* monster,const MonsterSDF* sdf,
    const ICamera* camera,const char* mode,float voxel,float age,const float* history,size_t historyCount,int width,int height) {
    char path[1024];snprintf(path,sizeof(path),"%s.scene.json",prefix);FILE* out=fopen(path,"w");if(!out)return false;
    fprintf(out,"{\"mode\":\"%s\",\"voxel\":%.9g,\"age\":%.9g,\"width\":%d,\"height\":%d,\"history\":[",mode,voxel,age,width,height);
    for(size_t i=0;i<historyCount;++i)fprintf(out,"%s%.9g",i?",":"",history[i]);
    fprintf(out,"],\"camera\":{\"position\":[%.9g,%.9g,%.9g],\"target\":[%.9g,%.9g,%.9g],\"up\":[%.9g,%.9g,%.9g],\"fov\":%.9g},\"features\":[",
        camera->position.x,camera->position.y,camera->position.z,camera->target.x,camera->target.y,camera->target.z,
        camera->up.x,camera->up.y,camera->up.z,camera->fov);
    bool first=true;
    for(size_t i=0;i<sdf->mouthCount;++i) {
        const MonsterSDFMouth* m=&sdf->mouths[i];WriteFeature(out,"head",m->headBounds,&first);
        if(m->hasEars) {
            WriteFeature(out,"left-ear",FeatureWorldBox(m,m->leftEarCenterLocal,m->earRadii),&first);
            WriteFeature(out,"right-ear",FeatureWorldBox(m,m->rightEarCenterLocal,m->earRadii),&first);
        }
    }
    for(size_t i=0;i<monster->anatomyGraph.nodeCount;++i) {
        const AnatomyNode* node=&monster->anatomyGraph.nodes[i];
        if(node->region==ANATOMY_REGION_TAIL||node->region==ANATOMY_REGION_DIGIT||
           node->region==ANATOMY_REGION_FORELIMB||node->region==ANATOMY_REGION_HINDLIMB) {
            char name[80];snprintf(name,sizeof(name),"%s-%u-%u",node->region==ANATOMY_REGION_TAIL?"tail":node->region==ANATOMY_REGION_DIGIT?"digit":"limb-junction",node->moduleInstanceId,node->localNodeId);
            AABB3D box=AABB_Empty();AABB_ExpandRadius(&box,node->center,node->envelopeRadii);AABB_Pad(&box,voxel);
            WriteFeature(out,name,box,&first);
        }
    }
    for(size_t i=0;i<sdf->connectorCount;++i) {
        const MonsterSDFConnector* c=&sdf->connectors[i];
        if(c->kind==BODY_CONNECTION_DIGIT_SEGMENT) {
            char name[80];snprintf(name,sizeof(name),"digit-segment-%u-%u",c->moduleInstanceId,(unsigned)(c->toId&65535u));
            WriteFeature(out,name,c->bounds,&first);
        }
    }
    for(size_t i=0;i<monster->anatomyGraph.nodeCount;++i) {
        const AnatomyNode* node=&monster->anatomyGraph.nodes[i];
        if(node->localNodeId!=LIMB_NODE_AUTOPOD||(node->region!=ANATOMY_REGION_FORELIMB&&node->region!=ANATOMY_REGION_HINDLIMB&&node->region!=ANATOMY_REGION_LIMB))continue;
        AABB3D foot=AABB_Empty();bool found=false;
        for(size_t j=0;j<sdf->connectorCount;++j) {
            const MonsterSDFConnector* c=&sdf->connectors[j];
            if(c->moduleInstanceId==node->moduleInstanceId&&(c->kind==BODY_CONNECTION_DIGIT_SEGMENT||(c->toId&65535u)==LIMB_NODE_AUTOPOD)) {
                AABB_ExpandPoint(&foot,c->bounds.start);AABB_ExpandPoint(&foot,c->bounds.end);found=true;
            }
        }
        if(found) {char name[80];snprintf(name,sizeof(name),"foot-%u",node->moduleInstanceId);WriteFeature(out,name,foot,&first);}
    }
    fputs("]}\n",out);return fclose(out)==0;
}

int main(int argc, char* argv[]) {
    int exitCode=0;
    bool cachedNormals=false,gpuSurface=false,gpuSparse=false,validateCompute=false,captureNormals=false,captureFlat=false,captureAlbedo=false,captureSemantic=false,strictReference=false,noMSAA=false;
    float fixedTimestep=0,sequence[512];size_t sequenceCount=0;
    float referenceMaxStep=0;unsigned referenceSteps=512;
    float gridVoxel=.0175f;unsigned gridRefinement=2,brickCells=16;
    const char* surfaceMode="sdf-raymarch";const char* gpuSignature=NULL;const char* gpuTracePath=NULL;
    float fixedAge=-1; const char* stressMotion=NULL; const char* capturePrefix=NULL; const char* cyclePrefix=NULL; int limitFrames=0, selectedView=3;
    bool legacyLag=false,noFur=false,exactSdf=true,localPatches=false;
    bool showChunkWireframe=false, showFallbackRegion=false,ownershipDebug=false,handoffDebug=false;
    unsigned cliWorkerDelay=0; float cliChunkSize=0.45f;
    for(int i=1;i<argc;++i) {
        if(strncmp(argv[i],"--age=",6)==0)fixedAge=Math_Clamp01(strtof(argv[i]+6,NULL));
        else if(strncmp(argv[i],"--capture-prefix=",17)==0)capturePrefix=argv[i]+17;
        else if(strcmp(argv[i],"--no-msaa")==0)noMSAA=true;
        else if(strncmp(argv[i],"--frames=",9)==0)limitFrames=atoi(argv[i]+9);
        else if(strncmp(argv[i],"--validate-cycle=",17)==0)cyclePrefix=argv[i]+17;
        else if(strncmp(argv[i],"--stress-motion=",16)==0)stressMotion=argv[i]+16;
        else if(strncmp(argv[i],"--view=",7)==0)selectedView=atoi(argv[i]+7);
        else if(strncmp(argv[i],"--worker-delay=",15)==0)cliWorkerDelay=(unsigned)atoi(argv[i]+15);
        else if(strncmp(argv[i],"--chunk-size=",13)==0)cliChunkSize=strtof(argv[i]+13,NULL);
        else if(strcmp(argv[i],"--chunk-wireframe")==0)showChunkWireframe=true;
        else if(strcmp(argv[i],"--fallback-region")==0)showFallbackRegion=true;
        else if(strcmp(argv[i],"--ownership-debug")==0)ownershipDebug=true;
        else if(strcmp(argv[i],"--handoff-debug")==0)handoffDebug=true;
        else if(strcmp(argv[i],"--legacy-lag")==0)legacyLag=true;
        else if(strcmp(argv[i],"--no-fur")==0)noFur=true;
        else if(strcmp(argv[i],"--capture-normals")==0)captureNormals=true;
        else if(strcmp(argv[i],"--capture-flat")==0)captureFlat=true;
        else if(strcmp(argv[i],"--capture-albedo")==0)captureAlbedo=true;
        else if(strcmp(argv[i],"--capture-semantic")==0)captureSemantic=true;
        else if(strncmp(argv[i],"--fixed-timestep=",17)==0)fixedTimestep=strtof(argv[i]+17,NULL);
        else if(strcmp(argv[i],"--realtime")==0)fixedTimestep=0;
        else if(strncmp(argv[i],"--age-sequence=",15)==0) {
            const char* cursor=argv[i]+15;
            while(*cursor) {
                char* end;float value=strtof(cursor,&end);
                if(end==cursor||!isfinite(value)||value<0||value>1||sequenceCount==512||(*end&&*end!=',')) {
                    fprintf(stderr,"[ERROR] Secuencia de edades inválida\n");return 2;
                }
                sequence[sequenceCount++]=value;cursor=*end?end+1:end;
            }
            if(!sequenceCount)return 2;
        }
        else if(strcmp(argv[i],"--sdf")==0||strcmp(argv[i],"--exact-sdf")==0) {
            exactSdf=true;gpuSurface=false;gpuSparse=false;strictReference=false;surfaceMode="sdf-raymarch";
        }
        else if(strncmp(argv[i],"--surface-mode=",15)==0) {
            surfaceMode=argv[i]+15;
            if(strcmp(surfaceMode,"gpu-sdf-mesh")==0||strcmp(surfaceMode,"gpu-sdf-dense")==0) {
                surfaceMode="gpu-sdf-dense";gpuSurface=true;gpuSparse=false;exactSdf=true;strictReference=false;
            }
            else if(strcmp(surfaceMode,"gpu-sdf-sparse")==0) {gpuSurface=true;gpuSparse=true;exactSdf=true;strictReference=false;}
            else if(strcmp(surfaceMode,"sdf-raymarch")==0) {exactSdf=true;gpuSurface=false;gpuSparse=false;strictReference=false;}
            else if(strcmp(surfaceMode,"exact-sdf")==0) {exactSdf=true;gpuSurface=false;gpuSparse=false;strictReference=true;}
            else if(strcmp(surfaceMode,"legacy-handoff")==0) {exactSdf=false;gpuSurface=false;gpuSparse=false;strictReference=false;}
            else {fprintf(stderr,"[ERROR] Modo de superficie desconocido: %s\n",surfaceMode);return 2;}
        }
        else if(strncmp(argv[i],"--reference-max-step=",21)==0)referenceMaxStep=strtof(argv[i]+21,NULL);
        else if(strncmp(argv[i],"--reference-steps=",18)==0)referenceSteps=(unsigned)strtoul(argv[i]+18,NULL,10);
        else if(strncmp(argv[i],"--gpu-voxel=",12)==0)gridVoxel=strtof(argv[i]+12,NULL);
        else if(strncmp(argv[i],"--gpu-brick-cells=",18)==0)brickCells=(unsigned)atoi(argv[i]+18);
        else if(strncmp(argv[i],"--gpu-refinement=",17)==0)gridRefinement=(unsigned)atoi(argv[i]+17);
        else if(strncmp(argv[i],"--gpu-trace=",12)==0)gpuTracePath=argv[i]+12;
        else if(strncmp(argv[i],"--gpu-signature=",16)==0)gpuSignature=argv[i]+16;
        else if(strcmp(argv[i],"--gpu-cached-normals")==0)cachedNormals=true;
        else if(strcmp(argv[i],"--validate-compute")==0)validateCompute=true;
        else if(strcmp(argv[i],"--local-patches")==0)localPatches=true;
        else {fprintf(stderr,"[ERROR] Argumento desconocido: %s\n",argv[i]);return 2;}
    }
    if(gpuSignature&&!gpuSurface){fprintf(stderr,"[ERROR] --gpu-signature requiere un modo de extracción GPU explícito\n");return 2;}
    if(!isfinite(fixedTimestep)||fixedTimestep<0||fixedTimestep>1)return 2;
    if(sequenceCount)fixedAge=sequence[0];
    int totalFrames=0; bool captured=false; unsigned cycleCapture=0; float elapsed=0;
    FILE* trace=NULL;
    if(cyclePrefix) { char path[1024];snprintf(path,sizeof(path),"%s.csv",cyclePrefix);trace=fopen(path,"w");if(!trace)return 1;
        fprintf(trace,"seconds,age,geometry_age,presented_age,generation,vertices,build_ms,discarded,presentation_ms,projection_ms,sdf_evals,p95_residual,max_residual,quality_scan_ms,quality_bricks,thin_triangles,opposed_triangles,full_fallbacks,render_gpu_ms,render_cpu_ms,exact_sdf_compile_ms,exact_sdf_submit_ms,patch_attempts,patch_publications,patch_ms,patch_module,worker_age,pending_age,body_mesh_ms,mouth_ms,binding_ms,mapping_ms,completed,coalesced,worker_retargets,certified_age,geometry_lag,topology_modules,frozen_vertices,body_opposed,body_degenerates,body_boundary_edges,body_components,body_validation_ms,invalid_dynamic_meshes,obsolete_topology_results,topology_freezes,implicit_fallback,fallback_regions,fallback_volume,mesh_masked,sdf_regions_rendered,fallback_frames\n"); }


    setvbuf(stdout, NULL, _IOLBF, 0);

    printf("========================================================================\n");
    printf("   MONSTER ENGINE 3D: METAMORFOSIS EXTREMA (MonsterAger Inter-Especies) \n");
    printf("        Cánido Digitígrado con Pelaje  --->  Lagarto Cuadrúpedo con Escamas\n");
    printf("========================================================================\n\n");
    printf(" Concepto de la prueba:\n");
    printf("  1. Biología paramétrica: Se pasa de un tetrápodo erecto mamífero a un\n");
    printf("     tetrápodo reptil de andadura baja (sprawling), articulando patas y dedos.\n");
    printf("  2. Superficie progresiva: El pelaje canino se acorta y reduce su densidad (0%% -> 45%%),\n");
    printf("     revelando piel desnuda intermedia (45%% -> 55%%) donde la anatomía se reorganiza;\n");
    printf("     luego brotan las escamas (55%% -> 100%%) ganando relieve, quilla y pigmento.\n");
    if(gpuSurface)printf("  3. Campo actual GPU: modo %s, resolución %.6f, bricks %u.\n\n",surfaceMode,gridVoxel,brickCells);
    else if(exactSdf)printf("  3. %s: trazado por píxel del campo actual, sin extracción volumétrica.\n\n",
        strictReference?"Referencia diagnóstica":"SDF interactivo");
    else printf("  3. Ruta legacy: deformación y remallado asíncrono con cesión SDF.\n\n");
    printf(" Controles interactivos:\n");
    printf("  - ESPACIO                  : Alternar metamorfosis automática (oscilación fluida)\n");
    printf("  - FLECHA DERECHA / ARRIBA  : Avanzar metamorfosis hacia Lagarto (+perc)\n");
    printf("  - FLECHA IZQUIERDA / ABAJO : Retroceder metamorfosis hacia Perro (-perc)\n");
    printf("  - 0                        : Fijar a 0%% (Perro 100%% con pelaje completo)\n");
    printf("  - 1                        : Fijar a 25%% (Pérdida y afinamiento de pelo)\n");
    printf("  - 2                        : Fijar a 50%% (Piel desnuda intermedia y cambio esquelético)\n");
    printf("  - 3                        : Fijar a 75%% (Escamas incipientes y pigmento verde)\n");
    printf("  - 4                        : Fijar a 100%% (Lagarto 100%% con escamas desarrolladas)\n");
    printf("  - F1..F5                   : Cámaras (lateral, frontal, 3/4, cabeza, cola)\n");
    printf("  - --view=6                 : Vista superior para validación automática\n");
    printf("  - --view=7                 : Primer plano de autopodio posterior\n");
    printf("  - --view=8                 : Vista posterior para validación automática\n");
    printf("  - R                        : Pausar / reanudar giro de órbita de cámara\n");
    printf("  - A                        : Alternar overlay del grafo esquelético / anatómico\n");
    printf("  - --no-fur                 : Captura anatómica sin pelaje\n");
    printf("  - E / --exact-sdf          : Comparar SDF exacto con pigmento y material procedural\n");
    printf("  - --local-patches          : Probar la reparación topológica local validada\n");
    printf("  - C                        : Alternar visualización wireframe de chunks volumétricos\n");
    printf("  - B                        : Alternar visualización de región de fallback SDF (AABB)\n");
    printf("  - L                        : Ciclar inyección de latencia del worker (0, 50, 250, 1000, 3000 ms)\n");
    printf("  - M                        : Alternar remallado incremental por chunks en worker\n");
    printf("  - Rueda de ratón           : Zoom de cámara in / out\n");
    printf("  - ESCAPE                   : Salir\n");
    printf("========================================================================\n\n");

    /* 1. Inicializar SDL2 y OpenGL */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "[ERROR] No se pudo inicializar SDL2: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, noMSAA?0:1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, noMSAA?0:4);

    int windowWidth = 1100;
    int windowHeight = 820;

    SDL_Window* window = SDL_CreateWindow(
        "Monster Engine | Metamorfosis Extrema: Perro -> Lagarto (MonsterAger)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowWidth, windowHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );

    if (!window) {
        fprintf(stderr, "[ERROR] No se pudo crear la ventana SDL2: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        fprintf(stderr, "[AVISO] Contexto 4x MSAA falló. Reintentando sin MSAA.\n");
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS, 0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 0);
        glContext = SDL_GL_CreateContext(window);
        if (!glContext) {
            fprintf(stderr, "[ERROR] Fallo crítico de OpenGL: %s\n", SDL_GetError());
            SDL_DestroyWindow(window);
            SDL_Quit();
            return 1;
        }
    }

    SDL_GL_SetSwapInterval(1); // VSync

    /* 2. Configurar cámara 3D */
    ICamera camera;
    camera.target = Vec3_Create(0.0f, 2.2f, -1.5f);
    camera.position = Vec3_Create(10.0f, 6.0f, 10.0f);
    camera.up = Vec3_Create(0.0f, 1.0f, 0.0f);
    camera.fov = 42.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 120.0f;

    Renderer3D renderer = OpenGLRenderer_Create(&camera);
    if(ownershipDebug||handoffDebug)OpenGLRenderer_SetSurfaceDebug(&renderer,handoffDebug?24:23);
    OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);

    if (!OpenGLRenderer_SurfaceReady(&renderer)) {
        fprintf(stderr, "[ERROR] No se pudo inicializar el shader de superficies procedurales.\n");
        OpenGLRenderer_Destroy(&renderer);
        SDL_GL_DeleteContext(glContext);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    printf("[SUPERFICIE] %s | %s | voxel=%.6f | timestep=%s %.6f\n",surfaceMode,
        gpuSparse?"SPARSE":gpuSurface?"DENSE":exactSdf?(strictReference?"REFERENCE":"RAYMARCH"):"LEGACY",gridVoxel,
        fixedTimestep>0?"FIXED":"REALTIME",fixedTimestep);
    if(gpuSparse&&brickCells!=8&&brickCells!=16)return 2;
    if(gpuSurface&&!OpenGLRenderer_SupportsComputeSurface(&renderer)) {
        fprintf(stderr,"[ERROR] gpu-sdf-mesh requiere OpenGL >=4.3; seleccione legacy-handoff.\n");return 2;
    }
    /* 3. Crear especímenes extremos */
    printf("[INICIALIZACIÓN] Construyendo espécimen 1: Perro Adulto (Canis)...\n");
    Monster dog = Monster_Create();
    const CreatureRecipe* dogRecipe = CreatureRecipes_Dog();
    CreaturePhenotype dogPhenotype = dogRecipe->adult;
    if (!Creature_BuildMonster(&dog, dogRecipe, &dogPhenotype)) {
        fprintf(stderr, "[ERROR] Fallo al resolver el modelo de perro.\n");
        return 1;
    }
    Monster_SetHeadOpenFactor(&dog, 0.0f);

    printf("[INICIALIZACIÓN] Construyendo espécimen 2: Lagarto Adulto (Lacerta)...\n");
    Monster lizard = Monster_Create();
    const CreatureRecipe* lizardRecipe = CreatureRecipes_Lizard();
    CreaturePhenotype lizardPhenotype = lizardRecipe->adult;
    if (!Creature_BuildMonster(&lizard, lizardRecipe, &lizardPhenotype)) {
        fprintf(stderr, "[ERROR] Fallo al resolver el modelo de lagarto.\n");
        return 1;
    }
    Monster_SetHeadOpenFactor(&lizard, 0.08f);
    MonsterSDF exactField=MonsterSDF_Create();
    size_t exactEyeCapacity=dog.eyeCount>lizard.eyeCount?dog.eyeCount:lizard.eyeCount;
    MonsterVisualEye* exactEyes=calloc(exactEyeCapacity,sizeof(*exactEyes));
    if(exactEyeCapacity&&!exactEyes)return 1;
    for(size_t i=0;i<exactEyeCapacity;++i)exactEyes[i].globe=Mesh_Create();

    /* 4. Inicializar MonsterAger inter-especies */
    Monster materialReference=Monster_Create();
    if(!CreatureMorph_Interpolate(&dog,&lizard,.5f,&materialReference))return 2;
    float ageFactor = fixedAge>=0?fixedAge:0.0f;
    MonsterAger ager = MonsterAger_Create(&dog, &lizard, ageFactor);
    MonsterAger_SetGeometrySteps(&ager, 20); // 20 niveles discretos de geometría

    /* 5. Inicializar orquestador visual asíncrono */
    MonsterVisualAsyncConfig asyncCfg = MonsterVisualAsync_DefaultConfig();
    asyncCfg.interactiveMesherConfig.voxelSize = 0.075f;
    asyncCfg.settledMesherConfig.voxelSize = 0.055f;
    asyncCfg.morphMesherConfig.voxelSize = 0.070f;
    asyncCfg.enableLocalMorphPatches = localPatches;
    asyncCfg.enableChunkedRemeshing = true;
    if (cliWorkerDelay > 0) asyncCfg.workerDelayMs = cliWorkerDelay;
    if (cliChunkSize > 0.05f) asyncCfg.chunkSize = cliChunkSize;
    MonsterVisualAsync* visual = MonsterVisualAsync_Create(asyncCfg);
    MonsterVisualAsync_SetContinuousMotion(visual, true);
    MonsterVisualAsync_SetMorphMode(visual, true);
    if(!exactSdf&&!legacyLag && !MonsterVisualAsync_SetMorphEndpoints(visual,&dog,&lizard))return 1;

    GpuSDFSurfaceGrid gpuGrid={0};GpuSDFSurfaceCache* gpuCache=NULL;
    if(gpuSurface) {
        AABB3D envelope=AABB_Empty();Monster sample=Monster_Create();MonsterSDF field=MonsterSDF_Create();
        for(int i=0;i<=32;++i) {
            if(!CreatureMorph_Interpolate(&dog,&lizard,(float)i/32,&sample)||
               !MonsterSDF_Build(&field,&sample,asyncCfg.sdfConfig))return 2;
            AABB_ExpandPoint(&envelope,field.bounds.start);AABB_ExpandPoint(&envelope,field.bounds.end);
        }
        AABB_Pad(&envelope,2*gridVoxel);
        if(!GpuSDFSurfaceGrid_Configure(&gpuGrid,envelope,gridVoxel,gridRefinement))return 2;
        MonsterSDF_Free(&field);Monster_Free(&sample);
        printf("[REJILLA GPU] origen=[%.4f %.4f %.4f] paso=%.4f celdas=%u×%u×%u muestras=%zu refinamiento=%u\n",
            gpuGrid.origin.x,gpuGrid.origin.y,gpuGrid.origin.z,gpuGrid.voxelSize,
            gpuGrid.cells[0],gpuGrid.cells[1],gpuGrid.cells[2],gpuGrid.sampleCount,gpuGrid.refinementSteps);
    }
    if(gpuSparse){gpuCache=GpuSDFSurfaceCache_Create(&gpuGrid,brickCells);if(!gpuCache)return 2;gpuCache->cachedNormals=cachedNormals;}
    FILE* gpuTrace=NULL;
    if(cyclePrefix||gpuTracePath) {char path[1024],metadataPath[1100];
        if(gpuTracePath)snprintf(path,sizeof(path),"%s",gpuTracePath);else snprintf(path,sizeof(path),"%s.gpu.csv",cyclePrefix);
        gpuTrace=fopen(path,"w");if(!gpuTrace)return 2;
        snprintf(metadataPath,sizeof(metadataPath),"%s.meta.json",path);
        FILE* metadata=fopen(metadataPath,"w");if(!metadata){fclose(gpuTrace);return 2;}
        fputs("{\"mode\":",metadata);WriteJSONText(metadata,surfaceMode);
        fputs(",\"revision\":",metadata);WriteJSONText(metadata,MONSTER_BUILD_REVISION);
        fputs(",\"device\":",metadata);WriteJSONText(metadata,(const char*)glGetString(GL_RENDERER));
        fputs(",\"opengl\":",metadata);WriteJSONText(metadata,(const char*)glGetString(GL_VERSION));
        fprintf(metadata,",\"width\":%d,\"height\":%d,\"voxel\":%.9g,\"refinement\":%u,\"brick_cells\":%u,\"fur\":%s,\"fixed_timestep\":%.9g,\"argv\":[",
            windowWidth,windowHeight,gridVoxel,gridRefinement,brickCells,noFur?"false":"true",fixedTimestep);
        for(int i=0;i<argc;++i){if(i)fputc(',',metadata);WriteJSONText(metadata,argv[i]);}
        fprintf(metadata,"],\"group_pruning\":%s}\n",getenv("MONSTER_SDF_NO_GROUP_PRUNING")?"false":"true");if(fclose(metadata)!=0)return 2;
        fprintf(gpuTrace,"age,generation,timing_generation,samples,cells,gpu_bytes,upload_cpu_ms,sampling_gpu_ms,classification_gpu_ms,emission_gpu_ms,render_gpu_ms,total_gpu_ms,slabs,frame_wall_ms,cpu_thread_ms,candidate_bricks,dirty_bricks,new_bricks,active_bricks,resident_sample_bricks,reused_samples,unique_edges,updated_edges,indices,exact_normals,submitted_generation,completed_generation\n");}

    /* 6. Bucle principal de simulación y renderizado */
    bool running = true;
    bool autoAnimate = fixedAge<0;
    float growthDirection = 1.0f;
    float endpointHoldTimer = 0.0f;
    const float endpointHoldDuration = 1.8f; // Pausa de 1.8s en cada extremo
    const float morphSpeed = 0.12f;          // ~8 segundos para completar la transición

    bool orbitCamera = fixedAge<0 && !cyclePrefix;
    float orbitAngle = 0.75f;
    float cameraDistance = 14.5f;
    int currentViewMode = selectedView; // 1: Lateral, 2: Frontal, 3: Tres cuartos, 4: Cabeza, 5: Cola
    bool showSkeleton = false;

    bool shellsActive = !noFur;
    bool guardsActive = !noFur;
    int forcedShells = 0;
    OpenGLRenderer_SetFurQuality(&renderer, shellsActive, guardsActive, forcedShells);

    Uint32 lastTime = SDL_GetTicks();
    float fpsTimer = 0.0f;
    int frameCount = 0;
    float currentFps = 60.0f;
    float titleTimer = 0.0f;

    while (running) {
        Uint64 frameBegin=SDL_GetPerformanceCounter();
        struct timespec cpuBegin;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpuBegin);
        Uint32 currentTime = SDL_GetTicks();
        float wallDelta = (currentTime - lastTime) / 1000.0f;
        float deltaTime = fixedTimestep>0?fixedTimestep:wallDelta;
        lastTime = currentTime;

        /* Procesamiento de eventos */
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_WINDOWEVENT && event.window.event == SDL_WINDOWEVENT_RESIZED) {
                windowWidth = event.window.data1;
                windowHeight = event.window.data2;
                OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);
            } else if (event.type == SDL_MOUSEWHEEL) {
                cameraDistance = Math_Clamp(cameraDistance * (event.wheel.y > 0 ? 0.90f : 1.10f), 3.5f, 30.0f);
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    running = false;
                    break;
                case SDLK_SPACE:
                    autoAnimate = !autoAnimate;
                    endpointHoldTimer = 0.0f;
                    printf("[CONTROL] Metamorfosis automática: %s\n", autoAnimate ? "ACTIVADA" : "PAUSADA");
                    break;
                case SDLK_RIGHT:
                case SDLK_UP:
                    autoAnimate = false;
                    ageFactor = Math_Clamp01(ageFactor + 0.05f);
                    break;
                case SDLK_LEFT:
                case SDLK_DOWN:
                    autoAnimate = false;
                    ageFactor = Math_Clamp01(ageFactor - 0.05f);
                    break;
                case SDLK_0:
                    autoAnimate = false; ageFactor = 0.0f;
                    printf("[PRESET] 0%%: Perro Adulto (Canis familiaris)\n");
                    break;
                case SDLK_1:
                    autoAnimate = false; ageFactor = 0.25f;
                    printf("[PRESET] 25%%: Pérdida y adelgazamiento de pelaje\n");
                    break;
                case SDLK_2:
                    autoAnimate = false; ageFactor = 0.50f;
                    printf("[PRESET] 50%%: Piel Desnuda intermedia, reorganización esquelética y de postura\n");
                    break;
                case SDLK_3:
                    autoAnimate = false; ageFactor = 0.75f;
                    printf("[PRESET] 75%%: Formación progresiva de escamas y pigmento verdoso\n");
                    break;
                case SDLK_4:
                    autoAnimate = false; ageFactor = 1.0f;
                    printf("[PRESET] 100%%: Lagarto Adulto (Lacerta viridis)\n");
                    break;
                case SDLK_r:
                    orbitCamera = !orbitCamera;
                    printf("[CONTROL] Giro de cámara: %s\n", orbitCamera ? "ACTIVADO" : "PAUSADO");
                    break;
                case SDLK_a:
                    showSkeleton = !showSkeleton;
                    printf("[CONTROL] Overlay de esqueleto anatómico: %s\n", showSkeleton ? "VISIBLE" : "OCULTO");
                    break;
                case SDLK_e:
                    if(gpuSurface)break;
                    exactSdf=!exactSdf;gpuSurface=false;gpuSparse=false;
                    surfaceMode=exactSdf?"exact-sdf":"legacy-handoff";
                    printf("[COMPARACIÓN] Cuerpo: %s\n",
                        exactSdf?"SDF exacto con apariencia procedural":"malla dinámica");
                    break;
                case SDLK_s:
                    shellsActive = !shellsActive;
                    OpenGLRenderer_SetFurQuality(&renderer, shellsActive, guardsActive, forcedShells);
                    printf("[PELAJE] Conchas (shells): %s\n", shellsActive ? "ACTIVADAS" : "DESACTIVADAS");
                    break;
                case SDLK_g:
                    guardsActive = !guardsActive;
                    OpenGLRenderer_SetFurQuality(&renderer, shellsActive, guardsActive, forcedShells);
                    printf("[PELAJE] Pelos de cobertura (guards): %s\n", guardsActive ? "ACTIVADOS" : "DESACTIVADOS");
                    break;
                case SDLK_c:
                    showChunkWireframe = !showChunkWireframe;
                    printf("[CHUNKS] Wireframe de chunks: %s\n", showChunkWireframe ? "ACTIVADO" : "DESACTIVADO");
                    break;
                case SDLK_b:
                    showFallbackRegion = !showFallbackRegion;
                    printf("[FALLBACK] Región de fallback SDF (AABB): %s\n", showFallbackRegion ? "VISIBLE" : "OCULTO");
                    break;
                case SDLK_l: {
                    static const unsigned latencies[] = {0, 50, 250, 1000, 3000};
                    static size_t latIndex = 0;
                    latIndex = (latIndex + 1) % (sizeof(latencies) / sizeof(latencies[0]));
                    visual->config.workerDelayMs = latencies[latIndex];
                    printf("[LATENCIA] Latencia simulada del worker: %u ms\n", visual->config.workerDelayMs);
                    break;
                }
                case SDLK_m:
                    visual->config.enableChunkedRemeshing = !visual->config.enableChunkedRemeshing;
                    printf("[CHUNKS] Remallado por chunks en worker: %s\n", visual->config.enableChunkedRemeshing ? "ACTIVADO" : "DESACTIVADO");
                    break;
                case SDLK_F1:
                    currentViewMode = 1; orbitCamera = false;
                    printf("[VISTA] F1: Lateral completo\n");
                    break;
                case SDLK_F2:
                    currentViewMode = 2; orbitCamera = false;
                    printf("[VISTA] F2: Frontal\n");
                    break;
                case SDLK_F3:
                    currentViewMode = 3; orbitCamera = false;
                    printf("[VISTA] F3: Tres cuartos\n");
                    break;
                case SDLK_F4:
                    currentViewMode = 4; orbitCamera = false;
                    printf("[VISTA] F4: Primer plano cabeza / hocico / orejas\n");
                    break;
                case SDLK_F5:
                    currentViewMode = 5; orbitCamera = false;
                    printf("[VISTA] F5: Primer plano cola y miembros posteriores\n");
                    break;
                default:
                    break;
                }
            }
        }

        MonsterVisualAsyncStats previousStats=MonsterVisualAsync_GetStats(visual);
        bool topologyFallbackAvailable=visual->morphModuleCount>0;
        bool endpointReady=exactSdf || topologyFallbackAvailable || (visual->displayGeneration &&
            (previousStats.requestedFingerprint==previousStats.displayedFingerprint || !legacyLag));
        /* Los extremos se mantienen hasta que la geometría correspondiente está publicada. */
        bool canAdvance=(exactSdf||topologyFallbackAvailable||visual->displayGeneration) &&
            ((ageFactor>0 && ageFactor<1) || endpointReady);
        /* Actualización del factor de edad / metamorfosis */
        if (autoAnimate && canAdvance) {
            if (endpointHoldTimer > 0.0f) {
                endpointHoldTimer -= deltaTime;
            } else {
                /* Retroalimentación: reducir suavemente la velocidad antes de
                 * extrapolar más de una celda geométrica de la malla publicada. */
                if(legacyLag)
                    ageFactor=MonsterAger_AdvanceWithGeometry(&ager,growthDirection*morphSpeed*deltaTime,
                        previousStats.displayedGeometryAge,1.0f/(float)ager.geometrySteps);
                else ageFactor=Math_Clamp01(ageFactor+growthDirection*morphSpeed*deltaTime);
                if (ageFactor >= 1.0f) {
                    ageFactor = 1.0f;
                    growthDirection = -1.0f;
                    endpointHoldTimer = endpointHoldDuration;
                } else if (ageFactor <= 0.0f) {
                    ageFactor = 0.0f;
                    growthDirection = 1.0f;
                    endpointHoldTimer = endpointHoldDuration;
                }
            }
        }

        /* Las capturas automáticas fijan exactamente su edad, sin depender
         * de la frecuencia de frames ni de la latencia del worker. */
        if(cyclePrefix&&fixedAge<0&&!stressMotion&&cycleCapture<13) {
            const float targets[]={0,.20f,.35f,.50f,.65f,.80f,1,.80f,.65f,.50f,.35f,.20f,0};
            float target=targets[cycleCapture];
            if(cycleCapture==0)ageFactor=0;
            else if(cycleCapture<7&&growthDirection>0&&ageFactor>target)ageFactor=target;
            else if(cycleCapture>=7&&growthDirection<0&&ageFactor<target)ageFactor=target;
        }

        if(stressMotion&&totalFrames>=30) {
            if(strcmp(stressMotion,"burst")==0) {
                float phase=(float)((totalFrames-30)%90)/30.0f;
                ageFactor=phase<1?phase:phase<2?2-phase:phase-2;
            } else {
                const float scrub[]={0,1,.2f,.8f,.35f,.65f,.5f,0,1};
                ageFactor=scrub[((totalFrames-30)/3)%9];
            }
        }

        if(sequenceCount) {
            size_t step=(size_t)totalFrames;if(step>=sequenceCount)step=sequenceCount-1;
            growthDirection=step&&sequence[step]<sequence[step-1]?-1:1;
            ageFactor=sequence[step];
        }
        /* Aplicar nuevo estado al MonsterAger */
        MonsterAger_SetPerc(&ager, ageFactor);
        const Monster* currentMonster = MonsterAger_GetResultConst(&ager);
        const Monster* geomMonster = MonsterAger_GetGeometryResultConst(&ager);

        /* Actualizar orquestador visual asíncrono */
        if(!exactSdf) {
            MonsterVisualAsync_SetContinuousMotion(visual, autoAnimate);
            MonsterVisualAsync_UpdateWithAppearance(visual, geomMonster, currentMonster, deltaTime);
        }

        /* Configurar cámara y encuadre dinámico anatómico */
        const AnatomyNode* thoraxNode = AnatomyGraph_FindModuleNode(&currentMonster->anatomyGraph, 1, 3);
        Vector3 dogCenter = Vec3_Create(0.0f, 2.3f, -1.4f);
        Vector3 lizardCenter = Vec3_Create(0.0f, 0.45f, -4.8f);
        Vector3 blendCenter = thoraxNode ? thoraxNode->center : Vec3_Lerp(dogCenter, lizardCenter, ageFactor);

        if (orbitCamera) {
            orbitAngle += deltaTime * 0.35f;
            if (orbitAngle > 6.2831853f) orbitAngle -= 6.2831853f;
        }

        Vector3 cameraOffset;
        if (currentViewMode == 1 || currentViewMode == 9) {
            // Lateral
            camera.target = blendCenter;
            cameraOffset = Vec3_Create(currentViewMode==9?-cameraDistance:cameraDistance, 0.8f, 0.0f);
        } else if (currentViewMode == 2) {
            // Frontal
            camera.target = blendCenter;
            cameraOffset = Vec3_Create(0.0f, 1.2f, cameraDistance);
        } else if (currentViewMode == 6) {
            // Vista superior; ligero desplazamiento longitudinal evita singularidad de cámara.
            camera.target = blendCenter;
            cameraOffset = Vec3_Create(0.0f, cameraDistance, .01f);
        } else if (currentViewMode == 8) {
            camera.target = blendCenter;
            cameraOffset = Vec3_Create(0.0f, 1.2f, -cameraDistance);
        } else if (currentViewMode == 7) {
            const AnatomyNode* foot=NULL;
            for(size_t n=0;n<currentMonster->anatomyGraph.nodeCount;++n) {
                const AnatomyNode* candidate=&currentMonster->anatomyGraph.nodes[n];
                if(candidate->region==ANATOMY_REGION_HINDLIMB&&
                   candidate->localNodeId==LIMB_NODE_AUTOPOD) {
                    foot=candidate;break;
                }
            }
            camera.target=foot?foot->center:blendCenter;
            cameraOffset=Vec3_Create(3.4f,1.6f,3.6f);
        } else if (currentViewMode == 4) {
            // Primer plano cabeza
            const AnatomyNode* headNode = AnatomyGraph_FindFirstRegion(&currentMonster->anatomyGraph, ANATOMY_REGION_HEAD);
            camera.target = headNode ? headNode->center : blendCenter;
            cameraOffset = Vec3_Create(sinf(orbitAngle) * 3.8f, 1.4f, cosf(orbitAngle) * 3.8f);
        } else if (currentViewMode == 5) {
            // Primer plano cola
            const AnatomyNode* tailNode = AnatomyGraph_FindFirstRegion(&currentMonster->anatomyGraph, ANATOMY_REGION_TAIL);
            camera.target = tailNode ? tailNode->center : blendCenter;
            cameraOffset = Vec3_Create(sinf(orbitAngle) * 4.2f, 1.8f, cosf(orbitAngle) * 4.2f);
        } else {
            // Tres cuartos / órbita normal
            camera.target = blendCenter;
            float elev = 0.38f;
            float c = cosf(orbitAngle), s = sinf(orbitAngle);
            cameraOffset = Vec3_Create(s * cameraDistance, cameraDistance * elev, c * cameraDistance);
        }

        camera.position = Vec3_Add(camera.target, cameraOffset);

        /* Cálculo de métricas y título */
        frameCount++;
        fpsTimer += wallDelta;
        if (fpsTimer >= 0.5f) {
            currentFps = (float)frameCount / fpsTimer;
            frameCount = 0;
            fpsTimer = 0.0f;
        }

        titleTimer += wallDelta;
        if (titleTimer >= 0.12f) {
            titleTimer = 0.0f;
            char phaseDesc[128];
            if (ageFactor < 0.45f) {
                float cov = currentMonster->surface.integument.coverage * 100.0f;
                snprintf(phaseDesc, sizeof(phaseDesc), "PELAJE CANINO (Cobertura: %.0f%%)", cov);
            } else if (ageFactor < 0.55f) {
                snprintf(phaseDesc, sizeof(phaseDesc), "TRANSICION: PIEL DESNUDA (Morfogénesis)");
            } else {
                float cov = currentMonster->surface.integument.coverage * 100.0f;
                snprintf(phaseDesc, sizeof(phaseDesc), "ESCAMAS REPTIL (Relieve: %.0f%%)", cov);
            }

            const MorphChunkManager* chunkMgr = MonsterVisualAsync_GetChunkManager(visual);
            size_t totalChunks = chunkMgr ? chunkMgr->chunkCount : 0;
            size_t activeChunks = chunkMgr ? chunkMgr->surfaceCount : 0;
            char titleBuf[256];
            snprintf(titleBuf, sizeof(titleBuf),
                "Monster Engine | Metamorfosis: %.1f%% | %s | %.0f FPS | Chunks: %zu (%zu activos) | Lat: %u ms | %s",
                ageFactor * 100.0f, phaseDesc, currentFps,
                totalChunks, activeChunks, visual->config.workerDelayMs,
                surfaceMode);
            SDL_SetWindowTitle(window, titleBuf);
        }

        /* Separar compilación, envío y tiempo GPU para comparar ambas rutas. */
        Uint64 renderBegin=SDL_GetPerformanceCounter();
        Uint64 exactCompileTicks=0,exactSubmitTicks=0;
        Uint64 timerFrequency=SDL_GetPerformanceFrequency();
        /* Renderizado 3D */
        renderer.beginFrame(&renderer);
        OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);
        if(!OpenGLRenderer_SetMorphSurfaceDomain(&renderer,currentMonster,&materialReference))return 2;
        if(captureNormals)OpenGLRenderer_SetSurfaceDebug(&renderer,25);
        if(captureFlat)OpenGLRenderer_SetSurfaceDebug(&renderer,26);
        if(captureAlbedo)OpenGLRenderer_SetSurfaceDebug(&renderer,27);
        if(captureSemantic)OpenGLRenderer_SetSurfaceDebug(&renderer,28);
        OpenGLRenderer_SetSDFReference(&renderer,strictReference,referenceMaxStep,referenceSteps);

        float certifiedStep=1.0f/(float)ager.geometrySteps;
        if(!exactSdf)MonsterVisualAsync_UpdateHandoffs(visual,ageFactor,certifiedStep,deltaTime);
        OpenGLRenderer_SetMorphHandoffs(&renderer,NULL,-1);
        bool implicitFallback=!exactSdf&&
            MonsterVisualAsync_NeedsExactFallback(visual,ageFactor,
                certifiedStep);
        const Mesh* bodyMesh = MonsterVisualAsync_GetDisplayMesh(visual);
        const Mesh* headMesh = MonsterVisualAsync_GetDisplayHeadMesh(visual);
        bool noCertifiedBody=!bodyMesh||bodyMesh->indexCount==0;
        MorphFallbackRegion fallbackRegions[MORPH_MAX_FALLBACK_REGIONS];
        AABB3D fallbackBoxes[MORPH_MAX_FALLBACK_REGIONS];
        size_t fallbackCount=0;
        size_t fallbackRendered=0;
        bool fallbackCompiled=false;
        MorphHandoffSet handoffs;MorphHandoffSet_Init(&handoffs);
        if(implicitFallback&&!noCertifiedBody) {
            Uint64 beforeCompile=SDL_GetPerformanceCounter();
            fallbackCompiled=MonsterSDF_Build(&exactField,currentMonster,asyncCfg.sdfConfig);
            exactCompileTicks=SDL_GetPerformanceCounter()-beforeCompile;
            if(fallbackCompiled) {
                fallbackCount=MonsterVisualAsync_GetFallbackRegions(visual,&exactField,
                    &currentMonster->anatomyGraph,ageFactor,certifiedStep,
                    fallbackRegions,MORPH_MAX_FALLBACK_REGIONS);
                for(size_t i=0;i<fallbackCount;++i) {
                    fallbackBoxes[i]=fallbackRegions[i].bounds;
                    if(handoffs.count==MORPH_HANDOFF_MAX_INTERFACES) {
                        fprintf(stderr,"[ERROR] Capacidad de interfaces excedida.\n");return 2;
                    }
                    handoffs.interfaces[handoffs.count++]=fallbackRegions[i].handoff;
                }
            }
        }
        size_t maskedTriangles=0;
        const Mesh* ownedBody=MonsterVisualAsync_GetOwnedMesh(visual,fallbackRegions,
            fallbackCount,&maskedTriangles);
        bool semanticOwner=ownedBody!=NULL;
        if(!semanticOwner)ownedBody=bodyMesh;
        bool headFallback=false;
        for(size_t i=0;i<fallbackCount;++i)
            headFallback|=fallbackRegions[i].kind==MORPH_FALLBACK_HEAD;

        if(exactSdf||noCertifiedBody) {
            Uint64 beforeCompile=SDL_GetPerformanceCounter();
            bool compiled=fallbackCompiled||MonsterSDF_Build(&exactField,currentMonster,asyncCfg.sdfConfig);
            exactCompileTicks=SDL_GetPerformanceCounter()-beforeCompile;
            Uint64 beforeSubmit=SDL_GetPerformanceCounter();
            bool rendered=compiled&&(gpuSurface?
                (gpuSparse?OpenGLRenderer_RenderGpuSparseSDFSurface(&renderer,&exactField,gpuCache,&camera):
                OpenGLRenderer_RenderGpuSDFSurface(&renderer,&exactField,&gpuGrid,&camera)):
                OpenGLRenderer_RenderSDF(&renderer,&exactField,&camera,windowWidth,windowHeight));
            exactSubmitTicks=SDL_GetPerformanceCounter()-beforeSubmit;
            if(!rendered) {
                fprintf(stderr,"[ERROR] Falló la superficie seleccionada.\n");
                exitCode=2;break;
            }
            if(MonsterVisual_UpdateEyes(exactEyes,exactEyeCapacity,currentMonster))
                for(size_t i=0;i<currentMonster->eyeCount;++i)
                    if(exactEyes[i].globe.vertexCount) {
                        if(renderer.renderEye)renderer.renderEye(&renderer,&exactEyes[i]);
                        else renderer.renderMesh(&renderer,&exactEyes[i].globe);
                    }
        } else {
            if(!OpenGLRenderer_SetMorphHandoffs(&renderer,&handoffs,-1)) {
                fprintf(stderr,"[ERROR] Capacidad de soporte GPU excedida.\n");return 2;
            }
            if(ownedBody)renderer.renderMesh(&renderer,ownedBody);
            OpenGLRenderer_SetMorphHandoffs(&renderer,NULL,-1);
            if(!headFallback) {
                if (headMesh) renderer.renderMesh(&renderer, headMesh);
                size_t mouthCount = MonsterVisualAsync_GetDisplayMouthCount(visual);
                for (size_t m = 0; m < mouthCount; ++m)
                    for (size_t part = 0; part < 2; ++part) {
                        const Mesh* mesh = MonsterVisualAsync_GetDisplayMouthMesh(visual, m, part);
                        if (mesh) renderer.renderMesh(&renderer, mesh);
                    }
                size_t eyeCount = MonsterVisualAsync_GetDisplayEyeCount(visual);
                for (size_t i = 0; i < eyeCount; ++i) {
                    const Mesh* sclera = MonsterVisualAsync_GetDisplayEyeSclera(visual, i);
                    const Mesh* iris = MonsterVisualAsync_GetDisplayEyeIris(visual, i);
                    const Mesh* pupil = MonsterVisualAsync_GetDisplayEyePupil(visual, i);
                    if (sclera) renderer.renderMesh(&renderer, sclera);
                    if (iris) renderer.renderMesh(&renderer, iris);
                    if (pupil) renderer.renderMesh(&renderer, pupil);
                }
            }

            /* Un único pase evita evaluar varias veces rayos de regiones que
             * se solapan. Cada intervalo de trabajo continúa siendo local. */
            if(fallbackCount) {
                Uint64 beforeSubmit=SDL_GetPerformanceCounter();
                if(!OpenGLRenderer_SetMorphHandoffs(&renderer,&handoffs,-2))return 2;
                bool rendered=OpenGLRenderer_RenderSDFRegions(&renderer,&exactField,
                    &camera,windowWidth,windowHeight,fallbackBoxes,fallbackCount);
                exactSubmitTicks+=SDL_GetPerformanceCounter()-beforeSubmit;
                if(!rendered) {fprintf(stderr,"[ERROR] Falló la cesión SDF local.\n");return 2;}
                for(size_t i=0;i<fallbackCount;++i)
                    fallbackRendered+=!handoffs.interfaces[i].retireOnly;
                if(showFallbackRegion)for(size_t i=0;i<fallbackCount;++i)
                    DrawFallbackOverlay(fallbackBoxes[i]);
            }
            if(headFallback&&MonsterVisual_UpdateEyes(exactEyes,exactEyeCapacity,currentMonster))
                for(size_t i=0;i<currentMonster->eyeCount;++i)
                    if(exactEyes[i].globe.vertexCount) {
                        if(renderer.renderEye)renderer.renderEye(&renderer,&exactEyes[i]);
                        else renderer.renderMesh(&renderer,&exactEyes[i].globe);
                    }
        }
        OpenGLRenderer_SetMorphHandoffs(&renderer,NULL,-1);
        if(!exactSdf)MonsterVisualAsync_RecordFallbackFrame(visual,fallbackRegions,
            fallbackCount,fallbackRendered,ownedBody&&ownedBody->indexCount>0,
            maskedTriangles);

        if (showChunkWireframe) {
            const MorphChunkManager* chunkMgr = MonsterVisualAsync_GetChunkManager(visual);
            DrawChunkOverlay(chunkMgr);
        }

        if (showSkeleton) {
            DrawAnatomyOverlay(&currentMonster->anatomyGraph);
        }

        if(validateCompute&&fallbackCompiled==false&&exactSdf&&totalFrames==0) {
            Vector3 points[1024];size_t count=0;
            for(size_t n=0;n<currentMonster->anatomyGraph.nodeCount&&count<512;++n)
                points[count++]=currentMonster->anatomyGraph.nodes[n].center;
            for(unsigned i=0;i<512;++i) {
                float x=(float)((i*97u)%509u)/508,y=(float)((i*193u)%503u)/502,z=(float)((i*251u)%499u)/498;
                Vector3 size=Vec3_Sub(exactField.bounds.end,exactField.bounds.start);
                points[count++]=Vec3_Add(exactField.bounds.start,Vec3_Create(x*size.x,y*size.y,z*size.z));
            }
            float computeError=0,rayError=0;
            if(!OpenGLRenderer_ValidateComputeSDF(&renderer,&exactField,points,count,&computeError)||
               !OpenGLRenderer_ValidateSDF(&renderer,&exactField,points,count,&rayError)||
               computeError>.0002f||rayError>.0002f) {
                fprintf(stderr,"[ERROR PARIDAD] compute=%.9f raymarch=%.9f\n",computeError,rayError);return 2;
            }
            printf("[PARIDAD COMPUTE] puntos=%zu compute=%.9f raymarch=%.9f PASS\n",count,computeError,rayError);
            if(gpuSurface&&!OpenGLRenderer_ValidateGpuSurface(&renderer,&exactField))return 2;
            float intervalError=0;
            if(!OpenGLRenderer_ValidatePinnaIntervals(&renderer,&exactField,&intervalError))return 2;
        }
        if(gpuSurface) {
            GpuSDFSurfaceStats gs=OpenGLRenderer_GetGpuSDFSurfaceStats(&renderer);
            if(totalFrames%30==0)printf("[GPU SUPERFICIE] muestreo=%.3f clasificación=%.3f emisión=%.3f raster=%.3f ms memoria=%zu\n",
                gs.samplingGPUms,gs.classificationGPUms,gs.emissionGPUms,gs.renderGPUms,gs.gpuBytes);
            if(totalFrames%30==0||sequenceCount)printf("[AGER MODE] %s voxel=%.6f age=%.6f direction=%s active_bricks=%zu dirty_bricks=%zu candidate_bricks=%zu resident_sample_bricks=%zu sampled=%zu reused=%zu\n",
                surfaceMode,gridVoxel,ageFactor,growthDirection<0?"lizard->dog":"dog->lizard",gs.activeBricks,gs.dirtyBricks,gs.candidateBricks,gs.residentSampleBricks,gs.samples,gs.reusedSamples);
        }
        renderer.endFrame(&renderer);
        double renderCpuMs=(double)(SDL_GetPerformanceCounter()-renderBegin)*1000.0/(double)timerFrequency;
        OpenGLMeshPerformanceStats renderStats=OpenGLRenderer_GetMeshPerformanceStats(&renderer);
        MonsterVisualAsyncStats stats=MonsterVisualAsync_GetStats(visual);
        elapsed+=wallDelta;
        if(trace)fprintf(trace,"%.5f,%.6f,%.6f,%.6f,%llu,%zu,%.3f,%llu,%.3f,%.3f,%zu,%.6f,%.6f,%.3f,%zu,%zu,%zu,%zu,%.3f,%.3f,%.3f,%.3f,%zu,%zu,%.3f,%u,%.6f,%.6f,%.3f,%.3f,%.3f,%.3f,%llu,%llu,%llu,%.6f,%.6f,%zu,%zu,%zu,%zu,%zu,%zu,%.3f,%llu,%llu,%llu,%d,%zu,%.6f,%zu,%zu,%llu\n",
            elapsed,ageFactor,stats.displayedGeometryAge,stats.presentedMorphAge,
            (unsigned long long)visual->displayGeneration,bodyMesh?bodyMesh->vertexCount:0,
            stats.lastBuildDurationMs,(unsigned long long)stats.staleBuildDiscardedCount,
            stats.presentationMs,stats.dynamicSurface.projectionMs,
            stats.dynamicSurface.distanceEvaluations,stats.dynamicSurface.p95Residual,
            stats.dynamicSurface.maximumResidual,stats.dynamicSurface.qualityScanMs,
            stats.dynamicSurface.qualityBricks,stats.dynamicSurface.thinTriangles,
            stats.dynamicSurface.opposedTriangles,stats.dynamicFullFallbackCount,
            renderStats.gpuRenderMs,renderCpuMs,
            (double)exactCompileTicks*1000.0/(double)timerFrequency,
            (double)exactSubmitTicks*1000.0/(double)timerFrequency,
            stats.localPatchAttempts,stats.localPatchPublications,
            stats.lastLocalPatchMs,stats.lastLocalPatchModule,
            stats.workingGeometryAge,stats.pendingGeometryAge,
            stats.bodyMeshMs,stats.mouthBuildMs,stats.morphBindingMs,
            stats.surfaceMappingMs,
            (unsigned long long)stats.completedBuildCount,
            (unsigned long long)stats.coalescedCount,
            (unsigned long long)stats.workerRetargetCount,
            stats.certifiedAge,stats.geometryLag,stats.topologyModuleCount,
            stats.topologyFrozenVertices,stats.bodyOpposedTriangles,
            stats.bodyDegenerateTriangles,stats.bodyBoundaryEdges,
            stats.bodyDisconnectedComponents,stats.bodyValidationMs,
            (unsigned long long)stats.invalidDynamicMeshCount,
            (unsigned long long)stats.obsoleteTopologyResultCount,
            (unsigned long long)stats.topologyFreezeCount,
            implicitFallback?1:0,stats.fallbackRegionCount,
            stats.fallbackCoverageVolume,stats.meshChunksMasked,
            stats.sdfFallbackRegionsRendered,(unsigned long long)stats.fallbackFrames);
        const float captureAges[]={0,.20f,.35f,.50f,.65f,.80f,1,.80f,.65f,.50f,.35f,.20f,0};
        if(cyclePrefix && fixedAge<0 && !stressMotion && cycleCapture<13 && (exactSdf || implicitFallback ||
            (bodyMesh && bodyMesh->vertexCount)) &&
           ((cycleCapture<7 && ageFactor>=captureAges[cycleCapture]) || (cycleCapture>=7 && (growthDirection<0 || ageFactor<=0) && ageFactor<=captureAges[cycleCapture])) &&
           ((ageFactor>0 && ageFactor<1) || endpointReady)) {
            char path[1024];snprintf(path,sizeof(path),"%s-%02u.ppm",cyclePrefix,cycleCapture);
            if(!OpenGLRenderer_SavePPM(path,windowWidth,windowHeight))return 2;
            printf("[CICLO] %u edad=%.5f geometria=%.5f presentada=%.5f build=%.1f descartes=%llu\n",cycleCapture,ageFactor,stats.displayedGeometryAge,stats.presentedMorphAge,stats.lastBuildDurationMs,(unsigned long long)stats.staleBuildDiscardedCount);
            printf("[PROPIEDAD] malla=%s triángulos_ocultos=%zu regiones_SDF=%zu\n",
                semanticOwner?"SEMÁNTICA":"ESPACIAL",maskedTriangles,fallbackRendered);
            if(ownershipDebug&&bodyMesh) {
                size_t regionVertices[SURFACE_REGION_COUNT]={0};
                for(size_t v=0;v<bodyMesh->vertexCount;++v) {
                    int r=(int)lrintf(bodyMesh->vertices[v].surface.region);
                    if(r>=0&&r<SURFACE_REGION_COUNT)++regionVertices[r];
                }
                printf("[PROPIEDAD] vértices=%zu índices=%zu congelados=%zu regiones:",
                    bodyMesh->vertexCount,bodyMesh->indexCount,stats.topologyFrozenVertices);
                for(size_t r=0;r<SURFACE_REGION_COUNT;++r)
                    printf(" %zu:%zu",r,regionVertices[r]);
                putchar('\n');
                if(fallbackCompiled)
                    printf("[PROPIEDAD] residual visible=%.4f certificado=%.4f descartes_calidad=%llu\n",
                        SampleMeshResidual(bodyMesh,&exactField),
                        SampleMeshResidual(&visual->displayCertifiedMesh,&exactField),
                        (unsigned long long)stats.invalidDynamicMeshCount);
            }
            for(size_t i=0;i<fallbackCount;++i)
                printf("[FALLBACK] tipo=%d módulo=%u chunk=%u caja=[%.3f %.3f %.3f]-[%.3f %.3f %.3f] origen=[%.3f %.3f %.3f] banda=%.4f visualAge=%.5f certAge=%.5f targetAge=%.5f estado=%d transición=%.3f\n",
                    (int)fallbackRegions[i].kind,fallbackRegions[i].moduleId,
                    fallbackRegions[i].handoff.chunkId,
                    fallbackBoxes[i].start.x,fallbackBoxes[i].start.y,fallbackBoxes[i].start.z,
                    fallbackBoxes[i].end.x,fallbackBoxes[i].end.y,fallbackBoxes[i].end.z,
                    fallbackRegions[i].handoff.origin.x,fallbackRegions[i].handoff.origin.y,
                    fallbackRegions[i].handoff.origin.z,fallbackRegions[i].handoff.blendWidth,
                    fallbackRegions[i].visualAge,fallbackRegions[i].certifiedAge,
                    fallbackRegions[i].targetAge,(int)fallbackRegions[i].handoffState,
                    fallbackRegions[i].handoff.transitionFactor);
            if(++cycleCapture==13)running=false;
        }
        if(cyclePrefix&&stressMotion&&totalFrames>=30&&totalFrames%10==0) {
            char path[1024];snprintf(path,sizeof(path),"%s-%03d.ppm",cyclePrefix,totalFrames);
            if(!OpenGLRenderer_SavePPM(path,windowWidth,windowHeight))return 2;
            printf("[ESTRÉS] frame=%d modo=%s edad=%.5f regiones=%zu\n",
                totalFrames,stressMotion,ageFactor,fallbackRendered);
        }
        if(capturePrefix && (!sequenceCount || (size_t)totalFrames+1>=sequenceCount) && !captured && (exactSdf || implicitFallback ||
            stats.displayedFingerprint==stats.requestedFingerprint) &&
           (exactSdf || implicitFallback || (bodyMesh && bodyMesh->vertexCount)) &&
           (fixedAge>=0 || stats.activeQualityTier==MONSTER_VISUAL_QUALITY_SETTLED)) {
            char path[1024];snprintf(path,sizeof(path),"%s.ppm",capturePrefix);
            if(gpuSparse){char fieldPath[1024];snprintf(fieldPath,sizeof(fieldPath),"%s.field.json",capturePrefix);
                if(!OpenGLRenderer_SaveSparseSDFDiagnostics(&renderer,gpuCache,fieldPath))return 2;}
            if(gpuSignature&&!OpenGLRenderer_ExportGpuSurfaceSignature(&renderer,gpuSignature))return 2;
            captured=OpenGLRenderer_SavePPM(path,windowWidth,windowHeight);
            if(!captured||!SaveCaptureMetadata(capturePrefix,currentMonster,&exactField,&camera,surfaceMode,
                gridVoxel,ageFactor,sequence,sequenceCount,windowWidth,windowHeight))return 2;
            snprintf(path,sizeof(path),"%s.depth.pfm",capturePrefix);
            if(!OpenGLRenderer_SaveDepth(path,windowWidth,windowHeight,&camera))return 2;
            printf("[CAPTURA] %s mode=%s voxel=%.6f direction=%s age=%.6f geometry=%.6f vertices=%zu build=%.2f ms\n",path,surfaceMode,gridVoxel,growthDirection<0?"lizard->dog":"dog->lizard",ageFactor,gpuSurface?ageFactor:stats.displayedGeometryAge,bodyMesh?bodyMesh->vertexCount:0,stats.lastBuildDurationMs);
            running=false;
        }
        SDL_GL_SwapWindow(window);
        if(gpuTrace) {
            struct timespec cpuEnd;clock_gettime(CLOCK_THREAD_CPUTIME_ID,&cpuEnd);
            double wallMs=(SDL_GetPerformanceCounter()-frameBegin)*1000.0/timerFrequency;
            double cpuMs=(cpuEnd.tv_sec-cpuBegin.tv_sec)*1000.0+(cpuEnd.tv_nsec-cpuBegin.tv_nsec)*1e-6;
            GpuSDFSurfaceStats gs=OpenGLRenderer_GetGpuSDFSurfaceStats(&renderer);
            if(!gpuSurface) {
                gs.generation=(uint64_t)totalFrames+1;gs.timingGeneration=renderStats.gpuTimingGeneration;
                gs.renderGPUms=gs.totalGPUms=renderStats.gpuRenderMs;
            }
            fprintf(gpuTrace,"%.6f,%llu,%llu,%zu,%zu,%zu,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%u,%.4f,%.4f,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%zu,%llu,%llu\n",
                ageFactor,(unsigned long long)gs.generation,(unsigned long long)gs.timingGeneration,
                gs.samples,gs.cells,gs.gpuBytes,gs.uploadCPUms,gs.samplingGPUms,
                gs.classificationGPUms,gs.emissionGPUms,gs.renderGPUms,gs.totalGPUms,gs.slabs,wallMs,cpuMs,gs.candidateBricks,gs.dirtyBricks,gs.newBricks,gs.activeBricks,gs.residentSampleBricks,gs.reusedSamples,gs.uniqueEdges,gs.updatedEdges,gs.publishedIndices,gs.exactNormals,
                (unsigned long long)(gpuCache?gpuCache->submittedGeneration:gs.generation),
                (unsigned long long)(gpuCache?gpuCache->completedGeneration:gs.timingGeneration));
        }
        ++totalFrames;
        if(limitFrames>0 && totalFrames>=limitFrames)running=false;
        if(sequenceCount && (size_t)totalFrames>=sequenceCount)running=false;
    }

    if(trace)fclose(trace);
    if(gpuTrace)fclose(gpuTrace);
    /* 7. Limpieza y liberación de recursos */
    printf("[SALIDA] Cerrando demo y liberando recursos...\n");
    MonsterVisualAsync_Free(visual);
    for(size_t i=0;i<exactEyeCapacity;++i)Mesh_Free(&exactEyes[i].globe);
    free(exactEyes);
    MonsterSDF_Free(&exactField);
    MonsterAger_Free(&ager);
    Monster_Free(&materialReference);
    Monster_Free(&dog);
    Monster_Free(&lizard);
    GpuSDFSurfaceCache_Destroy(gpuCache);
    OpenGLRenderer_Destroy(&renderer);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("[FIN] Ejecución finalizada correctamente.\n");
    return exitCode;
}
