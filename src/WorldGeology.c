/**
 * @file WorldGeology.c
 * @brief Generación determinista de variantes SDF de roca, colocación espacialmente acelerada
 *        y consultas O(1) de colisión y oclusión 3D.
 */
#include "WorldGeology.h"
#include "TerrainGenerator.h"
#include <float.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static uint32_t nextRandU32(uint32_t *state) {
    uint32_t x = *state ? *state : 1u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static float nextRand01(uint32_t *state) {
    return (float)(nextRandU32(state) & 0xFFFFFFu) / 16777216.0f;
}

static float nextRandRange(uint32_t *state, float lo, float hi) {
    return lo + (hi - lo) * nextRand01(state);
}

static void fnv1aUpdate(uint64_t *h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; ++i) {
        *h ^= (uint64_t)p[i];
        *h *= UINT64_C(1099511628211);
    }
}

static float ellipsoidSDF(Vector3 p, Vector3 r) {
    Vector3 q = Vec3_Create(p.x / r.x, p.y / r.y, p.z / r.z);
    float k0 = Vec3_Length(q);
    if (k0 < 1e-6f) return -fminf(r.x, fminf(r.y, r.z));
    Vector3 q2 = Vec3_Create(p.x / (r.x * r.x), p.y / (r.y * r.y), p.z / (r.z * r.z));
    float k1 = Vec3_Length(q2);
    return k0 * (k0 - 1.0f) / fmaxf(1e-6f, k1);
}

float WorldGeology_EvaluateVariantSDF(const WorldRockVariant *v, Vector3 p) {
    if (!v) return 1.0f;
    float d;
    if (v->family == ROCK_FAMILY_CLUSTER && v->lobeCount > 1u) {
        d = FLT_MAX;
        for (uint32_t i = 0; i < v->lobeCount; ++i) {
            Vector3 q = Vec3_Sub(p, v->lobeCenters[i]);
            float di = Vec3_Length(q) - v->lobeRadii[i];
            /* Unión suave para conglomerado orgánico */
            if (i == 0u) d = di;
            else {
                float k = 0.14f;
                float h = fmaxf(k - fabsf(d - di), 0.0f) / k;
                d = fminf(d, di) - h * h * k * 0.25f;
            }
        }
    } else {
        d = ellipsoidSDF(p, v->baseRadii);
    }

    /* Planos de fractura suavizados (intersección conservadora) */
    for (uint32_t i = 0; i < v->planeCount; ++i) {
        float dp = Vec3_Dot(p, v->planeNormals[i]) - v->planeOffsets[i];
        float k = (v->family == ROCK_FAMILY_FRACTURED) ? 0.05f : 0.12f;
        float h = fmaxf(k - fabsf(d - dp), 0.0f) / k;
        d = fmaxf(d, dp) + h * h * k * 0.25f;
    }

    /* Desplazamiento superficial acotado determinista (3D vía combinación de 2D simplex) */
    if (v->noiseAmp > 1e-4f) {
        float f = v->noiseFreq;
        float n1 = TerrainNoise_Simplex2D(p.x * f + p.y * f * 0.71f, p.z * f - p.y * f * 0.53f, v->seed);
        float n2 = TerrainNoise_Simplex2D(p.y * f * 1.4f + 7.3f, p.x * f * 1.3f - p.z * f * 0.9f, v->seed + 37u);
        d += (0.65f * n1 + 0.35f * n2) * v->noiseAmp;
    }
    return d;
}

