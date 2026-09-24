#include "Creature.h"
/**
 * @file main_lizard_viewer.c
 * @brief Aplicación de renderizado 3D en tiempo real con SDL2 + OpenGL (SDF Mesh Pipeline) para visualizar al monstruo Lagarto.
 * @author Monster Engine Team
 * @date 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <SDL2/SDL.h>
#include <GL/gl.h>

#include "Monster.h"
#include "ColorPalette.h"
#include "Vector.h"
#include "RenderInterfaces.h"
#include "OpenGLRenderer.h"
#include "MonsterVisualAsync.h"

int main(int argc, char* argv[]) {
    (void)argc; (void)argv;

    printf("========================================================\n");
    printf("     MONSTER ENGINE 3D: Visualizador SDF Lagarto (OpenGL)\n");
    printf("========================================================\n");

    /* 1. Inicializar SDL2 con soporte de Video OpenGL */
    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        printf("[ERROR] Fallo al inicializar SDL2: %s\n", SDL_GetError());
        return 1;
    }

    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    int windowWidth = 1024;
    int windowHeight = 768;

    SDL_Window* window = SDL_CreateWindow(
        "Monster Engine - Lagarto SDF 3D (OpenGL Asíncrono)",
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

    SDL_GL_SetSwapInterval(1); // VSync activado

    /* 2. Configurar la cámara agnóstica 3D */
    ICamera camera;
    camera.position = Vec3_Create(8.0f, 6.0f, 12.0f);
    camera.target = Vec3_Create(0.0f, 0.0f, -4.5f);
    camera.up = Vec3_Create(0.0f, 1.0f, 0.0f);
    camera.fov = 45.0f;
    camera.nearPlane = 0.1f;
    camera.farPlane = 100.0f;

    /* 3. Crear el Renderizador OpenGL agnóstico */
    Renderer3D renderer = OpenGLRenderer_Create(&camera);
    OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);

    /* 4. Construir al Monstruo Lagarto */
    Monster lizard=Monster_Create();
    CreaturePhenotype phenotype=CreatureRecipes_Lizard()->adult;
    if(!Creature_BuildMonster(&lizard,CreatureRecipes_Lizard(),&phenotype)) {
        fprintf(stderr,"[ERROR] No se pudo resolver el lagarto.\n");
        Monster_Free(&lizard);OpenGLRenderer_Destroy(&renderer);
        SDL_GL_DeleteContext(glContext);SDL_DestroyWindow(window);SDL_Quit();return 1;
    }
    Monster_SetHeadOpenFactor(&lizard,.10f);

    /* 5. Configurar el Orquestador Visual SDF Asíncrono */
    MonsterVisualAsyncConfig asyncCfg = MonsterVisualAsync_DefaultConfig();
    MonsterVisualAsync* visual = MonsterVisualAsync_Create(asyncCfg);

    /* 6. Bucle Principal */
    bool running = true;
    SDL_Event event;
    Uint32 lastTime = SDL_GetTicks();
    float rotationAngle = 0.0f;

    printf("[INFO] Ventana renderizada correctamente con malla SDF asíncrona. Rotando cámara...\n");

    while (running) {
        Uint32 currentTime = SDL_GetTicks();
        float deltaTime = (currentTime - lastTime) / 1000.0f;
        lastTime = currentTime;

        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_WINDOWEVENT) {
                if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                    windowWidth = event.window.data1;
                    windowHeight = event.window.data2;
                    OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);
                }
            }
        }

        rotationAngle += deltaTime * 0.5f;
        float radius = 14.0f;
        camera.position.x = sinf(rotationAngle) * radius;
        camera.position.z = cosf(rotationAngle) * radius - 4.5f;

        /* La inspección mantiene la pose anatómica; la locomoción es independiente. */
        MonsterVisualAsync_Update(visual, &lizard, deltaTime);

        renderer.beginFrame(&renderer);
        OpenGLRenderer_SetupCamera(&camera, windowWidth, windowHeight);

        MonsterVisualAsync_Render(visual, &renderer);

        renderer.endFrame(&renderer);
        SDL_GL_SwapWindow(window);
    }

    /* 7. Limpieza */
    MonsterVisualAsync_Free(visual);
    Monster_Free(&lizard);
    OpenGLRenderer_Destroy(&renderer);
    SDL_GL_DeleteContext(glContext);
    SDL_DestroyWindow(window);
    SDL_Quit();

    printf("[INFO] Aplicación finalizada limpiamente.\n");
    return 0;
}
