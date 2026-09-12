#include "LizardGaits.h"
GaitProfile LizardGaits_Walk(float scale) {
    return (GaitProfile){.cycleDuration=1.4f,.strideLength=.48f*scale,.stepHeight=.13f*scale,
        .stanceRatio=.68f,.limbPhase={0,.5f,.5f,0},.bodyBob=.012f*scale,.bodySway=.012f*scale,
        .spineWave=.012f,.tailCounterSwing=.06f};
}