static void buildVariantSpec(WorldRockVariant *v, uint32_t idx, uint32_t seed) {
    memset(v, 0, sizeof(*v));
    v->variantId = idx;
    v->seed = seed;
    uint32_t rng = seed;
    v->family = (WorldRockFamily)(idx % 4u);

    if (v->family == ROCK_FAMILY_BOULDER) {
        v->baseRadii = Vec3_Create(nextRandRange(&rng, 0.78f, 1.05f),
                                   nextRandRange(&rng, 0.52f, 0.80f),
                                   nextRandRange(&rng, 0.75f, 1.05f));
        v->planeCount = 2u;
        v->noiseAmp = 0.06f;
        v->noiseFreq = 2.2f;
    } else if (v->family == ROCK_FAMILY_FRACTURED) {
        v->baseRadii = Vec3_Create(nextRandRange(&rng, 0.82f, 1.12f),
                                   nextRandRange(&rng, 0.68f, 1.05f),
                                   nextRandRange(&rng, 0.76f, 1.08f));
        v->planeCount = 5u;
        v->noiseAmp = 0.05f;
        v->noiseFreq = 2.8f;
    } else if (v->family == ROCK_FAMILY_BEDROCK) {
        v->baseRadii = Vec3_Create(nextRandRange(&rng, 1.05f, 1.35f),
                                   nextRandRange(&rng, 0.45f, 0.70f),
                                   nextRandRange(&rng, 0.85f, 1.20f));
        v->planeCount = 4u;
        v->noiseAmp = 0.07f;
        v->noiseFreq = 2.1f;
    } else {
        /* ROCK_FAMILY_CLUSTER */
        v->lobeCount = 3u;
        v->lobeCenters[0] = Vec3_Create(0.0f, 0.0f, 0.0f);
        v->lobeRadii[0] = nextRandRange(&rng, 0.62f, 0.78f);
        float a1 = nextRandRange(&rng, 0.0f, 6.2831853f);
        float a2 = a1 + nextRandRange(&rng, 1.7f, 2.7f);
        v->lobeCenters[1] = Vec3_Create(cosf(a1) * 0.55f, nextRandRange(&rng, -0.15f, 0.12f), sinf(a1) * 0.55f);
        v->lobeRadii[1] = nextRandRange(&rng, 0.42f, 0.58f);
        v->lobeCenters[2] = Vec3_Create(cosf(a2) * 0.52f, nextRandRange(&rng, -0.18f, 0.10f), sinf(a2) * 0.52f);
        v->lobeRadii[2] = nextRandRange(&rng, 0.36f, 0.52f);
        v->baseRadii = Vec3_Create(1.0f, 0.75f, 1.0f);
        v->planeCount = 2u;
        v->noiseAmp = 0.055f;
        v->noiseFreq = 2.5f;
    }

    for (uint32_t p = 0; p < v->planeCount; ++p) {
        float az = nextRandRange(&rng, 0.0f, 6.2831853f);
        float el = nextRandRange(&rng, -0.25f, 0.85f);
        Vector3 n = Vec3_Normalize(Vec3_Create(cosf(az) * cosf(el), sinf(el), sinf(az) * cosf(el)));
        v->planeNormals[p] = n;
        v->planeOffsets[p] = nextRandRange(&rng, 0.55f, 0.86f);
    }

    if (v->lobeCount == 0u) {
        v->lobeCount = 1u;
        v->lobeCenters[0] = Vec3_Zero();
        v->lobeRadii[0] = fmaxf(v->baseRadii.x, v->baseRadii.z);
    }

    /* Medir cotas reales del SDF en una rejilla fina determinista para ajustar proxies. */
    float maxR = 0.0f;
    float minY = FLT_MAX, maxY = -FLT_MAX;
    const int N = 16;
    const float ext = 1.6f;
    const float step = (2.0f * ext) / (float)N;
    for (int iz = 0; iz <= N; ++iz) {
        float z = -ext + (float)iz * step;
        for (int iy = 0; iy <= N; ++iy) {
            float y = -ext + (float)iy * step;
            for (int ix = 0; ix <= N; ++ix) {
                float x = -ext + (float)ix * step;
                if (WorldGeology_EvaluateVariantSDF(v, Vec3_Create(x, y, z)) <= 0.02f) {
                    float r = sqrtf(x * x + z * z);
                    if (r > maxR) maxR = r;
                    if (y < minY) minY = y;
                    if (y > maxY) maxY = y;
                }
            }
        }
    }
    if (maxR < 0.25f) {
        maxR = fmaxf(v->baseRadii.x, v->baseRadii.z);
        minY = -v->baseRadii.y;
        maxY = v->baseRadii.y;
    }
    v->conservativeRadiusXZ = maxR + 0.04f;
    v->conservativeMinY = minY - 0.04f;
    v->conservativeMaxY = maxY + 0.04f;
}

