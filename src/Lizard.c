#include "Lizard.h"
#include "Monster.h"
#include "MathUtils.h"
#include "ColorPalette.h"
#include <math.h>

static float Lizard_Lerp(float a, float b, float t) { return a + (b - a) * t; }

static LizardPhenotype LizardPreset_Base(void) {
    LizardPhenotype p = {0};
    p.totalScale = 1.0f;
    p.neckLength = 1.15f; p.neckWidth = 0.72f;
    p.shoulderWidth = 1.55f;
    p.thoraxWidth = 1.78f; p.thoraxHeight = 1.28f;
    p.abdomenWidth = 1.62f; p.abdomenHeight = 1.14f;
    p.pelvicWidth = 1.72f; p.pelvicHeight = 1.18f;
    p.trunkLength = 4.8f; p.bodyFlattening = 0.92f;
    p.forelimbLength = 2.15f; p.forelimbThickness = 0.27f;
    p.hindlimbLength = 2.55f; p.hindlimbThickness = 0.36f;
    const float manualLengths[5] = {.60f, .82f, 1.0f, 1.06f, .70f};
    for (unsigned i=0;i<5;++i) p.manualDigitLengths[i]=manualLengths[i];
    p.tailLength = 6.8f; p.tailBaseWidth = 0.68f; p.tailBaseHeight = 0.48f;
    p.tailTipWidth = 0.055f; p.tailTipHeight = 0.045f; p.tailTaperCurve = 1.35f;
    p.colorMaturity=1.0f;
    p.eyeProportion = 0.50f;
    p.head = HeadPhenotype_LizardPreset();
    return p;
}

LizardPhenotype LizardPreset_Juvenile(void) {
    LizardPhenotype p = LizardPreset_Base();
    p.totalScale = 0.58f;
    p.neckLength = 0.98f; p.neckWidth = 0.66f;
    p.shoulderWidth = 1.45f; p.thoraxWidth = 1.62f; p.thoraxHeight = 1.08f;
    p.abdomenWidth = 1.52f; p.abdomenHeight = 1.04f;
    p.pelvicWidth = 1.58f; p.pelvicHeight = 1.02f; p.trunkLength = 4.35f;
    p.forelimbLength = 1.95f; p.forelimbThickness = 0.22f;
    p.hindlimbLength = 2.28f; p.hindlimbThickness = 0.28f;
    p.tailLength = 5.9f; p.tailBaseWidth = 0.60f; p.tailBaseHeight = 0.42f;
    p.colorMaturity=0.0f;
    p.eyeProportion = 0.67f;
    p.head.skullWidth = 0.60f; p.head.skullHeight = 0.30f; p.head.skullLength = 0.46f;
    p.head.muzzleLength = 0.50f; p.head.muzzleWidth = 0.58f; p.head.muzzleTaper = 0.56f;
    p.head.eyeSize = 0.67f; p.head.jawLength = 0.58f; p.head.jawDepth = 0.30f;
    p.head.jawStrength = 0.32f; p.head.cheekMass = 0.28f;
    return p;
}

LizardPhenotype LizardPreset_Adult(void) {
    LizardPhenotype p = LizardPreset_Base();
    p.head.skullWidth = 0.64f; p.head.skullHeight = 0.30f; p.head.skullLength = 0.50f;
    p.head.muzzleLength = 0.68f; p.head.muzzleWidth = 0.62f; p.head.muzzleTaper = 0.66f;
    p.head.eyeSize = 0.48f; p.head.jawLength = 0.86f; p.head.jawDepth = 0.43f;
    p.head.jawStrength = 0.66f; p.head.cheekMass = 0.48f;
    return p;
}

