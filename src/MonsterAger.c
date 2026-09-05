#include "MonsterAger.h"
#include "MathUtils.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

void MonsterAger_NormalizeEndpoints(Monster* monster1, Monster* monster2) {
    if (!monster1 || !monster2) return;

    size_t size1 = monster1->bodyPartCount;
    size_t size2 = monster2->bodyPartCount;

    /* 1. Igualar la cantidad de partes anatómicas */
    if (size1 > size2) {
        BodyPart lastPart2 = size2?monster2->bodyParts[size2 - 1]:(BodyPart){0};
        for (size_t i = 0; i < size1 - size2; ++i) {
            BodyPart part = monster1->bodyParts[size2 + i];
            part.position = lastPart2.position;
            part.oldPosition = lastPart2.oldPosition;
            part.positionRender = lastPart2.positionRender;
            part.width = 0.0f;
            part.height = 0.0f;
            part.length = 0.0f;
            part.widthRender = 0.0f;
            part.heightRender = 0.0f;
            part.lengthRender = 0.0f;
            part.groundOffset = lastPart2.groundOffset;
            part.groundOffsetRender = lastPart2.groundOffsetRender;
            part.traits = NULL;
            part.traitCount = 0;
            part.traitCapacity = 0;

            Monster_AddBodyPart(monster2, part);
        }
    } else if (size2 > size1) {
        BodyPart lastPart1 = size1?monster1->bodyParts[size1 - 1]:(BodyPart){0};
        for (size_t i = 0; i < size2 - size1; ++i) {
            BodyPart part = monster2->bodyParts[size1 + i];
            part.position = lastPart1.position;
            part.oldPosition = lastPart1.oldPosition;
            part.positionRender = lastPart1.positionRender;
            part.width = 0.0f;
            part.height = 0.0f;
            part.length = 0.0f;
            part.widthRender = 0.0f;
            part.heightRender = 0.0f;
            part.lengthRender = 0.0f;
            part.groundOffset = lastPart1.groundOffset;
            part.groundOffsetRender = lastPart1.groundOffsetRender;
            part.traits = NULL;
            part.traitCount = 0;
            part.traitCapacity = 0;

            Monster_AddBodyPart(monster1, part);
        }
    }

    /* 2. Igualar ojos */
    size_t eyes1 = monster1->eyeCount;
    size_t eyes2 = monster2->eyeCount;
    if (eyes1 > eyes2) {
        for (size_t i = 0; i < eyes1 - eyes2; ++i) {
            Eye eye = monster1->eyes[eyes2 + i];
            eye.scale = Vec3_Zero(); /* Escala 0 para que crezca progresivamente */
            Monster_AddEye(monster2, eye);
        }
    } else if (eyes2 > eyes1) {
        for (size_t i = 0; i < eyes2 - eyes1; ++i) {
            Eye eye = monster2->eyes[eyes1 + i];
            eye.scale = Vec3_Zero(); /* Escala 0 para que crezca progresivamente */
            Monster_AddEye(monster1, eye);
        }
    }

    /* 3. Igualar bocas */
    size_t mouths1 = monster1->mouthCount;
    size_t mouths2 = monster2->mouthCount;
    if (mouths1 > mouths2) {
        for (size_t i = 0; i < mouths1 - mouths2; ++i) {
            Mouth m = monster1->mouths[mouths2 + i];
            m.scale = Vec3_Zero();
            Monster_AddMouth(monster2, m);
        }
    } else if (mouths2 > mouths1) {
        for (size_t i = 0; i < mouths2 - mouths1; ++i) {
            Mouth m = monster2->mouths[mouths1 + i];
            m.scale = Vec3_Zero();
            Monster_AddMouth(monster1, m);
        }
    }

    /* 4. Igualar tamaños de paleta de colores */
    size_t colors1 = ColorPalette_GetCount(&monster1->colorPalette);
    size_t colors2 = ColorPalette_GetCount(&monster2->colorPalette);

    if (colors1 > colors2) {
        Color lastCol = ColorPalette_GetColor(&monster2->colorPalette, colors2 - 1);
        for (size_t i = 0; i < colors1 - colors2; ++i) {
            ColorPalette_AddColor(&monster2->colorPalette, lastCol);
        }
    } else if (colors2 > colors1) {
        Color lastCol = ColorPalette_GetColor(&monster1->colorPalette, colors1 - 1);
        for (size_t i = 0; i < colors2 - colors1; ++i) {
            ColorPalette_AddColor(&monster1->colorPalette, lastCol);
        }
    }
}

