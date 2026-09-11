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
#include "MonsterSDF.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Crea e inicializa una instancia de Renderer3D respaldada por el pipeline de OpenGL.
 * @param camera Puntero a la cámara 3D.
 * @return Estructura Renderer3D con sus callbacks apuntando a las funciones de OpenGL.
 */
Renderer3D OpenGLRenderer_Create(ICamera* camera);

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
 * @param renderer Puntero al renderizador 3D.
 * @param enabled true para wireframe, false para relleno.
 */
void OpenGLRenderer_SetWireframe(Renderer3D* renderer, bool enabled);

/**
 * @brief Guarda el framebuffer posterior como imagen PPM, invirtiendo el eje Y.
 * @param path Ruta de salida.
 * @param width Ancho del viewport.
 * @param height Alto del viewport.
 * @return true si se leyó y escribió la imagen completa.
 */
bool OpenGLRenderer_SavePPM(const char* path, int width, int height);

/**
 * @brief Libera los recursos del contexto del renderizador OpenGL y limpia sus callbacks.
 * @param renderer Puntero al renderizador 3D.
 */
void OpenGLRenderer_Destroy(Renderer3D* renderer);

/** @brief Dibuja directamente el campo compilado sin reconstruir triángulos.
 * @param renderer Backend OpenGL propietario de los recursos GPU.
 * @param sdf Campo compilado correspondiente al fotograma actual.
 * @param camera Cámara actual.
 * @param width Anchura del framebuffer.
 * @param height Altura del framebuffer.
 * @return false si el backend no admite GLSL 330 o falla la preparación.
 */
bool OpenGLRenderer_RenderSDF(Renderer3D* renderer,const MonsterSDF* sdf,
    const ICamera* camera,int width,int height);

/** @brief Inicia un fotograma SDF con resolución espacial adaptada al presupuesto GPU.
 * @param renderer Backend propietario del framebuffer y las consultas temporales.
 * @param camera Cámara actual; se conserva su proyección y relación de aspecto.
 * @param width Anchura de presentación.
 * @param height Altura de presentación.
 * @param adaptive Ajustar resolución para reservar 8 ms de trabajo GPU.
 * @return true si se preparó el framebuffer.
 */
bool OpenGLRenderer_BeginSDFFrame(Renderer3D* renderer,ICamera* camera,int width,int height,bool adaptive);
/** @brief Presenta el fotograma SDF y recoge tiempos GPU sin bloquear. */
void OpenGLRenderer_EndSDFFrame(Renderer3D* renderer,int width,int height);
/** @brief Última duración GPU completada, en milisegundos. */
float OpenGLRenderer_GetSDFGpuMs(const Renderer3D* renderer);
/** @brief Escala espacial actual del framebuffer SDF respecto a la ventana. */
float OpenGLRenderer_GetSDFResolutionScale(const Renderer3D* renderer);

/** @brief Compara en GPU y CPU puntos del último campo dibujado por RenderSDF.
 * @param renderer Backend que acaba de recibir el snapshot sdf.
 * @param sdf El mismo snapshot compilado, sin modificar desde RenderSDF.
 * @param points Puntos mundiales de comprobación.
 * @param count Cantidad de puntos, entre 1 y 4096.
 * @param maxError Salida: máximo error absoluto de distancia.
 * @return true si se ejecutó la lectura GPU; el llamador comprueba la tolerancia.
 */
bool OpenGLRenderer_ValidateSDF(Renderer3D* renderer,const MonsterSDF* sdf,
    const Vector3* points,size_t count,float* maxError);

/** @brief Espera a la GPU para medir el coste real en validaciones de rendimiento. */
void OpenGLRenderer_Finish(void);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_OPENGL_RENDERER_H