void LizardPhenotype_Normalize(LizardPhenotype* p) {
    if (!p) return;
#define POSITIVE(name, fallback) p->name = !isfinite(p->name) || p->name <= 0.0f ? fallback : Math_Clamp(p->name,(fallback)*.10f,(fallback)*4.0f)
    POSITIVE(totalScale, 1.0f); POSITIVE(neckLength, 1.0f); POSITIVE(neckWidth, 0.7f);
    POSITIVE(shoulderWidth, 1.5f); POSITIVE(thoraxWidth, 1.7f); POSITIVE(thoraxHeight, 0.75f);
    POSITIVE(abdomenWidth, 1.6f); POSITIVE(abdomenHeight, 0.7f);
    POSITIVE(pelvicWidth, 1.65f); POSITIVE(pelvicHeight, 0.72f); POSITIVE(trunkLength, 4.5f);
    POSITIVE(forelimbLength, 2.0f); POSITIVE(forelimbThickness, 0.18f);
    POSITIVE(hindlimbLength, 2.4f); POSITIVE(hindlimbThickness, 0.23f);
    POSITIVE(tailLength, 6.0f); POSITIVE(tailBaseWidth, 0.6f); POSITIVE(tailBaseHeight, 0.45f);
    POSITIVE(tailTipWidth, 0.05f); POSITIVE(tailTipHeight, 0.04f);
#undef POSITIVE
    const float manualDefaults[5] = {.60f, .82f, 1.0f, 1.06f, .70f};
    for (unsigned i=0;i<5;++i)
        p->manualDigitLengths[i] = isfinite(p->manualDigitLengths[i]) && p->manualDigitLengths[i]>0 ?
            Math_Clamp(p->manualDigitLengths[i],.35f,1.5f) : manualDefaults[i];
    p->totalScale = Math_Clamp(p->totalScale, 0.2f, 4.0f);
    if(!isfinite(p->bodyFlattening))p->bodyFlattening=.92f;
    if(!isfinite(p->tailTaperCurve))p->tailTaperCurve=1.35f;
    p->bodyFlattening = Math_Clamp(p->bodyFlattening, 0.30f, 1.0f);
    p->tailTaperCurve = Math_Clamp(p->tailTaperCurve, 0.6f, 2.5f);
    p->tailTipWidth = Math_Min(p->tailTipWidth, p->tailBaseWidth * 0.45f);
    p->tailTipHeight = Math_Min(p->tailTipHeight, p->tailBaseHeight * 0.45f);
    HeadPhenotype_Normalize(&p->head);
    p->eyeProportion=p->head.eyeSize;
    p->colorMaturity=isfinite(p->colorMaturity)?Math_Clamp01(p->colorMaturity):0;
    p->neckWidth=Math_Min(p->neckWidth,p->shoulderWidth*.80f);
}

LizardPhenotype LizardPhenotype_Interpolate(const LizardPhenotype* a,
                                             const LizardPhenotype* b, float age) {
    LizardPhenotype p = a ? *a : LizardPreset_Juvenile();
    LizardPhenotype q = b ? *b : LizardPreset_Adult();
    float t = isfinite(age) ? Math_Clamp01(age) : 0.0f;
    float linear = t;
    t = t*t*(3.0f-2.0f*t);
    float mature = t*t;
#define LERP_BODY(name) p.name = Lizard_Lerp(p.name, q.name, t)
    LERP_BODY(totalScale); LERP_BODY(neckLength); LERP_BODY(neckWidth);
    LERP_BODY(shoulderWidth); LERP_BODY(thoraxWidth); LERP_BODY(thoraxHeight);
    LERP_BODY(abdomenWidth); LERP_BODY(abdomenHeight); LERP_BODY(pelvicWidth);
    LERP_BODY(pelvicHeight); LERP_BODY(trunkLength); LERP_BODY(bodyFlattening);
    LERP_BODY(forelimbLength);
    p.forelimbThickness=Lizard_Lerp(p.forelimbThickness,q.forelimbThickness,mature);
    LERP_BODY(hindlimbLength);
    p.hindlimbThickness=Lizard_Lerp(p.hindlimbThickness,q.hindlimbThickness,mature);
    LERP_BODY(tailLength); LERP_BODY(tailBaseWidth); LERP_BODY(tailBaseHeight);
    LERP_BODY(tailTipWidth); LERP_BODY(tailTipHeight); LERP_BODY(tailTaperCurve);
    LERP_BODY(eyeProportion); LERP_BODY(colorMaturity);
#undef LERP_BODY
#define LERP_HEAD(name) p.head.name = Lizard_Lerp(p.head.name, q.head.name, t)
    LERP_HEAD(skullWidth); LERP_HEAD(skullHeight); LERP_HEAD(skullLength);
    LERP_HEAD(muzzleLength); LERP_HEAD(muzzleWidth); LERP_HEAD(muzzleTaper);
    LERP_HEAD(rostrumDepth); LERP_HEAD(rostrumDorsalSlope);
    LERP_HEAD(temporalWidth); LERP_HEAD(temporalDepth);
    p.head.eyeSize=Lizard_Lerp(p.head.eyeSize,q.head.eyeSize,linear); LERP_HEAD(eyeLaterality); LERP_HEAD(eyeForwardness);
    LERP_HEAD(eyeDorsality); LERP_HEAD(eyeExposure); LERP_HEAD(browProminence);
    LERP_HEAD(snoutBluntness);
    LERP_HEAD(jawLength); LERP_HEAD(jawDepth); p.head.jawStrength=Lizard_Lerp(p.head.jawStrength,q.head.jawStrength,mature);
    LERP_HEAD(noseScale); LERP_HEAD(earSize); LERP_HEAD(earPointiness);
    p.head.cheekMass=Lizard_Lerp(p.head.cheekMass,q.head.cheekMass,mature); LERP_HEAD(beakLength); LERP_HEAD(beakDepth);
    LERP_HEAD(beakTaper); LERP_HEAD(beakCurvature); LERP_HEAD(nostrilPosition);
    LERP_HEAD(orbitDepth); LERP_HEAD(tympanumSize);
#undef LERP_HEAD
    for (unsigned i=0;i<5;++i)
        p.manualDigitLengths[i]=Lizard_Lerp(p.manualDigitLengths[i],q.manualDigitLengths[i],t);
    LizardPhenotype_Normalize(&p);
    return p;
}

