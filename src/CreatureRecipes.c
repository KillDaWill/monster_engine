#include "CreatureRecipes.h"
#include "SurfacePresets.h"
#include "AxialBody.h"
#include "AxialBodySprawlingTetrapod.h"
#include <pthread.h>
static void Recipe_Finalize(CreaturePhenotype* p) {
    const unsigned formula[2][5]={{2,3,4,5,3},{2,3,4,5,4}};
    const float pedal[5]={.48f,.69f,.88f,1.15f,.74f};
    const float angles[2][5]={{-.65f,-.30f,.02f,.36f,1.05f},{-.60f,-.24f,.06f,.37f,1.30f}};
    p->limbs[1]=p->limbs[0]; p->limbs[3]=p->limbs[2];
    for(size_t i=0;i<4;++i) {
        LimbPhenotype* l=&p->limbs[i]; l->role=i<2?LIMB_FORE:LIMB_HIND;
        l->side=i%2?LIMB_RIGHT:LIMB_LEFT; l->archetype=LIMB_ARCHETYPE_REPTILE;
        l->development=1; l->sprawl=1; l->footScale=1; l->digitSpread=1; l->digitCount=5;
        for(unsigned d=0;d<5;++d) {
            l->digitLengths[d]=i<2?p->limbs[0].digitLengths[d]:pedal[d];
            l->digitPhalanges[d]=formula[i>=2][d]; l->digitAngles[d]=angles[i>=2][d];
        }
    }
    p->dorsalColor=Color_Lerp(Color_FromRGB(42,72,34),Color_FromRGB(48,66,34),p->development.colorMaturity);
    p->ventralColor=Color_Lerp(Color_FromRGB(126,158,67),Color_FromRGB(112,142,62),p->development.colorMaturity);
    p->unpigmentedVentralColor=Color_FromRGB(255,254,246);
    CreaturePhenotype_Normalize(p);
}
static CreaturePhenotype Recipe_Base(void) {
    CreaturePhenotype p = {0};
    p.axial.archetype = AXIAL_ARCHETYPE_SPRAWLING_TETRAPOD;
    p.limbCount=4; p.tailCount=1;
    p.eyes=(EyePhenotype){.size=1,.protrusion=1,.irisScale=.72f,.pupilScale=.34f,
        .pupilAspect=.34f,.pupilShape=PUPIL_VERTICAL,.scleraColor={38,46,24,255},
        .irisColor={188,158,48,255},.pupilColor={4,7,3,255}};
    p.tails[0].segmentCount=4; p.tails[0].development=1;
    p.development = (CreatureDevelopment){
        .appendages = 1.0f,
        .cephalic = 1.0f,
        .pigmentation = 1.0f,
        .integument = 1.0f,
        .colorMaturity = 1.0f
    };
    p.surface=SurfacePreset_ScaledReptile(2,1);
    p.axial.totalScale = 1.0f;
    p.axial.neckLength = 1.15f; p.axial.neckWidth = 0.72f;
    p.axial.shoulderWidth = 1.55f;
    p.axial.thoraxWidth = 1.78f; p.axial.thoraxHeight = 1.28f;
    p.axial.abdomenWidth = 1.62f; p.axial.abdomenHeight = 1.14f;
    p.axial.pelvicWidth = 1.72f; p.axial.pelvicHeight = 1.18f;
    p.axial.trunkLength = 4.8f; p.axial.bodyFlattening = 0.92f;
    p.limbs[0].length = 2.15f; p.limbs[0].thickness = 0.27f;
    p.limbs[2].length = 2.55f; p.limbs[2].thickness = 0.36f;
    p.development.appendages = 1.0f;
    const float manualLengths[5] = {.60f, .82f, 1.0f, 1.06f, .70f};
    for (unsigned i=0;i<5;++i) p.limbs[0].digitLengths[i]=manualLengths[i];
    p.tails[0].length = 6.8f; p.tails[0].baseWidth = 0.68f; p.tails[0].baseHeight = 0.48f;
    p.tails[0].tipWidth = 0.055f; p.tails[0].tipHeight = 0.045f; p.tails[0].taperCurve = 1.35f;
    p.development.colorMaturity=1.0f; p.development.pigmentation=1.0f; p.development.cephalic=1.0f;
    p.eyes.size = 0.50f;
    p.head = HeadPhenotype_LizardPreset();
    Recipe_Finalize(&p);
    return p;
}

