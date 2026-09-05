/**
 * @file demo_mouth_animation.c
 * @brief Demo visual 3D de la mandíbula SDF y su tejido gular articulado.
 * @author Monster Engine Team
 * @date 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <GL/gl.h>

#include "Monster.h"
#include "MonsterVisual.h"
#include "MathUtils.h"
#include "ColorPalette.h"
#include "Vector.h"
#include "RenderInterfaces.h"
#include "OpenGLRenderer.h"

static Monster Demo_CreateAnatomicalLizard(void) {
    Monster monster = Monster_Create();
    LizardPhenotype phenotype=LizardPreset_Adult();
    if(!Lizard_BuildMonster(&monster,&phenotype))
        fprintf(stderr,"[ERROR] No se pudo resolver el lagarto.\n");
    Monster_SetHeadOpenFactor(&monster,0.18f);
    /* El grafo completo permanece resuelto, pero el estudio cefálico no lo
     * incluye en el campo: así maxCells se dedica a las pequeñas cavidades. */
    monster.hasAnatomyGraph=false;
    return monster;
}

int main(int argc, char* argv[]) {
    float requestedOpen=-1.0f;
    const char* capture=NULL;
    for(int i=1;i<argc;++i)if(strncmp(argv[i],"--capture=",10)==0)capture=argv[i]+10;
    if(argc>1) {
        char* end=NULL;float parsed=strtof(argv[1],&end);
        if(end&&end!=argv[1])requestedOpen=Math_Clamp01(parsed);
    }

    printf("========================================================\n");
    printf("   MONSTER ENGINE 3D: Demo de Mandíbula Anatómica       \n");
    printf("========================================================\n");
    printf(" Controles:\n");
    printf("  - ESPACIO               : Pausar / Reanudar animación automática\n");
    printf("  - Flecha ARRIBA / ABAJO : Abrir / Cerrar boca manualmente\n");
    printf("  - R                     : Reiniciar parámetros\n");
    printf("  - ESC                   : Salir\n");
    printf("========================================================\n");

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("[ERROR] Fallo al inicializar SDL2: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    int windowWidth = 1024;
    int windowHeight = 768;

    SDL_Window* window = SDL_CreateWindow(
        "Monster Engine - Demo Mandíbula Anatómica",
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
        printf("[ERROR] No se pudo crear el contexto OpenGL: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(1);

    ICamera camera;
    camera.position = Vec3_Create(0.0f, 1.0f, 5.0f);
    camera.target = Vec3_Create(0.0f, 0.0f, 0.30f);
    camera.up = Vec3_Create(0.0f, 1.0f, 0.0f);
    camera.fov = 45.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    Renderer3D renderer = OpenGLRenderer_Create(&camera);
    OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);

    Monster monster = Demo_CreateAnatomicalLizard();
    if(requestedOpen>=0.0f)Monster_SetHeadOpenFactor(&monster,requestedOpen);

    SDFMesherConfig mesherCfg = SDFMesher_DefaultConfig();
    mesherCfg.voxelSize = HeadAnatomy_RecommendedVoxelSize(&monster.head.anatomy);
    mesherCfg.maxCells = 900000;
    MonsterVisual visual = MonsterVisual_Create(mesherCfg);
    MonsterSDFConfig sdfCfg = MonsterSDF_DefaultConfig();

    MonsterVisual_RebuildNow(&visual, &monster, sdfCfg);

    bool running = true;
    bool autoAnimate = requestedOpen<0.0f;
    float animTime = 0.0f;
    SDL_Event event;
    Uint32 lastTime = SDL_GetTicks();
    float cameraTime = 0.0f;

    while (running) {
        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN) {
                switch (event.key.keysym.sym) {
                    case SDLK_ESCAPE:
                        running = false;
                        break;
                    case SDLK_SPACE:
                        autoAnimate = !autoAnimate;
                        printf("[DEMO] Animación automática: %s\n", autoAnimate ? "ACTIVADA" : "PAUSADA");
                        break;
                    case SDLK_UP:
                        autoAnimate = false;
                        Monster_SetHeadOpenFactor(&monster, monster.mouths[0].openFactor + 0.05f);
                        printf("[DEMO] openFactor manual: %.2f\n", monster.mouths[0].openFactor);
                        break;
                    case SDLK_DOWN:
                        autoAnimate = false;
                        Monster_SetHeadOpenFactor(&monster, monster.mouths[0].openFactor - 0.05f);
                        printf("[DEMO] openFactor manual: %.2f\n", monster.mouths[0].openFactor);
                        break;
                    case SDLK_r:
                        autoAnimate = true;
                        animTime = 0.0f;
                        Monster_SetHeadOpenFactor(&monster, 0.42f);
                        printf("[DEMO] Parámetros reiniciados\n");
                        break;
                }
            } else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    windowWidth = event.window.data1;
                    windowHeight = event.window.data2;
                    OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);
                }
            }
        }

        if (autoAnimate) {
            animTime += deltaTime * 2.0f;
            Monster_SetHeadOpenFactor(&monster, 0.16f + 0.16f * sinf(animTime));
        }

        cameraTime += deltaTime;
        float cameraAngle = -1.02f + (capture?0:sinf(cameraTime * 0.24f) * 0.16f);
        float camRadius = 3.7f;
        camera.position.x = sinf(cameraAngle) * camRadius;
        camera.position.z = cosf(cameraAngle) * camRadius;

        MonsterVisual_Update(&visual, &monster, deltaTime, 0.0f, sdfCfg);

        uint64_t bodyGen = MonsterVisual_GetGeneration(&visual);
        uint64_t mouthGen = MonsterVisual_GetMouthVisualGeneration(&visual);

        char title[256];
        snprintf(title, sizeof(title), "Jaw SDF | openFactor: %.2f | Mouth Gen: %llu | Body Gen: %llu",
                 monster.mouths[0].openFactor, (unsigned long long)mouthGen, (unsigned long long)bodyGen);
        SDL_SetWindowTitle(window, title);

        renderer.beginFrame(&renderer);
        OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);

        MonsterVisual_Render(&visual, &renderer);

        renderer.endFrame(&renderer);
        if(capture && bodyGen>0) {
            if(!OpenGLRenderer_SavePPM(capture,windowWidth,windowHeight))fprintf(stderr,"[ERROR] Captura fallida\n");
            running=false;
        }
        SDL_GL_SwapWindow(window);
    }

    MonsterVisual_Free(&visual);
    Monster_Free(&monster);
    OpenGLRenderer_Destroy(&renderer);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("[INFO] Demo de animación de boca finalizada limpiamente.\n");
    return 0;
}