static bool Lizard_AddNode(AnatomyGraph* g, AnatomyId id, Vector3 center,
                           float width, float height, int color, AnatomyNodeRole role) {
    return AnatomyGraph_AddNode(g, (AnatomyNode){id, center, width, height, color, role});
}

static bool Lizard_AddEdge(AnatomyGraph* g, AnatomyId id, AnatomyId from,
                           AnatomyId to, BodyConnectionKind kind) {
    return AnatomyGraph_Connect(g, (BodyConnection){id, from, to, kind});
}

/* Perfil normalizado: falanges (incluido ungual), origen, abanico y curvatura.
 * El metapodio tiene dos estaciones propias y no cuenta como falange. */
typedef struct LizardDigitProfile {
    unsigned phalanges;
    float length, base, splay, bend;
} LizardDigitProfile;

static bool Lizard_AddAutopod(AnatomyGraph* g, AnatomyId handId, Vector3 hand,
    unsigned limb, bool hind, float length, float r, int color, const float manual[5]) {
    const unsigned formula[2][5]={{2,3,4,5,3},{2,3,4,5,4}};
    const float pedalLengths[5]={.48f,.69f,.88f,1.15f,.74f};
    const float angles[2][5]={{-.65f,-.30f,.02f,.36f,1.05f},
                             {-.60f,-.24f,.06f,.37f,1.30f}};
    float side=(limb%2==0)?1.0f:-1.0f, forward=hind?-1.0f:1.0f;
    for(unsigned digit=0;digit<5;++digit) {
        LizardDigitProfile p={formula[hind][digit],hind?pedalLengths[digit]:manual[digit],
            ((float)digit-2.0f)*.50f,angles[hind][digit],.34f};
        Vector3 root=Vec3_Create(hand.x+side*r*p.base,hand.y,
            hand.z+forward*r*(.32f-.20f*fabsf(p.base)));
        AnatomyId previous=handId;
        Vector3 position=root;
        float digitalLength=length*(hind?.29f:.26f)*p.length;
        /* Fracciones descendentes: articulaciones proximales largas, ungual corto. */
        float weights=0;
        for(unsigned j=0;j<p.phalanges;++j) weights+=1.0f-.12f*j;
        for(unsigned station=0;station<=p.phalanges+1;++station) {
            AnatomyId id=Anatomy_DigitId(limb,digit,station);
            float progress=station>0?(float)(station-1)/p.phalanges:0;
            float radius=r*(station==0?.40f:(.32f-.18f*progress));
            if(station>0) {
                float segment=station==1?r*.65f:digitalLength*(1.0f-.12f*(station-2))/weights;
                float angle=p.splay+p.bend*progress;
                position.x+=side*sinf(angle)*segment;
                position.z+=forward*cosf(angle)*segment;
                position.y+=segment*(station==p.phalanges+1?-.45f:.12f-.34f*progress);
            }
            if(!Lizard_AddNode(g,id,position,radius,radius*.88f,color,ANATOMY_ROLE_DIGIT) ||
               !Lizard_AddEdge(g,10000u+id,previous,id,BODY_CONNECTION_DIGIT_SEGMENT))return false;
            previous=id;
        }
    }
    return true;
}