static CreaturePhenotype Recipe_Juvenile(void) {
    CreaturePhenotype p = Recipe_Base();
    p.surface=SurfacePreset_ScaledReptile(2,0);
    p.axial.totalScale = 0.58f;
    p.axial.neckLength = 0.98f; p.axial.neckWidth = 0.66f;
    p.axial.shoulderWidth = 1.45f; p.axial.thoraxWidth = 1.62f; p.axial.thoraxHeight = 1.08f;
    p.axial.abdomenWidth = 1.52f; p.axial.abdomenHeight = 1.04f;
    p.axial.pelvicWidth = 1.58f; p.axial.pelvicHeight = 1.02f; p.axial.trunkLength = 4.35f;
    p.limbs[0].length = 1.95f; p.limbs[0].thickness = 0.22f;
    p.limbs[2].length = 2.28f; p.limbs[2].thickness = 0.28f;
    p.tails[0].length = 5.9f; p.tails[0].baseWidth = 0.60f; p.tails[0].baseHeight = 0.42f;
    p.development.colorMaturity=0.0f;
    p.eyes.size = 0.67f;
    p.head.skullWidth = 0.60f; p.head.skullHeight = 0.30f; p.head.skullLength = 0.46f;
    p.head.muzzleLength = 0.50f; p.head.muzzleWidth = 0.58f; p.head.muzzleTaper = 0.56f;
    p.head.eyeSize = 0.67f; p.head.jawLength = 0.58f; p.head.jawDepth = 0.30f;
    p.head.jawStrength = 0.32f; p.head.cheekMass = 0.28f;
    Recipe_Finalize(&p);
    return p;
}

static CreaturePhenotype Recipe_Larva(void) {
    CreaturePhenotype p = Recipe_Base();
    p.surface=SurfacePreset_ScaledReptile(2,0);
    p.surface.integument.coverage=0;
    p.surface.pigment.baseColor=Color_FromRGB(242,241,230);
    p.surface.pigment.patternStrength=0;
    p.axial.totalScale = 0.38f;
    p.axial.neckLength = 0.45f; p.axial.neckWidth = 0.90f;
    p.axial.shoulderWidth = 0.92f;
    p.axial.thoraxWidth = 0.92f; p.axial.thoraxHeight = 0.92f;
    p.axial.abdomenWidth = 0.92f; p.axial.abdomenHeight = 0.92f;
    p.axial.pelvicWidth = 0.90f; p.axial.pelvicHeight = 0.90f;
    p.axial.trunkLength = 2.40f; p.axial.bodyFlattening = 1.0f;
    p.tails[0].length = 0.50f; p.tails[0].baseWidth = 0.44f; p.tails[0].baseHeight = 0.44f;
    p.tails[0].tipWidth = 0.16f; p.tails[0].tipHeight = 0.16f; p.tails[0].taperCurve = 1.0f;
    p.development.appendages = 0.0f;
    p.development.colorMaturity = 0.0f; p.development.pigmentation = 0.0f; p.development.cephalic = 0.0f;
    p.eyes.size = 0.0f;
    p.head.skullWidth = 0.50f; p.head.skullHeight = 0.55f; p.head.skullLength = 0.40f;
    p.head.muzzleLength = 0.18f; p.head.muzzleWidth = 0.45f; p.head.muzzleTaper = 0.05f;
    p.head.rostrumDepth = 0.10f; p.head.rostrumDorsalSlope = 0.50f;
    p.head.temporalWidth = 0.0f; p.head.temporalDepth = 0.0f;
    p.head.eyeSize = 0.0f; p.head.browProminence = 0.0f;
    p.head.snoutBluntness = 1.0f; p.head.jawLength = 0.0f;
    p.head.jawDepth = 0.0f; p.head.jawStrength = 0.0f;
    p.head.noseScale = 0.0f; p.head.cheekMass = 0.0f; p.head.tympanumSize = 0.0f;
    CreaturePhenotype_Normalize(&p);
    Recipe_Finalize(&p);
    return p;
}

