/**
 * @file OpenGLRenderer.h
 * @brief Implementación OpenGL del backend gráfico agnóstico MonsterRenderer.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_OPENGL_RENDERER_H
#define MONSTER_OPENGL_RENDERER_H

#include "RenderInterfaces.h"
#include "Mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Crea e inicializa una instancia de MonsterRenderer respaldada por el pipeline de OpenGL.
 * @param camera Puntero a la cámara 3D.
 * @return Estructura MonsterRenderer con sus callbacks apuntando a las funciones de OpenGL.
 */
MonsterRenderer OpenGLRenderer_Create(ICamera* camera);

/**
 * @brief Configura la matriz de perspectiva e iluminación en OpenGL.
 * @param camera Puntero a la cámara.
 * @param width Ancho del viewport.
 * @param height Alto del viewport.
 */
void OpenGLRenderer_SetupCamera(ICamera* camera, int width, int height);

/**
 * @brief Dibuja una malla 3D (Mesh) directamente mediante el pipeline de OpenGL.
 * @param mesh Puntero a la malla 3D a renderizar.
 */
void OpenGLRenderer_RenderMesh(const Mesh* mesh);

/**
 * @brief Activa/desactiva el modo wireframe (contornos de triángulos).
 *
 * El cambio se aplica solo durante el renderizado de mallas y se restaura el
 * modo de polígono previo (sin fuga de estado OpenGL).
 * @param enabled true para wireframe, false para relleno.
 */
void OpenGLRenderer_SetWireframe(bool enabled);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_OPENGL_RENDERER_H
