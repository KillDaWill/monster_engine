/**
 * @file ColorPalette.h
 * @brief Módulo para la creación y muestreo de paletas de colores.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_COLOR_PALETTE_H
#define MONSTER_COLOR_PALETTE_H

#include "Color.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Capacidad máxima de colores por paleta fija */
#define MAX_PALETTE_COLORS 256

/**
 * @struct ColorPalette
 * @brief Representación de una paleta de colores.
 */
typedef struct {
    Color colors[MAX_PALETTE_COLORS]; /**< Arreglo de colores pertenecientes a la paleta */
    size_t count;                     /**< Cantidad actual de colores en la paleta */
} ColorPalette;

/**
 * @brief Crea una nueva paleta de colores vacía.
 * @return Estructura ColorPalette inicializada.
 */
ColorPalette ColorPalette_Create(void);

/**
 * @brief Añade un nuevo color al final de la paleta.
 * @param palette Puntero a la paleta.
 * @param color Color a añadir.
 * @return true si se añadió con éxito, false si la paleta está llena.
 */
bool ColorPalette_AddColor(ColorPalette* palette, Color color);

/**
 * @brief Establece un color en un índice específico dentro de la paleta.
 * @param palette Puntero a la paleta.
 * @param index Índice del elemento a modificar.
 * @param color Nuevo color.
 * @return true si el índice es válido y se modificó, false en caso contrario.
 */
bool ColorPalette_SetColor(ColorPalette* palette, size_t index, Color color);

/**
 * @brief Obtiene un color de la paleta dado su índice.
 * @param palette Puntero a la paleta constante.
 * @param index Índice del color.
 * @return Color correspondiente o COLOR_BLACK si el índice está fuera de rango.
 */
Color ColorPalette_GetColor(const ColorPalette* palette, size_t index);

/**
 * @brief Obtiene el número total de colores en la paleta.
 * @param palette Puntero a la paleta constante.
 * @return Número de colores almacenados.
 */
size_t ColorPalette_GetCount(const ColorPalette* palette);

/**
 * @brief Muestra un color en la paleta interpolando de forma continua en el rango [0.0, 1.0].
 * @param palette Puntero a la paleta constante.
 * @param t Factor de posición en el gradiente de la paleta (0.0f a 1.0f).
 * @return Color interpolado en el gradiente de la paleta.
 */
Color ColorPalette_Sample(const ColorPalette* palette, float t);

/**
 * @brief Genera una paleta con un gradiente lineal uniforme entre dos colores.
 * @param start Color inicial.
 * @param end Color final.
 * @param steps Número de muestras/colores en la paleta.
 * @return Paleta con el gradiente generado.
 */
ColorPalette ColorPalette_CreateGradient(Color start, Color end, size_t steps);

/**
 * @brief Genera una paleta armónica desplazando el tono (Hue) en el espacio HSV.
 * @param baseColor Color inicial base.
 * @param angleStep Paso del ángulo de Hue en grados (0.0 a 360.0).
 * @param count Cantidad de colores armónicos a generar.
 * @return Paleta armónica generada.
 */
ColorPalette ColorPalette_CreateHarmonic(Color baseColor, float angleStep, size_t count);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_COLOR_PALETTE_H
