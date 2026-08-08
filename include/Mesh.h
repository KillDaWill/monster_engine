/**
 * @file Mesh.h
 * @brief Estructura de datos genérica para mallas 3D compuestas de vértices e índices.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_MESH_H
#define MONSTER_MESH_H

#include "Vector.h"
#include "Color.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct MeshVertex
 * @brief Atributos de un vértice 3D (posición, normal y color RGBA).
 */
typedef struct MeshVertex {
    Vector3 position; /**< Posición en el espacio 3D */
    Vector3 normal;   /**< Vector normal unitario */
    Color color;      /**< Color RGBA */
} MeshVertex;

/**
 * @struct Mesh
 * @brief Representación desacoplada de una malla poligonal 3D indexada.
 */
typedef struct Mesh {
    MeshVertex* vertices;  /**< Arreglo dinámico de vértices */
    size_t vertexCount;    /**< Cantidad actual de vértices */
    size_t vertexCapacity; /**< Capacidad reservada de vértices */

    unsigned int* indices; /**< Arreglo dinámico de índices de triángulos */
    size_t indexCount;     /**< Cantidad actual de índices (3 por triángulo) */
    size_t indexCapacity;  /**< Capacidad reservada de índices */
} Mesh;

/**
 * @brief Crea e inicializa una estructura Mesh vacía.
 */
Mesh Mesh_Create(void);

/**
 * @brief Libera la memoria asignada para los arreglos de vértices e índices.
 */
void Mesh_Free(Mesh* mesh);

/**
 * @brief Resetea el conteo de vértices e índices a 0 sin liberar la capacidad reservada.
 */
void Mesh_Clear(Mesh* mesh);

/**
 * @brief Reserva capacidad anticipada para vértices.
 */
bool Mesh_ReserveVertices(Mesh* mesh, size_t capacity);

/**
 * @brief Reserva capacidad anticipada para índices.
 */
bool Mesh_ReserveIndices(Mesh* mesh, size_t capacity);

/**
 * @brief Añade un vértice a la malla y retorna su índice.
 */
size_t Mesh_AddVertex(Mesh* mesh, MeshVertex vertex);

/**
 * @brief Añade un triángulo a la malla dado un trío de índices de vértices.
 */
bool Mesh_AddTriangle(Mesh* mesh, unsigned int a, unsigned int b, unsigned int c);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_MESH_H
