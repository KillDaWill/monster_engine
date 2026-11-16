/**
 * @file CreatureRecipeVelkar.c
 * @brief Receta ontogenética del carnívoro felino Velkar.
 * @details Velcaris nocturna.
 */

#include "CreatureRecipes.h"
#include "HeadModule.h"
#include "SurfacePresets.h"
#include "AxialBody.h"
#include <pthread.h>

static CreatureRecipe velkarRecipe;
static pthread_once_t velkarOnce=PTHREAD_ONCE_INIT;

/** @brief Construye el plan corporal fijo del felino digitígrado. */
static BodyPlan Velkar_BodyPlan(void) {
    return (BodyPlan){
        .root={1,AXIAL_NODE_PELVIS},.moduleCount=7,.modules={
            {.instanceId=1,.kind=CREATURE_MODULE_AXIAL},
            {.instanceId=2,.kind=CREATURE_MODULE_HEAD,.attachment={1,AXIAL_SLOT_CERVICAL}},
            {.instanceId=20,.kind=CREATURE_MODULE_TAIL,.attachment={1,AXIAL_SLOT_CAUDAL}},
            {.instanceId=10,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PECTORAL_LEFT},.phenotypeIndex=0},
            {.instanceId=11,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PECTORAL_RIGHT},.phenotypeIndex=1},
            {.instanceId=12,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PELVIC_LEFT},.phenotypeIndex=2},
            {.instanceId=13,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PELVIC_RIGHT},.phenotypeIndex=3}}};
}

/** @brief Inicializa la anatomía adulta y su composición de módulos. */
static CreaturePhenotype Velkar_Adult(void) {
    CreaturePhenotype p={0};
    p.axial=(AxialPhenotype){.archetype=AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD,
        .contourProfile=0,.totalScale=.95f,
        .withersHeight=3.05f,.trunkLength=3.40f,.neckLength=1.08f,.neckWidth=1.02f,
        .shoulderWidth=1.04f,.thoraxWidth=1.00f,.thoraxHeight=1.22f,
        .abdomenWidth=.80f,.abdomenHeight=.90f,.pelvicWidth=.96f,.pelvicHeight=1.02f,
        .bodyFlattening=.94f,.limbAttachmentLateral=.76f,.cervicalCurvature=.28f,
        .cervicalDorsalMass=1.10f,.cervicalMidNarrowing=.97f,.withersElevation=.05f,
        .dewlapDepth=.01f,.brisketDepth=.04f};
    p.axial.profile[0]=(AxialStationMorph){.verticalOffset=-.02f,.widthScale=1.00f,.heightScale=1.00f};
    p.axial.profile[1]=(AxialStationMorph){.verticalOffset=.01f,.widthScale=1.00f,.heightScale=1.00f};
    p.axial.profile[2]=(AxialStationMorph){.verticalOffset=0,.widthScale=1.00f,.heightScale=1.00f};
    p.axial.profile[3]=(AxialStationMorph){.verticalOffset=.01f,.widthScale=.96f,.heightScale=.96f};
    p.axial.profile[4]=(AxialStationMorph){.verticalOffset=.01f,.widthScale=.94f,.heightScale=.94f};
    p.axial.profile[5]=(AxialStationMorph){.verticalOffset=-.04f,.widthScale=.86f,.heightScale=.86f};
    p.head=HeadPhenotype_FelinePreset();
    p.headEnvelope=(HeadEnvelopePhenotype){.scale=.64f,.widthScale=.76f,.heightScale=1.18f,.lengthScale=1.02f};
    p.eyes=(EyePhenotype){.size=.31f,.protrusion=.62f,.irisScale=.92f,.pupilScale=.34f,
        .pupilAspect=.22f,.pupilShape=PUPIL_VERTICAL,.scleraColor={245,200,75,255},
        .irisColor={255,215,55,255},.pupilColor={4,4,4,255}};
    p.limbCount=4;
    for (unsigned i=0;i<4;++i) {
        bool hind=i>=2; LimbPhenotype* l=&p.limbs[i];
        *l=(LimbPhenotype){.archetype=LIMB_ARCHETYPE_MAMMAL,.role=hind?LIMB_HIND:LIMB_FORE,
            .side=i%2?LIMB_RIGHT:LIMB_LEFT,.development=1,.length=hind?2.48f:2.38f,
            .thickness=hind?.235f:.205f,.sprawl=.008f,.footScale=hind?.72f:.70f,.footWidthScale=.86f,
            .footHeightScale=hind?1.02f:1.00f,.proximalScale=hind?1.02f:1.18f,.middleScale=hind?1.04f:.92f,.distalScale=hind?.84f:.82f,
            .rootThicknessScale=hind?1.24f:1.18f,.proximalThicknessScale=hind?1.06f:1.04f,
            .middleThicknessScale=hind?.96f:.96f,.distalThicknessScale=.88f,
            .digitThicknessScale=1.32f,.attachmentInset=hind?.48f:.54f,
            .digitCount=4,.digitSpread=.008f,.proximalSweep=hind?-.035f:-.070f,
            .distalSweep=hind?.005f:-.045f,.flexionScale=1.08f};
        for (unsigned d=0;d<4;++d) {
            l->digitLengths[d]=d==0||d==3?.28f:.36f;
            l->digitPhalanges[d]=2;l->digitAngles[d]=((float)d-1.5f)*.045f;
        }
    }
    p.tailCount=1;p.tails[0]=(TailPhenotype){.archetype=TAIL_ARCHETYPE_CUSTOM,.length=3.35f,
        .baseWidth=.125f,.baseHeight=.125f,.tipWidth=.060f,.tipHeight=.060f,.taperCurve=.84f,
        .curvature=.28f,.droop=.03f,.rootMatchStrength=0.0f,.segmentCount=10,.development=1};
    p.surface=SurfacePreset_FelineSleekCoat(4,1);
    p.ornamentCount=0;
    p.dorsalColor=p.surface.pigment.secondaryColor;p.ventralColor=p.surface.pigment.ventralColor;
    p.unpigmentedVentralColor=p.surface.pigment.baseColor;
    return p;
}

