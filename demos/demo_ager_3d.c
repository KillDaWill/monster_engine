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
#include "demo_ager_realtime.h"

static Monster Demo_CreateLizardStage(bool adult) {
    Monster lizard = Monster_Create();
    LizardPhenotype phenotype=adult?LizardPreset_Adult():LizardPreset_Seed();
    if(!Lizard_BuildMonster(&lizard,&phenotype))
        fprintf(stderr,"[ERROR] No se pudo resolver el lagarto.\n");
    Monster_SetHeadOpenFactor(&lizard,adult?0.10f:0.0f);
    return lizard;
}

/* Verifica los extremos de la anatomía que pertenece a la malla publicada,
 * no los de una solicitud más reciente todavía pendiente en el worker. */
static LizardPhenotype Demo_PhenotypeAtScale(float scale) {
    LizardPhenotype larva=LizardPreset_Seed(),adult=LizardPreset_Adult();
    float lo=0,hi=1;
    for(unsigned i=0;i<24;++i) {
        float t=(lo+hi)*.5f;
        LizardPhenotype p=LizardPhenotype_Interpolate(&larva,&adult,t);
        if(p.totalScale<scale)lo=t;else hi=t;
    }
    return LizardPhenotype_Interpolate(&larva,&adult,(lo+hi)*.5f);
}