static bool Lizard_AddLimb(AnatomyGraph* g, bool left, bool hind, const float manual[5],
                           float girdleX, float girdleY, float z, float length,
                           float thickness, int color) {
    float side = left ? 1.0f : -1.0f;
    AnatomyId base = hind ? (left ? ANATOMY_ID_HIND_LEFT_HIP : ANATOMY_ID_HIND_RIGHT_HIP)
                          : (left ? ANATOMY_ID_FORE_LEFT_SHOULDER : ANATOMY_ID_FORE_RIGHT_SHOULDER);
    float upper = length * (hind ? 0.36f : 0.34f);
    float lower = length * (hind ? 0.31f : 0.32f);
    Vector3 root = Vec3_Create(side * girdleX, girdleY, z);
    Vector3 elbow = Vec3_Create(side * (girdleX + upper), girdleY - length * 0.10f,
                                z + length * (hind ? 0.16f : -0.14f));
    Vector3 wrist = Vec3_Create(side * (girdleX + upper * 0.72f),
                                girdleY - lower * 0.98f,
                                z + (hind ? -lower * 0.36f : lower * 0.44f));
    Vector3 hand = Vec3_Create(side * (girdleX + upper * 0.78f),
                               girdleY - lower * 1.10f,
                               wrist.z + (hind ? -length * 0.16f : length * 0.14f));
    float r = thickness;
    if (!Lizard_AddNode(g, base, root, r * (hind ? 1.50f : 1.30f), r * 1.12f, color, ANATOMY_ROLE_JOINT) ||
        !Lizard_AddNode(g, base + 1, elbow, r, r * 0.82f, color, ANATOMY_ROLE_JOINT) ||
        !Lizard_AddNode(g, base + 2, wrist, r * 0.72f, r * 0.60f, color, ANATOMY_ROLE_JOINT) ||
        !Lizard_AddNode(g, base + 3, hand, r * (hind ? 1.40f : 1.25f), r * 0.42f, color, ANATOMY_ROLE_JOINT) ||
        !Lizard_AddEdge(g, 1000 + base, base, base + 1, BODY_CONNECTION_LIMB_SEGMENT) ||
        !Lizard_AddEdge(g, 1001 + base, base + 1, base + 2, BODY_CONNECTION_LIMB_SEGMENT) ||
        !Lizard_AddEdge(g, 1002 + base, base + 2, base + 3, BODY_CONNECTION_LIMB_SEGMENT)) return false;

    unsigned limbIndex = hind ? (left ? 2u : 3u) : (left ? 0u : 1u);
    return Lizard_AddAutopod(g,base+3,hand,limbIndex,hind,length,r,color,manual);
}