static CreaturePhenotype Recipe_Seed(void) {
    CreaturePhenotype p = Recipe_Base();
    p.surface = SurfacePreset_ScaledReptile(2, 0);
    p.surface.integument.coverage = 0;
    p.surface.pigment.baseColor = Color_FromRGB(252, 252, 248);
    p.surface.pigment.patternStrength = 0;
    p.axial.totalScale = 0.38f;
    p.axial.neckLength = 0.08f; p.axial.neckWidth = 0.90f;
    p.axial.shoulderWidth = 0.92f;
    p.axial.thoraxWidth = 0.95f; p.axial.thoraxHeight = 0.95f;
    p.axial.abdomenWidth = 0.95f; p.axial.abdomenHeight = 0.95f;
    p.axial.pelvicWidth = 0.92f; p.axial.pelvicHeight = 0.92f;
    p.axial.trunkLength = 0.20f; p.axial.bodyFlattening = 1.0f;
    p.tails[0].length = 0.06f; p.tails[0].baseWidth = 0.25f; p.tails[0].baseHeight = 0.25f;
    p.tails[0].tipWidth = 0.04f; p.tails[0].tipHeight = 0.04f; p.tails[0].taperCurve = 1.0f;
    p.limbs[0].length = 0.05f; p.limbs[0].thickness = 0.05f;
    p.limbs[2].length = 0.05f; p.limbs[2].thickness = 0.05f;
    p.development.appendages = 0.0f;
    p.development.colorMaturity = 0.0f; p.development.pigmentation = 0.0f; p.development.cephalic = 0.0f;
    p.eyes.size = 0.0f;
    p.head.skullWidth = 0.42f; p.head.skullHeight = 0.42f; p.head.skullLength = 0.15f;
    p.head.muzzleLength = 0.04f; p.head.muzzleWidth = 0.35f; p.head.muzzleTaper = 0.05f;
    p.head.rostrumDepth = 0.05f; p.head.rostrumDorsalSlope = 0.50f;
    p.head.temporalWidth = 0.0f; p.head.temporalDepth = 0.0f;
    p.head.eyeSize = 0.0f; p.head.browProminence = 0.0f;
    p.head.snoutBluntness = 1.0f; p.head.jawLength = 0.0f;
    p.head.jawDepth = 0.0f; p.head.jawStrength = 0.0f;
    p.head.noseScale = 0.0f; p.head.cheekMass = 0.0f; p.head.tympanumSize = 0.0f;
    CreaturePhenotype_Normalize(&p);
    Recipe_Finalize(&p);
    return p;
}

static CreaturePhenotype Recipe_Adult(void) {
    CreaturePhenotype p = Recipe_Base();
    p.head.skullWidth = 0.64f; p.head.skullHeight = 0.36f; p.head.skullLength = 0.52f;
    p.head.muzzleLength = 0.68f; p.head.muzzleWidth = 0.62f; p.head.muzzleTaper = 0.48f;
    p.head.eyeSize = 0.48f; p.head.jawLength = 0.86f; p.head.jawDepth = 0.43f;
    p.head.jawStrength = 0.45f; p.head.cheekMass = 0.40f;
    p.head.browProminence = 0.42f; p.head.tympanumSize = 0.35f;
    p.head.snoutBluntness = 0.60f; p.head.rostrumDepth = 0.62f;
    p.head.rostrumDorsalSlope = 0.32f; p.head.temporalWidth = 0.52f;
    p.head.temporalDepth = 0.40f; p.head.noseScale = 0.30f;
    p.development.appendages = 1.0f;
    p.development.colorMaturity = 1.0f; p.development.pigmentation = 1.0f; p.development.cephalic = 1.0f;
    CreaturePhenotype_Normalize(&p);
    Recipe_Finalize(&p);
    return p;
}


