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
        BodyPart lastPart2 = monster2->bodyParts[size2 - 1];
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
        BodyPart lastPart1 = monster1->bodyParts[size1 - 1];
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
        eDst->scale = Vec3_Lerp(e1->scale, e2->scale, perc);
        eDst->scleraColor = Color_Lerp(e1->scleraColor, e2->scleraColor, perc);
        eDst->pupilColor = Color_Lerp(e1->pupilColor, e2->pupilColor, perc);
        eDst->pupilScale = e1->pupilScale + perc * (e2->pupilScale - e1->pupilScale);
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
        mDst->lipThickness = m1->lipThickness + perc * (m2->lipThickness - m1->lipThickness);
        mDst->lipCurvature = m1->lipCurvature + perc * (m2->lipCurvature - m1->lipCurvature);
        mDst->lipProtrusion = m1->lipProtrusion + perc * (m2->lipProtrusion - m1->lipProtrusion);
    }

    /* 5. Mezclar transformaciones globales */
    dst->angle = monster1->angle + perc * (monster2->angle - monster1->angle);
    dst->updateSpeed = monster1->updateSpeed + perc * (monster2->updateSpeed - monster1->updateSpeed);
}

MonsterAger MonsterAger_Create(const Monster* first, const Monster* second, float perc) {
    MonsterAger ager;
    ager.monster1 = Monster_Clone(first);
    ager.monster2 = Monster_Clone(second);
    ager.perc = Math_Clamp01(perc);

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
