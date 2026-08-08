/**
 * @file Vector.h
 * @brief Módulo de álxebra vectorial de dimensiones fijas (2D, 3D, 4D) y N-Dimensional.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_VECTOR_H
#define MONSTER_VECTOR_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct Vector2
 * @brief Vector bidimensional (2D) con componentes flotantes.
 */
typedef struct {
    float x; /**< Componente X */
    float y; /**< Componente Y */
} Vector2;

/**
 * @struct Vector3
 * @brief Vector tridimensional (3D) con componentes flotantes.
 */
typedef struct {
    float x; /**< Componente X */
    float y; /**< Componente Y */
    float z; /**< Componente Z */
} Vector3;

/**
 * @struct Vector4
 * @brief Vector tetradimensional (4D) con componentes flotantes.
 */
typedef struct {
    float x; /**< Componente X */
    float y; /**< Componente Y */
    float z; /**< Componente Z */
    float w; /**< Componente W */
} Vector4;

/* --- Operaciones Vector2 --- */

/** @brief Crea un Vector2. */
Vector2 Vec2_Create(float x, float y);
/** @brief Retorna un Vector2 con valores (0, 0). */
Vector2 Vec2_Zero(void);
/** @brief Retorna un Vector2 con valores (1, 1). */
Vector2 Vec2_One(void);
/** @brief Suma dos vectores 2D. */
Vector2 Vec2_Add(Vector2 a, Vector2 b);
/** @brief Resta dos vectores 2D. */
Vector2 Vec2_Sub(Vector2 a, Vector2 b);
/** @brief Multiplica un Vector2 por un escalar. */
Vector2 Vec2_Scale(Vector2 v, float scalar);
/** @brief Divide un Vector2 por un escalar. */
Vector2 Vec2_Div(Vector2 v, float scalar);
/** @brief Producto punto entre dos vectores 2D. */
float   Vec2_Dot(Vector2 a, Vector2 b);
/** @brief Producto cruz escalar en 2D (a.x * b.y - a.y * b.x). */
float   Vec2_Cross(Vector2 a, Vector2 b);
/** @brief Cuadrado de la longitud de un Vector2. */
float   Vec2_LengthSq(Vector2 v);
/** @brief Longitud / Magnitud de un Vector2. */
float   Vec2_Length(Vector2 v);
/** @brief Retorna el vector unitario 2D normalizado. */
Vector2 Vec2_Normalize(Vector2 v);
/** @brief Calcula la distancia entre dos puntos 2D. */
float   Vec2_Distance(Vector2 a, Vector2 b);
/** @brief Interpolación lineal entre dos vectores 2D. */
Vector2 Vec2_Lerp(Vector2 a, Vector2 b, float t);

/* --- Operaciones Vector3 --- */

/** @brief Crea un Vector3. */
Vector3 Vec3_Create(float x, float y, float z);
/** @brief Retorna un Vector3 con valores (0, 0, 0). */
Vector3 Vec3_Zero(void);
/** @brief Retorna un Vector3 con valores (1, 1, 1). */
Vector3 Vec3_One(void);
/** @brief Suma dos vectores 3D. */
Vector3 Vec3_Add(Vector3 a, Vector3 b);
/** @brief Resta dos vectores 3D. */
Vector3 Vec3_Sub(Vector3 a, Vector3 b);
/** @brief Multiplica un Vector3 por un escalar. */
Vector3 Vec3_Scale(Vector3 v, float scalar);
/** @brief Divide un Vector3 por un escalar. */
Vector3 Vec3_Div(Vector3 v, float scalar);
/** @brief Producto punto entre dos vectores 3D. */
float   Vec3_Dot(Vector3 a, Vector3 b);
/** @brief Producto cruz tridimensional entre dos vectores 3D. */
Vector3 Vec3_Cross(Vector3 a, Vector3 b);
/** @brief Cuadrado de la longitud de un Vector3. */
float   Vec3_LengthSq(Vector3 v);
/** @brief Longitud / Magnitud de un Vector3. */
float   Vec3_Length(Vector3 v);
/** @brief Retorna el vector unitario 3D normalizado. */
Vector3 Vec3_Normalize(Vector3 v);
/** @brief Calcula la distancia entre dos puntos 3D. */
float   Vec3_Distance(Vector3 a, Vector3 b);
/** @brief Interpolación lineal entre dos vectores 3D. */
Vector3 Vec3_Lerp(Vector3 a, Vector3 b, float t);

/* --- Operaciones Vector4 --- */

