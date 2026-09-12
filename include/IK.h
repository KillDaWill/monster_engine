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
#endif
