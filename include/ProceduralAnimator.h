/** @file ProceduralAnimator.h
 * @brief Coordinador genérico de FK secundario, contactos e IK.
 */
#ifndef MONSTER_PROCEDURAL_ANIMATOR_H
#define MONSTER_PROCEDURAL_ANIMATOR_H
#include "Locomotion.h"
#include "AnatomicalReach.h"
typedef enum AquaticSwimMode {
    AQUATIC_SWIM_NONE = 0,
    AQUATIC_SWIM_THUNNIFORM,
    AQUATIC_SWIM_CARANGIFORM,
    AQUATIC_SWIM_SEAHORSE,
    AQUATIC_SWIM_TURTLE,
    AQUATIC_SWIM_CETACEAN
} AquaticSwimMode;

typedef struct ProceduralAnimator {
    Locomotion locomotion;
    GaitProfile gait;
    AquaticSwimMode swimMode; /**< Modo de propulsión acuática generalizado; NONE conserva locomoción terrestre/aérea. */
    float swimPhase; /**< Fase continua del ciclo de nado acuático. */
    Vector3 desiredVelocity,lookDirection;
    float angularVelocity,currentAngularVelocity;
    float mouthOpen;
    float defensiveCurl; /**< Flexión defensiva progresiva [0..1] para enrollamiento axial-caudal. */
    float wingFold; /**< Plegado alar [0..1] para recogida articular de húmero, codo, carpo y radios digitales. */
    float wingFlap; /**< Oscilación de batida alar [-1..1] sobre el eje escapular. */
    float hindWingFlap; /**< Oscilación independiente de alas posteriores [-1..1] (p. ej. odonatos). */
    float finWavePhase; /**< Fase continua de onda hidrodinámica para aletas pectorales batoideas. */
    float finWaveAmplitude; /**< Amplitud de batido ondulatorio en aletas [0..1]. */
    float bellPulsePhase; /**< Fase de contracción/relajación de campana radial medusoide. */
    float bellPulseAmplitude; /**< Amplitud de pulsación radial [0..1]. */
    float armWavePhase; /**< Fase de ondulación/rastreo de brazos hidrostáticos independientes. */
    float armSpread; /**< Apertura radial o extensión de brazos hidrostáticos [-1..1]. */
    double crouchRemainder;
    float reachCrouch, reachCrouchVelocity, oralReachTime;
    bool oralReachActive; /**< Solicitud corporal de alcanzar un punto con la boca. */
    Vector3 oralReachTarget; /**< Objetivo en el mismo marco que la pose locomotora. */
    Quaternion cervicalRotation[3]; /**< Continuidad del gesto cervical, sin mover el tronco. */
    bool (*validateInteractionPose)(const Rig *,const SkeletonPose *,void *);
    void *interactionContext;
    void (*applyAxialPose)(const Rig*,SkeletonPose*,void*); /**< Autoridad corporal opcional, antes de resolver apoyos. */
    void *axialContext;
    AnatomicalReachProfile reachProfile;
    AnatomicalReachState reachState;
    InteractionTarget interactionTarget;
    ReachResult reachResult;
    IKResult limbResults[RIG_MAX_LIMBS];
} ProceduralAnimator;
/** @brief Inicializa controlador con perfil y posición mundial. */
bool ProceduralAnimator_Init(ProceduralAnimator* animator,const Rig* rig,GaitProfile gait,World* world,Vector3 position);
/** @brief FK axial antes de IK para no desplazar después los pies plantados. */
bool ProceduralAnimator_Update(ProceduralAnimator* animator,const Rig* rig,SkeletonPose* pose,World* world,float dt);
#endif
