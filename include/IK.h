/** @file IK.h
 * @brief FABRIK genérico con proyección articular y longitudes locales constantes.
 */
#ifndef MONSTER_IK_H
#define MONSTER_IK_H
#include "Skeleton.h"
#define IK_MAX_CHAIN_JOINTS 32
typedef struct IKChain {
    int jointIndices[IK_MAX_CHAIN_JOINTS];
    size_t jointCount;
    int endEffectorJoint;
    Vector3 targetPosition;
    int maxIterations;
    float tolerance;
} IKChain;
typedef struct IKResult { bool valid, converged; int iterations; float error; } IKResult;
/** @brief Resuelve una cadena contigua sin cambiar traslaciones locales ni anatomía.
 * @param skeleton Definición inmutable.
 * @param pose Pose de entrada/salida.
 * @param chain Objetivo y presupuesto; admite segmentos nulos.
 * @return Residuo real después de FK; nunca declara convergencia ficticia por límites.
 */
IKResult IK_SolveFABRIK(const Skeleton* skeleton, SkeletonPose* pose, const IKChain* chain);
/** @brief Resuelve tres segmentos digitígrados preservando la orientación distal de apoyo.
 * @param skeleton Esqueleto inmutable. @param pose Pose mutable.
 * @param chain Cadena de cuatro articulaciones. @param distalDirection Dirección distal mundial.
 * @param dt Tiempo del paso para relajar continuamente la preferencia postural.
 * @return Validez, convergencia y residuo del efector real. */
IKResult IK_SolveDigitigrade(const Skeleton* skeleton,SkeletonPose* pose,const IKChain* chain,Vector3 distalDirection,float dt);
#endif