bool Lizard_ResolveAnatomy(const LizardPhenotype* source, AnatomyGraph* graph) {
    if (!source || !graph) return false;
    LizardPhenotype p = *source; LizardPhenotype_Normalize(&p);
    AnatomyGraph_Init(graph);
    float s = p.totalScale;
    float headY = 0.42f * s;
    float neckZ = -p.neckLength * s;
    float pectoralZ = neckZ - p.trunkLength * 0.10f * s;
    float thoraxAZ = neckZ - p.trunkLength * 0.28f * s;
    float thoraxPZ = neckZ - p.trunkLength * 0.49f * s;
    float abdomenZ = neckZ - p.trunkLength * 0.70f * s;
    float pelvisZ = neckZ - p.trunkLength * s;
    float vertical = p.bodyFlattening;
    if (!Lizard_AddNode(graph, ANATOMY_ID_HEAD, Vec3_Create(0, headY, -0.25f*s),
                        0.58f*s, 0.38f*s, 3, ANATOMY_ROLE_AXIAL) ||
        !Lizard_AddNode(graph, ANATOMY_ID_NECK, Vec3_Create(0, 0.30f*s, neckZ),
                        p.neckWidth*0.50f*s, p.neckWidth*0.46f*vertical*s, 2, ANATOMY_ROLE_AXIAL) ||
        !Lizard_AddNode(graph, ANATOMY_ID_PECTORAL, Vec3_Create(0, 0.28f*s, pectoralZ),
                        p.shoulderWidth*0.50f*s, p.thoraxHeight*0.48f*vertical*s, 2, ANATOMY_ROLE_AXIAL) ||
        !Lizard_AddNode(graph, ANATOMY_ID_THORAX_ANTERIOR, Vec3_Create(0, 0.34f*s, thoraxAZ),
                        p.thoraxWidth*0.50f*s, p.thoraxHeight*0.50f*vertical*s, 3, ANATOMY_ROLE_AXIAL) ||
        !Lizard_AddNode(graph, ANATOMY_ID_THORAX_POSTERIOR, Vec3_Create(0, 0.31f*s, thoraxPZ),
                        p.thoraxWidth*0.47f*s, p.thoraxHeight*0.47f*vertical*s, 3, ANATOMY_ROLE_AXIAL) ||
        !Lizard_AddNode(graph, ANATOMY_ID_ABDOMEN, Vec3_Create(0, 0.25f*s, abdomenZ),
                        p.abdomenWidth*0.50f*s, p.abdomenHeight*0.50f*vertical*s, 2, ANATOMY_ROLE_AXIAL) ||
        !Lizard_AddNode(graph, ANATOMY_ID_PELVIS, Vec3_Create(0, 0.24f*s, pelvisZ),
                        p.pelvicWidth*0.50f*s, p.pelvicHeight*0.50f*vertical*s, 2, ANATOMY_ROLE_AXIAL)) return false;

    AnatomyId axial[] = {ANATOMY_ID_HEAD, ANATOMY_ID_NECK, ANATOMY_ID_PECTORAL,
        ANATOMY_ID_THORAX_ANTERIOR, ANATOMY_ID_THORAX_POSTERIOR, ANATOMY_ID_ABDOMEN,
        ANATOMY_ID_PELVIS};
    for (size_t i = 0; i + 1 < sizeof(axial)/sizeof(axial[0]); ++i)
        if (!Lizard_AddEdge(graph, 10u + (AnatomyId)i, axial[i], axial[i+1], BODY_CONNECTION_AXIAL_LOFT)) return false;

    float tailT[] = {0.0f, 0.28f, 0.62f, 1.0f};
    AnatomyId tailIds[] = {ANATOMY_ID_TAIL_BASE, ANATOMY_ID_TAIL_MIDDLE,
                           ANATOMY_ID_TAIL_DISTAL, ANATOMY_ID_TAIL_TIP};
    for (size_t i = 0; i < 4; ++i) {
        float t = tailT[i], taper = powf(1.0f - t, p.tailTaperCurve);
        float wr = Lizard_Lerp(p.tailTipWidth, p.tailBaseWidth, taper) * s;
        float hr = Lizard_Lerp(p.tailTipHeight, p.tailBaseHeight, taper) * s;
        Vector3 c = Vec3_Create(0, (0.24f + 0.12f*t)*s, pelvisZ - (0.38f + p.tailLength*t)*s);
        if (!Lizard_AddNode(graph, tailIds[i], c, wr, hr, i < 2 ? 2 : 1, ANATOMY_ROLE_AXIAL)) return false;
    }
    if (!Lizard_AddEdge(graph, 30, ANATOMY_ID_PELVIS, ANATOMY_ID_TAIL_BASE, BODY_CONNECTION_AXIAL_LOFT)) return false;
    for (size_t i = 0; i < 3; ++i)
        if (!Lizard_AddEdge(graph, 31u + (AnatomyId)i, tailIds[i], tailIds[i+1], BODY_CONNECTION_AXIAL_LOFT)) return false;

    const AnatomyNode* pectoral = AnatomyGraph_FindNode(graph, ANATOMY_ID_PECTORAL);
    const AnatomyNode* pelvis = AnatomyGraph_FindNode(graph, ANATOMY_ID_PELVIS);
    if (!pectoral || !pelvis) return false;
    float shoulderX = p.shoulderWidth * 0.43f * s;
    float hipX = p.pelvicWidth * 0.43f * s;
    if (!Lizard_AddLimb(graph, true, false, p.manualDigitLengths, shoulderX, pectoral->center.y,
                        pectoral->center.z, p.forelimbLength*s, p.forelimbThickness*s, 2) ||
        !Lizard_AddLimb(graph, false, false, p.manualDigitLengths, shoulderX, pectoral->center.y,
                        pectoral->center.z, p.forelimbLength*s, p.forelimbThickness*s, 2) ||
        !Lizard_AddLimb(graph, true, true, p.manualDigitLengths, hipX, pelvis->center.y,
                        pelvis->center.z, p.hindlimbLength*s, p.hindlimbThickness*s, 2) ||
        !Lizard_AddLimb(graph, false, true, p.manualDigitLengths, hipX, pelvis->center.y,
                        pelvis->center.z, p.hindlimbLength*s, p.hindlimbThickness*s, 2)) return false;

    if (!Lizard_AddEdge(graph, 80, ANATOMY_ID_PECTORAL, ANATOMY_ID_FORE_LEFT_SHOULDER, BODY_CONNECTION_LIMB_SEGMENT) ||
        !Lizard_AddEdge(graph, 81, ANATOMY_ID_PECTORAL, ANATOMY_ID_FORE_RIGHT_SHOULDER, BODY_CONNECTION_LIMB_SEGMENT) ||
        !Lizard_AddEdge(graph, 82, ANATOMY_ID_PELVIS, ANATOMY_ID_HIND_LEFT_HIP, BODY_CONNECTION_LIMB_SEGMENT) ||
        !Lizard_AddEdge(graph, 83, ANATOMY_ID_PELVIS, ANATOMY_ID_HIND_RIGHT_HIP, BODY_CONNECTION_LIMB_SEGMENT)) return false;
    return AnatomyGraph_Validate(graph);
}

