/**
 * @file CreatureRecipeBrumacerno.c
 * @brief Receta ontogenética del herbívoro bovino Brumacerno.
 * @details Brumacervus silvaticus.
 */

#include "CreatureRecipes.h"
#include "HeadModule.h"
#include "SurfacePresets.h"
#include "AxialBody.h"
#include <pthread.h>

static CreatureRecipe brumacernoRecipe;
static pthread_once_t brumacernoOnce=PTHREAD_ONCE_INIT;

/** @brief Construye el plan corporal fijo con dos cuernos persistentes. */
static BodyPlan Brumacerno_BodyPlan(void) {
    return (BodyPlan){
        .root={1,AXIAL_NODE_PELVIS},.moduleCount=9,.modules={
            {.instanceId=1,.kind=CREATURE_MODULE_AXIAL},
            {.instanceId=2,.kind=CREATURE_MODULE_HEAD,.attachment={1,AXIAL_SLOT_CERVICAL}},
            {.instanceId=20,.kind=CREATURE_MODULE_TAIL,.attachment={1,AXIAL_SLOT_CAUDAL}},
            {.instanceId=10,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PECTORAL_LEFT},.phenotypeIndex=0},
            {.instanceId=11,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PECTORAL_RIGHT},.phenotypeIndex=1},
            {.instanceId=12,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PELVIC_LEFT},.phenotypeIndex=2},
            {.instanceId=13,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PELVIC_RIGHT},.phenotypeIndex=3},
            {.instanceId=30,.kind=CREATURE_MODULE_ORNAMENT,.attachment={2,HEAD_SLOT_HORN_LEFT},.phenotypeIndex=0},
            {.instanceId=31,.kind=CREATURE_MODULE_ORNAMENT,.attachment={2,HEAD_SLOT_HORN_RIGHT},.phenotypeIndex=1}}};
}

/** @brief Inicializa la anatomía adulta y su composición de módulos. */
static CreaturePhenotype Brumacerno_Adult(void) {
    CreaturePhenotype p={0};
    p.axial=(AxialPhenotype){.archetype=AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD,.totalScale=1,
        .withersHeight=3.9f,.trunkLength=4.4f,.neckLength=1.05f,.neckWidth=.95f,
        .shoulderWidth=2.2f,.thoraxWidth=2.5f,.thoraxHeight=2.55f,
        .abdomenWidth=2.15f,.abdomenHeight=2.05f,.pelvicWidth=2.25f,.pelvicHeight=2.2f,
        .bodyFlattening=.95f,.limbAttachmentLateral=.95f,.cervicalCurvature=.38f,
        .cervicalDorsalMass=1.55f,.cervicalMidNarrowing=.86f,.withersElevation=.18f};
    p.head=HeadPhenotype_BovinePreset();
    p.headEnvelope=(HeadEnvelopePhenotype){.scale=.90f,.widthScale=1.20f,.heightScale=1.16f,.lengthScale=.96f};
    p.eyes=(EyePhenotype){.size=.48f,.protrusion=.56f,.irisScale=.78f,.pupilScale=.40f,
        .pupilAspect=1,.pupilShape=PUPIL_ROUND,.scleraColor={52,42,30,255},
        .irisColor={92,62,28,255},.pupilColor={7,5,3,255}};
    p.limbCount=4;
    for (unsigned i=0;i<4;++i) {
        bool hind=i>=2; LimbPhenotype* l=&p.limbs[i];
        *l=(LimbPhenotype){.archetype=LIMB_ARCHETYPE_MAMMAL,.role=hind?LIMB_HIND:LIMB_FORE,
            .side=i%2?LIMB_RIGHT:LIMB_LEFT,.development=1,.length=hind?2.80f:2.65f,
            .thickness=hind?.48f:.43f,.sprawl=.01f,.footScale=1.08f,.footWidthScale=1.15f,
            .footHeightScale=1,.proximalScale=1.12f,.middleScale=1,.distalScale=.92f,
            .rootThicknessScale=hind?1.65f:1.28f,.middleThicknessScale=1.16f,
            .distalThicknessScale=1.08f,.digitThicknessScale=1.12f,.attachmentInset=.58f,
            .digitCount=2,.digitSpread=.12f};
        for (unsigned d=0;d<2;++d) {
            l->digitLengths[d]=.60f;
            l->digitPhalanges[d]=2;l->digitAngles[d]=((float)d-.5f)*.06f;
        }
    }
    p.tailCount=1;p.tails[0]=(TailPhenotype){.archetype=TAIL_ARCHETYPE_TAPERED,.length=.78f,
        .baseWidth=.18f,.baseHeight=.20f,.tipWidth=.045f,.tipHeight=.045f,.taperCurve=1.15f,
        .curvature=.18f,.rootMatchStrength=.62f,.segmentCount=6,.development=1};
    p.ornamentCount=2;
    p.ornaments[0]=(OrnamentPhenotype){.archetype=ORNAMENT_ARCHETYPE_HORN,.length=1.25f,
        .baseRadius=.28f,.tipRadius=.06f,.curvature=-.12f,.development=1,
        .growthDirection={-.52f,.80f,.72f},.lateralSweep=-.9f};
    p.ornaments[1]=p.ornaments[0];
    p.ornaments[1].growthDirection.x=.52f;p.ornaments[1].lateralSweep=.9f;
    p.surface=SurfacePreset_BovineShaggyCoat(3,1);
    p.dorsalColor=p.surface.pigment.secondaryColor;p.ventralColor=p.surface.pigment.ventralColor;
    p.unpigmentedVentralColor=p.surface.pigment.baseColor;
    return p;
}

