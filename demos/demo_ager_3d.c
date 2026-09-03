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
#include <SDL2/SDL.h>
#include <GL/gl.h>

#include "Monster.h"
#include "MonsterAger.h"
#include "ColorPalette.h"
#include "Vector.h"
#include "RenderInterfaces.h"
#include "OpenGLRenderer.h"
#include "MonsterVisualAsync.h"

static void Demo_SetPartDimensions(BodyPart* part, float width, float height, float length) {
    if (!part) return;
    part->width = part->widthRender = width;
    part->height = part->heightRender = height;
    part->length = part->lengthRender = length;
}

static void Demo_AddLizardSection(Monster* monster, Vector3 position,
                                  float width, float height, float length, int colorIndex) {
    BodyPart part = BodyPart_Create(position.x, position.y, position.z,
                                    width, length, height, 0.0f);
    part.color.index = colorIndex;
    part.bellyColor.index = colorIndex > 0 ? colorIndex - 1 : 0;
    part.bellyThreshold = 0.28f;
    Monster_AddBodyPart(monster, part);
}

static Monster Demo_CreateLizardStage(bool adult) {
    Monster lizard = Monster_Create();
    Monster_Init(&lizard);

    if (adult) {
        lizard.colorPalette = ColorPalette_CreateGradient(
            Color_FromRGB(34, 86, 42), Color_FromRGB(178, 142, 54), 6);
    } else {
        lizard.colorPalette = ColorPalette_CreateGradient(
            Color_FromRGB(58, 132, 68), Color_FromRGB(142, 196, 94), 6);
    }

    const float scale = adult ? 1.0f : 0.56f;
    BodyPart* head = Monster_GetHead(&lizard);
    head->position = head->oldPosition = head->positionRender =
        Vec3_Create(0.0f, 0.30f * scale, 0.0f);
    Demo_SetPartDimensions(head, 2.20f * scale, 1.20f * scale, 2.55f * scale);
    head->color.index = adult ? 2 : 3;
    head->bellyColor.index = 0;
    head->bellyThreshold = 0.25f;

    /* Cadena axial temporal con solapes amplios: cuello, tórax, abdomen, pelvis y cola. */
    Demo_AddLizardSection(&lizard, Vec3_Create(0.0f, 0.18f * scale, -1.75f * scale),
                          1.25f * scale, 0.82f * scale, 1.35f * scale, 2);
    Demo_AddLizardSection(&lizard, Vec3_Create(0.0f, 0.08f * scale, -3.05f * scale),
                          2.35f * scale, 1.28f * scale, 2.25f * scale, 3);
    Demo_AddLizardSection(&lizard, Vec3_Create(0.0f, 0.02f * scale, -5.15f * scale),
                          2.10f * scale, 1.12f * scale, 2.35f * scale, 3);
    Demo_AddLizardSection(&lizard, Vec3_Create(0.0f, 0.08f * scale, -7.10f * scale),
                          1.72f * scale, 0.96f * scale, 1.85f * scale, 2);
    Demo_AddLizardSection(&lizard, Vec3_Create(0.0f, 0.13f * scale, -8.85f * scale),
                          1.18f * scale, 0.68f * scale, 1.75f * scale, 2);
    Demo_AddLizardSection(&lizard, Vec3_Create(0.0f, 0.20f * scale, -10.55f * scale),
                          0.72f * scale, 0.45f * scale, 1.70f * scale, 1);
    Demo_AddLizardSection(&lizard, Vec3_Create(0.0f, 0.28f * scale, -12.05f * scale),
                          0.30f * scale, 0.24f * scale, 1.35f * scale, 0);

    Head anatomicalHead = Head_Create(HEAD_ARCHETYPE_LIZARD, 0,
        Vec3_Create(head->widthRender * 0.5f, head->heightRender * 0.5f, head->lengthRender * 0.5f));
    HeadPhenotype* phenotype = &anatomicalHead.phenotype;
    phenotype->skullWidth = adult ? 0.70f : 0.62f;
    phenotype->skullHeight = adult ? 0.30f : 0.24f;
    phenotype->skullLength = adult ? 0.64f : 0.52f;
    phenotype->muzzleLength = adult ? 0.84f : 0.58f;
    phenotype->muzzleWidth = adult ? 0.72f : 0.65f;
    phenotype->muzzleTaper = adult ? 0.38f : 0.30f;
    phenotype->eyeSize = adult ? 0.46f : 0.66f;
    phenotype->eyeLaterality = 0.92f;
    phenotype->eyeForwardness = adult ? 0.16f : 0.22f;
    phenotype->jawLength = adult ? 0.88f : 0.62f;
    phenotype->jawDepth = adult ? 0.40f : 0.28f;
    phenotype->jawStrength = adult ? 0.66f : 0.32f;
    phenotype->cheekMass = adult ? 0.38f : 0.24f;
    phenotype->nostrilPosition = 0.88f;
    HeadPhenotype_Normalize(phenotype);
    Monster_SetHead(&lizard, anatomicalHead);
    Monster_SetHeadOpenFactor(&lizard, adult ? 0.58f : 0.18f);

    for (size_t i = 0; i < lizard.eyeCount; ++i) {
        lizard.eyes[i].scleraColor = Color_FromRGB(214, 190, 78);
        lizard.eyes[i].pupilColor = Color_FromRGB(8, 12, 6);
        lizard.eyes[i].pupilScale = adult ? 0.38f : 0.46f;
    }
    if (lizard.mouthCount > 0) lizard.mouths[0].insideColor = Color_FromRGB(54, 8, 12);
    return lizard;
}

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    printf("========================================================\n");
    printf("   MONSTER ENGINE 3D: Demo SDF Transición (MonsterAger) \n");
    printf("========================================================\n");
    printf(" Controles:\n");
    printf("  - Flecha DERECHA / Flecha ARRIBA  : Avanzar edad (+ perc)\n");
    printf("  - Flecha IZQUIERDA / Flecha ABAJO : Retroceder edad (- perc)\n");
    printf("  - TECLA ESPACIO                   : Alternar animación automática\n");
    printf("========================================================\n");

    /* 1. Inicializar SDL2 y OpenGL */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("[ERROR] Fallo al inicializar SDL2: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

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
        printf("[ERROR] No se pudo crear el contexto OpenGL: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    SDL_GL_SetSwapInterval(1);

    /* 2. Configurar la cámara agnóstica 3D */
    ICamera camera;
    camera.position = Vec3_Create(0.0f, 6.2f, 14.0f);
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
    float ageFactor = 0.0f;
    bool autoAnimate = true;
    MonsterAger ager = MonsterAger_Create(&youngLizard, &adultLizard, ageFactor);

    MonsterVisualAsyncConfig asyncCfg = MonsterVisualAsync_DefaultConfig();
    MonsterVisualAsync* visual = MonsterVisualAsync_Create(asyncCfg);

    /* 6. Bucle de Renderizado 3D */
    bool running = true;
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
                    case SDLK_RIGHT:
                    case SDLK_UP:
                        autoAnimate = false;
                        ageFactor += 0.05f;
                        if (ageFactor > 1.0f) ageFactor = 1.0f;
                        MonsterAger_SetPerc(&ager, ageFactor);
                        printf("[AGER] Porcentaje manual: %.0f%%\n", ageFactor * 100.0f);
                        break;
                    case SDLK_LEFT:
                    case SDLK_DOWN:
                        autoAnimate = false;
                        ageFactor -= 0.05f;
                        if (ageFactor < 0.0f) ageFactor = 0.0f;
                        MonsterAger_SetPerc(&ager, ageFactor);
                        printf("[AGER] Porcentaje manual: %.0f%%\n", ageFactor * 100.0f);
                        break;
                    case SDLK_SPACE:
                        autoAnimate = !autoAnimate;
                        printf("[AGER] Animación automática: %s\n", autoAnimate ? "ACTIVADA" : "DESACTIVADA");
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
            static float timeAcc = 0.0f;
            timeAcc += deltaTime * 0.8f;
            float newFactor = (sinf(timeAcc) + 1.0f) * 0.5f;
            if (fabsf(newFactor - ageFactor) > 0.01f) {
                ageFactor = newFactor;
                MonsterAger_SetPerc(&ager, ageFactor);
            }
        }

        cameraTime += deltaTime;
        float cameraAngle = sinf(cameraTime * 0.28f) * 0.48f;
        float camRadius = 15.5f;
        camera.position.x = sinf(cameraAngle) * camRadius;
        camera.position.z = cosf(cameraAngle) * camRadius - 6.0f;

        const Monster* currentMonster = MonsterAger_GetResultConst(&ager);
        MonsterVisualAsync_Update(visual, currentMonster, deltaTime);

        renderer.beginFrame(&renderer);
        OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);

        MonsterVisualAsync_Render(visual, &renderer);

        renderer.endFrame(&renderer);
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
    return 0;
}
