/** @file Locomotion.h
 * @brief Gait y contactos mundiales; genera objetivos sin resolver articulaciones.
 */
#ifndef MONSTER_LOCOMOTION_H
#define MONSTER_LOCOMOTION_H
#include "RigBuilder.h"
#include "WorldInterface.h"
typedef enum FootPhase { FOOT_STANCE, FOOT_SWING } FootPhase;
typedef struct FootState {
    FootPhase phase;
    Vector3 plantedPosition,swingStart,swingTarget,targetPosition,normal;
    float swingProgress,phaseOffset;
    bool wasSwingWindow;
} FootState;
typedef struct GaitProfile {
    float cycleDuration,strideLength,stepHeight,stanceRatio;
    float limbPhase[RIG_MAX_LIMBS];
    float bodyBob,bodySway,spineWave,tailCounterSwing;
    float spineTurn,tailTurn,bodyBank;
    float supportCompression; /**< Descenso de apoyo que reserva alcance durante la zancada. */
} GaitProfile;
typedef struct Locomotion {
    FootState feet[RIG_MAX_LIMBS];
    Vector3 bodyPosition,velocity;
    Vector3 supportCenter; /**< Centro de apoyos en reposo; pivote horizontal del giro. */
    float phase;
    float angularVelocity;
    bool initialized;
} Locomotion;
/** @brief Inicializa contactos desde reposo, proyección al suelo y perfil. */
bool Locomotion_Init(Locomotion* state,const Rig* rig,const GaitProfile* gait,World* world,Vector3 bodyPosition);
/** @brief Avanza cuerpo y objetivos con contactos fijos durante stance. Sin asignaciones. */
bool Locomotion_Update(Locomotion* state,const Rig* rig,const GaitProfile* gait,
    World* world,Vector3 desiredVelocity,float dt);
/** @brief Transporta apoyos al girar el marco del modelo; conserva su posición mundial.
 * @param state Contactos en el marco locomotor. @param yawDelta Giro real del modelo, en radianes. */
void Locomotion_TurnFrame(Locomotion* state,float yawDelta);
#endif
