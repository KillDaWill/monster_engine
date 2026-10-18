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