void MonsterAger_Interpolate(const Monster* monster1, const Monster* monster2, float perc, Monster* dst) {
    if (!monster1 || !monster2 || !dst) return;

    /* Una sola autoridad resuelve cuerpo, anfitrión cefálico y anclajes. */
    if (monster1->hasLizardPhenotype && monster2->hasLizardPhenotype) {
        LizardPhenotype phenotype=LizardPhenotype_Interpolate(
            &monster1->lizardPhenotype,&monster2->lizardPhenotype,perc);
        float open=monster1->head.anatomy.oralSystem.openFactor;
        open+=Math_Clamp01(perc)*(monster2->head.anatomy.oralSystem.openFactor-open);
        if (!Lizard_BuildMonster(dst,&phenotype)) return;
        Monster_SetHeadOpenFactor(dst,open);
        dst->angle=Math_Lerp(monster1->angle,monster2->angle,perc);
        dst->updateSpeed=Math_Lerp(monster1->updateSpeed,monster2->updateSpeed,perc);
        return;
    }

    /* 1. Sincronizar y mezclar la paleta de colores */
    dst->colorPalette.count = 0;
    size_t paletteSize = ColorPalette_GetCount(&monster1->colorPalette);
    for (size_t i = 0; i < paletteSize; ++i) {
        Color c1 = ColorPalette_GetColor(&monster1->colorPalette, i);
        Color c2 = ColorPalette_GetColor(&monster2->colorPalette, i);
        Color cLerp = Color_Lerp(c1, c2, perc);
        ColorPalette_AddColor(&dst->colorPalette, cLerp);
    }

    /* 2. Mezclar dimensiones y posiciones de las partes del cuerpo */
    size_t partsCount = monster1->bodyPartCount < monster2->bodyPartCount ? monster1->bodyPartCount : monster2->bodyPartCount;

    for (size_t i = 0; i < partsCount; ++i) {
        BodyPart* pDst = &dst->bodyParts[i];
        const BodyPart* p1 = &monster1->bodyParts[i];
        const BodyPart* p2 = &monster2->bodyParts[i];

        pDst->position = Vec3_Lerp(p1->position, p2->position, perc);
        pDst->oldPosition = Vec3_Lerp(p1->oldPosition, p2->oldPosition, perc);
        pDst->positionRender = Vec3_Lerp(p1->positionRender, p2->positionRender, perc);

        pDst->width = p1->width + perc * (p2->width - p1->width);
        pDst->height = p1->height + perc * (p2->height - p1->height);
        pDst->length = p1->length + perc * (p2->length - p1->length);
        pDst->groundOffset = p1->groundOffset + perc * (p2->groundOffset - p1->groundOffset);

        pDst->widthRender = pDst->width;
        pDst->heightRender = pDst->height;
        pDst->lengthRender = pDst->length;
        pDst->groundOffsetRender = pDst->groundOffset;

        pDst->bellyThreshold = p1->bellyThreshold + perc * (p2->bellyThreshold - p1->bellyThreshold);
    }

    /* 3. Mezclar Ojos */
    size_t eyeCount = monster1->eyeCount < monster2->eyeCount ? monster1->eyeCount : monster2->eyeCount;
    for (size_t i = 0; i < eyeCount; ++i) {
        Eye* eDst = &dst->eyes[i];
        const Eye* e1 = &monster1->eyes[i];
        const Eye* e2 = &monster2->eyes[i];

        eDst->bodyPartIndex = e1->bodyPartIndex;
        eDst->offset = Vec3_Lerp(e1->offset, e2->offset, perc);
        eDst->rotation = Vec3_Lerp(e1->rotation, e2->rotation, perc);
        eDst->forward = Vec3_Normalize(Vec3_Lerp(e1->forward, e2->forward, perc));
        eDst->scale = Vec3_Lerp(e1->scale, e2->scale, perc);
        eDst->scleraColor = Color_Lerp(e1->scleraColor, e2->scleraColor, perc);
        eDst->irisColor = Color_Lerp(e1->irisColor, e2->irisColor, perc);
        eDst->pupilColor = Color_Lerp(e1->pupilColor, e2->pupilColor, perc);
        eDst->irisScale = e1->irisScale + perc * (e2->irisScale - e1->irisScale);
        eDst->pupilScale = e1->pupilScale + perc * (e2->pupilScale - e1->pupilScale);
        eDst->pupilAspect = e1->pupilAspect + perc * (e2->pupilAspect - e1->pupilAspect);
    }

    /* 4. Mezclar Bocas */
    size_t mouthCount = monster1->mouthCount < monster2->mouthCount ? monster1->mouthCount : monster2->mouthCount;
    for (size_t i = 0; i < mouthCount; ++i) {
        Mouth* mDst = &dst->mouths[i];
        const Mouth* m1 = &monster1->mouths[i];
        const Mouth* m2 = &monster2->mouths[i];

        mDst->bodyPartIndex = m1->bodyPartIndex;
        mDst->offset = Vec3_Lerp(m1->offset, m2->offset, perc);
        mDst->rotation = Vec3_Lerp(m1->rotation, m2->rotation, perc);
        mDst->scale = Vec3_Lerp(m1->scale, m2->scale, perc);
        mDst->insideColor = Color_Lerp(m1->insideColor, m2->insideColor, perc);
        mDst->lipColor = Color_Lerp(m1->lipColor, m2->lipColor, perc);
        mDst->openFactor = m1->openFactor + perc * (m2->openFactor - m1->openFactor);
        mDst->shape = perc < .5f ? m1->shape : m2->shape;
        mDst->slitThickness = m1->slitThickness + perc * (m2->slitThickness - m1->slitThickness);
        mDst->slitSoftness = m1->slitSoftness + perc * (m2->slitSoftness - m1->slitSoftness);
        mDst->cornerRadius = m1->cornerRadius + perc * (m2->cornerRadius - m1->cornerRadius);
        mDst->jawPivot = Vec3_Lerp(m1->jawPivot, m2->jawPivot, perc);
        mDst->jawLength = m1->jawLength + perc * (m2->jawLength - m1->jawLength);
        mDst->jawWidth = m1->jawWidth + perc * (m2->jawWidth - m1->jawWidth);
        mDst->jawThickness = m1->jawThickness + perc * (m2->jawThickness - m1->jawThickness);
        mDst->jawRearMass = m1->jawRearMass + perc * (m2->jawRearMass - m1->jawRearMass);
        mDst->jawMuscle = m1->jawMuscle + perc * (m2->jawMuscle - m1->jawMuscle);
        mDst->maxJawAngle = m1->maxJawAngle + perc * (m2->maxJawAngle - m1->maxJawAngle);
        mDst->hingeRadius = m1->hingeRadius + perc * (m2->hingeRadius - m1->hingeRadius);
        mDst->throatRadius = m1->throatRadius + perc * (m2->throatRadius - m1->throatRadius);
        mDst->cranium = Vec3_Lerp(m1->cranium, m2->cranium, perc);
        mDst->snout = Vec3_Lerp(m1->snout, m2->snout, perc);
        mDst->cheeks = Vec3_Lerp(m1->cheeks, m2->cheeks, perc);
        mDst->brows = Vec3_Lerp(m1->brows, m2->brows, perc);
        Mouth_Normalize(mDst);
    }

    /* 5. Interpolar semántica de cabeza y volver a resolver landmarks. */
    if(monster1->hasHead && monster2->hasHead) {
        const HeadPhenotype* h1=&monster1->head.phenotype;
        const HeadPhenotype* h2=&monster2->head.phenotype;
        HeadPhenotype* hd=&dst->head.phenotype;
        *hd=*h1; hd->archetype=perc<.5f?h1->archetype:h2->archetype;
#define LERP_HEAD_FIELD(name) hd->name=h1->name+perc*(h2->name-h1->name)
        LERP_HEAD_FIELD(skullWidth); LERP_HEAD_FIELD(skullHeight); LERP_HEAD_FIELD(skullLength);
        LERP_HEAD_FIELD(muzzleLength); LERP_HEAD_FIELD(muzzleWidth); LERP_HEAD_FIELD(muzzleTaper);
        LERP_HEAD_FIELD(rostrumDepth); LERP_HEAD_FIELD(rostrumDorsalSlope);
        LERP_HEAD_FIELD(temporalWidth); LERP_HEAD_FIELD(temporalDepth);
        LERP_HEAD_FIELD(eyeSize); LERP_HEAD_FIELD(eyeLaterality); LERP_HEAD_FIELD(eyeForwardness);
        LERP_HEAD_FIELD(eyeDorsality); LERP_HEAD_FIELD(eyeExposure);
        LERP_HEAD_FIELD(browProminence); LERP_HEAD_FIELD(snoutBluntness);
        LERP_HEAD_FIELD(jawLength); LERP_HEAD_FIELD(jawDepth); LERP_HEAD_FIELD(jawStrength);
        LERP_HEAD_FIELD(noseScale); LERP_HEAD_FIELD(earSize); LERP_HEAD_FIELD(earPointiness);
        LERP_HEAD_FIELD(cheekMass); LERP_HEAD_FIELD(beakLength); LERP_HEAD_FIELD(beakDepth);
        LERP_HEAD_FIELD(beakTaper); LERP_HEAD_FIELD(beakCurvature); LERP_HEAD_FIELD(nostrilPosition);
        LERP_HEAD_FIELD(orbitDepth); LERP_HEAD_FIELD(tympanumSize);
#undef LERP_HEAD_FIELD
        HeadPhenotype_Normalize(hd); dst->hasHead=true;
        float open=mouthCount>0?dst->mouths[0].openFactor:dst->head.anatomy.oralSystem.openFactor;
        dst->head.anatomy.oralSystem.openFactor=open;
        Monster_ResolveHead(dst); Monster_SetHeadOpenFactor(dst,open);
    }

    /* 6. El cuerpo de lagarto se interpola en semántica y se vuelve a resolver. */
    if (monster1->hasLizardPhenotype && monster2->hasLizardPhenotype) {
        dst->lizardPhenotype=LizardPhenotype_Interpolate(&monster1->lizardPhenotype,
                                                         &monster2->lizardPhenotype,perc);
        dst->hasLizardPhenotype=true;
        dst->hasAnatomyGraph=Lizard_ResolveAnatomy(&dst->lizardPhenotype,&dst->anatomyGraph);
    }

    /* 7. Mezclar transformaciones globales */
    dst->angle = monster1->angle + perc * (monster2->angle - monster1->angle);
    dst->updateSpeed = monster1->updateSpeed + perc * (monster2->updateSpeed - monster1->updateSpeed);
}

