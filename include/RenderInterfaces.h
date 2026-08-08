/**
 * @file RenderInterfaces.h
 * @brief Interfaces agnósticas de renderizado y cámara para Monster Engine.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_RENDER_INTERFACES_H
#define MONSTER_RENDER_INTERFACES_H

#include "Vector.h"
#include "Color.h"
#include "BodyPart.h"
#include "Eye.h"
#include "Mouth.h"

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
 * @brief Interfaz agnóstica de renderizador (Patrón VTable / Punteros a Funciones).
 */
typedef struct MonsterRenderer {
    void* user_data; /**< Puntero opcional al contexto gráfico nativo (OpenGL, Vulkan, etc.) */

    /** Callback para iniciar el frame/limpiar buffer */
    void (*beginFrame)(struct MonsterRenderer* self);

    /** Callback para finalizar el frame y presentar/swap buffers */
    void (*endFrame)(struct MonsterRenderer* self);

    /** Callback para renderizar una esfera 3D (nodo o articulación) */
    void (*renderSphere)(struct MonsterRenderer* self, Vector3 center, float radius, Color color);

    /** Callback para renderizar una cápsula o elipsoide de conexión entre partes */
    void (*renderCapsule)(struct MonsterRenderer* self, Vector3 p1, Vector3 p2, float r1, float r2, Color color);

    /** Callback para renderizar un ojo en 3D con posición, rotación, escala y colores */
    void (*renderEye)(struct MonsterRenderer* self, Vector3 pos, Vector3 rot, Vector3 scale, Color scleraColor, Color pupilColor, float pupilScale);

    /** Callback para renderizar una boca / cavidad bucal en 3D */
    void (*renderMouth)(struct MonsterRenderer* self, Vector3 pos, Vector3 rot, Vector3 scale, Color insideColor, Color lipColor, float openFactor);

    /** Callback agnóstico para dibujar la lista completa de partes del monstruo */
    void (*renderBodyParts)(struct MonsterRenderer* self, struct Monster* monster, const BodyPart* parts, size_t count);
} MonsterRenderer;

#ifdef __cplusplus
}
#endif

#endif // MONSTER_RENDER_INTERFACES_H
