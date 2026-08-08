/**
 * @file OpenGLRenderer.h
 * @brief Implementación OpenGL del backend gráfico agnóstico MonsterRenderer.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_OPENGL_RENDERER_H
#define MONSTER_OPENGL_RENDERER_H

#include "RenderInterfaces.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Crea e inicializa una instancia de MonsterRenderer respaldada por OpenGL pipeline.
 * @param camera Puntero a la cámara 3D para configurar matrices de proyección y vista.
 * @return Estructura MonsterRenderer con sus callbacks apuntando a las funciones OpenGL.
 */
MonsterRenderer OpenGLRenderer_Create(ICamera* camera);

/**
 * @brief Configura la matriz de perspectiva e iluminación en OpenGL.
 * @param camera Puntero a la cámara.
 * @param width Ancho de la ventana/viewport.
 * @param height Alto de la ventana/viewport.
 */
void OpenGLRenderer_SetupCamera(ICamera* camera, int width, int height);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_OPENGL_RENDERER_H
