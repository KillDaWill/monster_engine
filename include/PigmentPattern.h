/**
 * @file PigmentPattern.h
 * @brief Patrones y capas procedimentales de pigmentación en el espacio de reposo anatómico.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_PIGMENT_PATTERN_H
#define MONSTER_PIGMENT_PATTERN_H

#include "Color.h"
#include "Vector.h"
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @enum PigmentPattern
 * @brief Algoritmos procedimentales de pigmentación biológica.
 */
typedef enum PigmentPattern {
    PIGMENT_PATTERN_SOLID = 0, /**< Cobertura cromática uniforme */
    PIGMENT_PATTERN_NOISE,     /**< Micro-variación celular continua */
    PIGMENT_PATTERN_SPOTS,     /**< Moteado disperso delimitado (leopardo/salamandra) */
    PIGMENT_PATTERN_BANDS,     /**< Franjas transversales al eje longitudinal (anillado) */
    PIGMENT_PATTERN_STRIPES,   /**< Rayas longitudinales paralelas al tronco/columna */
    PIGMENT_PATTERN_BLOTCHES,  /**< Manchas poligonales/irregulares amplias (gecko/pitón) */
    PIGMENT_PATTERN_OCELLI,    /**< Ocelos concéntricos (ojos dérmicos) */
    PIGMENT_PATTERN_GRADIENT   /**< Transición direccional continua */
} PigmentPattern;

/**
 * @struct PigmentLayer
 * @brief Capa compositiva apilable de pigmentación procedimental.
 */
typedef struct PigmentLayer {
    PigmentPattern pattern; /**< Tipo de distribución procedural */
    Color color;            /**< Color de la capa */
    float strength;         /**< Opacidad o factor de mezcla [0.0 - 1.0] */
    float scale;            /**< Frecuencia espacial del patrón */
    float sharpness;        /**< Nitidez o pendiente de transición de los bordes [0.0 - 1.0] */
    Vector3 direction;      /**< Vector directriz para gradientes y bandas */
    uint32_t seed;          /**< Semilla pseudoaleatoria determinista */
} PigmentLayer;

#define SURFACE_MAX_PIGMENT_LAYERS 4

/**
 * @struct PlatePhenotype
 * @brief Parámetros geométricos para el tegumento de placas óseas/escudos dérmicos.
 */
typedef struct PlatePhenotype {
    float size;          /**< Escala espacial de la placa */
    float aspect;        /**< Relación de aspecto longitudinal */
    float thickness;     /**< Altura o espesor del escudo */
    float bevel;         /**< Chaflán o biselado perimetral */
    float overlap;       /**< Solapamiento imbricado */
    float irregularity;  /**< Distorsión angular aleatoria */
    float relief;        /**< Relieve en la normal */
} PlatePhenotype;

/**
 * @brief Evalúa el valor escalar de un patrón procedimental en un punto 3D.
 * @param pattern Tipo de patrón.
 * @param p Coordenadas mundiales o locales de reposo.
 * @param scale Frecuencia del patrón.
 * @param sharpness Nitidez de borde [0, 1].
 * @param direction Vector de dirección orientativo.
 * @param seed Semilla del patrón.
 * @return Intensidad del patrón en [0.0, 1.0].
 */
float PigmentPattern_Sample(
    PigmentPattern pattern,
    Vector3 p,
    float scale,
    float sharpness,
    Vector3 direction,
    uint32_t seed);

/**
 * @brief Evalúa la mezcla de color de una capa sobre un color base existente.
 * @param layer Parámetros de la capa.
 * @param p Coordenadas en reposo.
 * @param baseColor Color anterior antes de aplicar la capa.
 * @return Color resultante tras la aplicación de la capa.
 */
Color PigmentPattern_EvaluateLayer(
    const PigmentLayer* layer,
    Vector3 p,
    Color baseColor);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_PIGMENT_PATTERN_H */