static bool buildRockGrid(WorldGeology *geo, const TerrainHeightfield *tf) {
    WorldRockGrid *g = &geo->grid;
    uint32_t dim = (uint32_t)ceilf(tf->span / 6.0f);
    if (dim < 8u) dim = 8u;
    if (dim > 96u) dim = 96u;
    g->dim = dim;
    g->originX = tf->originX;
    g->originZ = tf->originZ;
    g->cellSize = tf->span / (float)dim;
    g->invCellSize = 1.0f / g->cellSize;

    size_t cellCount = (size_t)dim * (size_t)dim;
    g->cellOffsets = calloc(cellCount + 1u, sizeof(uint32_t));
    if (!g->cellOffsets) return false;

    /* Contar entradas por celda para todas las rocas colisionables. */
    for (uint32_t i = 0; i < geo->rockCount; ++i) {
        const WorldRockInstance *rk = &geo->rocks[i];
        if (!rk->collides) continue;
        int x0 = (int)floorf((rk->position.x - rk->boundRadiusXZ - g->originX) * g->invCellSize);
        int x1 = (int)floorf((rk->position.x + rk->boundRadiusXZ - g->originX) * g->invCellSize);
        int z0 = (int)floorf((rk->position.z - rk->boundRadiusXZ - g->originZ) * g->invCellSize);
        int z1 = (int)floorf((rk->position.z + rk->boundRadiusXZ - g->originZ) * g->invCellSize);
        if (x0 < 0) x0 = 0;
        if (z0 < 0) z0 = 0;
        if (x1 >= (int)dim) x1 = (int)dim - 1;
        if (z1 >= (int)dim) z1 = (int)dim - 1;
        for (int cz = z0; cz <= z1; ++cz) {
            for (int cx = x0; cx <= x1; ++cx) {
                ++g->cellOffsets[(size_t)cz * dim + (size_t)cx + 1u];
            }
        }
    }
    for (size_t c = 0; c < cellCount; ++c) {
        g->cellOffsets[c + 1u] += g->cellOffsets[c];
    }
    g->totalEntries = g->cellOffsets[cellCount];
    g->cellIndices = malloc((g->totalEntries > 0u ? g->totalEntries : 1u) * sizeof(uint32_t));
    uint32_t *cursor = malloc(cellCount * sizeof(uint32_t));
    if (!g->cellIndices || !cursor) { free(cursor); return false; }
    memcpy(cursor, g->cellOffsets, cellCount * sizeof(uint32_t));

    for (uint32_t i = 0; i < geo->rockCount; ++i) {
        const WorldRockInstance *rk = &geo->rocks[i];
        if (!rk->collides) continue;
        int x0 = (int)floorf((rk->position.x - rk->boundRadiusXZ - g->originX) * g->invCellSize);
        int x1 = (int)floorf((rk->position.x + rk->boundRadiusXZ - g->originX) * g->invCellSize);
        int z0 = (int)floorf((rk->position.z - rk->boundRadiusXZ - g->originZ) * g->invCellSize);
        int z1 = (int)floorf((rk->position.z + rk->boundRadiusXZ - g->originZ) * g->invCellSize);
        if (x0 < 0) x0 = 0;
        if (z0 < 0) z0 = 0;
        if (x1 >= (int)dim) x1 = (int)dim - 1;
        if (z1 >= (int)dim) z1 = (int)dim - 1;
        for (int cz = z0; cz <= z1; ++cz) {
            for (int cx = x0; cx <= x1; ++cx) {
                size_t ci = (size_t)cz * dim + (size_t)cx;
                g->cellIndices[cursor[ci]++] = i;
            }
        }
    }
    free(cursor);
    geo->memoryBytes += (cellCount + 1u) * sizeof(uint32_t) + g->totalEntries * sizeof(uint32_t);
    return true;
}

void WorldGeology_Free(WorldGeology *geo) {
    if (!geo) return;
    free(geo->variants);
    free(geo->rocks);
    free(geo->grid.cellOffsets);
    free(geo->grid.cellIndices);
    memset(geo, 0, sizeof(*geo));
}

