/**
 * @file RenderInterfaces.h
 * @brief Interfaces agnósticas de renderizado y cámara para Monster Engine (SDF Mesh Pipeline).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_RENDER_INTERFACES_H
#define MONSTER_RENDER_INTERFACES_H

#include "Vector.h"
#include "Color.h"
#include "Mesh.h"

#ifdef __cplusplus
extern "C" {
#endif

struct Monster;

/**
 * @struct ICamera
 * @brief Interfaz agnóstica de cámara 3D.
 */
typedef struct ICamera {
    Vector3 position;
    Vector3 target;
    Vector3 up;
    float fov;
    float aspectRatio;
    float nearPlane;
    float farPlane;
} ICamera;

/**
 * @struct MonsterRenderer
 * @brief Interfaz agnóstica de renderizador (Patrón VTable).
 */
typedef struct MonsterRenderer {
    void* user_data; /**< Contexto gráfico nativo (ej. OpenGL, Vulkan) */

    /** Callback para iniciar el frame/limpiar buffer */
    void (*beginFrame)(struct MonsterRenderer* self);

    /** Callback para finalizar el frame y presentar/swap buffers */
    void (*endFrame)(struct MonsterRenderer* self);

    /** Callback para renderizar una malla 3D (Mesh) */
    void (*renderMesh)(struct MonsterRenderer* self, const Mesh* mesh);
} MonsterRenderer;

#ifdef __cplusplus
}
#endif

#endif // MONSTER_RENDER_INTERFACES_H