static unsigned Demo_VisibleDigits(const Mesh* mesh,float scale) {
    LizardPhenotype p=Demo_PhenotypeAtScale(scale);
    AnatomyGraph graph;if(!Lizard_ResolveAnatomy(&p,&graph))return 0;
    const unsigned formula[2][5]={{2,3,4,5,3},{2,3,4,5,4}};
    unsigned visible=0;
    for(unsigned limb=0;limb<4;++limb)for(unsigned digit=0;digit<5;++digit) {
        const AnatomyNode* n=AnatomyGraph_FindNode(&graph,
            Anatomy_DigitId(limb,digit,formula[limb/2][digit]+1));
        if(!n || n->widthRadius < 0.005f) continue;
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
    bool directSdf=false,nativeScale=true,validateGPU=false,scalesEnabled=true;int benchmarkFrames=0,meshBenchmarkFrames=0;
    const char* profilePrefix=NULL;
    bool inspectHead=false;
    bool headWireframe=false;
    const char* capturePrefix=NULL;
    const char* cyclePrefix=NULL;
    bool captureMorph=false,turntableCapture=false;
    int targetFps=60;
    bool adaptiveFps=true;
    for(int argi=1;argi<argc;++argi) {
        if(strcmp(argv[argi],"--validate-gpu")==0){validateGPU=true;continue;}
        if(strcmp(argv[argi],"--sdf")==0){directSdf=true;continue;}
        if(strcmp(argv[argi],"--turntable")==0){turntableCapture=true;continue;}
        if(strcmp(argv[argi],"--mesh")==0){directSdf=false;continue;}
        if(strcmp(argv[argi],"--native-scale")==0){nativeScale=true;continue;}
        if(strcmp(argv[argi],"--adaptive")==0||strcmp(argv[argi],"--adaptive-scale")==0){nativeScale=false;continue;}
        if(strncmp(argv[argi],"--benchmark=",12)==0){benchmarkFrames=atoi(argv[argi]+12);directSdf=true;continue;}
        if(strncmp(argv[argi],"--profile-prefix=",17)==0){profilePrefix=argv[argi]+17;directSdf=true;continue;}
        if(strcmp(argv[argi],"--validate-cycle")==0){cyclePrefix="/tmp/cycle";directSdf=false;continue;}
        if(strncmp(argv[argi],"--validate-cycle=",17)==0){cyclePrefix=argv[argi]+17;directSdf=false;continue;}
        if(strcmp(argv[argi],"--morph")==0){captureMorph=true;continue;}
        if(strcmp(argv[argi],"--scales-off")==0){scalesEnabled=false;continue;}
        if(strncmp(argv[argi],"--benchmark-mesh=",17)==0){meshBenchmarkFrames=atoi(argv[argi]+17);directSdf=false;continue;}
        if(strcmp(argv[argi],"--head")==0){inspectHead=true;continue;}
        if(strcmp(argv[argi],"--wire-head")==0){inspectHead=true;headWireframe=true;continue;}
        if(strcmp(argv[argi],"--30fps")==0){targetFps=30;adaptiveFps=false;continue;}
        if(strcmp(argv[argi],"--60fps")==0){targetFps=60;adaptiveFps=false;continue;}
        if(strncmp(argv[argi],"--fps=",6)==0){targetFps=atoi(argv[argi]+6);adaptiveFps=false;continue;}
        if(strncmp(argv[argi],"--target-fps=",13)==0){targetFps=atoi(argv[argi]+13);adaptiveFps=false;continue;}
        if(strcmp(argv[argi],"--adaptive-fps")==0){adaptiveFps=true;continue;}
        if(strcmp(argv[argi],"--no-adaptive-fps")==0){adaptiveFps=false;continue;}
        char* end=NULL; float parsed=strtof(argv[argi],&end);
        if(end&&end!=argv[argi]) {
            if(parsed<0.0f)parsed=0.0f;
            if(parsed>1.0f)parsed=1.0f;
            requestedAge=parsed;
        }
    }
    if(validateGPU || benchmarkFrames>0 || profilePrefix)directSdf=true;
    if(capturePrefix&&requestedAge<0.0f)requestedAge=0.0f;

    printf("========================================================\n");
    printf(" MONSTER ENGINE 3D: Metamorfosis Semilla -> Lagarto (MonsterAger)\n");
    printf("========================================================\n");
    printf(" Controles:\n");
    printf("  - Flecha DERECHA / Flecha ARRIBA  : Avanzar edad (+ perc)\n");
    printf("  - Flecha IZQUIERDA / Flecha ABAJO : Retroceder edad (- perc)\n");
    printf("  - TECLA ESPACIO                   : Alternar animación automática\n");
    printf("  - H / teclas F1-F4                  : Inspección cabeza / vistas anatómicas\n");
    printf("  - 0..4 : Edades 0 / 25 / 50 / 75 / 100%%; rueda : zoom\n");
    printf("  - R                               : Pausar/reanudar giro de cámara\n");
    printf("  - W                               : Wireframe de la malla local de cabeza\n");
    printf("  - F                               : Alternar límite 30 FPS / 60 FPS (margen CPU)\n");
    printf(" Opciones CLI de rendimiento:\n");
    printf("  - --fps=30 / --30fps              : Ejecutar a 30 FPS para máximo margen\n");
    printf("  - --adaptive-fps                  : Reducir automáticamente a 30 FPS si hay retardo\n");
    printf("========================================================\n");

    /* 1. Inicializar SDL2 y OpenGL */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("[ERROR] Fallo al inicializar SDL2: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLEBUFFERS,directSdf?0:1);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES,directSdf?0:4);

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

    if (benchmarkFrames > 0) SDL_GL_SetSwapInterval(0);
    else if (SDL_GL_SetSwapInterval(-1) < 0) SDL_GL_SetSwapInterval(1);

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

    /* 3. Crear FASE 1: semilla blanca esférica */
    Monster larva = Demo_CreateLizardStage(false);

    /* 4. Crear FASE 2: Lagarto Alfa */
    Monster adultLizard = Demo_CreateLizardStage(true);
    if(!scalesEnabled) {
        larva.surface.integument.coverage=0;adultLizard.surface.integument.coverage=0;
        Monster_SetSurface(&larva,&larva.surface);Monster_SetSurface(&adultLizard,&adultLizard.surface);
    }

    /* 5. Inicializar MonsterAger y MonsterVisualAsync */
    float ageFactor = requestedAge>=0.0f?requestedAge:0.0f;
    bool autoAnimate = requestedAge<0.0f;
    MonsterAger ager = MonsterAger_Create(&larva, &adultLizard, ageFactor);
    if (!cyclePrefix) {
        MonsterAger_SetGeometrySteps(&ager, 100);
    }

    if(directSdf) {
        int result=Demo_RunRealtime(window,&renderer,&camera,&ager,requestedAge,capturePrefix,
            benchmarkFrames,profilePrefix,nativeScale,inspectHead,validateGPU);
        MonsterAger_Free(&ager);Monster_Free(&larva);Monster_Free(&adultLizard);
        OpenGLRenderer_Destroy(&renderer);SDL_GL_DeleteContext(glContext);SDL_DestroyWindow(window);SDL_Quit();
        return result;
    }

    if(!OpenGLRenderer_SurfaceReady(&renderer)) {
        fprintf(stderr,"[ERROR] No se pudo inicializar el shader de superficies.\n");
        MonsterAger_Free(&ager);Monster_Free(&larva);Monster_Free(&adultLizard);
        OpenGLRenderer_Destroy(&renderer);SDL_GL_DeleteContext(glContext);SDL_DestroyWindow(window);SDL_Quit();
        return 1;
    }

    MonsterVisualAsyncConfig asyncCfg = MonsterVisualAsync_DefaultConfig();
    MonsterVisualAsync* visual = MonsterVisualAsync_Create(asyncCfg);

    /* 6. Bucle de Renderizado 3D */
    bool running = true;
    SDL_Event event;
    Uint32 lastTime = SDL_GetTicks();
    float cameraZoom = 1.0f;
    float orbitAngle = 0.0f;
    bool orbitEnabled = true;
    float growthDirection=1.0f;
    float endpointHold=0.0f;
    bool waitingAtEndpoint=false;
    int headView=inspectHead?1:0;
    uint64_t printedGeneration=0;
    bool captureActive=false,captureComplete=false,settledReady=false;
    int captureView=0,captureFrames=0;
    bool cycleReturning=false,cycleDone=false;
    unsigned cycleFrames=0,cycleFailures=0;
    unsigned cycleCoverage[2]={0,0};
    float previousCycleAge=0,maximumCycleStep=0;
    const char* captureNames[]={"whole","body-lateral","body-front","body-dorsal","head-oblique","head-lateral","head-frontal","head-dorsal","manus-dorsal","pes-dorsal","manus-oblique","pes-oblique"};

    float ageSpeed = 0.20f; /* Velocidad máxima; el reloj respeta la publicación. */
    float fpsTimer = 0.0f;
    float titleTimer = 0.0f;
    int fpsFrames = 0;
    unsigned renderedMeshFrames=0;
    float currentFps = 0.0f;
    float currentBuildsPerSec = 0.0f;
    uint64_t lastBuildCountForFps = 0;
    int activeSwapInterval = 1;
    bool isThrottledTo30 = false;

    while (running) {
        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if(capturePrefix || cyclePrefix || meshBenchmarkFrames>0) {
                /* Las capturas automáticas no consumen teclas destinadas a otras ventanas. */
                continue;
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_RIGHT:
                    case SDLK_UP:
                        autoAnimate = false;
                        waitingAtEndpoint=false;endpointHold=0.0f;
                        MonsterVisualAsync_SetMorphMode(visual, true);
                        ageFactor = Math_Clamp01(ageFactor + 0.05f);
                        MonsterAger_SetPerc(&ager, ageFactor);
                        printf("[AGER] Porcentaje manual: %.0f%%\n", ageFactor * 100.0f);
                        break;
                    case SDLK_LEFT:
                    case SDLK_DOWN:
                        autoAnimate = false;
                        waitingAtEndpoint=false;endpointHold=0.0f;
                        MonsterVisualAsync_SetMorphMode(visual, true);
                        ageFactor = Math_Clamp01(ageFactor - 0.05f);
                        MonsterAger_SetPerc(&ager, ageFactor);
                        printf("[AGER] Porcentaje manual: %.0f%%\n", ageFactor * 100.0f);
                        break;
                    case SDLK_0: case SDLK_1: case SDLK_2: case SDLK_3: case SDLK_4:
                        autoAnimate = false;
                        waitingAtEndpoint=false;endpointHold=0.0f;
                        MonsterVisualAsync_SetMorphMode(visual, true);
                        ageFactor = (event.key.keysym.sym - SDLK_0) * 0.25f;
                        MonsterAger_SetPerc(&ager, ageFactor);
                        printf("[AGER] Porcentaje directo: %.0f%%\n", ageFactor * 100.0f);
                        break;
                    case SDLK_ESCAPE: running = false; break;
                    case SDLK_SPACE:
                        autoAnimate = !autoAnimate;
                        if(autoAnimate){waitingAtEndpoint=false;endpointHold=0.0f;}
                        MonsterVisualAsync_SetMorphMode(visual, autoAnimate);
                        printf("[AGER] Animación automática: %s\n", autoAnimate ? "ACTIVADA" : "DESACTIVADA");
                        break;
                    case SDLK_r: orbitEnabled = !orbitEnabled; break;
                    case SDLK_h:
                        inspectHead = !inspectHead; headView = inspectHead ? 1 : 0;
                        printf("[VISTA] Inspección cefálica: %s\n", inspectHead ? "ACTIVA" : "INACTIVA");
                        break;
                    case SDLK_w:
                        headWireframe = !headWireframe;
                        printf("[VISTA] Wireframe local de cabeza: %s\n", headWireframe ? "ACTIVO" : "INACTIVO");
                        break;
                    case SDLK_f:
                        targetFps = (targetFps == 30) ? 60 : 30;
                        adaptiveFps = false;
                        printf("[FPS] Límite de fotogramas configurado a %d FPS (%s)\n",
                               targetFps, targetFps == 30 ? "mayor margen CPU/worker" : "máxima fluidez");
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
            /* El reloj objetivo es continuo en modo interactivo para permitir
             * que la deformación morfológica fluya a 60 FPS sin parones. En modo de
             * validación de ciclo (--validate-cycle) se sincroniza por snapshot. */
            if (MonsterVisualAsync_GetDisplayGeneration(visual) > 0) {
                if(waitingAtEndpoint) {
                    MonsterVisualAsyncStats visible=MonsterVisualAsync_GetStats(visual);
                    bool readyForEndpoint = !cyclePrefix || (visible.displayedFingerprint==visible.requestedFingerprint);
                    if(readyForEndpoint) endpointHold+=deltaTime;
                    else endpointHold=0.0f;
                    if(endpointHold>=.75f) {
                        waitingAtEndpoint=false;endpointHold=0.0f;
                        if(ageFactor>=1.0f){growthDirection=-1.0f;cycleReturning=true;}
                        else {
                            growthDirection=1.0f;
                            if(cyclePrefix&&cycleReturning){cycleDone=true;autoAnimate=false;}
                        }
                    }
                } else {
                    if(cyclePrefix) {
                        /* En validación de ciclo se espera a la publicación de cada snapshot */
                        MonsterVisualAsyncStats visible=MonsterVisualAsync_GetStats(visual);
                        if(visible.displayedFingerprint==visible.requestedFingerprint)
                            ageFactor += growthDirection * fminf(ageSpeed * deltaTime, .005f);
                    } else {
                        /* En reproducción interactiva, avance continuo fluido a 60 FPS con amortiguación elástica de retardo */
                        MonsterVisualAsyncStats visible = MonsterVisualAsync_GetStats(visual);
                        float presentedScale = visible.presentedScale > 0.0f ? visible.presentedScale : visible.displayedScale;
                        (void)presentedScale;
                        float workerAge = visible.displayedScale > 0.0f
                            ? Lizard_AgeFromScaleBetween(visible.displayedScale, larva.lizardPhenotype.totalScale, adultLizard.lizardPhenotype.totalScale)
                            : ageFactor;
                        float lag = fabsf(ageFactor - workerAge);
                        if (adaptiveFps && autoAnimate) {
                            if (!isThrottledTo30 && lag > 0.12f) {
                                isThrottledTo30 = true;
                                printf("[FPS ADAPTATIVO] Retardo geométrico (%.2f > 0.12): reduciendo a 30 FPS para otorgar margen a la CPU/worker.\n", lag);
                            } else if (isThrottledTo30 && lag <= 0.06f) {
                                isThrottledTo30 = false;
                                printf("[FPS ADAPTATIVO] Retardo normalizado (%.2f): restaurando %d FPS.\n", lag, targetFps);
                            }
                        } else if (!adaptiveFps) {
                            isThrottledTo30 = false;
                        }
                        float speedFactor = 1.0f;
                        const float safeLag = 0.08f;
                        const float maxLag = 0.22f;
                        if (lag > safeLag) {
                            float excess = (lag - safeLag) / (maxLag - safeLag);
                            if (excess > 1.0f) excess = 1.0f;
                            speedFactor = 1.0f - excess * excess * 0.85f;
                        }
                        ageFactor += growthDirection * ageSpeed * speedFactor * deltaTime;
                    }
                    if(ageFactor>=1.0f){ageFactor=1.0f;waitingAtEndpoint=true;}
                    else if(ageFactor<=0.0f){ageFactor=0.0f;waitingAtEndpoint=true;}
                }
                MonsterAger_SetPerc(&ager, ageFactor);
            }
        } else {
            MonsterVisualAsync_SetMorphMode(visual, captureMorph || !cyclePrefix);
        }

        int effectiveTargetFps = isThrottledTo30 ? 30 : targetFps;
        if (effectiveTargetFps == 30) {
            if (activeSwapInterval != 2 && activeSwapInterval != 0) {
                if (SDL_GL_SetSwapInterval(2) == 0) activeSwapInterval = 2;
                else { SDL_GL_SetSwapInterval(0); activeSwapInterval = 0; }
            }
        } else {
            if (activeSwapInterval != 1 && activeSwapInterval != -1) {
                if (SDL_GL_SetSwapInterval(-1) == 0) activeSwapInterval = -1;
                else { SDL_GL_SetSwapInterval(1); activeSwapInterval = 1; }
            }
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
        /* Girar sólo la cámara: conservar geometría y coordenadas de superficie. */
        if(turntableCapture && captureActive) {
            camera.target=Vec3_Create(0,0.25f,-5.3f);
            camera.up=Vec3_Create(0,1,0);
            offset=Vec3_Create(-11,9,14);
            orbitAngle=(float)captureView*6.28318530718f/12.0f;
        } else if(orbitEnabled && !inspectHead && !capturePrefix && !cyclePrefix) {
            orbitAngle=fmodf(orbitAngle+fminf(deltaTime,.1f)*.25f,6.28318530718f);
        }
        if((!inspectHead && !capturePrefix && !cyclePrefix) || (turntableCapture && captureActive)) {
            float c=cosf(orbitAngle),s=sinf(orbitAngle);
            offset=Vec3_Create(c*offset.x+s*offset.z,offset.y,-s*offset.x+c*offset.z);
        }
        camera.position = Vec3_Add(camera.target, Vec3_Scale(offset, cameraZoom));

        MonsterVisualAsync_SetContinuousMotion(visual, autoAnimate || captureMorph);
        MonsterVisualAsync_UpdateWithAppearance(visual,
            MonsterAger_GetGeometryResultConst(&ager),currentMonster,deltaTime);

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
        float presentedScale = (cyclePrefix || stats.presentedScale <= 0.0f) ? stats.displayedScale : stats.presentedScale;
        float displayedAge = presentedScale > 0.0f
            ? Lizard_AgeFromScaleBetween(presentedScale,
                larva.lizardPhenotype.totalScale,adultLizard.lizardPhenotype.totalScale)
            : ageFactor;
        float ageLag = fabsf(displayedAge - ageFactor);
        const char* tierStr = (stats.activeQualityTier == MONSTER_VISUAL_QUALITY_SETTLED)
            ? "SETTLED"
            : ((stats.activeQualityTier == MONSTER_VISUAL_QUALITY_MORPH) ? "MORPH" : "INTERACTIVE");

        if (generation != 0 && generation != printedGeneration) {
            printedGeneration = generation;
            settledReady = (stats.activeQualityTier == (captureMorph?MONSTER_VISUAL_QUALITY_MORPH:MONSTER_VISUAL_QUALITY_SETTLED));
            printf("[MALLA] edad_solicitada=%.2f edad_visible=%.2f lag=%.2f escala=%.5f gen=%llu fp=%llu tier=%s ms=%.2f fps=%.1f builds/s=%.1f grid=%dx%dx%d celdas=%zu activas=%zu refinadas=%zu muestras=%zu triangulos=%zu coalescidas=%llu\n",
                ageFactor, displayedAge, ageLag, presentedScale,
                (unsigned long long)generation, (unsigned long long)stats.displayedFingerprint,
                tierStr, stats.lastBuildDurationMs, currentFps, currentBuildsPerSec,
                stats.bodyMesher.resolutionX, stats.bodyMesher.resolutionY, stats.bodyMesher.resolutionZ,
                stats.bodyMesher.cellCount, stats.bodyMesher.activeCellCount, stats.bodyMesher.refinedCellCount,
                stats.bodyMesher.distanceEvaluationCount, stats.bodyMesher.generatedTriangleCount,
                (unsigned long long)stats.coalescedCount);
            printf("[EDADES] objetivo=%.5f morph=%.5f geometria=%.5f trabajo=%.5f pendiente=%.5f lag_geometrico=%.5f\n",
                stats.targetAge,stats.presentedMorphAge,stats.displayedGeometryAge,
                stats.workingGeometryAge,stats.pendingGeometryAge,stats.geometryLag);
            printf("[PERF CPU] ager=%.3f ms sdf=%.3f ms cuerpo=%.3f ms cabeza=%.3f ms mapa=%.3f ms boca=%.3f ms bind=%.3f ms deform=%.3f ms candidatos=%.2f/%zu publicación=%.3f+%.3f ms\n",
                ager.lastInterpolationMs,stats.sdfBuildMs,stats.bodyMeshMs,stats.headMeshMs,
                stats.surfaceMappingMs,stats.mouthBuildMs,stats.morphBindingMs,stats.morphDeformMs,
                stats.surfaceMapper.averageCandidatesPerVertex,stats.surfaceMapper.anatomySegmentCount,
                stats.readyPublicationMs,stats.displayPublicationMs);
        }

        titleTimer += deltaTime;
        if (titleTimer >= 0.10f || generation != printedGeneration) {
            titleTimer = 0.0f;
            char title[256];
            snprintf(title, sizeof(title),
                "Monster Engine | Semilla -> Lagarto %.0f%% (lag: %.1f%%) | Tier: %s | Worker: %.1fms | FPS: %.0f/%d%s | Builds/s: %.1f | %s",
                ageFactor * 100.0f, ageLag * 100.0f, tierStr, stats.lastBuildDurationMs,
                currentFps, effectiveTargetFps, isThrottledTo30 ? " (adaptativo)" : "",
                currentBuildsPerSec,
                autoAnimate ? "ANIMANDO" : "PAUSA");
            SDL_SetWindowTitle(window, title);
        }

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
            if(cycleFrames) {
                float step=fabsf(displayedAge-previousCycleAge);
                maximumCycleStep=fmaxf(maximumCycleStep,step);
                if(step>1.0f/32.0f+.002f)cycleFailures++;
            }
            previousCycleAge=displayedAge;
            cycleCoverage[cycleReturning?1:0]|=1u<<(unsigned)(Math_Clamp01(displayedAge)*10);
            cycleFrames=(unsigned)generation;
            unsigned visible=Demo_VisibleDigits(bodyMesh,presentedScale);
            LizardPhenotype visiblePhenotype=Demo_PhenotypeAtScale(presentedScale);
            if(visiblePhenotype.appendageDevelopment<.02f)cycleFailures+=visible!=0;
            else if(visiblePhenotype.appendageDevelopment>.92f)cycleFailures+=visible!=20;
            char path[768];snprintf(path,sizeof(path),"%s-%04u.ppm",cyclePrefix,cycleFrames);
            if(!OpenGLRenderer_SavePPM(path,windowWidth,windowHeight))cycleFailures++;
            printf("[CICLO] generación=%u extremos_visibles=%u/20 escala=%.6f\n",cycleFrames,visible,presentedScale);
        }
        if(cyclePrefix&&cycleDone&&stats.activeQualityTier==MONSTER_VISUAL_QUALITY_SETTLED)running=false;
        if(!OpenGLRenderer_CheckErrors()) {cycleFailures++;running=false;}
        if(captureActive && ++captureFrames>=3) {
            captureFrames=0;
            char path[768];snprintf(path,sizeof(path),"%s-%s.ppm",capturePrefix,turntableCapture?"giro":captureNames[captureView]);
            if(turntableCapture)snprintf(path,sizeof(path),"%s-giro-%02d.ppm",capturePrefix,captureView);
            if(OpenGLRenderer_SavePPM(path,windowWidth,windowHeight))printf("[CAPTURA] %s\n",path);
            else fprintf(stderr,"[ERROR] No se pudo guardar %s\n",path);
            captureView++;
            if(captureView>=12){captureActive=false;captureComplete=true;running=false;}
        } else if(!captureActive&&capturePrefix&&settledReady&&!captureComplete) {
            captureActive=true;captureView=0;
        }
        SDL_GL_SwapWindow(window);
        if (effectiveTargetFps > 0 && activeSwapInterval != 2) {
            Uint32 frameElapsed = SDL_GetTicks() - currentTime;
            Uint32 targetFrameMs = (Uint32)(1000.0f / (float)effectiveTargetFps);
            if (frameElapsed < targetFrameMs) {
                SDL_Delay(targetFrameMs - frameElapsed);
            }
        }
        if(meshBenchmarkFrames>0 && generation && ++renderedMeshFrames>=(unsigned)meshBenchmarkFrames)running=false;
    }

    /* Limpieza */
    OpenGLMeshPerformanceStats gpuMesh=OpenGLRenderer_GetMeshPerformanceStats(&renderer);
    printf("[PERF GPU MESH] render=%.3f ms uploads=%llu cache_hits=%llu bytes=%llu último_upload=%.3f ms\n",
        gpuMesh.gpuRenderMs,(unsigned long long)gpuMesh.uploadCount,
        (unsigned long long)gpuMesh.cacheHitCount,(unsigned long long)gpuMesh.totalBytesUploaded,
        gpuMesh.lastUploadMs);
    MonsterVisualAsync_Free(visual);
    MonsterAger_Free(&ager);
    Monster_Free(&larva);
    Monster_Free(&adultLizard);
    OpenGLRenderer_Destroy(&renderer);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("[INFO] Demo de envejecimiento finalizada limpiamente.\n");
    if(cyclePrefix) {
        /* Cada décima debe haberse publicado en ambos sentidos. */
        if((cycleCoverage[0]&1023u)!=1023u||(cycleCoverage[1]&1023u)!=1023u||!cycleDone)
            cycleFailures++;
        printf("[CICLO] recorrido 0 -> 1 -> 0 terminado: %u mallas, %u fallos, paso máximo %.6f\n",
            cycleFrames,cycleFailures,maximumCycleStep);
    }
    return cycleFailures?2:0;
}
