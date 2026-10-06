/** @file MorphModuleState.c
 * @brief Política pura de publicación; no depende del orden de los workers.
 */
#include "MorphModuleState.h"
#include <math.h>
#include <string.h>

void MorphModuleState_Init(MorphModuleState* state,uint32_t moduleId,
    uint64_t morphGeneration,bool topologyTransition) {
    if(!state)return;
    memset(state,0,sizeof(*state));
    state->moduleId=moduleId;
    state->morphGeneration=morphGeneration;
    state->topologyVersion=1;
    state->interfaceVersion=1;
    state->sdfVersion=1;
    state->topologyTransition=topologyTransition;
    state->certifiedAge=NAN;
    state->targetAge=NAN;
    state->fallbackActive=topologyTransition;
    state->handoffState=topologyTransition?MORPH_SDF_ACTIVE:MORPH_MESH_ACTIVE;
    state->handoffTransition=topologyTransition?1:0;
    state->fallbackWanted=topologyTransition;
}

void MorphModuleState_Request(MorphModuleState* state,float age) {
    if(!state)return;
    state->targetAge=age;
    state->fallbackActive=!MorphModuleState_CanPredict(state,age);
}

MorphPatchToken MorphModuleState_Token(const MorphModuleState* state,float age) {
    MorphPatchToken token={0};
    if(!state)return token;
    token.morphGeneration=state->morphGeneration;
    token.topologyVersion=state->topologyVersion;
    token.interfaceVersion=state->interfaceVersion;
    token.sdfVersion=state->sdfVersion;
    token.moduleId=state->moduleId;
    token.age=age;
    return token;
}

bool MorphModuleState_Accept(MorphModuleState* state,MorphPatchToken token) {
    if(!state||!isfinite(token.age)||token.moduleId!=state->moduleId||
       token.morphGeneration!=state->morphGeneration||
       token.topologyVersion!=state->topologyVersion||
       token.interfaceVersion!=state->interfaceVersion||
       token.sdfVersion!=state->sdfVersion)return false;
    /* Un resultado anterior no puede reemplazar otro más próximo al objetivo. */
    if(state->certified&&isfinite(state->targetAge)&&
       fabsf(token.age-state->targetAge)>fabsf(state->certifiedAge-state->targetAge)+1e-5f)
        return false;
    state->certifiedAge=token.age;
    state->certified=true;
    state->fallbackActive=!MorphModuleState_CanPredict(state,state->targetAge);
    return true;
}

bool MorphModuleState_CanPredict(const MorphModuleState* state,float age) {
    if(!state||!state->certified||!isfinite(age))return false;
    if(!state->topologyTransition)return true;
    return fabsf(age-state->certifiedAge)<=1e-5f;
}

bool MorphModuleState_NeedsExactFallback(const MorphModuleState* state,
    float age,float maximumFrozenAgeLag) {
    if(!state||!state->topologyTransition)return false;
    if(!state->certified||!isfinite(state->certifiedAge)||!isfinite(age)||
       !isfinite(maximumFrozenAgeLag)||maximumFrozenAgeLag<0)return true;
    return fabsf(age-state->certifiedAge)>maximumFrozenAgeLag;
}

void MorphModuleState_UpdateHandoff(MorphModuleState* s,float age,float enterLag,
    bool interfaceValid,float dt,float duration) {
    if(!s||!isfinite(dt)||dt<0||!isfinite(enterLag)||enterLag<0||
       !isfinite(duration)||duration<=0)return;
    s->handoffInitialized=true;
    float lag=s->certified&&isfinite(s->certifiedAge)&&isfinite(age)?
        fabsf(age-s->certifiedAge):INFINITY;
    bool wanted=s->fallbackWanted;
    if(s->topologyTransition) {
        if(lag>enterLag||!interfaceValid)wanted=true;
        else if(s->certified&&lag<enterLag*.5f&&interfaceValid)wanted=false;
    } else wanted=false;
    if(wanted!=s->fallbackWanted) {s->fallbackWanted=wanted;s->stableSeconds=0;}
    else s->stableSeconds+=dt;
    bool goingSDF=s->handoffState==MORPH_ENTERING_SDF||s->handoffState==MORPH_SDF_ACTIVE;
    if(wanted!=goingSDF&&s->stableSeconds>=duration*.4f)
        s->handoffState=wanted?MORPH_ENTERING_SDF:MORPH_ENTERING_MESH;
    if(s->handoffState==MORPH_ENTERING_SDF) {
        s->handoffTransition=fminf(1,s->handoffTransition+dt/duration);
        if(s->handoffTransition>=1)s->handoffState=MORPH_SDF_ACTIVE;
    } else if(s->handoffState==MORPH_ENTERING_MESH) {
        s->handoffTransition=fmaxf(0,s->handoffTransition-dt/duration);
        if(s->handoffTransition<=0)s->handoffState=MORPH_MESH_ACTIVE;
    }
}
