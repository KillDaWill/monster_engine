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
        .root={1,AXIAL_NODE_PELVIS},.moduleCount=9,.modules={
            {.instanceId=1,.kind=CREATURE_MODULE_AXIAL},
            {.instanceId=2,.kind=CREATURE_MODULE_HEAD,.attachment={1,AXIAL_SLOT_CERVICAL}},
            {.instanceId=20,.kind=CREATURE_MODULE_TAIL,.attachment={1,AXIAL_SLOT_CAUDAL}},
            {.instanceId=10,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PECTORAL_LEFT},.phenotypeIndex=0},
            {.instanceId=11,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PECTORAL_RIGHT},.phenotypeIndex=1},
            {.instanceId=12,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PELVIC_LEFT},.phenotypeIndex=2},
            {.instanceId=13,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PELVIC_RIGHT},.phenotypeIndex=3},
            {.instanceId=30,.kind=CREATURE_MODULE_ORNAMENT,.attachment={2,HEAD_SLOT_PINNA_LEFT},.phenotypeIndex=0},
            {.instanceId=31,.kind=CREATURE_MODULE_ORNAMENT,.attachment={2,HEAD_SLOT_PINNA_RIGHT},.phenotypeIndex=1}}};
}

/** @brief Inicializa la anatomía adulta y su composición de módulos. */
static CreaturePhenotype Velkar_Adult(void) {
    CreaturePhenotype p={0};
    p.axial=(AxialPhenotype){.archetype=AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD,.totalScale=.95f,
        .withersHeight=3.20f,.trunkLength=3.65f,.neckLength=1.05f,.neckWidth=.76f,
        .shoulderWidth=1.40f,.thoraxWidth=1.48f,.thoraxHeight=1.56f,
        .abdomenWidth=1.02f,.abdomenHeight=.98f,.pelvicWidth=1.30f,.pelvicHeight=1.34f,
        .bodyFlattening=.72f,.limbAttachmentLateral=1.08f,.cervicalCurvature=.62f,
        .cervicalDorsalMass=1.12f,.cervicalMidNarrowing=.78f,.withersElevation=.08f};
    p.axial.profile[0].verticalOffset=-.28f;
    p.head=HeadPhenotype_FelinePreset();
    p.head.earSize=.42f;p.head.earWidthRatio=1.25f;p.head.earBaseWidth=.72f;
    p.head.earLongitudinalCurve=.28f;p.head.earMarginBow=.22f;
    p.headEnvelope=(HeadEnvelopePhenotype){.scale=.96f,.widthScale=1.08f,.heightScale=1.12f,.lengthScale=.94f};
    p.eyes=(EyePhenotype){.size=.62f,.protrusion=.56f,.irisScale=.86f,.pupilScale=.46f,
        .pupilAspect=.24f,.pupilShape=PUPIL_VERTICAL,.scleraColor={72,70,62,255},
        .irisColor={172,145,48,255},.pupilColor={4,4,3,255}};
    p.limbCount=4;
    for (unsigned i=0;i<4;++i) {
        bool hind=i>=2; LimbPhenotype* l=&p.limbs[i];
        *l=(LimbPhenotype){.archetype=LIMB_ARCHETYPE_MAMMAL,.role=hind?LIMB_HIND:LIMB_FORE,
            .side=i%2?LIMB_RIGHT:LIMB_LEFT,.development=1,.length=hind?3.12f:2.86f,
            .thickness=hind?.34f:.28f,.sprawl=.015f,.footScale=1.12f,.footWidthScale=1.06f,
            .footHeightScale=.86f,.proximalScale=1.02f,.middleScale=1.08f,.distalScale=.86f,
            .rootThicknessScale=hind?1.42f:1.16f,.middleThicknessScale=1.02f,
            .distalThicknessScale=.96f,.digitThicknessScale=.90f,.attachmentInset=.30f,
            .digitCount=4,.digitSpread=.22f,.proximalSweep=.06f,.distalSweep=.03f,.flexionScale=1.5f};
        for (unsigned d=0;d<4;++d) {
            l->digitLengths[d]=d==0||d==3?.62f:.78f;
            l->digitPhalanges[d]=3;l->digitAngles[d]=((float)d-1.5f)*.10f;
        }
    }
    p.tailCount=1;p.tails[0]=(TailPhenotype){.archetype=TAIL_ARCHETYPE_TAPERED,.length=3.55f,
        .baseWidth=.23f,.baseHeight=.24f,.tipWidth=.060f,.tipHeight=.060f,.taperCurve=1.25f,
        .curvature=.35f,.rootMatchStrength=.40f,.segmentCount=8,.development=1};
    p.surface=SurfacePreset_FelineSleekCoat(4,1);
    p.ornamentCount=2;
    p.ornaments[0]=(OrnamentPhenotype){.archetype=ORNAMENT_ARCHETYPE_SPINE,
        .length=.15f,.baseRadius=.035f,.tipRadius=.012f,.development=1};
    p.ornaments[1]=p.ornaments[0];
    p.dorsalColor=p.surface.pigment.secondaryColor;p.ventralColor=p.surface.pigment.ventralColor;
    p.unpigmentedVentralColor=p.surface.pigment.baseColor;
    return p;
}

