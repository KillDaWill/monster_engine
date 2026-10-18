/** @file benchmark_interactions.c
 * @brief Instrumentación reproducible del alcance oral y coste corporal. */
#include "Creature.h"
#include "CreatureRecipes.h"
#include "Monster.h"
#include "MonsterAnimation.h"
#include "CreatureRig.h"
#include "GaitPresets.h"
#include <math.h>
#include <stdio.h>
#include <time.h>
static double seconds(void) { struct timespec t; timespec_get(&t,TIME_UTC); return t.tv_sec+t.tv_nsec*1e-9; }
int main(void) {
    for(int species=0;species<2;++species) {
        const CreatureRecipe *recipe=species?CreatureRecipes_Dog():CreatureRecipes_Lizard();
        static Monster m; m=Monster_Create();
        if(!Creature_BuildMonster(&m,recipe,&recipe->adult))return 1;
        Rig rig; if(!CreatureRig_Build(&m,&rig))return 3;
        if(!MonsterAnimation_Configure(&m,&rig,species?GaitPreset_CanidWalk(&rig):GaitPreset_SprawlingQuadruped(1)))return 4;
        for(int test=0;test<4;++test) {
            ProceduralAnimator *a=&m.animation->animator; const Rig *r=&m.animation->rig;
            MonsterAnimation_Update(&m,1.f/60);
            Vector3 neutral=Vec3_Add(m.animation->pose.joints[r->headJoint].worldPosition,
                Quat_RotateVector(m.animation->pose.joints[r->headJoint].worldRotation,r->oralPoint));
            a->oralReachActive=true;
            a->oralReachTarget=Vec3_Add(neutral,Vec3_Create(test==2?.35f:0,test==3?10:test==1?.3f:-.4f,0));
            double start=seconds();float maximum=0;
            for(int frame=0;frame<360;++frame) {
                if(!MonsterAnimation_Update(&m,1.f/60))return 2;
                int chain[]={r->cervicalBaseJoint,r->neckJoint,r->headJoint};
                for(int k=0;k<3;++k)if(chain[k]>=0) {
                    Quaternion q=m.animation->pose.joints[chain[k]].localRotation;
                    maximum=fmaxf(maximum,2*acosf(fminf(1,fabsf(q.w))));
                }
            }
            Vector3 mouth=Vec3_Add(m.animation->pose.joints[r->headJoint].worldPosition,
                Quat_RotateVector(m.animation->pose.joints[r->headJoint].worldRotation,r->oralPoint));
            printf("species=%d case=%d mean_ms=%.6f residual=%.6f max_angle=%.6f\n",species,test,(seconds()-start)*1000/360,Vec3_Distance(mouth,a->oralReachTarget),maximum);
        }
        Monster_Free(&m);
    }
}
