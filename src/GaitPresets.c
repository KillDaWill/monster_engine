#include "GaitPresets.h"
#include <math.h>
GaitProfile GaitPreset_SprawlingQuadruped(float scale) {
    return (GaitProfile){.cycleDuration=1.4f,.strideLength=.48f*scale,.stepHeight=.13f*scale,
        .stanceRatio=.68f,.limbPhase={0,.5f,.5f,0},.bodyBob=.012f*scale,.bodySway=.012f*scale,
        .spineWave=.012f,.tailCounterSwing=.06f,
        .spineTurn=.35f,.tailTurn=.45f,.bodyBank=0.0f};
}

GaitProfile GaitPreset_CanidWalk(const Rig* rig) {
    float reach=INFINITY,distal=INFINITY;
    if(rig)for(size_t i=0;i<rig->limbCount;++i) {
        const LimbRig* limb=&rig->limbs[i];
        if(!limb->walking)continue;
        float length=0;
        for(size_t k=1;k<limb->chain.jointCount;++k)
            length+=Vec3_Length(rig->skeleton.joints[limb->chain.jointIndices[k]].restPosition);
        reach=fminf(reach,length);
        distal=fminf(distal,Vec3_Length(rig->skeleton.joints[limb->endEffectorJoint].restPosition));
    }
    if(!isfinite(reach)||reach<=0)reach=1;
    if(!isfinite(distal)||distal<=0)distal=reach*.12f;
    GaitProfile g={.cycleDuration=.9f,.strideLength=.28f*reach,.stepHeight=.45f*distal,
        .stanceRatio=.76f,.bodyBob=.004f*reach,.bodySway=.003f*reach,
        .spineWave=.006f,.tailCounterSwing=.045f,.spineTurn=.10f,.tailTurn=.16f,.bodyBank=.06f,
        .supportCompression=.12f*.28f*reach};
    /* Secuencia de paso: posterior izquierda, anterior izquierda,
     * posterior derecha, anterior derecha. No depende del orden del grafo. */
    if(rig)for(size_t i=0;i<rig->limbCount;++i)
        g.limbPhase[i]=(rig->limbs[i].role==LIMB_FORE?.25f:0)+(rig->limbs[i].side==LIMB_RIGHT?.5f:0);
    return g;
}

GaitProfile GaitPreset_BovineWalk(const Rig* rig) {
    GaitProfile g=GaitPreset_CanidWalk(rig);
    g.cycleDuration*=1.6f;
    g.strideLength*=1.2f;
    g.stepHeight*=.85f;
    g.stanceRatio=.82f;
    g.bodyBob*=.5f;
    g.bodySway*=1.15f;
    g.spineWave*=.75f;
    g.tailCounterSwing*=.8f;
    g.spineTurn*=.8f;
    g.tailTurn*=.85f;
    g.bodyBank*=1.25f;
    g.supportCompression*=1.35f;
    return g;
}

GaitProfile GaitPreset_FelineStalk(const Rig* rig) {
    GaitProfile g=GaitPreset_CanidWalk(rig);
    g.cycleDuration*=1.55f;
    g.strideLength*=.92f;
    g.stepHeight*=1.05f;
    g.stanceRatio=.82f;
    g.bodyBob*=.45f;
    g.bodySway*=.75f;
    g.spineWave=.016f;
    g.tailCounterSwing=.085f;
    g.spineTurn=.10f;
    g.tailTurn=.22f;
    g.bodyBank=.05f;
    g.supportCompression*=1.10f;
    return g;
}

GaitProfile GaitPreset_QuadrupedMotion(const Rig* rig,bool agile,float speed,float running) {
    GaitProfile walk=agile?GaitPreset_FelineStalk(rig):GaitPreset_BovineWalk(rig);
    GaitProfile g=walk;
    float t=fmaxf(0,fminf(1,running));
    g.strideLength=walk.strideLength*(1+t*(agile?2.2f:1.3f));
    g.stanceRatio=walk.stanceRatio+( (agile?.55f:.62f)-walk.stanceRatio)*t;
    g.stepHeight=walk.stepHeight*(1+t*(agile?2.0f:1.25f));
    /* La cadencia sigue el desplazamiento real: el controlador no debe
     * recortar la velocidad mientras el mundo sigue avanzando al animal. */
    g.cycleDuration=fminf(walk.cycleDuration,g.strideLength/(fmaxf(.01f,speed)*g.stanceRatio*1.05f));
    g.cycleDuration=fmaxf(.08f,g.cycleDuration);
    g.spineWave=walk.spineWave+(agile?.035f:.008f)*t;
    g.tailCounterSwing=agile?.10f:.025f;
    if(rig)for(size_t i=0;i<rig->limbCount;++i) {
        const LimbRig* l=&rig->limbs[i];
        /* Carrera felina de pares asimétricos; trote pesado diagonal. */
        float runPhase=agile?((l->role==LIMB_FORE?.48f:0)+(l->side==LIMB_RIGHT?.10f:0)):
            ((l->role==LIMB_FORE?0:.5f)+(l->side==LIMB_RIGHT?.5f:0));
        float delta=runPhase-walk.limbPhase[i];
        delta-=roundf(delta);
        g.limbPhase[i]=walk.limbPhase[i]+delta*t;
        if(g.limbPhase[i]<0)g.limbPhase[i]+=1;
    }
    return g;
}

GaitProfile GaitPreset_BipedalHop(const Rig* rig) {
    float reach=INFINITY,distal=INFINITY;
    if(rig)for(size_t i=0;i<rig->limbCount;++i) {
        const LimbRig* limb=&rig->limbs[i];
        if(!limb->walking)continue;
        float length=0;
        for(size_t k=1;k<limb->chain.jointCount;++k)
            length+=Vec3_Length(rig->skeleton.joints[limb->chain.jointIndices[k]].restPosition);
        reach=fminf(reach,length);
        distal=fminf(distal,Vec3_Length(rig->skeleton.joints[limb->endEffectorJoint].restPosition));
    }
    if(!isfinite(reach)||reach<=0)reach=1;
    if(!isfinite(distal)||distal<=0)distal=reach*.22f;
    GaitProfile g={.cycleDuration=.68f,.strideLength=.92f*reach,.stepHeight=.28f*reach,
        .stanceRatio=.44f,.bodyBob=.18f*reach,.bodySway=0.0f,
        .spineWave=.014f,.tailCounterSwing=.09f,.spineTurn=.06f,.tailTurn=.14f,.bodyBank=.02f,
        .supportCompression=.045f*reach};
    /* Salto bípedo sincronizado: ambas extremidades posteriores comparten fase 0. */
    if(rig)for(size_t i=0;i<rig->limbCount;++i)g.limbPhase[i]=0.0f;
    return g;
}