bool WorldGeology_Generate(WorldGeology *geo, const TerrainHeightfield *tf,
                           const WorldHydrology *hydro, const WorldBiomeField *biome,
                           const WorldRecipe *recipe, uint32_t geologySeed) {
    if (!geo || !tf || !recipe) return false;
    memset(geo, 0, sizeof(*geo));

    uint32_t vCount = recipe->geology.rockVariants;
    if (vCount < 1u) vCount = 1u;
    if (vCount > WORLD_MAX_ROCK_VARIANTS) vCount = WORLD_MAX_ROCK_VARIANTS;

    geo->variants = calloc(vCount, sizeof(WorldRockVariant));
    geo->rocks = calloc(WORLD_MAX_ROCK_INSTANCES, sizeof(WorldRockInstance));
    if (!geo->variants || !geo->rocks) {
        WorldGeology_Free(geo);
        return false;
    }
    geo->variantCount = vCount;
    geo->memoryBytes = vCount * sizeof(WorldRockVariant) + WORLD_MAX_ROCK_INSTANCES * sizeof(WorldRockInstance);

    for (uint32_t i = 0; i < vCount; ++i) {
        buildVariantSpec(&geo->variants[i], i, WorldSeed_Derive(geologySeed, WORLD_SEED_GEOLOGY, i + 1u));
    }

    if (recipe->flat || recipe->geology.rockDensity <= 1e-4f) {
        return buildRockGrid(geo, tf);
    }

    /* Colocación determinista acelerada por rejilla de celdas (estilo Poisson estratificado):
     * nunca O(N^2), favorece laderas, zonas rocosas y agrupaciones por ruido sin bloquear orillas. */
    uint32_t rng = WorldSeed_Derive(geologySeed, WORLD_SEED_GEOLOGY, 1000u);
    uint32_t clusterSeed = WorldSeed_Derive(geologySeed, WORLD_SEED_GEOLOGY, 1001u);

    float density = recipe->geology.rockDensity;
    float baseCell = fmaxf(5.5f, 15.0f - 8.5f * density);
    uint32_t gDim = (uint32_t)floorf(tf->span / baseCell);
    if (gDim < 4u) gDim = 4u;
    if (gDim > 48u) gDim = 48u;
    float cellSpan = tf->span / (float)gDim;
    float borderMargin = 3.0f;

    /* Mantener lista compacta de ubicaciones en celdas vecinas para evitar solapes severos. */
    int32_t *cellRockIdx = malloc((size_t)gDim * (size_t)gDim * sizeof(int32_t));
    if (!cellRockIdx) { WorldGeology_Free(geo); return false; }
    for (size_t i = 0; i < (size_t)gDim * (size_t)gDim; ++i) cellRockIdx[i] = -1;

    for (uint32_t gz = 0; gz < gDim && geo->rockCount < WORLD_MAX_ROCK_INSTANCES; ++gz) {
        for (uint32_t gx = 0; gx < gDim && geo->rockCount < WORLD_MAX_ROCK_INSTANCES; ++gx) {
            float wx = tf->originX + ((float)gx + nextRandRange(&rng, 0.15f, 0.85f)) * cellSpan;
            float wz = tf->originZ + ((float)gz + nextRandRange(&rng, 0.15f, 0.85f)) * cellSpan;
            if (fabsf(wx) > tf->halfSize - borderMargin || fabsf(wz) > tf->halfSize - borderMargin) continue;

            /* Evaluar idoneidad geológica (pendiente, altitud, ruido de agrupación, agua). */
            SurfaceHit hit;
            if (!Terrain_SampleGround(tf, wx, wz, &hit)) continue;
            WaterSample ws = {0};
            if (hydro && WorldHydrology_SampleWater(hydro, tf, wx, wz, &ws) && ws.wet && ws.depth > 0.25f) {
                continue;
            }
            WorldBiomeSample bs = {0};
            float rockBias = 0.25f;
            if (biome && WorldBiome_Sample(biome, tf, wx, wz, &bs)) {
                rockBias = 0.18f + 0.65f * bs.matRock + 0.35f * bs.altitudeNorm;
                if (bs.slopeRadians > 0.30f) rockBias += 0.25f;
            }
            float clusterN = 0.5f + 0.5f * TerrainNoise_Fbm2D(wx * 0.025f, wz * 0.025f, clusterSeed, 2u, 0.5f, 2.0f);
            float prob = density * (0.30f + 0.70f * rockBias) * (0.45f + 0.90f * clusterN);
            if (nextRand01(&rng) > prob) continue;

            /* No obstruir sitios de orilla obligatorios ni el corredor central inmediato si es pequeño. */
            if (hydro && hydro->shoreSiteCount > 0u) {
                const WorldShoreSite *nearShore = WorldHydrology_FindNearestShore(hydro, hit.position, 4.2f, 0u);
                if (nearShore) continue;
            }

            uint32_t vIdx = nextRandU32(&rng) % vCount;
            const WorldRockVariant *var = &geo->variants[vIdx];
            float sc = nextRandRange(&rng, recipe->geology.minRockScale, recipe->geology.maxRockScale);
            if (bs.biome == BIOME_ROCKY_HIGHLAND) sc *= 1.15f;
            if (ws.wet) sc *= 0.65f; /* Rocas de orilla más bajas */
            Vector3 scale = Vec3_Create(sc * nextRandRange(&rng, 0.88f, 1.15f),
                                        sc * nextRandRange(&rng, 0.78f, 1.12f),
                                        sc * nextRandRange(&rng, 0.88f, 1.15f));
            float maxXZScale = fmaxf(scale.x, scale.z);
            float boundR = var->conservativeRadiusXZ * maxXZScale;

            /* Comprobar separación contra rocas en celdas vecinas 3x3. */
            bool overlap = false;
            for (int dz = -1; dz <= 1 && !overlap; ++dz) {
                int nz = (int)gz + dz;
                if (nz < 0 || nz >= (int)gDim) continue;
                for (int dx = -1; dx <= 1; ++dx) {
                    int nx = (int)gx + dx;
                    if (nx < 0 || nx >= (int)gDim) continue;
                    int32_t otherIdx = cellRockIdx[(size_t)nz * gDim + (size_t)nx];
                    if (otherIdx < 0) continue;
                    const WorldRockInstance *other = &geo->rocks[otherIdx];
                    float minSep = boundR + other->boundRadiusXZ + 0.8f;
                    float ox = wx - other->position.x, oz = wz - other->position.z;
                    if (ox * ox + oz * oz < minSep * minSep) { overlap = true; break; }
                }
            }
            if (overlap) continue;

            /* Orientación: sigue parcialmente la normal del terreno con guiñada y cabeceo variados. */
            float yaw = nextRandRange(&rng, 0.0f, 6.2831853f);
            Vector3 blendUp = Vec3_Normalize(Vec3_Lerp(Vec3_Create(0.0f, 1.0f, 0.0f), hit.normal, 0.45f));
            Quaternion qTilt = Quat_FromTo(Vec3_Create(0.0f, 1.0f, 0.0f), blendUp);
            Quaternion qYaw = Quat_FromAxisAngle(Vec3_Create(0.0f, 1.0f, 0.0f), yaw);
            Quaternion qRoll = Quat_FromAxisAngle(Vec3_Create(1.0f, 0.0f, 0.0f), nextRandRange(&rng, -0.20f, 0.20f));
            Quaternion orient = Quat_Normalize(Quat_Multiply(qTilt, Quat_Multiply(qYaw, qRoll)));

            /* Enterrar base en el terreno para eliminar cualquier roca flotante incluso en pendiente. */
            float localHalfY = 0.5f * (var->conservativeMaxY - var->conservativeMinY) * scale.y;
            float slopeDrop = boundR * tanf(bs.slopeRadians) * 0.55f;
            float burial = localHalfY * 0.35f + slopeDrop;
            Vector3 pos = Vec3_Create(wx, hit.position.y + localHalfY - burial, wz);

            WorldRockInstance *rk = &geo->rocks[geo->rockCount];
            memset(rk, 0, sizeof(*rk));
            rk->rockId = geo->rockCount + 1u;
            rk->variantIndex = vIdx;
            rk->family = var->family;
            rk->position = pos;
            rk->orientation = orient;
            rk->scale = scale;
            rk->burialDepth = burial;
            float shade = nextRandRange(&rng, 0.82f, 1.12f);
            rk->tint[0] = shade * nextRandRange(&rng, 0.96f, 1.04f);
            rk->tint[1] = shade * nextRandRange(&rng, 0.96f, 1.03f);
            rk->tint[2] = shade * nextRandRange(&rng, 0.94f, 1.04f);
            /* Sólo rocas diminutas (< 0.32m) son puramente decorativas; toda formación visible colisiona. */
            rk->collides = (boundR >= 0.32f);
            rk->boundRadiusXZ = boundR;
            rk->minY = pos.y + var->conservativeMinY * scale.y;
            rk->maxY = pos.y + var->conservativeMaxY * scale.y;

            /* Construir proxies compuestos (1 para bloques simples, hasta 3 para conglomerados). */
            rk->lobeCount = (uint8_t)var->lobeCount;
            if (rk->lobeCount > ROCK_MAX_COMPOUND_LOBES) rk->lobeCount = ROCK_MAX_COMPOUND_LOBES;
            for (uint8_t l = 0; l < rk->lobeCount; ++l) {
                Vector3 lc = Vec3_Create(var->lobeCenters[l].x * scale.x,
                                         var->lobeCenters[l].y * scale.y,
                                         var->lobeCenters[l].z * scale.z);
                rk->lobes[l].center = Vec3_Add(pos, Quat_RotateVector(orient, lc));
                rk->lobes[l].radiusXZ = (rk->lobeCount == 1u)
                                            ? boundR * 0.94f
                                            : (var->lobeRadii[l] * maxXZScale * 1.02f);
                rk->lobes[l].halfHeight = (rk->lobeCount == 1u)
                                              ? fmaxf(0.25f, 0.5f * (rk->maxY - rk->minY))
                                              : (var->lobeRadii[l] * scale.y * 1.02f);
            }

            cellRockIdx[(size_t)gz * gDim + (size_t)gx] = (int32_t)geo->rockCount;
            ++geo->rockCount;
            if (rk->collides) ++geo->collidingRockCount;
        }
    }

    free(cellRockIdx);
    return buildRockGrid(geo, tf);
}