bool Lizard_BuildMonster(struct Monster* monster, const LizardPhenotype* source) {
    if (!monster || !source) return false;
    LizardPhenotype p = *source; LizardPhenotype_Normalize(&p);
    if (monster->bodyPartCount == 0) Monster_Init(monster);
    if (monster->bodyPartCount == 0) return false;
    monster->colorPalette = ColorPalette_CreateGradient(
        Color_Lerp(Color_FromRGB(42,72,34),Color_FromRGB(48,66,34),p.colorMaturity),
        Color_Lerp(Color_FromRGB(126,158,67),Color_FromRGB(112,142,62),p.colorMaturity), 6);
    BodyPart* host = Monster_GetHead(monster);
    float s = p.totalScale;
    host->position = host->oldPosition = host->positionRender = Vec3_Create(0, 0.42f*s, 0);
    host->width = host->widthRender = 2.05f*s;
    host->height = host->heightRender = 1.20f*s;
    host->length = host->lengthRender = 2.20f*s;
    host->color.index = 3; host->bellyColor.index = 0; host->bellyThreshold = 0.24f;
    if (!Lizard_ResolveAnatomy(&p, &monster->anatomyGraph)) return false;
    monster->hasAnatomyGraph = true;
    monster->lizardPhenotype = p;
    monster->hasLizardPhenotype = true;
    Head head = Head_Create(HEAD_ARCHETYPE_LIZARD, 0,
        Vec3_Create(host->widthRender*.5f, host->heightRender*.5f, host->lengthRender*.5f));
    head.phenotype = p.head;
    if (!Monster_SetHead(monster, head)) return false;
    for (size_t i = 0; i < monster->eyeCount; ++i) {
        monster->eyes[i].scleraColor = Color_FromRGB(38, 46, 24);
        monster->eyes[i].irisColor = Color_FromRGB(188, 158, 48);
        monster->eyes[i].pupilColor = Color_FromRGB(4, 7, 3);
        monster->eyes[i].irisScale = 0.72f;
        monster->eyes[i].pupilScale = 0.34f;
        monster->eyes[i].pupilAspect = 0.34f;
    }
    return true;
}