/** @brief Genera una etapa bovina cambiando únicamente proporciones numéricas. */
static CreaturePhenotype Brumacerno_Stage(unsigned stage) {
    const float scale[]={.22f,.36f,.68f,1.0f};
    const float maturity[]={0,.18f,.68f,1.0f};
    const float cephalic[]={.20f,.42f,.76f,1.0f};
    const float trunk[]={2.25f,3.05f,3.85f,4.4f};
    const float chest[]={1.72f,2.02f,2.30f,2.55f};
    const float hornLength[]={.025f,.06f,.70f,1.25f};
    const float hornRadius[]={.012f,.025f,.16f,.28f};
    CreaturePhenotype p=Brumacerno_Adult();
    unsigned i=stage<4?stage:3;
    p.axial.totalScale=scale[i];p.axial.trunkLength=trunk[i];p.axial.thoraxHeight=chest[i];
    p.axial.thoraxWidth=1.55f+chest[i]*.37f;p.axial.abdomenWidth=1.40f+chest[i]*.29f;
    p.axial.abdomenHeight=1.28f+chest[i]*.30f;p.axial.pelvicWidth=1.52f+chest[i]*.29f;
    p.axial.pelvicHeight=1.42f+chest[i]*.31f;p.axial.shoulderWidth=1.55f+chest[i]*.25f;
    p.axial.withersHeight=2.55f+chest[i]*.53f;p.axial.neckLength=.66f+cephalic[i]*.39f;
    p.axial.neckWidth=.72f+cephalic[i]*.23f;
    p.headEnvelope.scale*=1.25f-.25f*cephalic[i];
    /* La altura del tronco y la longitud vertical comparten la misma suela;
     * acortar sólo las patas dejaba a las crías fuera de alcance del suelo. */
    p.axial.withersHeight*=.75f+.25f*cephalic[i];
    for(unsigned leg=0;leg<4;++leg) {
        LimbPhenotype* limb=&p.limbs[leg];
        float sole=limb->thickness*.48f*limb->footScale*limb->footHeightScale;
        limb->length=p.axial.withersHeight-(leg>=2?p.axial.pelvicHeight:p.axial.thoraxHeight)*.5f-sole+.12f;
    }
    p.tails[0].length*=.65f+.35f*cephalic[i];
    p.head.skullWidth=.68f+cephalic[i]*.12f;p.head.skullHeight=.64f+cephalic[i]*.08f;
    p.head.muzzleWidth=.70f+cephalic[i]*.18f;p.head.jawDepth=.58f+cephalic[i]*.20f;
    p.eyes.size=.56f-cephalic[i]*.08f;p.development=(CreatureDevelopment){.appendages=1,.cephalic=cephalic[i],.pigmentation=maturity[i],.integument=1,.colorMaturity=maturity[i]};
    p.surface=SurfacePreset_BovineShaggyCoat(3,maturity[i]);
    p.dorsalColor=p.surface.pigment.secondaryColor;p.ventralColor=p.surface.pigment.ventralColor;
    p.unpigmentedVentralColor=p.surface.pigment.baseColor;
    for (unsigned h=0;h<2;++h) { p.ornaments[h].length=hornLength[i];p.ornaments[h].baseRadius=hornRadius[i];p.ornaments[h].tipRadius=hornRadius[i]*.22f;p.ornaments[h].development=maturity[i]; }
    CreaturePhenotype_Normalize(&p);return p;
}

/** @brief Inicializa la receta registrada de Brumacerno una sola vez. */
static void Brumacerno_Init(void) {
    brumacernoRecipe.id=3;brumacernoRecipe.name="Brumacerno";brumacernoRecipe.gait=CREATURE_GAIT_BOVINE_WALK;
    brumacernoRecipe.bodyPlan=Brumacerno_BodyPlan();brumacernoRecipe.seed=Brumacerno_Stage(0);brumacernoRecipe.larva=Brumacerno_Stage(1);
    brumacernoRecipe.juvenile=Brumacerno_Stage(2);brumacernoRecipe.adult=Brumacerno_Stage(3);
}

/** @brief Devuelve la receta inmutable de Brumacerno. */
const CreatureRecipe* CreatureRecipes_Brumacerno(void) {
    pthread_once(&brumacernoOnce,Brumacerno_Init);return &brumacernoRecipe;
}