MonsterAger MonsterAger_Create(const Monster* first, const Monster* second, float perc) {
    MonsterAger ager;
    ager.monster1 = Monster_Clone(first);
    ager.monster2 = Monster_Clone(second);
    ager.perc = Math_Clamp01(perc);

    if(!first->hasLizardPhenotype || !second->hasLizardPhenotype)
        MonsterAger_NormalizeEndpoints(&ager.monster1, &ager.monster2);

    ager.result = Monster_Clone(&ager.monster1);
    MonsterAger_Interpolate(&ager.monster1, &ager.monster2, ager.perc, &ager.result);

    return ager;
}

void MonsterAger_SetPerc(MonsterAger* ager, float perc) {
    if (!ager) return;
    ager->perc = Math_Clamp01(perc);
    MonsterAger_Interpolate(&ager->monster1, &ager->monster2, ager->perc, &ager->result);
}

Monster* MonsterAger_GetResult(MonsterAger* ager) {
    return ager ? &ager->result : NULL;
}

const Monster* MonsterAger_GetResultConst(const MonsterAger* ager) {
    return ager ? &ager->result : NULL;
}

void MonsterAger_Free(MonsterAger* ager) {
    if (!ager) return;
    Monster_Free(&ager->monster1);
    Monster_Free(&ager->monster2);
    Monster_Free(&ager->result);
}