static inline float segDistSqXZ(Vector3 a, Vector3 b, Vector3 c) {
    float dx = b.x - a.x, dz = b.z - a.z;
    float d2 = dx * dx + dz * dz;
    float t = d2 > 1e-8f ? fmaxf(0.0f, fminf(1.0f, ((c.x - a.x) * dx + (c.z - a.z) * dz) / d2)) : 0.0f;
    float px = a.x + t * dx - c.x;
    float pz = a.z + t * dz - c.z;
    return px * px + pz * pz;
}

bool WorldGeology_CollidesDisc(const WorldGeology *geo, Vector3 pos, float radius) {
    if (!geo || !geo->rocks || geo->collidingRockCount == 0u || !geo->grid.cellOffsets) return false;
    const WorldRockGrid *g = &geo->grid;
    int x0 = (int)floorf((pos.x - radius - g->originX) * g->invCellSize);
    int x1 = (int)floorf((pos.x + radius - g->originX) * g->invCellSize);
    int z0 = (int)floorf((pos.z - radius - g->originZ) * g->invCellSize);
    int z1 = (int)floorf((pos.z + radius - g->originZ) * g->invCellSize);
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 >= (int)g->dim) x1 = (int)g->dim - 1;
    if (z1 >= (int)g->dim) z1 = (int)g->dim - 1;

    for (int cz = z0; cz <= z1; ++cz) {
        for (int cx = x0; cx <= x1; ++cx) {
            size_t ci = (size_t)cz * g->dim + (size_t)cx;
            uint32_t begin = g->cellOffsets[ci];
            uint32_t end = g->cellOffsets[ci + 1u];
            for (uint32_t k = begin; k < end; ++k) {
                const WorldRockInstance *rk = &geo->rocks[g->cellIndices[k]];
                float bRad = rk->boundRadiusXZ + radius;
                float dx = pos.x - rk->position.x, dz = pos.z - rk->position.z;
                if (dx * dx + dz * dz >= bRad * bRad) continue;
                for (uint8_t l = 0; l < rk->lobeCount; ++l) {
                    float lRad = rk->lobes[l].radiusXZ + radius;
                    float lx = pos.x - rk->lobes[l].center.x;
                    float lz = pos.z - rk->lobes[l].center.z;
                    if (lx * lx + lz * lz < lRad * lRad) return true;
                }
            }
        }
    }
    return false;
}

