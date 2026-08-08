#include "MonsterQueries.h"
#include "Monster.h"
#include <math.h>

/* Implementaciones de funciones de consulta sobre Monster */

float Monster_GetWorldHeight(Monster* monster, float worldX, float worldZ) {
    if (!monster || !monster->world) return 0.0f;

    if (monster->world->getWalkingHeight) {
        return monster->world->getWalkingHeight(monster->world, worldX, worldZ);
    }

    return 0.0f;
}

Vector3 Monster_GetCenter(const Monster* monster) {
    if (!monster || monster->bodyPartCount == 0) return Vec3_Zero();

    Vector3 sum = Vec3_Zero();
    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        sum = Vec3_Add(sum, monster->bodyParts[i].position);
    }

    return Vec3_Div(sum, (float)monster->bodyPartCount);
}

Vector3 Monster_GetNonWiggleCenter(const Monster* monster) {
    if (!monster || monster->bodyPartCount == 0) return Vec3_Zero();

    Vector3 sum = Vec3_Zero();
    size_t count = 0;

    const TraitType WIGGLE_TRAIT_TYPE = 99;

    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        if (BodyPart_ContainsTrait(&monster->bodyParts[i], WIGGLE_TRAIT_TYPE)) break;
        sum = Vec3_Add(sum, monster->bodyParts[i].position);
        count++;
    }

    if (count == 0) count = 1;
    return Vec3_Div(sum, (float)count);
}

float Monster_GetPartWidth(const Monster* monster, float index) {
    if (!monster || monster->bodyPartCount == 0) return 0.0f;

    int whole = (int)index;
    float fract = index - (float)whole;

    if (index <= 0.0f) return monster->bodyParts[0].width;

    size_t curIdx = (size_t)whole < monster->bodyPartCount ? (size_t)whole : monster->bodyPartCount - 1;
    size_t nextIdx = (size_t)(whole + 1) < monster->bodyPartCount ? (size_t)(whole + 1) : monster->bodyPartCount - 1;

    float w1 = monster->bodyParts[curIdx].width;
    float w2 = monster->bodyParts[nextIdx].width;

    return w1 + fract * (w2 - w1);
}

float Monster_GetPartHeight(const Monster* monster, float index) {
    if (!monster || monster->bodyPartCount == 0) return 0.0f;

    int whole = (int)index;
    float fract = index - (float)whole;

    if (index <= 0.0f) return monster->bodyParts[0].height;

    size_t curIdx = (size_t)whole < monster->bodyPartCount ? (size_t)whole : monster->bodyPartCount - 1;
    size_t nextIdx = (size_t)(whole + 1) < monster->bodyPartCount ? (size_t)(whole + 1) : monster->bodyPartCount - 1;

    float h1 = monster->bodyParts[curIdx].height;
    float h2 = monster->bodyParts[nextIdx].height;

    return h1 + fract * (h2 - h1);
}

Vector3 Monster_GetDirection(const Monster* monster, int index) {
    if (!monster || monster->bodyPartCount <= 1) return Vec3_Create(1.0f, 0.0f, 0.0f);

    if (index < 0) index = 0;
    if ((size_t)index >= monster->bodyPartCount) index = (int)monster->bodyPartCount - 1;

    if (index <= 0) {
        return Vec3_Sub(monster->bodyParts[0].position, monster->bodyParts[1].position);
    } else {
        return Vec3_Sub(monster->bodyParts[index - 1].position, monster->bodyParts[index].position);
    }
}

Vector3 Monster_GetDirectionFromNextPart(const Monster* monster, int index) {
    if (!monster || monster->bodyPartCount <= 1) return Vec3_Create(1.0f, 0.0f, 0.0f);

    if (index < 0) index = 0;
    if ((size_t)index >= monster->bodyPartCount) index = (int)monster->bodyPartCount - 1;

    if ((size_t)index >= monster->bodyPartCount - 1) {
        return Vec3_Sub(monster->bodyParts[index - 1].position, monster->bodyParts[index].position);
    } else {
        return Vec3_Sub(monster->bodyParts[index].position, monster->bodyParts[index + 1].position);
    }
}

float Monster_GetTotalLength(const Monster* monster) {
    if (!monster) return 0.0f;
    float total = 0.0f;
    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        total += monster->bodyParts[i].lengthRender;
    }
    return total;
}

Vector3 Monster_GetPosition(const Monster* monster, double percent) {
    if (!monster || monster->bodyPartCount == 0) return Vec3_Zero();
    if (monster->bodyPartCount == 1) return monster->bodyParts[0].positionRender;

    float totalLength = Monster_GetTotalLength(monster);

    if (percent < 0.0) {
        BodyPart first = monster->bodyParts[0];
        BodyPart second = monster->bodyParts[1];
        float segLen = first.lengthRender;
        if (segLen < 1e-6f) return first.positionRender;
        float factor = (float)(percent * totalLength / segLen);
        Vector3 dir = Vec3_Normalize(Vec3_Sub(first.positionRender, second.positionRender));
        return Vec3_Add(first.positionRender, Vec3_Scale(dir, -factor * segLen));
    }

    if (percent > 1.0) {
        BodyPart last = monster->bodyParts[monster->bodyPartCount - 1];
        BodyPart secondLast = monster->bodyParts[monster->bodyPartCount - 2];
        float segLen = last.lengthRender;
        if (segLen < 1e-6f) return last.positionRender;
        float factor = (float)((percent - 1.0) * totalLength / segLen);
        Vector3 dir = Vec3_Normalize(Vec3_Sub(last.positionRender, secondLast.positionRender));
        return Vec3_Add(last.positionRender, Vec3_Scale(dir, factor * segLen));
    }

    float targetLength = (float)(percent * totalLength);
    float accumulated = 0.0f;

    for (size_t i = 0; i < monster->bodyPartCount - 1; ++i) {
        BodyPart cur = monster->bodyParts[i];
        BodyPart next = monster->bodyParts[i + 1];

        accumulated += cur.lengthRender;

        if (accumulated >= targetLength) {
            float overshoot = accumulated - targetLength;
            float segLen = cur.lengthRender;
            if (segLen < 1e-6f) return cur.positionRender;
            float factor = (segLen - overshoot) / segLen;
            return Vec3_Lerp(cur.positionRender, next.positionRender, factor);
        }
    }

    return monster->bodyParts[monster->bodyPartCount - 1].positionRender;
}

AABB3D Monster_GetBoundingBox(const Monster* monster, AABB3D dst) {
    dst.start = Vec3_Create(1e9f, 1e9f, 1e9f);
    dst.end = Vec3_Create(-1e9f, -1e9f, -1e9f);

    if (!monster) return dst;

    for (size_t i = 0; i < monster->bodyPartCount; ++i) {
        Vector3 pos = monster->bodyParts[i].positionRender;
        dst.start.x = fminf(dst.start.x, pos.x);
        dst.start.y = fminf(dst.start.y, pos.y);
        dst.start.z = fminf(dst.start.z, pos.z);

        dst.end.x = fmaxf(dst.end.x, pos.x);
        dst.end.y = fmaxf(dst.end.y, pos.y);
        dst.end.z = fmaxf(dst.end.z, pos.z);
    }

    return dst;
}