static CreatureRecipe recipe;
static pthread_once_t recipeOnce=PTHREAD_ONCE_INIT;
static void InitRecipe(void) {
    recipe.id=1; recipe.name="Lagarto";
    recipe.gait=CREATURE_GAIT_SPRAWLING_QUADRUPED;
    recipe.seed=Recipe_Seed(); recipe.larva=Recipe_Larva();
    recipe.juvenile=Recipe_Juvenile(); recipe.adult=Recipe_Adult();
    recipe.bodyPlan=(BodyPlan){
        .root={.moduleInstanceId=1,.localNodeId=AXIAL_NODE_PELVIS},
        .moduleCount=7,
        .modules={
            {.instanceId=1,.kind=CREATURE_MODULE_AXIAL},
            {.instanceId=2,.kind=CREATURE_MODULE_HEAD,
             .attachment={.hostModuleInstanceId=1,.slotId=AXIAL_SPRAWLING_SLOT_CERVICAL}},
            {.instanceId=20,.kind=CREATURE_MODULE_TAIL,
             .attachment={.hostModuleInstanceId=1,.slotId=AXIAL_SPRAWLING_SLOT_CAUDAL}},
            {.instanceId=10,.kind=CREATURE_MODULE_LIMB,
             .attachment={.hostModuleInstanceId=1,.slotId=AXIAL_SPRAWLING_SLOT_PECTORAL_LEFT},.phenotypeIndex=0},
            {.instanceId=11,.kind=CREATURE_MODULE_LIMB,
             .attachment={.hostModuleInstanceId=1,.slotId=AXIAL_SPRAWLING_SLOT_PECTORAL_RIGHT},.phenotypeIndex=1},
            {.instanceId=12,.kind=CREATURE_MODULE_LIMB,
             .attachment={.hostModuleInstanceId=1,.slotId=AXIAL_SPRAWLING_SLOT_PELVIC_LEFT},.phenotypeIndex=2},
            {.instanceId=13,.kind=CREATURE_MODULE_LIMB,
             .attachment={.hostModuleInstanceId=1,.slotId=AXIAL_SPRAWLING_SLOT_PELVIC_RIGHT},.phenotypeIndex=3}}};
}
const CreatureRecipe* CreatureRecipes_Lizard(void) { pthread_once(&recipeOnce,InitRecipe); return &recipe; }
const CreatureRecipe* CreatureRecipes_Find(CreatureRecipeId id) { return id==1?CreatureRecipes_Lizard():id==2?CreatureRecipes_Dog():NULL; }

/* Las etapas provisionales comparten topología y proporciones: todavía no
 * representan ontogenia canina, solo escalas compatibles con la API. */
