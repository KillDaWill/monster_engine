/**
 * @file demo_ager_3d.c
 * @brief Demo visual 3D interactiva en OpenGL (SDF Mesh Pipeline) para mostrar la transición de envejecimiento/evolución (MonsterAger) de un Lagarto.
 * @author Monster Engine Team
 * @date 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include <string.h>
#include <SDL2/SDL.h>
#include <GL/gl.h>

#include "Monster.h"
#include "MonsterAger.h"
#include "ColorPalette.h"
#include "Vector.h"
#include "MathUtils.h"
#include "RenderInterfaces.h"
#include "OpenGLRenderer.h"
#include "MonsterVisualAsync.h"

static Monster Demo_CreateLizardStage(bool adult) {
    Monster lizard = Monster_Create();
    LizardPhenotype phenotype=adult?LizardPreset_Adult():LizardPreset_Juvenile();
    if(!Lizard_BuildMonster(&lizard,&phenotype))
        fprintf(stderr,"[ERROR] No se pudo resolver el lagarto.\n");
    Monster_SetHeadOpenFactor(&lizard,0.10f);
    return lizard;
}

/* Verifica los extremos de la anatomía que pertenece a la malla publicada,
 * no los de una solicitud más reciente todavía pendiente en el worker. */
static unsigned Demo_VisibleDigits(const Mesh* mesh,float scale) {
    float lo=0,hi=1;
    for(unsigned i=0;i<24;++i) {
        float t=(lo+hi)*.5f;
        LizardPhenotype p=LizardPhenotype_Interpolate(NULL,NULL,t);
        if(p.totalScale<scale)lo=t;else hi=t;
    }
    LizardPhenotype p=LizardPhenotype_Interpolate(NULL,NULL,(lo+hi)*.5f);
    AnatomyGraph graph;if(!Lizard_ResolveAnatomy(&p,&graph))return 0;
    const unsigned formula[2][5]={{2,3,4,5,3},{2,3,4,5,4}};
    unsigned visible=0;
    for(unsigned limb=0;limb<4;++limb)for(unsigned digit=0;digit<5;++digit) {
        const AnatomyNode* n=AnatomyGraph_FindNode(&graph,
            Anatomy_DigitId(limb,digit,formula[limb/2][digit]+1));
        float nearest=1e6f;
        for(size_t v=0;v<mesh->vertexCount;++v)
            nearest=fminf(nearest,Vec3_Distance(n->center,mesh->vertices[v].position));
        visible+=nearest<=n->widthRadius*1.8f;
    }
    return visible;
}

