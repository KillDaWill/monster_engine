/**
 * @file PigmentPattern.c
 * @brief Implementación de algoritmos procedimentales de pigmentación.
 * @author Monster Engine Team
 * @date 2026
 */

#include "PigmentPattern.h"
#include "MathUtils.h"
#include <math.h>

static inline uint32_t Hash3(int x, int y, int z, uint32_t seed) {
    uint32_t h = seed ^ ((uint32_t)x * 73856093u) ^ ((uint32_t)y * 19349663u) ^ ((uint32_t)z * 83492791u);
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

static inline float HashFloat3(int x, int y, int z, uint32_t seed) {
    return (float)(Hash3(x, y, z, seed) & 0x00ffffffu) * (1.0f / 16777216.0f);
}

static float Smoothstep(float edge0, float edge1, float x) {
    if (edge1 <= edge0) return x >= edge0 ? 1.0f : 0.0f;
    float t = Math_Clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0f - 2.0f * t);
}

static float SurfaceHashFloat3(int x,int y,int z,uint32_t seed) {
    uint32_t h=(uint32_t)x*0x9e3779b9u^(uint32_t)y*0x85ebca6bu^seed^(uint32_t)z;
    h^=h>>16;h*=0x7feb352du;h^=h>>15;h*=0x846ca68bu;h^=h>>16;
    return (float)(h>>8)*(1.f/16777216.f);
}
static float ValueNoiseWithHash(Vector3 p,uint32_t seed,float (*hash)(int,int,int,uint32_t)) {
    int ix = (int)floorf(p.x);
    int iy = (int)floorf(p.y);
    int iz = (int)floorf(p.z);
    float fx = p.x - (float)ix;
    float fy = p.y - (float)iy;
    float fz = p.z - (float)iz;

    float ux = fx * fx * (3.0f - 2.0f * fx);
    float uy = fy * fy * (3.0f - 2.0f * fy);
    float uz = fz * fz * (3.0f - 2.0f * fz);

    float c000 = hash(ix,     iy,     iz,     seed);
    float c100 = hash(ix + 1, iy,     iz,     seed);
    float c010 = hash(ix,     iy + 1, iz,     seed);
    float c110 = hash(ix + 1, iy + 1, iz,     seed);
    float c001 = hash(ix,     iy,     iz + 1, seed);
    float c101 = hash(ix + 1, iy,     iz + 1, seed);
    float c011 = hash(ix,     iy + 1, iz + 1, seed);
    float c111 = hash(ix + 1, iy + 1, iz + 1, seed);

    float x00 = c000 + ux * (c100 - c000);
    float x10 = c010 + ux * (c110 - c010);
    float x01 = c001 + ux * (c101 - c001);
    float x11 = c011 + ux * (c111 - c011);

    float y0 = x00 + uy * (x10 - x00);
    float y1 = x01 + uy * (x11 - x01);

    return y0 + uz * (y1 - y0);
}
static float ValueNoise3D(Vector3 p,uint32_t seed){return ValueNoiseWithHash(p,seed,HashFloat3);}
static float SurfaceNoise3D(Vector3 p,uint32_t seed){return ValueNoiseWithHash(p,seed,SurfaceHashFloat3);}