static CreatureRecipe dogRecipe;
static pthread_once_t dogOnce=PTHREAD_ONCE_INIT;
static void InitDogRecipe(void) {
    CreatureRecipe* r=&dogRecipe;
    r->id=2; r->name="Perro"; r->gait=CREATURE_GAIT_NONE;
    r->bodyPlan=(BodyPlan){.root={1,AXIAL_NODE_PELVIS},.moduleCount=7,.modules={
        {.instanceId=1,.kind=CREATURE_MODULE_AXIAL},
        {.instanceId=2,.kind=CREATURE_MODULE_HEAD,.attachment={1,AXIAL_SLOT_CERVICAL}},
        {.instanceId=20,.kind=CREATURE_MODULE_TAIL,.attachment={1,AXIAL_SLOT_CAUDAL}},
        {.instanceId=10,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PECTORAL_LEFT},.phenotypeIndex=0},
        {.instanceId=11,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PECTORAL_RIGHT},.phenotypeIndex=1},
        {.instanceId=12,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PELVIC_LEFT},.phenotypeIndex=2},
        {.instanceId=13,.kind=CREATURE_MODULE_LIMB,.attachment={1,AXIAL_SLOT_PELVIC_RIGHT},.phenotypeIndex=3}}};
    CreaturePhenotype* p=&r->adult;
    p->axial=(AxialPhenotype){.archetype=AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD,
        .totalScale=1,.withersHeight=4,.trunkLength=3.3f,.neckLength=.95f,.neckWidth=.72f,
        .shoulderWidth=1.25f,.thoraxWidth=1.40f,.thoraxHeight=1.92f,
        .abdomenWidth=.92f,.abdomenHeight=1.12f,.pelvicWidth=1.12f,.pelvicHeight=1.44f,
        .bodyFlattening=1,.limbAttachmentLateral=.82f,
        .cervicalCurvature=0.45f,.cervicalDorsalMass=1.20f,.cervicalMidNarrowing=0.82f,.withersElevation=0.08f};
    p->head=HeadPhenotype_CanidPreset();
    /* Envolvente baja y longitudinal: el stop lo resuelve la anatomía. */
    p->headEnvelope=(HeadEnvelopePhenotype){.scale=.62f,.widthScale=1.02f,.heightScale=1.12f,.lengthScale=1};
    p->eyes=(EyePhenotype){.size=.48f,.protrusion=.58f,.irisScale=.80f,.pupilScale=.42f,
        .pupilAspect=1,.pupilShape=PUPIL_ROUND,.scleraColor={52,38,26,255},
        .irisColor={65,42,24,255},.pupilColor={8,6,4,255}};
    p->limbCount=4;
    for (unsigned i=0;i<4;++i) {
        bool hind=i>=2;
        LimbPhenotype* l=&p->limbs[i];
        *l=(LimbPhenotype){.archetype=LIMB_ARCHETYPE_MAMMAL,.role=hind?LIMB_HIND:LIMB_FORE,
            .side=i%2?LIMB_RIGHT:LIMB_LEFT,.development=1,.thickness=.20f,
            .sprawl=.02f,.footScale=1,.footWidthScale=1,.footHeightScale=1,
            .proximalScale=1,.middleScale=1,.distalScale=1,
            .rootThicknessScale=hind?1.55f:1.05f,.middleThicknessScale=1,
            .distalThicknessScale=1,.digitThicknessScale=1,.attachmentInset=.55f,
            .digitCount=4,.digitSpread=.3f};
        l->length=p->axial.withersHeight-(hind?p->axial.pelvicHeight:p->axial.thoraxHeight)*.5f-l->thickness*.48f;
        for (unsigned d=0;d<4;++d) {
            l->digitLengths[d]=(d==0 || d==3)?.72f:.95f;
            l->digitPhalanges[d]=3; l->digitAngles[d]=((float)d-1.5f)*.12f;
        }
    }
    p->tailCount=1;
    p->tails[0]=(TailPhenotype){.archetype=TAIL_ARCHETYPE_TAPERED,.length=2.1f,
        .baseWidth=.23f,.baseHeight=.23f,.tipWidth=.045f,.tipHeight=.045f,
        .taperCurve=1.15f,.segmentCount=6,.development=1};
    p->development=(CreatureDevelopment){1,1,1,1,1};
    p->surface=SurfacePreset_CanidShortDoubleCoat(2,1.0f);
    p->dorsalColor=p->surface.pigment.secondaryColor;
    p->ventralColor=p->surface.pigment.ventralColor;
    p->unpigmentedVentralColor=p->surface.pigment.baseColor;
    CreaturePhenotype_Normalize(p);
    r->seed=r->larva=r->juvenile=r->adult;
    r->seed.axial.totalScale=.25f;r->larva.axial.totalScale=.40f;r->juvenile.axial.totalScale=.70f;
}
const CreatureRecipe* CreatureRecipes_Dog(void) { pthread_once(&dogOnce,InitDogRecipe); return &dogRecipe; }