bool WorldGeology_CollidesSegmentXZ(const WorldGeology *geo, Vector3 a, Vector3 b, float radius) {
    if (!geo || !geo->rocks || geo->collidingRockCount == 0u || !geo->grid.cellOffsets) return false;
    const WorldRockGrid *g = &geo->grid;
    float minX = fminf(a.x, b.x) - radius;
    float maxX = fmaxf(a.x, b.x) + radius;
    float minZ = fminf(a.z, b.z) - radius;
    float maxZ = fmaxf(a.z, b.z) + radius;
    int x0 = (int)floorf((minX - g->originX) * g->invCellSize);
    int x1 = (int)floorf((maxX - g->originX) * g->invCellSize);
    int z0 = (int)floorf((minZ - g->originZ) * g->invCellSize);
    int z1 = (int)floorf((maxZ - g->originZ) * g->invCellSize);
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 >= (int)g->dim) x1 = (int)g->dim - 1;
    if (z1 >= (int)g->dim) z1 = (int)g->dim - 1;

    for (int cz = z0; cz <= z1; ++cz) {
        for (int cx = x0; cx <= x1; ++cx) {
            size_t ci = (size_t)cz * g->dim + (size_t)cx;
            uint32_t begin = g->cellOffsets[ci];
            uint32_t end = g->cellOffsets[ci + 1u];
            for (uint32_t k = begin; k < end; ++k) {
                const WorldRockInstance *rk = &geo->rocks[g->cellIndices[k]];
                float bRad = rk->boundRadiusXZ + radius;
                if (segDistSqXZ(a, b, rk->position) >= bRad * bRad) continue;
                for (uint8_t l = 0; l < rk->lobeCount; ++l) {
                    float lRad = rk->lobes[l].radiusXZ + radius;
                    if (segDistSqXZ(a, b, rk->lobes[l].center) < lRad * lRad) return true;
                }
            }
        }
    }
    return false;
}