/** @brief Genera una etapa felina cambiando únicamente proporciones numéricas. */
static CreaturePhenotype Velkar_Stage(unsigned stage) {
    const float scale[]={.22f,.36f,.64f,.95f};
    const float maturity[]={0,.20f,.70f,1.0f};
    const float cephalic[]={.20f,.44f,.78f,1.0f};
    const float trunk[]={1.95f,2.55f,3.18f,3.65f};
    const float chest[]={1.12f,1.28f,1.44f,1.56f};
    CreaturePhenotype p=Velkar_Adult();unsigned i=stage<4?stage:3;
    p.axial.totalScale=scale[i];p.axial.trunkLength=trunk[i];p.axial.thoraxHeight=chest[i];
    p.axial.thoraxWidth=1.02f+cephalic[i]*.46f;p.axial.abdomenWidth=.78f+cephalic[i]*.24f;
    p.axial.abdomenHeight=.78f+cephalic[i]*.20f;p.axial.pelvicWidth=.98f+cephalic[i]*.32f;
    p.axial.pelvicHeight=1.04f+cephalic[i]*.30f;p.axial.shoulderWidth=1.02f+cephalic[i]*.38f;
    p.axial.withersHeight=2.70f+cephalic[i]*.50f;p.axial.neckLength=.70f+cephalic[i]*.35f;
    p.axial.neckWidth=.54f+cephalic[i]*.22f;
    p.headEnvelope.scale*=1.30f-.30f*cephalic[i];
    /* La altura del tronco y la longitud vertical comparten la misma suela;
     * acortar sólo las patas dejaba a las crías fuera de alcance del suelo. */
    p.axial.withersHeight*=.72f+.28f*cephalic[i];
    for(unsigned leg=0;leg<4;++leg) {
        LimbPhenotype* limb=&p.limbs[leg];
        float sole=limb->thickness*.48f*limb->footScale*limb->footHeightScale;
        limb->length=p.axial.withersHeight-(leg>=2?p.axial.pelvicHeight:p.axial.thoraxHeight)*.5f-sole+.12f;
    }
    p.tails[0].length*=.55f+.45f*cephalic[i];
    p.head.skullWidth=.54f+cephalic[i]*.08f;p.head.skullHeight=.64f+cephalic[i]*.10f;
    p.head.muzzleLength=.20f+cephalic[i]*.08f;p.head.eyeSize=.58f-cephalic[i]*.10f;
    p.eyes.size=.64f-cephalic[i]*.12f;p.development=(CreatureDevelopment){.appendages=1,.cephalic=cephalic[i],.pigmentation=maturity[i],.integument=1,.colorMaturity=maturity[i]};
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
