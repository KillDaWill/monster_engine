/** @file MonsterAnimation.h
 * @brief Estado opcional poseído por Monster; snapshots morfológicos no lo copian.
 */
#ifndef MONSTER_ANIMATION_H
#define MONSTER_ANIMATION_H
#include "ProceduralAnimator.h"
struct Monster;
typedef struct MonsterAnimation {
    Rig rig;
    SkeletonPose pose;
    ProceduralAnimator animator;
    uint64_t restFingerprint,rigGeneration;
    float standingHeight;
} MonsterAnimation;
/** @brief Instala/reconfigura rig tras un cambio real; conserva movimiento y fase. */
bool MonsterAnimation_Configure(struct Monster* monster,const Rig* rig,GaitProfile gait);
/** @brief Actualiza solo pose. Rechaza anatomía cambiada sin reconfiguración. */
bool MonsterAnimation_Update(struct Monster* monster,float dt);
/** @brief Destruye el estado opcional. */
void MonsterAnimation_Free(MonsterAnimation* animation);
#endif
