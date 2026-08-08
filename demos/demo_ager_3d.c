/**
 * @file demo_ager_3d.c
 * @brief Demo visual 3D interactiva en OpenGL con SDL2 para mostrar la transición de envejecimiento/evolución (MonsterAger) de un Lagarto.
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

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    printf("========================================================\n");
    printf("   MONSTER ENGINE 3D: Demo de Transición (MonsterAger)  \n");
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
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);

    int windowWidth = 1024;
    int windowHeight = 768;

    SDL_Window* window = SDL_CreateWindow(
        "Monster Engine - Demo Transición/Envejecimiento (MonsterAger)",
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

    MonsterRenderer renderer = OpenGLRenderer_Create(&camera);
    OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);

    /* 3. Crear FASE 1: Lagarto Joven (Pequeño, Verde Esmeralda, 3 secciones) */
    Monster youngLizard = Monster_Create();
    Monster_Init(&youngLizard);
    youngLizard.colorPalette = ColorPalette_CreateGradient(Color_FromRGB(40, 200, 80), Color_FromRGB(100, 230, 120), 4);

    BodyPart* headY = Monster_GetHead(&youngLizard);
    if (headY) {
        headY->width = 1.0f; headY->height = 0.8f; headY->length = 1.2f;
    }
    BodyPart chestY = BodyPart_Create(0.0f, 0.0f, -1.3f, 1.2f, 1.4f, 0.9f, 0.0f);
    BodyPart tailY  = BodyPart_Create(0.0f, 0.0f, -2.8f, 0.6f, 1.5f, 0.5f, 0.0f);
    /* Añadir 2 Ojos al Lagarto Joven */
    Eye youngEyeL = Eye_Create(0, Vec3_Create(-0.55f, 0.3f, 0.4f), Vec3_Create(0.35f, 0.35f, 0.35f), COLOR_WHITE, COLOR_BLUE);
    Eye youngEyeR = Eye_Create(0, Vec3_Create(0.55f, 0.3f, 0.4f), Vec3_Create(0.35f, 0.35f, 0.35f), COLOR_WHITE, COLOR_BLUE);
    /* Añadir Boca rasa al Lagarto Joven */
    Mouth youngMouth = Mouth_Create(0, Vec3_Create(0.0f, -0.15f, 0.48f), Vec3_Create(0.6f, 0.3f, 0.5f), Color_FromRGB(100, 10, 10), COLOR_TRANSPARENT);
    youngMouth.openFactor = 0.3f;
    Monster_AddMouth(&youngLizard, youngMouth);

    /* 4. Crear FASE 2: Lagarto Alfa/Adulto (Gigante, Carmesí/Dorado, 5 secciones) */
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

    /* Añadir 3 Ojos al Lagarto Alfa (Incluyendo Tercer Ojo Místico Rojo en la Frente) */
    Eye adultEyeL = Eye_Create(0, Vec3_Create(-1.2f, 0.6f, 0.8f), Vec3_Create(0.7f, 0.7f, 0.7f), Color_FromRGB(255, 230, 100), Color_FromRGB(200, 0, 0));
    Eye adultEyeR = Eye_Create(0, Vec3_Create(1.2f, 0.6f, 0.8f), Vec3_Create(0.7f, 0.7f, 0.7f), Color_FromRGB(255, 230, 100), Color_FromRGB(200, 0, 0));
    Eye adultEyeC = Eye_Create(0, Vec3_Create(0.0f, 1.2f, 0.2f), Vec3_Create(0.9f, 0.9f, 0.9f), Color_FromRGB(30, 30, 30), Color_FromRGB(255, 50, 0));

    Monster_AddEye(&adultLizard, adultEyeL);
    Monster_AddEye(&adultLizard, adultEyeR);
    Monster_AddEye(&adultLizard, adultEyeC);

    /* Añadir Boca Enorme Feroz rasa al Lagarto Alfa */
    Mouth adultMouth = Mouth_Create(0, Vec3_Create(0.0f, -0.3f, 1.2f), Vec3_Create(1.6f, 0.9f, 1.2f), Color_FromRGB(60, 0, 0), COLOR_TRANSPARENT);
    adultMouth.openFactor = 0.95f; // Mandíbula rugiendo totalmente abierta
    Monster_AddMouth(&adultLizard, adultMouth);
    adultMouth.openFactor = 0.95f; // Mandíbula rugiendo totalmente abierta
    Monster_AddMouth(&adultLizard, adultMouth);
    adultMouth.openFactor = 0.95f; // Mandíbula rugiendo totalmente abierta
    Monster_AddMouth(&adultLizard, adultMouth);

    /* 5. Inicializar MonsterAger con las dos fases */
    float ageFactor = 0.0f;
    bool autoAnimate = true;
    MonsterAger ager = MonsterAger_Create(&youngLizard, &adultLizard, ageFactor);

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

        /* Si la animación automática está activa, oscilar senoidalmente entre 0% y 100% */
        if (autoAnimate) {
            static float timeAcc = 0.0f;
            timeAcc += deltaTime * 0.8f;
            ageFactor = (sinf(timeAcc) + 1.0f) * 0.5f; // Oscilar entre 0.0 y 1.0
            MonsterAger_SetPerc(&ager, ageFactor);
        }

        /* Animar cámara orbital */
        cameraAngle += deltaTime * 0.4f;
        float camRadius = 18.0f;
        camera.position.x = sinf(cameraAngle) * camRadius;
        camera.position.z = cosf(cameraAngle) * camRadius - 6.0f;

        /* Obtener el monstruo mezclado resultante y actualizarlo */
        Monster* currentMonster = (Monster*)MonsterAger_GetResult(&ager);
        Monster_Update(currentMonster, deltaTime);
        Monster_RenderUpdate(currentMonster, deltaTime, 1.0);

        /* Renderizar escena 3D */
        renderer.beginFrame(&renderer);
        OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);
        Monster_Render(currentMonster, &renderer, &camera, deltaTime, 0);
        renderer.endFrame(&renderer);

        SDL_GL_SwapWindow(window);
    }

    /* Limpieza */
    MonsterAger_Free(&ager);
    Monster_Free(&youngLizard);
    Monster_Free(&adultLizard);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    return 0;
}
