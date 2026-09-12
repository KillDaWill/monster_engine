/** @file Skeleton.h
 * @brief Jerarquía inmutable y pose mutable independiente de la anatomía de reposo.
 */
#ifndef MONSTER_SKELETON_H
#define MONSTER_SKELETON_H
#include "Anatomy.h"
#include "Quaternion.h"
#define SKELETON_MAX_JOINTS (ANATOMY_MAX_NODES + 8)
typedef enum JointType { JOINT_FIXED, JOINT_HINGE, JOINT_BALL } JointType;
/** @brief Límites relativos a restRotation; eje expresado en el marco de reposo local. */
typedef struct JointConstraint {
    JointType type;
    Vector3 hingeAxis;
    float minAngle, maxAngle, maxSwingAngle, maxTwistAngle;
} JointConstraint;
typedef struct SkeletonJoint {
    uint32_t id;
    int parentIndex;
    AnatomyId anatomyId;
    Vector3 restPosition; /**< Traslación local; la raíz usa coordenadas del modelo. */
    Quaternion restRotation;
    JointConstraint constraint;
} SkeletonJoint;
typedef struct Skeleton { SkeletonJoint joints[SKELETON_MAX_JOINTS]; size_t jointCount; } Skeleton;
typedef struct JointPose {
    Vector3 localPosition, worldPosition;
    Quaternion localRotation, worldRotation;
} JointPose;
typedef struct SkeletonPose { JointPose joints[SKELETON_MAX_JOINTS]; size_t jointCount; } SkeletonPose;
/** @brief Valida identidad, finitud y orden topológico (padre antes que hijo). */
bool Skeleton_Validate(const Skeleton* skeleton);
/** @brief Busca identidad estable; -1 si no existe. */
int Skeleton_FindJoint(const Skeleton* skeleton, AnatomyId id);
/** @brief Crea una pose local y mundial de reposo. */
bool SkeletonPose_Init(SkeletonPose* pose, const Skeleton* skeleton);
/** @brief Restablece pose sin cambiar el esqueleto. */
bool SkeletonPose_ResetToRest(SkeletonPose* pose, const Skeleton* skeleton);
/** @brief Deriva exclusivamente de locales los transformados mundiales. */
bool SkeletonPose_UpdateWorldTransforms(SkeletonPose* pose, const Skeleton* skeleton);
/** @brief Copia una pose sin referencias compartidas. */
void SkeletonPose_Copy(SkeletonPose* dst, const SkeletonPose* src);
/** @brief Proyecta una rotación local sobre límites de bisagra o cono swing/twist. */
Quaternion Skeleton_ConstrainRotation(const SkeletonJoint* joint, Quaternion rotation);
/** @brief Construye una copia anatómica posada; nunca escribe en rest. */
bool SkeletonPose_ToAnatomy(const Skeleton* skeleton, const SkeletonPose* pose,
    const AnatomyGraph* rest, AnatomyGraph* posed);
#endif
