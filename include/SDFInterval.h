/** @file SDFInterval.h
 * @brief Intervalos de recetas escalares; conservan incertidumbre y redondeo exterior.
 */
#ifndef MONSTER_SDF_INTERVAL_H
#define MONSTER_SDF_INTERVAL_H
#include "AABB.h"
#include "SDFPrimitives.h"
/** @brief Cotas inclusivas; [-infinito,+infinito] representa un resultado desconocido. */
typedef struct SDFInterval {float lo,hi;} SDFInterval;
/** @brief Intervalo del mismo pabellón curvo, sin suponer derivadas acotadas.
 * @param bounds Caja de posiciones relativas al centro del pabellón.
 * @param shape Dimensiones resueltas del campo escalar.
 * @param up Eje longitudinal.
 * @param side Eje transversal.
 * @param normal Eje de espesor.
 * @param tipFraction Anchura distal relativa.
 * @param concavity Desarrollo de la concha.
 * @param curve Curvatura longitudinal.
 * @param rootRoll Enrollamiento basal.
 * @param tipRoundness Redondeo distal.
 * @param fold Pliegue longitudinal.
 * @param rootFlare Ensanchamiento basal.
 * @param marginBow Curvatura del margen.
 * @param marginAsymmetry Asimetría transversal.
 * @return Cotas del campo escalar, incluido redondeo exterior por operación.
 */
SDFInterval SDF_CurvedPinnaInterval(AABB3D bounds,Vector3 shape,Vector3 up,Vector3 side,Vector3 normal,
    float tipFraction,float concavity,float curve,float rootRoll,float tipRoundness,float fold,
    float rootFlare,float marginBow,float marginAsymmetry);
/** @brief Cotas de la receta elipsoidal, incluidas sus ramas junto al centro.
 * @param box Posiciones locales. @param radii Radios. @return Intervalo inclusivo.
 */
SDFInterval SDF_EllipsoidInterval(AABB3D box,Vector3 radii);
/** @brief Cotas de la cápsula escalar.
 * @param box Posiciones. @param a Inicio. @param b Final. @param radius Radio.
 * @return Intervalo inclusivo.
 */
SDFInterval SDF_CapsuleInterval(AABB3D box,Vector3 a,Vector3 b,float radius);
/** @brief Cotas de la entrada oral extruida.
 * @param box Posiciones locales. @param halfWidth Semiancho.
 * @param halfHeight Semialto. @param halfDepth Semiprofundidad.
 * @return Intervalo inclusivo.
 */
SDFInterval SDF_RoundedSlotInterval(AABB3D box,float halfWidth,float halfHeight,float halfDepth);
/** @brief Unión con el mismo orden aritmético del campo.
 * @param a Primer campo. @param b Segundo campo. @param k Suavidad.
 * @return Intervalo inclusivo.
 */
SDFInterval SDF_SmoothUnionInterval(SDFInterval a,SDFInterval b,float k);
/** @brief Resta con el mismo orden aritmético del campo.
 * @param body Campo exterior. @param cutter Campo restado. @param k Suavidad.
 * @return Intervalo inclusivo.
 */
SDFInterval SDF_SmoothSubtractInterval(SDFInterval body,SDFInterval cutter,float k);
/** @brief Cotas de un tramo del barrido; el llamador restringe la caja a su selección Z.
 * @param box Posiciones del tramo. @param centerA Centro inicial. @param centerB Centro final.
 * @param widthA Semiancho inicial. @param widthB Semiancho final.
 * @param heightA Semialto inicial. @param heightB Semialto final.
 * @param widthSlopeA Pendiente inicial del ancho. @param widthSlopeB Pendiente final del ancho.
 * @param heightSlopeA Pendiente inicial del alto. @param heightSlopeB Pendiente final del alto.
 * @param centerSlopeA Pendiente inicial del centro Y. @param centerSlopeB Pendiente final del centro Y.
 * @param firstZ Primera estación del barrido completo. @param lastZ Última estación.
 * @return Intervalo inclusivo; un span singular devuelve desconocido.
 */
SDFInterval SDF_SweepSegmentInterval(AABB3D box,Vector3 centerA,Vector3 centerB,
    float widthA,float widthB,float heightA,float heightB,float widthSlopeA,float widthSlopeB,
    float heightSlopeA,float heightSlopeB,float centerSlopeA,float centerSlopeB,float firstZ,float lastZ);
/** @brief Combina los tramos que puede seleccionar el evaluador escalar.
 * @param box Posiciones locales. @param stations Estaciones ordenadas por Z descendente.
 * @param count Número de estaciones. @return Intervalo inclusivo, desconocido ante datos inválidos.
 */
SDFInterval SDF_EllipticalSweepInterval(AABB3D box,const SDFSweepStation* stations,int count);
/** @brief Cotas de la receta de conector con ejes resueltos y perfil cuadrático.
 * @param box Posiciones mundiales. @param origin Origen del conector.
 * @param forward Eje longitudinal. @param side Eje lateral. @param up Eje vertical.
 * @param length Longitud. @param widthA Semiancho inicial. @param widthB Semiancho final.
 * @param heightA Semialto inicial. @param heightB Semialto final.
 * @param widthBulge Abombamiento lateral. @param heightBulge Abombamiento vertical.
 * @return Intervalo inclusivo, sin suponer una derivada acotada.
 */
SDFInterval SDF_ConnectorInterval(AABB3D box,Vector3 origin,Vector3 forward,Vector3 side,Vector3 up,
    float length,float widthA,float widthB,float heightA,float heightB,float widthBulge,float heightBulge);
/** @brief Intervalo del conector elíptico o superelíptico de casco, con la misma sección escalar. */
SDFInterval SDF_ConnectorPowerInterval(AABB3D box,Vector3 origin,Vector3 forward,Vector3 side,Vector3 up,
    float length,float widthA,float widthB,float heightA,float heightB,float widthBulge,float heightBulge,bool hoof);
#endif