static bool testLobeSegment3D(const RockCollisionLobe *lb, Vector3 a, Vector3 b, float radius) {
    /* Intersección de segmento 3D contra cilindro vertical acotado [center.y - hh, center.y + hh]. */
    float dx = b.x - a.x, dz = b.z - a.z;
    float x = a.x - lb->center.x, z = a.z - lb->center.z;
    float aa = dx * dx + dz * dz;
    float bb = 2.0f * (x * dx + z * dz);
    float rad = lb->radiusXZ + radius;
    float cc = x * x + z * z - rad * rad;
    float lo = 0.0f, hi = 1.0f;
    if (aa < 1e-12f) {
        if (cc > 0.0f) return false;
    } else {
        float disc = bb * bb - 4.0f * aa * cc;
        if (disc < 0.0f) return false;
        float root = sqrtf(disc);
        lo = fmaxf(lo, (-bb - root) / (2.0f * aa));
        hi = fminf(hi, (-bb + root) / (2.0f * aa));
    }
    float yMin = lb->center.y - lb->halfHeight - radius;
    float yMax = lb->center.y + lb->halfHeight + radius;
    float dy = b.y - a.y;
    if (fabsf(dy) < 1e-8f) {
        if (a.y < yMin || a.y > yMax) return false;
    } else {
        float t0 = (yMin - a.y) / dy;
        float t1 = (yMax - a.y) / dy;
        lo = fmaxf(lo, fminf(t0, t1));
        hi = fminf(hi, fmaxf(t0, t1));
    }
    return lo <= hi;
}