int main(int argc, char* argv[]) {
    setvbuf(stdout,NULL,_IOLBF,0);
    float requestedAge=-1.0f;
    bool inspectHead=false;
    bool headWireframe=false;
    const char* capturePrefix=NULL;
    const char* cyclePrefix=NULL;
    bool captureMorph=false;
    for(int argi=1;argi<argc;++argi) {
        if(strncmp(argv[argi],"--validate-cycle=",17)==0){cyclePrefix=argv[argi]+17;continue;}
        if(strcmp(argv[argi],"--morph")==0){captureMorph=true;continue;}
        if(strcmp(argv[argi],"--head")==0){inspectHead=true;continue;}
        if(strcmp(argv[argi],"--wire-head")==0){inspectHead=true;headWireframe=true;continue;}
        if(strncmp(argv[argi],"--capture-prefix=",17)==0){capturePrefix=argv[argi]+17;continue;}
        char* end=NULL; float parsed=strtof(argv[argi],&end);
        if(end&&end!=argv[argi]) {
            if(parsed<0.0f)parsed=0.0f;
            if(parsed>1.0f)parsed=1.0f;
            requestedAge=parsed;
        }
    }
    if(capturePrefix&&requestedAge<0.0f)requestedAge=0.0f;

    printf("========================================================\n");
    printf("   MONSTER ENGINE 3D: Demo SDF Transición (MonsterAger) \n");
    printf("========================================================\n");
    printf(" Controles:\n");
    printf("  - Flecha DERECHA / Flecha ARRIBA  : Avanzar edad (+ perc)\n");
    printf("  - Flecha IZQUIERDA / Flecha ABAJO : Retroceder edad (- perc)\n");
    printf("  - TECLA ESPACIO                   : Alternar animación automática\n");
    printf("  - H / teclas F1-F4                  : Inspección cabeza / vistas anatómicas\n");
    printf("  - 0..4 : Edades 0 / 25 / 50 / 75 / 100%%; rueda : zoom\n");
    printf("  - W                               : Wireframe de la malla local de cabeza\n");
    printf("========================================================\n");

    /* 1. Inicializar SDL2 y OpenGL */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("[ERROR] Fallo al inicializar SDL2: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,4);

    int windowWidth = 1024;
    int windowHeight = 768;

    SDL_Window* window = SDL_CreateWindow(
        "Monster Engine - Demo SDF Transición/Envejecimiento (Asíncrono)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        windowWidth, windowHeight,
        SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_SHOWN
    );

    if (!window) {
        printf("[ERROR] No se pudo crear la ventana SDL2: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext glContext = SDL_GL_CreateContext(window);
    if (!glContext) {
        fprintf(stderr,"[WARN] Contexto 4x MSAA no disponible: %s. Reintentando sin MSAA.\n",SDL_GetError());
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,0);
        SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,0);
        glContext=SDL_GL_CreateContext(window);
        if(!glContext){printf("[ERROR] No se pudo crear el contexto OpenGL: %s\n", SDL_GetError());SDL_DestroyWindow(window);SDL_Quit();return 1;}
    }
    int sampleBuffers=0,samples=0;SDL_GL_GetAttribute(SDL_GL_MULTISAMPLEBUFFERS,&sampleBuffers);SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES,&samples);
    printf("[RENDER] MSAA: %s (%dx)\n",sampleBuffers>0?"activo":"no disponible",sampleBuffers>0?samples:0);

    SDL_GL_SetSwapInterval(1);

    /* 2. Configurar la cámara agnóstica 3D */
    ICamera camera;
    camera.position = Vec3_Create(8.0f, 5.4f, 10.0f);
    camera.target = Vec3_Create(0.0f, 0.0f, -4.8f);
    camera.up = Vec3_Create(0.0f, 1.0f, 0.0f);
    camera.fov = 45.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    Renderer3D renderer = OpenGLRenderer_Create(&camera);
    OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);

    /* 3. Crear FASE 1: Lagarto Joven */
    Monster youngLizard = Demo_CreateLizardStage(false);

    /* 4. Crear FASE 2: Lagarto Alfa */
    Monster adultLizard = Demo_CreateLizardStage(true);

    /* 5. Inicializar MonsterAger y MonsterVisualAsync */
    float ageFactor = requestedAge>=0.0f?requestedAge:0.0f;
    bool autoAnimate = requestedAge<0.0f;
    MonsterAger ager = MonsterAger_Create(&youngLizard, &adultLizard, ageFactor);

    MonsterVisualAsyncConfig asyncCfg = MonsterVisualAsync_DefaultConfig();
    MonsterVisualAsync* visual = MonsterVisualAsync_Create(asyncCfg);

    /* 6. Bucle de Renderizado 3D */
    bool running = true;
    SDL_Event event;
    Uint32 lastTime = SDL_GetTicks();
    float cameraZoom = 1.0f;
    float growthDirection=1.0f;
    int headView=inspectHead?1:0;
    uint64_t printedGeneration=0;
    bool captureActive=false,captureComplete=false,settledReady=false;
    int captureView=0,captureFrames=0;
    bool cycleReturning=false,cycleDone=false;
    unsigned cycleFrames=0,cycleFailures=0;
    const char* captureNames[]={"whole","body-lateral","body-front","body-dorsal","head-oblique","head-lateral","head-frontal","head-dorsal","manus-dorsal","pes-dorsal","manus-oblique","pes-oblique"};

    float ageSpeed = 0.20f; /* ~5 s para recorrido completo 0 -> 1 */
    float fpsTimer = 0.0f;
    int fpsFrames = 0;
    float currentFps = 0.0f;
    float currentBuildsPerSec = 0.0f;
    uint64_t lastBuildCountForFps = 0;

    while (running) {
        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_RIGHT:
                    case SDLK_UP:
                        autoAnimate = false;
                        MonsterVisualAsync_SetMorphMode(visual, captureMorph);
                        ageFactor = Math_Clamp01(ageFactor + 0.05f);
                        MonsterAger_SetPerc(&ager, ageFactor);
                        printf("[AGER] Porcentaje manual: %.0f%%\n", ageFactor * 100.0f);
                        break;
                    case SDLK_LEFT:
                    case SDLK_DOWN:
                        autoAnimate = false;
                        MonsterVisualAsync_SetMorphMode(visual, captureMorph);
                        ageFactor = Math_Clamp01(ageFactor - 0.05f);
                        MonsterAger_SetPerc(&ager, ageFactor);
                        printf("[AGER] Porcentaje manual: %.0f%%\n", ageFactor * 100.0f);
                        break;
                    case SDLK_0: case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
                        autoAnimate = false;
                        MonsterVisualAsync_SetMorphMode(visual, captureMorph);
                        ageFactor = (event.key.keysym.sym - SDLK_0) * 0.25f;
                        MonsterAger_SetPerc(&ager, ageFactor);
                        printf("[AGER] Porcentaje directo: %.0f%%\n", ageFactor * 100.0f);
                        break;
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_SPACE:
                        autoAnimate = !autoAnimate;
                        MonsterVisualAsync_SetMorphMode(visual, autoAnimate);
                        printf("[AGER] Animación automática: %s\n", autoAnimate ? "ACTIVADA" : "DESACTIVADA");
                        break;
                    case SDLK_h:
                        inspectHead = !inspectHead; headView = inspectHead ? 1 : 0;
                        printf("[VISTA] Inspección cefálica: %s\n", inspectHead ? "ACTIVA" : "INACTIVA");
                        break;
                    case SDLK_w:
                        headWireframe = !headWireframe;
                        printf("[VISTA] Wireframe local de cabeza: %s\n", headWireframe ? "ACTIVO" : "INACTIVO");
                        break;
                    case SDLK_F1: inspectHead = true; headView = 1; break;
                    case SDLK_F2: inspectHead = true; headView = 2; break;
                    case SDLK_F3: inspectHead = true; headView = 3; break;
                    case SDLK_F4: inspectHead = true; headView = 4; break;
                }
            } else if (event.type == SDL_MOUSEWHEEL) {
                cameraZoom = Math_Clamp(cameraZoom * (event.wheel.y > 0 ? 0.90f : 1.10f), 0.25f, 2.5f);
            } else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    windowWidth = event.window.data1;
                    windowHeight = event.window.data2;
                    OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);
                }
            }
        }
        if (captureActive) { inspectHead = captureView >= 4 && captureView < 8; headView = captureView - 3; }

        if (autoAnimate) {
            MonsterVisualAsync_SetMorphMode(visual, true);
            /* Reloj de animación continuo e independiente de los fotogramas del mallador */
            if (MonsterVisualAsync_GetDisplayGeneration(visual) > 0) {
                ageFactor += growthDirection * ageSpeed * deltaTime;
                if (ageFactor >= 1.0f) {
                    ageFactor = 1.0f;
                    growthDirection = -1.0f;
                    cycleReturning=true;
                } else if (ageFactor <= 0.0f) {
                    ageFactor = 0.0f;
                    growthDirection = 1.0f;
                    if(cyclePrefix&&cycleReturning){cycleDone=true;autoAnimate=false;}
                }
                MonsterAger_SetPerc(&ager, ageFactor);
            }
        } else {
            MonsterVisualAsync_SetMorphMode(visual, captureMorph);
        }

        const Monster* currentMonster = MonsterAger_GetResultConst(&ager);
        /* Cámaras de comparación ancladas al adulto, independientes de edad. */
        camera.target = Vec3_Create(0, 0.25f, -5.3f);
        camera.up=Vec3_Create(0,1,0);
        Vector3 offset = Vec3_Create(-11, 9, 14);
        if (captureActive && captureView == 1) offset = Vec3_Create(18, 1, 0);
        if (captureActive && captureView == 2) offset = Vec3_Create(0, 2, 18);
        if (captureActive && captureView == 3) {offset = Vec3_Create(0,20,0);camera.up=Vec3_Create(0,0,-1);}
        if (inspectHead) {
            camera.target = Vec3_Create(0, 0.42f, 0.15f);
            if (headView == 2) offset = Vec3_Create(3.4f, 0.20f, 0);
            else if (headView == 3) offset = Vec3_Create(0, 0.25f, 3.6f);
            else if (headView == 4) offset = Vec3_Create(0.01f, 3.5f, 0.01f);
            else offset = Vec3_Create(2.8f, 1.8f, 2.8f);
        }
        if(captureActive&&captureView>=8) {
            const AnatomyNode* n=AnatomyGraph_FindNode(&currentMonster->anatomyGraph,
                captureView%2?ANATOMY_ID_HIND_LEFT_FOOT:ANATOMY_ID_FORE_LEFT_HAND);
            camera.target=n->center;
            float scale=currentMonster->lizardPhenotype.totalScale;
            if(captureView<10){offset=Vec3_Create(0,2.6f*scale,0);camera.up=Vec3_Create(0,0,-1);}
            else offset=Vec3_Scale(Vec3_Create(1.8f,1.4f,1.2f),scale);
        }
        camera.position = Vec3_Add(camera.target, Vec3_Scale(offset, cameraZoom));

        MonsterVisualAsync_SetContinuousMotion(visual, autoAnimate);
        MonsterVisualAsync_Update(visual, currentMonster, deltaTime);

        MonsterVisualAsyncStats stats = MonsterVisualAsync_GetStats(visual);
        uint64_t generation = MonsterVisualAsync_GetDisplayGeneration(visual);

        /* Cálculo de FPS y tasa de mallas generadas por segundo */
        fpsFrames++;
        fpsTimer += deltaTime;
        if (fpsTimer >= 0.5f) {
            currentFps = (float)fpsFrames / fpsTimer;
            uint64_t buildsDelta = stats.completedBuildCount - lastBuildCountForFps;
            currentBuildsPerSec = (float)buildsDelta / fpsTimer;
            fpsFrames = 0;
            fpsTimer = 0.0f;
            lastBuildCountForFps = stats.completedBuildCount;
        }

        /* Desfase entre edad mostrada en pantalla y edad objetivo */
        float juvenileScale = 0.58f;
        float adultScale = 1.0f;
        float displayedAge = stats.displayedScale > 0.0f
            ? Math_Clamp01((stats.displayedScale - juvenileScale) / (adultScale - juvenileScale))
            : ageFactor;
        float ageLag = fabsf(displayedAge - ageFactor);
        const char* tierStr = (stats.activeQualityTier == MONSTER_VISUAL_QUALITY_SETTLED)
            ? "SETTLED"
            : ((stats.activeQualityTier == MONSTER_VISUAL_QUALITY_MORPH) ? "MORPH" : "INTERACTIVE");

        if (generation != 0 && generation != printedGeneration) {
            printedGeneration = generation;
            settledReady = (stats.activeQualityTier == (captureMorph?MONSTER_VISUAL_QUALITY_MORPH:MONSTER_VISUAL_QUALITY_SETTLED));
            printf("[MALLA] edad_solicitada=%.2f edad_visible=%.2f lag=%.2f escala=%.5f gen=%llu fp=%llu tier=%s ms=%.2f fps=%.1f builds/s=%.1f grid=%dx%dx%d celdas=%zu activas=%zu refinadas=%zu muestras=%zu triangulos=%zu coalescidas=%llu\n",
                ageFactor, displayedAge, ageLag, stats.displayedScale,
                (unsigned long long)generation, (unsigned long long)stats.displayedFingerprint,
                tierStr, stats.lastBuildDurationMs, currentFps, currentBuildsPerSec,
                stats.bodyMesher.resolutionX, stats.bodyMesher.resolutionY, stats.bodyMesher.resolutionZ,
                stats.bodyMesher.cellCount, stats.bodyMesher.activeCellCount, stats.bodyMesher.refinedCellCount,
                stats.bodyMesher.distanceEvaluationCount, stats.bodyMesher.generatedTriangleCount,
                (unsigned long long)stats.coalescedCount);
        }

        char title[256];
        snprintf(title, sizeof(title),
            "Monster Engine | Lagarto %.0f%% (lag: %.1f%%) | Tier: %s | Worker: %.1fms | FPS: %.0f | Builds/s: %.1f | %s",
            ageFactor * 100.0f, ageLag * 100.0f, tierStr, stats.lastBuildDurationMs,
            currentFps, currentBuildsPerSec,
            autoAnimate ? "ANIMANDO" : "PAUSA");
        SDL_SetWindowTitle(window, title);

        renderer.beginFrame(&renderer);
        OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);

        const Mesh* bodyMesh=MonsterVisualAsync_GetDisplayMesh(visual);
        const Mesh* headMesh=MonsterVisualAsync_GetDisplayHeadMesh(visual);
        if(bodyMesh)renderer.renderMesh(&renderer,bodyMesh);
        OpenGLRenderer_SetWireframe(&renderer,headWireframe);
        if(headMesh)renderer.renderMesh(&renderer,headMesh);
        OpenGLRenderer_SetWireframe(&renderer,false);
        for(size_t m=0;m<MonsterVisualAsync_GetDisplayMouthCount(visual);++m)
            for(size_t part=0;part<2;++part){const Mesh* mesh=MonsterVisualAsync_GetDisplayMouthMesh(visual,m,part);if(mesh)renderer.renderMesh(&renderer,mesh);}
        for(size_t i=0;i<MonsterVisualAsync_GetDisplayEyeCount(visual);++i) {
            const Mesh* sclera=MonsterVisualAsync_GetDisplayEyeSclera(visual,i);const Mesh* iris=MonsterVisualAsync_GetDisplayEyeIris(visual,i);const Mesh* pupil=MonsterVisualAsync_GetDisplayEyePupil(visual,i);
            if(sclera)renderer.renderMesh(&renderer,sclera);
            if(iris)renderer.renderMesh(&renderer,iris);
            if(pupil)renderer.renderMesh(&renderer,pupil);
        }

        renderer.endFrame(&renderer);
        if(cyclePrefix&&generation&&cycleFrames!=(unsigned)generation) {
            cycleFrames=(unsigned)generation;
            unsigned visible=Demo_VisibleDigits(bodyMesh,stats.displayedScale);
            cycleFailures+=visible!=20;
            char path[768];snprintf(path,sizeof(path),"%s-%04u.ppm",cyclePrefix,cycleFrames);
            if(!OpenGLRenderer_SavePPM(path,windowWidth,windowHeight))cycleFailures++;
            printf("[CICLO] generación=%u extremos_visibles=%u/20 escala=%.6f\n",cycleFrames,visible,stats.displayedScale);
        }
        if(cyclePrefix&&cycleDone&&stats.activeQualityTier==MONSTER_VISUAL_QUALITY_SETTLED)running=false;
        if(captureActive && ++captureFrames>=3) {
            captureFrames=0;
            char path[768];snprintf(path,sizeof(path),"%s-%s.ppm",capturePrefix,captureNames[captureView]);
            if(OpenGLRenderer_SavePPM(path,windowWidth,windowHeight))printf("[CAPTURA] %s\n",path);
            else fprintf(stderr,"[ERROR] No se pudo guardar %s\n",path);
            captureView++;
            if(captureView>=12){captureActive=false;captureComplete=true;running=false;}
        } else if(!captureActive&&capturePrefix&&settledReady&&!captureComplete) {
            captureActive=true;captureView=0;
        }
        SDL_GL_SwapWindow(window);
    }

    /* Limpieza */
    MonsterVisualAsync_Free(visual);
    MonsterAger_Free(&ager);
    Monster_Free(&youngLizard);
    Monster_Free(&adultLizard);
    OpenGLRenderer_Destroy(&renderer);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("[INFO] Demo de envejecimiento finalizada limpiamente.\n");
    if(cyclePrefix)printf("[CICLO] recorrido 0 -> 1 -> 0 terminado: %u mallas, %u fallos\n",cycleFrames,cycleFailures);
    return cycleFailures?2:0;
}
