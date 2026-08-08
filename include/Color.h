/**
 * @file Color.h
 * @brief Módulo de gestión y manipulación de colores agnóstico del sistema de renderizado.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_COLOR_H
#define MONSTER_COLOR_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct Color
 * @brief Representación de un color en formato RGBA de 8 bits por canal (0-255).
 */
typedef struct {
    uint8_t r; /**< Canal Rojo (0-255) */
    uint8_t g; /**< Canal Verde (0-255) */
    uint8_t b; /**< Canal Azul (0-255) */
    uint8_t a; /**< Canal Alpha / Opacidad (0-255) */
} Color;

/**
 * @struct ColorHSV
 * @brief Representación de un color en el espacio HSV (Hue, Saturation, Value).
 */
typedef struct {
    float h; /**< Tono / Hue (0.0 - 360.0) */
    float s; /**< Saturación (0.0 - 1.0) */
    float v; /**< Valor / Brillo (0.0 - 1.0) */
    float a; /**< Opacidad (0.0 - 1.0) */
} ColorHSV;

/* Constantes de colores predefinidos */
extern const Color COLOR_WHITE;       /**< Blanco sólido (255, 255, 255, 255) */
extern const Color COLOR_BLACK;       /**< Negro sólido (0, 0, 0, 255) */
extern const Color COLOR_RED;         /**< Rojo puro (255, 0, 0, 255) */
extern const Color COLOR_GREEN;       /**< Verde puro (0, 255, 0, 255) */
extern const Color COLOR_BLUE;        /**< Azul puro (0, 0, 255, 255) */
extern const Color COLOR_YELLOW;      /**< Amarillo puro (255, 255, 0, 255) */
extern const Color COLOR_MAGENTA;     /**< Magenta puro (255, 0, 255, 255) */
extern const Color COLOR_CYAN;        /**< Cyan puro (0, 255, 255, 255) */
extern const Color COLOR_TRANSPARENT; /**< Transparente (0, 0, 0, 0) */

/**
 * @brief Crea una estructura Color a partir de valores RGBA de 8 bits.
 * @param r Componente roja.
 * @param g Componente verde.
 * @param b Componente azul.
 * @param a Componente alfa.
 * @return Estructura Color configurada.
 */
Color Color_Create(uint8_t r, uint8_t g, uint8_t b, uint8_t a);

/**
 * @brief Crea un color opaco (Alpha = 255) a partir de componentes RGB.
 * @param r Componente roja.
 * @param g Componente verde.
 * @param b Componente azul.
 * @return Estructura Color opaca.
 */
Color Color_FromRGB(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Crea una estructura Color a partir de un entero hexadecimal (0xRRGGBBAA).
 * @param hex Valor en formato 0xRRGGBBAA.
 * @return Estructura Color correspondiente.
 */
Color Color_FromHex(uint32_t hex);

/**
 * @brief Crea un color a partir de componentes flotantes normalizados (0.0f a 1.0f).
 * @param r Componente roja normalizada.
 * @param g Componente verde normalizada.
 * @param b Componente azul normalizada.
 * @param a Componente alfa normalizada.
 * @return Estructura Color adaptada a 8 bits.
 */
Color Color_FromNormalized(float r, float g, float b, float a);

/**
 * @brief Convierte un color RGBA al espacio de color HSV.
 * @param color Color en formato RGBA.
 * @return Color representado en el espacio HSV.
 */
ColorHSV Color_ToHSV(Color color);

/**
 * @brief Convierte un color HSV de vuelta a formato RGBA.
 * @param hsv Color en formato HSV.
 * @return Color representado en formato RGBA.
 */
Color Color_FromHSV(ColorHSV hsv);

/**
 * @brief Realiza una interpolación lineal suave entre dos colores.
 * @param start Color inicial (t = 0.0f).
 * @param end Color final (t = 1.0f).
 * @param t Factor de interpolación entre 0.0f y 1.0f.
 * @return Color interpolado resultante.
 */
Color Color_Lerp(Color start, Color end, float t);

/**
 * @brief Combina dos colores utilizando el algoritmo de Alpha Blending.
 * @param src Color fuente (capa superior).
 * @param dest Color destino (capa inferior).
 * @return Color mezclado resultante.
 */
Color Color_Blend(Color src, Color dest);

/**
 * @brief Multiplica los canales RGB de un color por un factor escalar.
 * @param color Color base.
 * @param factor Factor de escala de brillo/intensidad.
 * @return Color modificado.
 */
Color Color_Multiply(Color color, float factor);

/**
 * @brief Compara si dos colores son idénticos en todos sus canales.
 * @param a Primer color.
 * @param b Segundo color.
 * @return true si son iguales, false en caso contrario.
 */
bool Color_Equals(Color a, Color b);

/**
 * @brief Empaqueta el color en un entero de 32 bits en formato RGBA.
 * @param color Estructura color.
 * @return Valor entero 0xRRGGBBAA.
 */
uint32_t Color_ToRGBA(Color color);

/**
 * @brief Empaqueta el color en un entero de 32 bits en formato ARGB.
 * @param color Estructura color.
 * @return Valor entero 0xAARRGGBB.
 */
uint32_t Color_ToARGB(Color color);

/**
 * @brief Empaqueta el color en un entero de 32 bits en formato BGRA.
 * @param color Estructura color.
 * @return Valor entero 0xBBGGRRAA.
 */
uint32_t Color_ToBGRA(Color color);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_COLOR_H
