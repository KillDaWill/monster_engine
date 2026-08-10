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
    camera.position = Vec3_Create(0.0f, 8.0f, 16.0f);
    camera.target = Vec3_Create(0.0f, 0.0f, -4.5f);
    camera.up = Vec3_Create(0.0f, 1.0f, 0.0f);
    camera.fov = 45.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    Renderer3D renderer = OpenGLRenderer_Create(&camera);
    OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);

    /* 3. Crear FASE 1: Lagarto Joven */
    Monster youngLizard = Monster_Create();
    Monster_Init(&youngLizard);
    youngLizard.colorPalette = ColorPalette_CreateGradient(Color_FromRGB(40, 200, 80), Color_FromRGB(100, 230, 120), 4);

    BodyPart* headY = Monster_GetHead(&youngLizard);
    if (headY) {
        headY->width = 1.0f; headY->height = 0.8f; headY->length = 1.2f;
    }
    BodyPart chestY = BodyPart_Create(0.0f, 0.0f, -1.3f, 1.2f, 1.4f, 0.9f, 0.0f);
    BodyPart tailY  = BodyPart_Create(0.0f, 0.0f, -2.8f, 0.6f, 1.5f, 0.5f, 0.0f);
    Monster_AddBodyPart(&youngLizard, chestY);
    Monster_AddBodyPart(&youngLizard, tailY);

    Mouth youngMouth = Mouth_Create(0, Vec3_Create(0.0f, -0.15f, 0.48f), Vec3_Create(0.6f, 0.3f, 0.5f), Color_FromRGB(100, 10, 10), Color_FromRGB(150, 40, 40));
    youngMouth.openFactor = 0.3f;
    Monster_AddMouth(&youngLizard, youngMouth);

    Eye youngLeftEye = Eye_Create(0, Vec3_Create(-0.28f, 0.12f, 0.45f), Vec3_Create(0.16f, 0.15f, 0.10f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    youngLeftEye.pupilScale = 0.4f;
    Monster_AddEye(&youngLizard, youngLeftEye);

    Eye youngRightEye = Eye_Create(0, Vec3_Create(0.28f, 0.12f, 0.45f), Vec3_Create(0.16f, 0.15f, 0.10f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    youngRightEye.pupilScale = 0.4f;
    Monster_AddEye(&youngLizard, youngRightEye);

    /* 4. Crear FASE 2: Lagarto Alfa */
    Monster adultLizard = Monster_Create();
    Monster_Init(&adultLizard);
    adultLizard.colorPalette = ColorPalette_CreateGradient(Color_FromRGB(220, 40, 40), Color_FromRGB(240, 180, 30), 4);

    BodyPart* headA = Monster_GetHead(&adultLizard);
    if (headA) {
        headA->width = 2.5f; headA->height = 1.8f; headA->length = 2.8f;
    }
    BodyPart chestA   = BodyPart_Create(0.0f, 0.0f, -3.0f, 3.2f, 3.5f, 2.2f, 0.0f);
    BodyPart abdomenA = BodyPart_Create(0.0f, 0.0f, -6.6f, 2.8f, 3.2f, 1.9f, 0.0f);
    BodyPart tail1A   = BodyPart_Create(0.0f, 0.0f, -10.0f, 1.8f, 3.0f, 1.4f, 0.0f);
    BodyPart tail2A   = BodyPart_Create(0.0f, 0.0f, -13.1f, 0.9f, 2.5f, 0.8f, 0.0f);

    Monster_AddBodyPart(&adultLizard, chestA);
    Monster_AddBodyPart(&adultLizard, abdomenA);
    Monster_AddBodyPart(&adultLizard, tail1A);
    Monster_AddBodyPart(&adultLizard, tail2A);

    Mouth adultMouth = Mouth_Create(0, Vec3_Create(0.0f, -0.3f, 1.2f), Vec3_Create(1.6f, 0.9f, 1.2f), Color_FromRGB(60, 0, 0), Color_FromRGB(200, 30, 30));
    adultMouth.openFactor = 0.95f;
    Monster_AddMouth(&adultLizard, adultMouth);

    Eye adultLeftEye = Eye_Create(0, Vec3_Create(-0.75f, 0.35f, 1.1f), Vec3_Create(0.42f, 0.4f, 0.25f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    adultLeftEye.pupilScale = 0.45f;
    Monster_AddEye(&adultLizard, adultLeftEye);

    Eye adultRightEye = Eye_Create(0, Vec3_Create(0.75f, 0.35f, 1.1f), Vec3_Create(0.42f, 0.4f, 0.25f), COLOR_WHITE, Color_FromRGB(20, 20, 20));
    adultRightEye.pupilScale = 0.45f;
    Monster_AddEye(&adultLizard, adultRightEye);

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
    float cameraAngle = 0.0f;

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

        cameraAngle += deltaTime * 0.4f;
        float camRadius = 18.0f;
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
