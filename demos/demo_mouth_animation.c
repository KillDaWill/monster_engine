/**
 * @file demo_mouth_animation.c
 * @brief Demo visual 3D de la mandíbula SDF y su tejido gular articulado.
 * @author Monster Engine Team
 * @date 2026
 */

#include <stdio.h>
#include <stdlib.h>
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

static void Demo_SetPartDimensions(BodyPart* part, float width, float height, float length) {
    if (!part) return;
    part->width = part->widthRender = width;
    part->height = part->heightRender = height;
    part->length = part->lengthRender = length;
}

static Monster Demo_CreateAnatomicalLizard(void) {
    Monster monster = Monster_Create();
    Monster_Init(&monster);
    monster.colorPalette = ColorPalette_CreateGradient(
        Color_FromRGB(32, 92, 48), Color_FromRGB(150, 176, 72), 6);

    BodyPart* head = Monster_GetHead(&monster);
    head->position = head->oldPosition = head->positionRender = Vec3_Create(0.0f, 0.18f, 0.35f);
    Demo_SetPartDimensions(head, 1.85f, 0.92f, 2.20f);
    head->color.index = 3;
    head->bellyColor.index = 0;
    head->bellyThreshold = 0.24f;

    BodyPart neck = BodyPart_Create(0.0f, 0.08f, -1.10f, 1.00f, 1.15f, 0.68f, 0.0f);
    neck.color.index = 2;
    BodyPart shoulders = BodyPart_Create(0.0f, -0.02f, -2.15f, 1.55f, 1.55f, 0.88f, 0.0f);
    shoulders.color.index = 2;
    Monster_AddBodyPart(&monster, neck);
    Monster_AddBodyPart(&monster, shoulders);

    Head anatomicalHead = Head_Create(HEAD_ARCHETYPE_LIZARD, 0,
        Vec3_Create(head->widthRender * 0.5f, head->heightRender * 0.5f, head->lengthRender * 0.5f));
    HeadPhenotype* phenotype = &anatomicalHead.phenotype;
    phenotype->skullWidth = 0.70f;
    phenotype->skullHeight = 0.25f;
    phenotype->skullLength = 0.62f;
    phenotype->muzzleLength = 0.88f;
    phenotype->muzzleWidth = 0.74f;
    phenotype->muzzleTaper = 0.40f;
    phenotype->eyeSize = 0.50f;
    phenotype->eyeLaterality = 0.94f;
    phenotype->eyeForwardness = 0.16f;
    phenotype->jawLength = 0.92f;
    phenotype->jawDepth = 0.38f;
    phenotype->jawStrength = 0.62f;
    phenotype->cheekMass = 0.34f;
    phenotype->nostrilPosition = 0.90f;
    HeadPhenotype_Normalize(phenotype);
    Monster_SetHead(&monster, anatomicalHead);
    Monster_SetHeadOpenFactor(&monster, 0.42f);

    for (size_t i = 0; i < monster.eyeCount; ++i) {
        monster.eyes[i].scleraColor = Color_FromRGB(218, 190, 72);
        monster.eyes[i].pupilColor = Color_FromRGB(5, 9, 4);
        monster.eyes[i].pupilScale = 0.36f;
    }
    if (monster.mouthCount > 0) monster.mouths[0].insideColor = Color_FromRGB(48, 6, 10);
    return monster;
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

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
    camera.target = Vec3_Create(0.0f, 0.0f, -0.35f);
    camera.up = Vec3_Create(0.0f, 1.0f, 0.0f);
    camera.fov = 45.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    Renderer3D renderer = OpenGLRenderer_Create(&camera);
    OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);

    Monster monster = Demo_CreateAnatomicalLizard();

    SDFMesherConfig mesherCfg = SDFMesher_DefaultConfig();
    MonsterVisual visual = MonsterVisual_Create(mesherCfg);
    MonsterSDFConfig sdfCfg = MonsterSDF_DefaultConfig();

    MonsterVisual_RebuildNow(&visual, &monster, sdfCfg);

    bool running = true;
    bool autoAnimate = true;
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
            Monster_SetHeadOpenFactor(&monster, 0.5f + 0.5f * sinf(animTime));
        }

        cameraTime += deltaTime;
        float cameraAngle = -0.36f + sinf(cameraTime * 0.32f) * 0.18f;
        float camRadius = 4.8f;
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