static float SamplePattern(
    PigmentPattern pattern,
    Vector3 p,
    float scale,
    float sharpness,
    Vector3 direction,
    uint32_t seed,float (*noise)(Vector3,uint32_t)) {
    float sc = scale > 1e-4f ? scale : 1.0f;
    Vector3 q = Vec3_Scale(p, sc);
    float sh = Math_Clamp01(sharpness);

    switch (pattern) {
    case PIGMENT_PATTERN_SOLID:
        return 1.0f;

    case PIGMENT_PATTERN_NOISE:
        return noise(q, seed);

    case PIGMENT_PATTERN_SPOTS: {
        float n = noise(q, seed);
        float w = 0.16f * (1.0f - sh * 0.75f);
        return Smoothstep(0.56f - w, 0.56f + w, n);
    }

    case PIGMENT_PATTERN_BANDS: {
        float phase = (float)(seed & 1023u) * (6.2831853f / 1024.0f);
        float n = noise(q, seed);
        float wave = 0.5f + 0.5f * sinf(q.z * 4.5f + phase + n * 2.0f);
        float w = 0.18f * (1.0f - sh * 0.8f);
        return Smoothstep(0.50f - w, 0.50f + w, wave);
    }

    case PIGMENT_PATTERN_STRIPES: {
        float phase = (float)(seed & 1023u) * (6.2831853f / 1024.0f);
        float n = noise(q, seed);
        float wave = 0.5f + 0.5f * sinf(q.x * 6.5f + phase + n * 1.8f);
        float w = 0.16f * (1.0f - sh * 0.8f);
        return Smoothstep(0.50f - w, 0.50f + w, wave);
    }

    case PIGMENT_PATTERN_BLOTCHES: {
        float n1 = noise(Vec3_Scale(q, 0.40f), seed);
        float n2 = noise(Vec3_Scale(q, 0.85f), seed + 77u);
        float combined = n1 * 0.70f + n2 * 0.30f;
        float w = 0.15f * (1.0f - sh * 0.8f);
        return Smoothstep(0.48f - w, 0.48f + w, combined);
    }

    case PIGMENT_PATTERN_OCELLI: {
        float n = noise(q, seed);
        float ringDist = fabsf(n - 0.54f);
        float wRing = 0.10f * (1.0f - sh * 0.6f);
        float ring = 1.0f - Smoothstep(0.0f, wRing, ringDist);
        float center = 1.0f - Smoothstep(0.0f, 0.06f, fabsf(n - 0.72f));
        return fmaxf(ring, center);
    }

    case PIGMENT_PATTERN_GRADIENT: {
        float len = Vec3_Length(direction);
        Vector3 dir = len > 1e-4f ? Vec3_Scale(direction, 1.0f / len) : Vec3_Create(0.0f, -1.0f, 0.0f);
        float d = Vec3_Dot(p, dir) * sc;
        return Math_Clamp01(d * 0.5f + 0.5f);
    }

    default:
        return 0.0f;
    }
}
float PigmentPattern_Sample(PigmentPattern pattern,Vector3 p,float scale,float sharpness,Vector3 direction,uint32_t seed) {
    return SamplePattern(pattern,p,scale,sharpness,direction,seed,ValueNoise3D);
}
float PigmentPattern_SampleSurface(PigmentPattern pattern,Vector3 p,float scale,float sharpness,Vector3 direction,uint32_t seed) {
    return SamplePattern(pattern,p,fmaxf(scale,.01f),sharpness,direction,seed,SurfaceNoise3D);
}

Color PigmentPattern_EvaluateLayer(
    const PigmentLayer* layer,
    Vector3 p,
    Color baseColor) {
    if (!layer || layer->strength <= 1e-4f) return baseColor;
    float sample = PigmentPattern_Sample(
        layer->pattern,
        p,
        layer->scale,
        layer->sharpness,
        layer->direction,
        layer->seed
    );
    float blend = Math_Clamp01(sample * layer->strength * PigmentDomain_Weight(&layer->domain,p,layer->seed));
    return Color_Lerp(baseColor, layer->color, blend);
}

float PigmentDomain_Weight(const PigmentDomain* d,Vector3 p,uint32_t seed) {
    if(!d || d->radii.x<=0 || d->radii.y<=0 || d->radii.z<=0)return 1;
    Vector3 q=Vec3_Sub(p,d->center);
    q.x/=d->radii.x;q.y/=d->radii.y;q.z/=d->radii.z;
    /* Ondulación acotada y continua, idéntica en CPU/GLSL, sin costuras UV. */
    float phase=(float)(seed&1023u)*.00613592315f;
    float warp=sinf(q.x*3.1f+phase)*sinf(q.y*4.3f-phase)*sinf(q.z*3.7f+phase);
    float distance=Vec3_Length(q)+d->irregularity*.15f*warp;
    float softness=fmaxf(.001f,d->softness);
    float t=Math_Clamp01((1-distance)/softness);
    return t*t*(3-2*t);
}

float PigmentPattern_RegionWeight(const PigmentLayer* layer,unsigned primary,unsigned secondary,float blend) {
    if(!layer)return 0;
    if(!layer->regionMask)return 1;
    float a=primary<32&&(layer->regionMask&(1u<<primary))?1:0;
    float b=secondary<32&&(layer->regionMask&(1u<<secondary))?1:0;
    return Math_Lerp(a,b,Math_Clamp01(blend));
}
Color PigmentPattern_EvaluateRegionalLayer(const PigmentLayer* layer,Vector3 p,Color base,
    unsigned primary,unsigned secondary,float blend) {
    if(!layer)return base;
    PigmentLayer local=*layer;
    local.strength*=PigmentPattern_RegionWeight(layer,primary,secondary,blend);
    return PigmentPattern_EvaluateLayer(&local,p,base);
}