/** @brief Genera una etapa felina cambiando únicamente proporciones numéricas. */
static CreaturePhenotype Velkar_Stage(unsigned stage) {
    const float scale[]={.24f,.38f,.66f,.95f};
    const float maturity[]={0,.22f,.70f,1.0f};
    const float cephalic[]={.20f,.44f,.78f,1.0f};
    CreaturePhenotype p=Velkar_Adult();unsigned i=stage<4?stage:3;
    p.axial.totalScale=scale[i];
    p.axial.trunkLength*=.66f+.34f*cephalic[i];
    p.axial.thoraxHeight*=.78f+.22f*cephalic[i];
    p.axial.thoraxWidth*=.82f+.18f*cephalic[i];
    p.axial.abdomenWidth*=.84f+.16f*cephalic[i];
    p.axial.abdomenHeight*=.80f+.20f*cephalic[i];
    p.axial.pelvicWidth*=.84f+.16f*cephalic[i];
    p.axial.pelvicHeight*=.78f+.22f*cephalic[i];
    p.axial.shoulderWidth*=.80f+.20f*cephalic[i];
    p.axial.neckLength*=.74f+.26f*cephalic[i];
    p.axial.neckWidth*=.76f+.24f*cephalic[i];
    p.headEnvelope.scale*=1.24f-.24f*cephalic[i];
    /* La altura del tronco y la longitud vertical comparten la misma suela;
     * acortar sólo las patas dejaba a las crías fuera de alcance del suelo. */
    p.axial.withersHeight*=.72f+.28f*cephalic[i];
    for(unsigned leg=0;leg<4;++leg) {
        LimbPhenotype* limb=&p.limbs[leg];
        float sole=limb->thickness*.48f*limb->footScale*limb->footHeightScale;
        limb->length=p.axial.withersHeight-(leg>=2?p.axial.pelvicHeight:p.axial.thoraxHeight)*.5f-sole+.08f;
    }
    p.tails[0].length*=.58f+.42f*cephalic[i];
    p.head.skullWidth*=.90f+.10f*cephalic[i];p.head.skullHeight*=.95f+.05f*cephalic[i];
    p.head.muzzleLength*=.80f+.20f*cephalic[i];p.head.eyeSize*=1.18f-.18f*cephalic[i];
    p.eyes.size*=1.18f-.18f*cephalic[i];p.development=(CreatureDevelopment){.appendages=1,.cephalic=cephalic[i],.pigmentation=maturity[i],.integument=1,.colorMaturity=maturity[i]};
    p.surface=SurfacePreset_FelineSleekCoat(4,maturity[i]);
    p.dorsalColor=p.surface.pigment.secondaryColor;p.ventralColor=p.surface.pigment.ventralColor;
    p.unpigmentedVentralColor=p.surface.pigment.baseColor;
    CreaturePhenotype_Normalize(&p);return p;
}

/** @brief Inicializa la receta registrada de Velkar una sola vez. */
static void Velkar_Init(void) {
    velkarRecipe.id=4;velkarRecipe.name="Velkar";velkarRecipe.gait=CREATURE_GAIT_FELINE_STALK;
    velkarRecipe.bodyPlan=Velkar_BodyPlan();velkarRecipe.seed=Velkar_Stage(0);velkarRecipe.larva=Velkar_Stage(1);
    velkarRecipe.juvenile=Velkar_Stage(2);velkarRecipe.adult=Velkar_Stage(3);
}

/** @brief Devuelve la receta inmutable de Velkar. */
const CreatureRecipe* CreatureRecipes_Velkar(void) {
    pthread_once(&velkarOnce,Velkar_Init);return &velkarRecipe;
}