bool WorldGeology_OccludesSegment3D(const WorldGeology *geo, Vector3 a, Vector3 b, float radius) {
    if (!geo || !geo->rocks || geo->collidingRockCount == 0u || !geo->grid.cellOffsets) return false;
    const WorldRockGrid *g = &geo->grid;
    float minX = fminf(a.x, b.x) - radius;
    float maxX = fmaxf(a.x, b.x) + radius;
    float minZ = fminf(a.z, b.z) - radius;
    float maxZ = fmaxf(a.z, b.z) + radius;
    float segMinY = fminf(a.y, b.y) - radius;
    float segMaxY = fmaxf(a.y, b.y) + radius;

    int x0 = (int)floorf((minX - g->originX) * g->invCellSize);
    int x1 = (int)floorf((maxX - g->originX) * g->invCellSize);
    int z0 = (int)floorf((minZ - g->originZ) * g->invCellSize);
    int z1 = (int)floorf((maxZ - g->originZ) * g->invCellSize);
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 >= (int)g->dim) x1 = (int)g->dim - 1;
    if (z1 >= (int)g->dim) z1 = (int)g->dim - 1;

    for (int cz = z0; cz <= z1; ++cz) {
        for (int cx = x0; cx <= x1; ++cx) {
            size_t ci = (size_t)cz * g->dim + (size_t)cx;
            uint32_t begin = g->cellOffsets[ci];
            uint32_t end = g->cellOffsets[ci + 1u];
            for (uint32_t k = begin; k < end; ++k) {
                const WorldRockInstance *rk = &geo->rocks[g->cellIndices[k]];
                if (segMinY > rk->maxY || segMaxY < rk->minY) continue;
                float bRad = rk->boundRadiusXZ + radius;
                if (segDistSqXZ(a, b, rk->position) >= bRad * bRad) continue;
                for (uint8_t l = 0; l < rk->lobeCount; ++l) {
                    if (testLobeSegment3D(&rk->lobes[l], a, b, radius)) return true;
                }
            }
        }
    }
    return false;
}

float WorldGeology_SignedClearanceXZ(const WorldGeology *geo, float x, float z, float maxDist) {
    float best = maxDist;
    if (!geo || !geo->rocks || geo->collidingRockCount == 0u || !geo->grid.cellOffsets) return best;
    const WorldRockGrid *g = &geo->grid;
    int x0 = (int)floorf((x - maxDist - g->originX) * g->invCellSize);
    int x1 = (int)floorf((x + maxDist - g->originX) * g->invCellSize);
    int z0 = (int)floorf((z - maxDist - g->originZ) * g->invCellSize);
    int z1 = (int)floorf((z + maxDist - g->originZ) * g->invCellSize);
    if (x0 < 0) x0 = 0;
    if (z0 < 0) z0 = 0;
    if (x1 >= (int)g->dim) x1 = (int)g->dim - 1;
    if (z1 >= (int)g->dim) z1 = (int)g->dim - 1;

    for (int cz = z0; cz <= z1; ++cz) {
        for (int cx = x0; cx <= x1; ++cx) {
            size_t ci = (size_t)cz * g->dim + (size_t)cx;
            uint32_t begin = g->cellOffsets[ci];
            uint32_t end = g->cellOffsets[ci + 1u];
            for (uint32_t k = begin; k < end; ++k) {
                const WorldRockInstance *rk = &geo->rocks[g->cellIndices[k]];
                for (uint8_t l = 0; l < rk->lobeCount; ++l) {
                    float dx = x - rk->lobes[l].center.x;
                    float dz = z - rk->lobes[l].center.z;
                    float d = sqrtf(dx * dx + dz * dz) - rk->lobes[l].radiusXZ;
                    if (d < best) best = d;
                }
            }
        }
    }
    return best;
}

uint64_t WorldGeology_Fingerprint(const WorldGeology *geo) {
    if (!geo) return 0u;
    uint64_t h = UINT64_C(14695981039346656037);
    fnv1aUpdate(&h, &geo->variantCount, sizeof(geo->variantCount));
    fnv1aUpdate(&h, &geo->rockCount, sizeof(geo->rockCount));
    for (uint32_t i = 0; i < geo->rockCount; ++i) {
        const WorldRockInstance *rk = &geo->rocks[i];
        int32_t qx = (int32_t)llround((double)rk->position.x * 1000.0);
        int32_t qy = (int32_t)llround((double)rk->position.y * 1000.0);
        int32_t qz = (int32_t)llround((double)rk->position.z * 1000.0);
        int32_t qr = (int32_t)llround((double)rk->boundRadiusXZ * 1000.0);
        fnv1aUpdate(&h, &rk->variantIndex, sizeof(rk->variantIndex));
        fnv1aUpdate(&h, &qx, sizeof(qx));
        fnv1aUpdate(&h, &qy, sizeof(qy));
        fnv1aUpdate(&h, &qz, sizeof(qz));
        fnv1aUpdate(&h, &qr, sizeof(qr));
    }
    return h;
}
