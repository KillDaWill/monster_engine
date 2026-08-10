/**
 * @file PrimitiveMesh.h
 * @brief Generador de mallas primitivas indexadas (esferas UV) para render y pruebas.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_PRIMITIVE_MESH_H
#define MONSTER_PRIMITIVE_MESH_H

#include "Mesh.h"
#include "Vector.h"
#include "Color.h"
#include "Transform3D.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Genera una esfera UV indexada (con polos) en la malla de salida.
 *
 * Cada vértice lleva normal unitaria radial, color uniforme y posición
 * centrada en `center` con radio `radius`. La malla de salida se vacía
 * (Mesh_Clear) antes de generar, conservando su capacidad reservada.
 *
 * Topología: `segments` vértices por anillo, `rings` bandas de latitud.
 * VertexCount = 2 + (rings - 1) * segments, indexCount = 6 * segments * (rings - 1).
 *
 * @param out Malla destino (no NULL).
 * @param center Centro de la esfera.
 * @param radius Radio de la esfera (debe ser > 0).
 * @param segments Segmentos alrededor del ecuador (mínimo 3).
 * @param rings Bandas de latitud entre polos (mínimo 2).
 * @param color Color RGBA aplicado a todos los vértices.
 * @return true si se generó la malla, false si los parámetros son inválidos o falló la memoria.
 */
bool PrimitiveMesh_GenerateUVSphere(Mesh* out, Vector3 center, float radius, unsigned int segments, unsigned int rings, Color color);

/**
 * @brief Genera una malla de elipsoide 3D transformada (rotación Euler + escala no uniforme).
 *
 * Transforma posiciones y normales de forma matemáticamente exacta para escalas no uniformes
 * (la normal se escala por el inverso de los radios antes de rotar).
 *
 * @param out Malla destino (no NULL).
 * @param transform Posición, rotación (grados Euler) y radios/escala de la elipse.
 * @param segments Segmentos ecuatoriales (mínimo 3).
 * @param rings Bandas de latitud (mínimo 2).
 * @param color Color RGBA.
 * @return true si se generó exitosamente.
 */
bool PrimitiveMesh_GenerateEllipsoid(Mesh* out, Transform3D transform, unsigned int segments, unsigned int rings, Color color);

/**
 * @brief Genera una malla de elipsoide 3D con opción de normales hacia el interior y bobinado invertido.
 * @param out Malla destino.
 * @param transform Posición, rotación y radios de la elipse.
 * @param segments Segmentos ecuatoriales (mínimo 3).
 * @param rings Bandas de latitud (mínimo 2).
 * @param color Color RGBA.
 * @param inwardFacing Si es true, las normales apuntan hacia el centro y los triángulos se orientan hacia adentro.
 * @return true si se generó exitosamente.
 */
bool PrimitiveMesh_GenerateEllipsoidEx(Mesh* out, Transform3D transform, unsigned int segments, unsigned int rings, Color color, bool inwardFacing);

/**
 * @brief Genera un tubo circular 3D a lo largo de una curva Bézier cuadrática P(t) = (1-t)^2 P0 + 2(1-t)t P1 + t^2 P2.
 * @param out Malla destino.
 * @param p0 Punto inicial de la curva.
 * @param p1 Punto de control de la curva.
 * @param p2 Punto final de la curva.
 * @param radius Radio del tubo circular.
 * @param curveSegments Número de segmentos a lo largo de la curva (mínimo 2).
 * @param radialSegments Número de segmentos alrededor de la sección transversal circular (mínimo 3).
 * @param color Color RGBA.
 * @return true si se generó exitosamente.
 */
bool PrimitiveMesh_GenerateQuadraticBezierTube(Mesh* out, Vector3 p0, Vector3 p1, Vector3 p2, float radius, int curveSegments, int radialSegments, Color color);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_PRIMITIVE_MESH_H