/** @brief Crea un Vector4. */
Vector4 Vec4_Create(float x, float y, float z, float w);
/** @brief Retorna un Vector4 nulo. */
Vector4 Vec4_Zero(void);
/** @brief Retorna un Vector4 unitario. */
Vector4 Vec4_One(void);
/** @brief Suma dos vectores 4D. */
Vector4 Vec4_Add(Vector4 a, Vector4 b);
/** @brief Resta dos vectores 4D. */
Vector4 Vec4_Sub(Vector4 a, Vector4 b);
/** @brief Escalado de un Vector4. */
Vector4 Vec4_Scale(Vector4 v, float scalar);
/** @brief División de un Vector4. */
Vector4 Vec4_Div(Vector4 v, float scalar);
/** @brief Producto punto entre dos vectores 4D. */
float   Vec4_Dot(Vector4 a, Vector4 b);
/** @brief Cuadrado de la longitud de un Vector4. */
float   Vec4_LengthSq(Vector4 v);
/** @brief Magnitud de un Vector4. */
float   Vec4_Length(Vector4 v);
/** @brief Normalización de un Vector4. */
Vector4 Vec4_Normalize(Vector4 v);
/** @brief Interpolación lineal entre dos vectores 4D. */
Vector4 Vec4_Lerp(Vector4 a, Vector4 b, float t);

/* =========================================================================
 * Vector Genérico N-Dimensional (VectorND)
 * ========================================================================= */

/**
 * @struct VectorND
 * @brief Vector dinámico con dimensión N arbitraria especificada en tiempo de ejecución.
 */
typedef struct {
    float* data; /**< Apuntador al arreglo dinámico de componentes */
    size_t dim;  /**< Dimensión N del vector */
} VectorND;

/**
 * @brief Crea un VectorND asignando memoria para la dimensión dada.
 * @param dim Dimensión del vector.
 * @return Estructura VectorND inicializada a cero.
 */
VectorND VecND_Create(size_t dim);

/**
 * @brief Crea un VectorND copiando los datos de un buffer flotante inicial.
 * @param dim Dimensión N.
 * @param data Puntero al arreglo contiguo de floats.
 * @return VectorND inicializado.
 */
VectorND VecND_FromData(size_t dim, const float* data);

/**
 * @brief Libera la memoria asignada dinámicamente para un VectorND.
 * @param v Puntero al VectorND.
 */
void VecND_Free(VectorND* v);

/**
 * @brief Realiza una copia independiente de un VectorND.
 * @param v Puntero al vector original.
 * @return Nuevo VectorND duplicado.
 */
VectorND VecND_Copy(const VectorND* v);

/**
 * @brief Obtiene el valor en un índice del VectorND con comprobación de límites.
 * @param v Puntero al vector.
 * @param index Índice de la componente.
 * @return Valor en el índice o 0.0f si el índice es inválido.
 */
float VecND_Get(const VectorND* v, size_t index);

/**
 * @brief Estipula el valor en un índice del VectorND.
 * @param v Puntero al vector.
 * @param index Índice de la componente.
 * @param value Nuevo valor.
 */
void VecND_Set(VectorND* v, size_t index, float value);

/**
 * @brief Suma dos vectores N-Dimensionales de la misma dimensión.
 * @param a Primer operando.
 * @param b Segundo operando.
 * @param out Vector de salida donde se guardará el resultado.
 * @return true si las dimensiones coinciden y fue exitoso, false en caso contrario.
 */
bool VecND_Add(const VectorND* a, const VectorND* b, VectorND* out);

/**
 * @brief Resta dos vectores N-Dimensionales.
 * @param a Minuendo.
 * @param b Sustraendo.
 * @param out Vector de salida.
 * @return true si la operación se realizó correctamente.
 */
bool VecND_Sub(const VectorND* a, const VectorND* b, VectorND* out);

/**
 * @brief Multiplica un VectorND por un escalar.
 * @param v Vector de entrada.
 * @param scalar Escalar.
 * @param out Vector de salida.
 * @return true si las dimensiones coinciden.
 */
bool VecND_Scale(const VectorND* v, float scalar, VectorND* out);

/**
 * @brief Calcula el producto punto de dos vectores N-Dimensionales.
 * @param a Vector A.
 * @param b Vector B.
 * @return Resultado del producto punto.
 */
float VecND_Dot(const VectorND* a, const VectorND* b);

/**
 * @brief Calcula el cuadrado de la magnitud de un VectorND.
 * @param v Vector.
 * @return Magnitud al cuadrado.
 */
float VecND_LengthSq(const VectorND* v);

/**
 * @brief Calcula la magnitud de un VectorND.
 * @param v Vector.
 * @return Magnitud/Longitud flotante.
 */
float VecND_Length(const VectorND* v);

/**
 * @brief Normaliza un VectorND de forma segura.
 * @param v Vector de entrada.
 * @param out Vector de salida normalizado.
 * @return true en caso de éxito.
 */
bool VecND_Normalize(const VectorND* v, VectorND* out);

/**
 * @brief Calcula la distancia entre dos vectores N-Dimensionales.
 * @param a Punto A.
 * @param b Punto B.
 * @return Distancia euclidiana.
 */
float VecND_Distance(const VectorND* a, const VectorND* b);

/**
 * @brief Interpolación lineal entre dos vectores N-Dimensionales.
 * @param a Vector inicial.
 * @param b Vector final.
 * @param t Factor de interpolación (0.0 a 1.0).
 * @param out Vector de salida interpolado.
 * @return true si las dimensiones coinciden.
 */
bool VecND_Lerp(const VectorND* a, const VectorND* b, float t, VectorND* out);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_VECTOR_H
