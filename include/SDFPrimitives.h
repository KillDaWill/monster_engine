/**
 * @file SDFPrimitives.h
 * @brief Primitivas geométricas continuas representadas mediante Signed Distance Fields (SDF).
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_SDF_PRIMITIVES_H
#define MONSTER_SDF_PRIMITIVES_H

#include "Vector.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Estación de barrido sobre Z con sección elíptica y derivadas por unidad Z. */
typedef struct SDFSweepStation {
    Vector3 center; /**< Centro de la sección. */
    float width, height; /**< Semiejes lateral y vertical. */
    float widthSlope, heightSlope, centerSlope; /**< Tangentes de Hermite. */
} SDFSweepStation;

/** @brief Evalúa un barrido de estaciones ordenadas de mayor a menor Z, cerrado en extremos. */
float SDF_EllipticalSweepZ(Vector3 p, const SDFSweepStation* stations, int count);
/** @brief Valida y resuelve tangentes que preservan monotonía entre estaciones. */
bool SDF_SweepResolveTangents(SDFSweepStation* stations,int count);


/** @brief Pinna triangular redondeada orientada; campo conservador 1-Lipschitz.
 * @param point Posición relativa al centro del pabellón.
 * @param shape Semianchura basal, altura, semiespesor.
 * @param direction Eje unitario de base a punta.
 * @param tipFraction Anchura distal relativa.
 * @param concavity Profundidad de concha normalizada. @return Distancia conservadora. */
float SDF_TaperedPinna(Vector3 point, Vector3 shape, Vector3 direction, float tipFraction, float concavity);
/** @brief Lámina auricular curva; los tres ejes ortonormales vienen de la anatomía resuelta. */
float SDF_CurvedPinna(Vector3 point,Vector3 shape,Vector3 up,Vector3 side,Vector3 normal,
                      float tipFraction,float concavity,float longitudinalCurve,float rootRoll,
                      float tipRoundness,float fold,float rootFlare,float marginBow,float marginAsymmetry);

/**
 * @brief SDF de una esfera centrada en el origen.
 */
float SDF_Sphere(Vector3 point, float radius);

/**
 * @brief SDF aproximado de un elipsoide centrado en el origen con radios (r.x, r.y, r.z).
 */
float SDF_Ellipsoid(Vector3 point, Vector3 radii);

/**
 * @brief SDF de una cápsula (cilindro con tapas esféricas) entre los puntos a y b.
 */
float SDF_Capsule(Vector3 point, Vector3 a, Vector3 b, float radius);

/**
 * @brief SDF aproximado de una cápsula cónica (cono redondeado) entre los puntos a y b con radios r1 y r2.
 */
float SDF_TaperedCapsuleApprox(Vector3 point, Vector3 a, Vector3 b, float r1, float r2);

/**
 * @brief Cápsula elíptica y ahusada entre dos centros.
 * @param point Punto a evaluar.
 * @param a Centro del extremo raíz.
 * @param b Centro del extremo distal.
 * @param radiiA Radios elípticos en la raíz.
 * @param radiiB Radios elípticos en el extremo distal.
 * @return Distancia firmada aproximada, estable incluso para un segmento degenerado.
 */
float SDF_TaperedEllipticalCapsuleApprox(Vector3 point, Vector3 a, Vector3 b,
                                         Vector3 radiiA, Vector3 radiiB);

/**
 * @brief Loft elíptico aproximado de tres secciones con perfiles independientes.
 * @param point Punto a evaluar.
 * @param root Centro de la sección preorbital.
 * @param mid Centro de la sección nasal.
 * @param tip Centro de la sección premaxilar.
 * @param rootRadii Radios de la sección preorbital.
 * @param midRadii Radios de la sección nasal.
 * @param tipRadii Radios de la sección premaxilar.
 * @param blend Suavidad local de la unión entre ambos tramos.
 * @return Campo firmado aproximado cuya superficie cero interpola las secciones.
 * @note Prioriza una superficie cero y gradientes estables para polygonización;
 *       no constituye una distancia euclídea exacta lejos de la superficie.
 */
float SDF_ThreeSectionEllipticalLoftApprox(Vector3 point,
                                           Vector3 root, Vector3 mid, Vector3 tip,
                                           Vector3 rootRadii, Vector3 midRadii,
                                           Vector3 tipRadii, float blend);

/**
 * @brief Segmento ahusado con sección elíptica orientada por su eje.
 * @param point Punto a evaluar.
 * @param a Centro de la sección raíz.
 * @param b Centro de la sección distal.
 * @param widthA Radio transversal en la raíz.
 * @param heightA Radio vertical en la raíz.
 * @param widthB Radio transversal distal.
 * @param heightB Radio vertical distal.
 */
float SDF_TaperedEllipticalSegmentApprox(Vector3 point, Vector3 a, Vector3 b,
                                         float widthA, float heightA,
                                         float widthB, float heightB);

/**
 * @brief Cuña/frustum redondeado y ahusado alineado entre raíz y extremo.
 * @param point Punto a evaluar.
 * @param root Centro de la cara raíz.
 * @param tip Centro de la cara distal.
 * @param rootHalfWidth Semianchura en la raíz.
 * @param tipHalfWidth Semianchura distal.
 * @param rootHalfHeight Semialtura en la raíz.
 * @param tipHalfHeight Semialtura distal.
 * @param rounding Radio de redondeo de caras y tapas.
 * @return Distancia firmada aproximada a la cuña.
 * @note Está destinada a volúmenes con caras dorsal/ventral y extremos poco bulbosos.
 */
float SDF_RoundedTaperedWedge(Vector3 point, Vector3 root, Vector3 tip,
                             float rootHalfWidth, float tipHalfWidth,
                             float rootHalfHeight, float tipHalfHeight,
                             float rounding);

/**
 * @brief SDF de una caja 3D orientada a los ejes centrada en el origen con mitades de dimensión b.
 */
float SDF_Box(Vector3 point, Vector3 halfExtents);

/**
 * @brief SDF de una ranura o cápsula 2D extruida en Z (sección XY horizontal redondeada).
 * @param point Punto a evaluar en espacio local.
 * @param halfWidth Mitad del ancho total en X.
 * @param halfHeight Mitad de la altura total en Y (define el radio de curvatura).
 * @param halfDepth Mitad de la profundidad de extrusión en Z.
 */
float SDF_RoundedSlotExtruded(Vector3 point, float halfWidth, float halfHeight, float halfDepth);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_SDF_PRIMITIVES_H
