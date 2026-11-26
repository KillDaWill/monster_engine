/**
 * @file WorldHydrology.c
 * @brief Implementación de Priority-Flood (Barnes et al.), cuencas lacustres, excavación fluvial
 *        y extracción de estaciones accesibles de orilla.
 */
#include "WorldHydrology.h"
#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void fnv1aUpdate(uint64_t *h, const void *data, size_t n) {
    const unsigned char *p = (const unsigned char *)data;
    for (size_t i = 0; i < n; ++i) {
        *h ^= (uint64_t)p[i];
        *h *= UINT64_C(1099511628211);
    }
}

/* Vecindad D8 determinista */
static const int g_dx8[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
static const int g_dz8[8] = {0, 0, -1, 1, -1, -1, 1, 1};
static const float g_dist8[8] = {1.0f, 1.0f, 1.0f, 1.0f, 1.41421356f, 1.41421356f, 1.41421356f, 1.41421356f};

typedef struct PFHeapNode {
    float elev;
    uint32_t idx;
} PFHeapNode;

typedef struct PFMinHeap {
    PFHeapNode *data;
    uint32_t size;
    uint32_t cap;
} PFMinHeap;

static inline bool pfLess(PFHeapNode a, PFHeapNode b) {
    if (a.elev != b.elev) return a.elev < b.elev;
    return a.idx < b.idx; /* Desempate determinista por índice */
}

static void pfPush(PFMinHeap *h, float elev, uint32_t idx) {
    uint32_t i = h->size++;
    PFHeapNode n = {elev, idx};
    h->data[i] = n;
    while (i > 0u) {
        uint32_t p = (i - 1u) >> 1;
        if (!pfLess(h->data[i], h->data[p])) break;
        PFHeapNode tmp = h->data[i];
        h->data[i] = h->data[p];
        h->data[p] = tmp;
        i = p;
    }
}

static PFHeapNode pfPop(PFMinHeap *h) {
    PFHeapNode top = h->data[0];
    uint32_t last = --h->size;
    if (last > 0u) {
        h->data[0] = h->data[last];
        uint32_t i = 0u;
        for (;;) {
            uint32_t l = (i << 1) + 1u;
            uint32_t r = l + 1u;
            uint32_t best = i;
            if (l < last && pfLess(h->data[l], h->data[best])) best = l;
            if (r < last && pfLess(h->data[r], h->data[best])) best = r;
            if (best == i) break;
            PFHeapNode tmp = h->data[i];
            h->data[i] = h->data[best];
            h->data[best] = tmp;
            i = best;
        }
    }
    return top;
}

void WorldHydrology_Free(WorldHydrology *hydro) {
    if (!hydro) return;
    free(hydro->waterSurfaceY);
    free(hydro->waterDepth);
    free(hydro->surfaceBodyId);
    free(hydro->waterBodyId);
    free(hydro->flowX);
    free(hydro->flowZ);
    free(hydro->drainageAccum);
    free(hydro->bodies);
    free(hydro->shoreSites);
    memset(hydro, 0, sizeof(*hydro));
}

static bool allocHydroBuffers(WorldHydrology *hydro, uint32_t res) {
    size_t count = (size_t)res * (size_t)res;
    hydro->resolution = res;
    hydro->waterSurfaceY = calloc(count, sizeof(float));
    hydro->waterDepth = calloc(count, sizeof(float));
    hydro->waterBodyId = calloc(count, sizeof(uint16_t));
    hydro->flowX = calloc(count, sizeof(float));
    hydro->flowZ = calloc(count, sizeof(float));
    hydro->drainageAccum = calloc(count, sizeof(float));
    hydro->bodies = calloc(WORLD_MAX_WATER_BODIES, sizeof(WorldWaterBody));
    hydro->shoreSites = calloc(WORLD_MAX_SHORE_SITES, sizeof(WorldShoreSite));
    if (!hydro->waterSurfaceY || !hydro->waterDepth || !hydro->waterBodyId ||
        !hydro->flowX || !hydro->flowZ || !hydro->drainageAccum ||
        !hydro->bodies || !hydro->shoreSites) {
        WorldHydrology_Free(hydro);
        return false;
    }
    hydro->memoryBytes = count * (sizeof(float) * 5u + sizeof(uint16_t)) +
                         WORLD_MAX_WATER_BODIES * sizeof(WorldWaterBody) +
                         WORLD_MAX_SHORE_SITES * sizeof(WorldShoreSite);
    return true;
}

static void initWaterBody(WorldWaterBody *b, uint16_t id, WaterBodyType type,
                          const WorldRecipe *recipe) {
    memset(b, 0, sizeof(*b));
    b->id = id;
    b->type = type;
    snprintf(b->substanceId, sizeof(b->substanceId), "%s",
             recipe->hydrology.waterSubstanceId[0] ? recipe->hydrology.waterSubstanceId : "clean_water");
    b->reservoirPolicy = recipe->hydrology.reservoirPolicy;
    b->initialVolume = (b->reservoirPolicy == WORLD_RESERVOIR_FINITE_FIXED_VOLUME)
                           ? recipe->hydrology.finiteVolume : 0.0f;
    b->bounds = AABB_Empty();
    b->minSurfaceY = FLT_MAX;
    b->maxSurfaceY = -FLT_MAX;
}

static bool runPriorityFlood(const float *rawHeights, uint32_t res,
                             float *filledHeights, int32_t *parent, uint32_t *drainOrder) {
    size_t count = (size_t)res * (size_t)res;
    uint8_t *visited = calloc(count, 1u);
    PFHeapNode *heapMem = malloc(count * sizeof(PFHeapNode));
    uint32_t *pitQueue = malloc(count * sizeof(uint32_t));
    if (!visited || !heapMem || !pitQueue) {
        free(visited); free(heapMem); free(pitQueue);
        return false;
    }

    PFMinHeap heap = {.data = heapMem, .size = 0u, .cap = (uint32_t)count};
    uint32_t pitHead = 0u, pitTail = 0u;
    uint32_t orderCount = 0u;

    /* Sembrar borde completo como salidas abiertas (open_boundary). */
    for (uint32_t iz = 0; iz < res; ++iz) {
        for (uint32_t ix = 0; ix < res; ++ix) {
            if (ix == 0u || iz == 0u || ix + 1u == res || iz + 1u == res) {
                uint32_t idx = iz * res + ix;
                visited[idx] = 1u;
                filledHeights[idx] = rawHeights[idx];
                parent[idx] = -1;
                pfPush(&heap, rawHeights[idx], idx);
            }
        }
    }

    while (pitHead < pitTail || heap.size > 0u) {
        uint32_t cur;
        if (pitHead < pitTail) {
            cur = pitQueue[pitHead++];
        } else {
            cur = pfPop(&heap).idx;
        }
        drainOrder[orderCount++] = cur;

        int cx = (int)(cur % res);
        int cz = (int)(cur / res);
        float cElev = filledHeights[cur];

        for (int d = 0; d < 8; ++d) {
            int nx = cx + g_dx8[d];
            int nz = cz + g_dz8[d];
            if (nx < 0 || nz < 0 || nx >= (int)res || nz >= (int)res) continue;
            uint32_t ni = (uint32_t)nz * res + (uint32_t)nx;
            if (visited[ni]) continue;
            visited[ni] = 1u;
            parent[ni] = (int32_t)cur;
            float nRaw = rawHeights[ni];
            if (nRaw <= cElev) {
                filledHeights[ni] = cElev;
                pitQueue[pitTail++] = ni;
            } else {
                filledHeights[ni] = nRaw;
                pfPush(&heap, nRaw, ni);
            }
        }
    }

    free(visited);
    free(heapMem);
    free(pitQueue);
    return orderCount == (uint32_t)count;
}

static bool verifyAcyclicRouting(const int32_t *parent, uint32_t count) {
    /* 0 = no visitado, 1 = en pila actual, 2 = verificado acíclico */
    uint8_t *mark = calloc(count, 1u);
    uint32_t *stack = malloc(count * sizeof(uint32_t));
    if (!mark || !stack) { free(mark); free(stack); return false; }

    bool ok = true;
    for (uint32_t i = 0; i < count && ok; ++i) {
        if (mark[i] != 0u) continue;
        uint32_t sp = 0u;
        int32_t cur = (int32_t)i;
        while (cur >= 0) {
            uint32_t u = (uint32_t)cur;
            if (mark[u] == 1u) { ok = false; break; }
            if (mark[u] == 2u) break;
            mark[u] = 1u;
            stack[sp++] = u;
            cur = parent[u];
        }
        for (uint32_t k = 0; k < sp; ++k) mark[stack[k]] = 2u;
    }
    free(mark);
    free(stack);
    return ok;
}

typedef struct RiverNode {
    float x, z;                /* Coordenadas de la rejilla, tras limitar el suavizado. */
    float radius, depth, speed;
    float headDistance;
    uint32_t incoming;         /* Predecesor cuando indegree == 1. */
    uint8_t indegree;
    uint16_t body;
    float directionX, directionZ;
} RiverNode;

static float smoothUnit(float t) {
    t = fminf(1.0f, fmaxf(0.0f, t));
    return t * t * (3.0f - 2.0f * t);
}

typedef struct RiverBlend {
    float weight, ground, surface, flowX, flowZ;
} RiverBlend;

static bool usesGradedBanks(const WorldRecipe *recipe) {
    return recipe->generatorVersion == WORLD_GENERATOR_VERSION_MORPHOLOGY ||
           recipe->generatorVersion == WORLD_GENERATOR_VERSION_SHORELINE;
}

/* Suavizado espacial en unidades físicas. Se conserva el borde como salida. */
static void conditionRouting(const TerrainHeightfield *tf, float *dem) {
    uint32_t n = tf->resolution;
    unsigned step = (unsigned)fminf(8.0f, fmaxf(1.0f, roundf(1.25f * tf->invSpacing)));
    memcpy(dem, tf->heights, (size_t)n * n * sizeof(*dem));
    for (uint32_t z = step; z + step < n; ++z) {
        for (uint32_t x = step; x + step < n; ++x) {
            uint32_t i = z * n + x;
            const float *h = tf->heights;
            uint32_t dz = step * n;
            float mean = .25f * (h[i - step] + h[i + step] + h[i - dz] + h[i + dz]);
            dem[i] = h[i] + .58f * (mean - h[i]);
        }
    }
}

static void fitRiverProfile(const TerrainHeightfield *tf, RiverNode *nodes,
                           const int32_t *parent, const uint32_t *order,
                           float *surface, float *target, float typicalGrade, bool limitIncision) {
    uint32_t count = tf->resolution * tf->resolution;
    /* Difusión en longitud de arco sin mover las salidas y confluencias. */
    for (unsigned pass = 0; pass < 20u; ++pass) {
        for (uint32_t k = 0; k < count; ++k) {
            uint32_t i = order[k];
            const RiverNode *n = &nodes[i];
            if (!n->body) continue;
            target[i] = surface[i];
            int32_t p = parent[i];
            if (n->indegree != 1u || p < 0 || !nodes[p].body) continue;
            const RiverNode *up = &nodes[n->incoming], *down = &nodes[p];
            float du = hypotf(up->x - n->x, up->z - n->z);
            float dd = hypotf(down->x - n->x, down->z - n->z);
            float mean = (surface[n->incoming] * dd + surface[p] * du) / fmaxf(.01f, du + dd);
            target[i] += .52f * (mean - surface[i]);
        }
        for (uint32_t k = 0; k < count; ++k) {
            uint32_t i = order[k];
            if (!nodes[i].body) continue;
            int32_t p = parent[i];
            surface[i] = p >= 0 && nodes[p].body ? fmaxf(target[i], surface[p]) : target[i];
        }
    }
    /* Rebajar roturas de rasante hasta el presupuesto de corte. El tramo
     * conserva una rápida si suavizarlo excede el corte permitido. */
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t i = order[k];
        const RiverNode *n = &nodes[i];
        int32_t p = parent[i];
        if (!n->body) continue;
        float floor = tf->heights[i] - (limitIncision ? .55f : 1.45f);
        /* Aplicar el límite también a la cota producida por la difusión. */
        if (limitIncision) surface[i] = fmaxf(floor, surface[i]);
        if (p < 0 || !nodes[p].body) continue;
        float ds = tf->spacing * hypotf(n->x - nodes[p].x, n->z - nodes[p].z);
        float limit = fmaxf(floor, surface[p] + typicalGrade * ds);
        surface[i] = fmaxf(surface[p], fminf(surface[i], limit));
    }
}

/* Un río que pasa por debajo de un lago vecino no debe romper el dique
 * natural de esa cuenca. La transición vuelve a admitir corte al alejarse. */
static float preserveLakeDivide(const WorldHydrology *h, const TerrainHeightfield *tf,
                                uint32_t i, float bed) {
    float base = tf->heights[i];
    float cut = base - bed;
    if (cut <= 0.0f || !h->lakeCount) return bed;

    int n = (int)tf->resolution, x = (int)(i % tf->resolution), z = (int)(i / tf->resolution);
    int radius = (int)fminf(12.0f, ceilf(fminf(3.0f, cut / .40f) * tf->invSpacing) + 1.0f);
    float level = h->waterSurfaceY[i];
    for (int dz = -radius; dz <= radius; ++dz) {
        if (z + dz < 0 || z + dz >= n) continue;
        for (int dx = -radius; dx <= radius; ++dx) {
            if (x + dx < 0 || x + dx >= n) continue;
            uint16_t lake = h->waterBodyId[(z + dz) * n + x + dx];
            if (!lake || lake > h->lakeCount || h->bodies[lake - 1u].surfaceY <= level + .20f)
                continue;
            float run = (sqrtf((float)(dx * dx + dz * dz)) - 1.42f) * tf->spacing;
            cut = fminf(cut, fmaxf(0.0f, .40f * run));
            if (cut == 0.0f) return base;
        }
    }
    return base - cut;
}

static bool carveContinuousRivers(WorldHydrology *hydro, TerrainHeightfield *tf,
                                  const WorldRecipe *recipe, const int32_t *parent,
                                  const uint32_t *order, const uint8_t *channel,
                                  float *surface, float threshold, float *target,
                                  WorldHydrologyTraceFn trace, void *context) {
    uint32_t res = tf->resolution;
    uint32_t count = res * res;
    RiverNode *nodes = calloc(count, sizeof(*nodes));
    float *best = malloc((size_t)count * sizeof(*best));
    const bool morphology = usesGradedBanks(recipe);
    const bool shoreline = recipe->generatorVersion == WORLD_GENERATOR_VERSION_SHORELINE;
    RiverBlend *blend = morphology ? calloc(count, sizeof(*blend)) : NULL;
    uint32_t *winner = trace ? calloc(count, sizeof(*winner)) : NULL;
    uint16_t lakeNetwork[WORLD_MAX_WATER_BODIES + 1u] = {0};
    if (!nodes || !best || (morphology && !blend) || (trace && !winner)) {
        free(nodes); free(best); free(blend); free(winner);
        return false;
    }

    for (uint32_t i = 0; i < count; ++i) {
        best[i] = FLT_MAX;
        if (!channel[i]) continue;
        int32_t p = parent[i];
        if (p >= 0 && channel[p]) {
            nodes[p].incoming = i;
            ++nodes[p].indegree; /* Como máximo ocho por la vecindad D8. */
        }
    }

    /* Materializar los enlaces entre fuentes, confluencias, lagos y salidas.
     * Cada red desemboca en la misma salida de borde o en el mismo lago. */
    for (uint32_t k = 0; k < count; ++k) {
        uint32_t i = order[k];
        if (!channel[i]) continue;
        RiverNode *n = &nodes[i];
        n->x = (float)(i % res);
        n->z = (float)(i / res);
        int32_t p = parent[i];
        if (p >= 0 && channel[p]) {
            n->body = nodes[p].body;
            if (n->indegree == 1u) {
                uint32_t u = n->incoming;
                /* Una pasada local, como máximo 1/4 de celda por eje. Los
                 * extremos y cruces quedan anclados en el corredor D8. */
                n->x += 0.125f * ((float)(u % res) + (float)((uint32_t)p % res) - 2.0f * n->x);
                n->z += 0.125f * ((float)(u / res) + (float)((uint32_t)p / res) - 2.0f * n->z);
            }
        } else {
            uint16_t lake = p >= 0 ? hydro->waterBodyId[p] : 0;
            if (lake) n->body = lakeNetwork[lake];
            if (!n->body && hydro->bodyCount < WORLD_MAX_WATER_BODIES) {
                n->body = (uint16_t)(hydro->bodyCount + 1u);
                initWaterBody(&hydro->bodies[hydro->bodyCount++], n->body, WATER_BODY_RIVER, recipe);
                ++hydro->riverCount;
                if (lake) lakeNetwork[lake] = n->body;
            }
        }
    }

    if (morphology) {
        /* Mantener el polígono dentro de la franja de su celda D8. */
        for (unsigned pass = 0; pass < 4u; ++pass) {
            for (uint32_t i = 0; i < count; ++i) {
                RiverNode *n = &nodes[i];
                if (!n->body) continue;
                n->directionX = n->x;
                n->directionZ = n->z;
                int32_t p = parent[i];
                if (n->indegree != 1u || p < 0 || !nodes[p].body) continue;
                RiverNode *u = &nodes[n->incoming];
                float x = (float)(i % res), z = (float)(i / res);
                n->directionX = x + fminf(.36f, fmaxf(-.36f, .25f * (u->x + nodes[p].x - 2.0f * x)));
                n->directionZ = z + fminf(.36f, fmaxf(-.36f, .25f * (u->z + nodes[p].z - 2.0f * z)));
            }
            for (uint32_t i = 0; i < count; ++i) {
                nodes[i].x = nodes[i].directionX;
                nodes[i].z = nodes[i].directionZ;
            }
        }
        fitRiverProfile(tf, nodes, parent, order, surface, target,
                        .055f + .15f * recipe->terrain.ridgeWeight, shoreline);
    }

    /* Suavizar la cota longitudinal dentro del corredor. El filtro convexo
     * respeta la posición de las juntas, lagos y salidas, y limita el cambio. */
    for (unsigned pass = 0; !morphology && pass < 2; ++pass) {
        for (uint32_t i = 0; i < count; ++i) {
            const RiverNode *n = &nodes[i];
            if (!n->body) continue;
            target[i] = surface[i];
            int32_t p = parent[i];
            if (n->indegree != 1u || p < 0 || !channel[p]) continue;
            const RiverNode *a = &nodes[n->incoming], *b = &nodes[p];
            float up = hypotf(a->x - n->x, a->z - n->z);
            float down = hypotf(b->x - n->x, b->z - n->z);
            float linear = (surface[n->incoming] * down + surface[p] * up) / fmaxf(0.01f, up + down);
            target[i] += fminf(0.23f, fmaxf(-0.23f, (linear - surface[i]) * 0.68f));
        }
        for (uint32_t k = 0; k < count; ++k) {
            uint32_t i = order[k];
            if (!nodes[i].body) continue;
            int32_t p = parent[i];
            surface[i] = target[i];
            if (p >= 0 && channel[p]) surface[i] = fmaxf(surface[i], surface[p]);
        }
    }

    for (uint32_t k = count; k > 0; --k) {
        uint32_t i = order[k - 1u];
        if (!channel[i]) continue;
        RiverNode *n = &nodes[i];
        int32_t p = parent[i];
        float reach = sqrtf(fmaxf(1.0f, hydro->drainageAccum[i] / fmaxf(1.0f, threshold * 0.25f)));
        float scale = log1pf(reach);
        float taper = smoothUnit(n->headDistance / 6.0f);
        n->radius = (0.42f + 0.58f * taper) * (1.15f + 0.55f * scale) * recipe->hydrology.riverWidthScale;
        n->depth = (0.34f + 0.66f * taper) * (0.48f + 0.24f * scale) * recipe->hydrology.riverDepthScale;
        n->speed = fminf(2.0f, 0.45f + 0.20f * scale);
        if (p >= 0 && channel[p]) {
            float step = hypotf(nodes[p].x - n->x, nodes[p].z - n->z) * tf->spacing;
            nodes[p].headDistance = fmaxf(nodes[p].headDistance, n->headDistance + step);
        }
        /* Tangente compartida por los enlaces. En confluencias domina la salida. */
        float dx = 0.0f, dz = 0.0f;
        if (p >= 0) {
            dx = (channel[p] ? nodes[p].x : (float)((uint32_t)p % res)) - n->x;
            dz = (channel[p] ? nodes[p].z : (float)((uint32_t)p / res)) - n->z;
        }
        if (n->indegree == 1u || (p < 0 && n->indegree)) {
            dx += n->x - nodes[n->incoming].x;
            dz += n->z - nodes[n->incoming].z;
        }
        float len = hypotf(dx, dz);
        if (len > 0.001f) {
            n->directionX = dx / len;
            n->directionZ = dz / len;
        }
    }

    for (unsigned pass = 0; pass < (morphology ? 2u : 1u); ++pass) {
        float junctionAccumulation = -1.0f;
        for (uint32_t i = 0; i < count; ++i) {
            const RiverNode *a = &nodes[i];
            if (!a->body) continue;
            if (pass == 0 && a->indegree > 1u) {
                ++hydro->riverJunctionCount;
                if (hydro->drainageAccum[i] > junctionAccumulation) {
                    junctionAccumulation = hydro->drainageAccum[i];
                    hydro->majorJunction = Vec3_Create(tf->originX + a->x * tf->spacing, surface[i],
                                                      tf->originZ + a->z * tf->spacing);
                }
            }
            int32_t p = parent[i];
            if (p < 0 && a->indegree) continue; /* La arista entrante ya cubre la salida. */
            RiverNode end = *a;
            float endY = surface[i];
            if (p >= 0 && channel[p]) {
                end = nodes[p];
                endY = surface[p];
            } else if (p >= 0 && hydro->waterBodyId[p]) {
                end.x = (float)((uint32_t)p % res);
                end.z = (float)((uint32_t)p / res);
                endY = hydro->waterSurfaceY[p];
            }
            if (pass == 0 && p >= 0) ++hydro->riverSegmentCount;
            float dx = end.x - a->x, dz = end.z - a->z;
            float invLen2 = 1.0f / fmaxf(0.001f, dx * dx + dz * dz);
            float maxRadius = fmaxf(a->radius, end.radius);
            /* Trabajo local al AABB, con anchura en unidades físicas (sin truncar en celdas). */
            float reachBank = fmaxf(1.5f, 0.85f * maxRadius);
            if (morphology) {
                float cut = fmaxf(0.0f, tf->heights[i] - surface[i]);
                reachBank = fminf(fminf(10.0f, tf->span * .09f),
                                  2.8f + 1.2f * maxRadius + cut / .31f);
            }
            float extent = (maxRadius + reachBank) * tf->invSpacing;
            int minX = (int)fmaxf(0, floorf(fminf(a->x, end.x) - extent));
            int minZ = (int)fmaxf(0, floorf(fminf(a->z, end.z) - extent));
            int maxX = (int)fminf((float)res - 1.0f, ceilf(fmaxf(a->x, end.x) + extent));
            int maxZ = (int)fminf((float)res - 1.0f, ceilf(fmaxf(a->z, end.z) + extent));

            for (int z = minZ; z <= maxZ; ++z) {
                for (int x = minX; x <= maxX; ++x) {
                    uint32_t j = (uint32_t)z * res + (uint32_t)x;
                    if (hydro->waterBodyId[j]) continue; /* Conservar plano y lecho lacustre. */
                    float rx = (float)x - a->x, rz = (float)z - a->z;
                    float t = fminf(1.0f, fmaxf(0.0f, (rx * dx + rz * dz) * invLen2));
                    float ex = rx - t * dx, ez = rz - t * dz;
                    float radius = a->radius + t * (end.radius - a->radius);
                    float bankWidth = morphology ? reachBank : fmaxf(1.5f, radius * 0.85f);
                    float dist2 = (ex * ex + ez * ez) * tf->spacing * tf->spacing;
                    if (dist2 >= (radius + bankWidth) * (radius + bankWidth)) continue;
                    float dist = sqrtf(dist2);
                    float score = dist / radius;
                    if (pass == 0 && score >= best[j]) continue;

                    float y = surface[i] + t * (endY - surface[i]);
                    float depth = a->depth + t * (end.depth - a->depth);
                    float base = tf->heights[j];
                    float bed;
                    if (morphology) {
                        float across = dist - radius;
                        if (across < 0.0f) {
                            bed = y - depth * (1.0f - score * score);
                        } else {
                            float u = across / bankWidth;
                            /* Transición hacia la cota existente sin crear una berma
                             * si el terreno ya está cerca de la lámina. */
                            float bank = fminf(fmaxf(base, y),
                                               y + fminf(.30f, 2.0f * depth / radius) * across);
                            bed = bank + (base - bank) * smoothUnit(u);
                        }
                        if (pass == 1u) {
                            if (hydro->surfaceBodyId[j] != a->body || fabsf(y - hydro->waterSurfaceY[j]) > .38f)
                                continue;
                            float w = (1.0f - smoothUnit((score - best[j]) / .48f))
                                    * (1.0f - smoothUnit(fabsf(y - hydro->waterSurfaceY[j]) / .38f));
                            float speed = a->speed + t * (end.speed - a->speed);
                            blend[j].weight += w;
                            blend[j].ground += w * bed;
                            blend[j].surface += w * y;
                            blend[j].flowX += w * speed * (a->directionX + t * (end.directionX - a->directionX));
                            blend[j].flowZ += w * speed * (a->directionZ + t * (end.directionZ - a->directionZ));
                            continue;
                        }
                    } else if (dist < radius) {
                        float interior = 1.0f - smoothUnit(score);
                        bed = y - depth * interior;
                        /* Relleno local que llega a cero en el borde, con corte libre. */
                        bed = fminf(base + 0.22f * interior, bed);
                    } else {
                        float bank = smoothUnit((dist - radius) / bankWidth);
                        /* Ribera de corte; un bajo existente fuera del cauce no es agua. */
                        bed = fminf(base, y + (base - y) * bank);
                    }
                    best[j] = score;
                    if (winner) winner[j] = i + 1u;
                    target[j] = fmaxf(0.05f, bed);
                    hydro->surfaceBodyId[j] = a->body;
                    hydro->waterSurfaceY[j] = morphology || dist < radius ? y : fminf(y, target[j]);
                    float speed = a->speed + t * (end.speed - a->speed);
                    hydro->flowX[j] = speed * (a->directionX + t * (end.directionX - a->directionX));
                    hydro->flowZ[j] = speed * (a->directionZ + t * (end.directionZ - a->directionZ));
                }
            }
        }

    }
    if (trace) {
        trace(context, "segment_winner_u32", winner, count, sizeof(*winner));
        trace(context, "channel_surface_f32", surface, count, sizeof(*surface));
    }
    for (uint32_t i = 0; i < count; ++i) {
        if (best[i] == FLT_MAX) continue;
        if (blend && blend[i].weight > 0.0f) {
            float inv = 1.0f / blend[i].weight;
            target[i] = fmaxf(.05f, blend[i].ground * inv);
            hydro->waterSurfaceY[i] = blend[i].surface * inv;
            hydro->flowX[i] = blend[i].flowX * inv;
            hydro->flowZ[i] = blend[i].flowZ * inv;
        }
        if (shoreline) {
            /* Acotar el relleno bajo la lámina y dejar las riberas secas a
             * su cota original cuando el perfil pasa sobre un bajo lateral. */
            float depth = hydro->waterSurfaceY[i] - target[i];
            float limit = tf->heights[i] + fminf(.22f, .25f * fmaxf(0.0f, depth));
            if (target[i] > limit) {
                float fade = 1.0f - smoothUnit((target[i] - limit) / .28f);
                target[i] = limit;
                hydro->waterSurfaceY[i] = limit + depth * fade;
            }
            target[i] = preserveLakeDivide(hydro, tf, i, target[i]);
        }
        if (morphology && best[i] >= 1.0f) {
            /* El bajo exterior conserva su altura física, pero no pertenece al cauce mojado. */
            hydro->waterSurfaceY[i] = fminf(hydro->waterSurfaceY[i], target[i]);
        }
        tf->heights[i] = target[i];
        hydro->waterBodyId[i] = hydro->surfaceBodyId[i];
    }
    if (trace) {
        for (uint32_t i = 0; i < count; ++i) best[i] = nodes[i].body ? nodes[i].x : 0.0f;
        trace(context, "channel_x_f32", best, count, sizeof(*best));
        for (uint32_t i = 0; i < count; ++i) best[i] = nodes[i].body ? nodes[i].z : 0.0f;
        trace(context, "channel_z_f32", best, count, sizeof(*best));
    }
    free(nodes); free(best); free(blend); free(winner);
    return true;
}

static bool isWetDomain(const WorldHydrology *h, const float *ground, uint32_t i) {
    return h->surfaceBodyId[i] && h->waterSurfaceY[i] - ground[i] > WORLD_WATER_DEPTH_EPSILON;
}

static float seamRiseLimit(const float *base, uint32_t a, uint32_t b, float run) {
    /* Los taludes originales pronunciados conservan su desnivel. El tallado
     * admite un talud moderado incluso en una llanura originalmente plana. */
    return fmaxf(.36f * run, fabsf(base[a] - base[b]) + .04f * run);
}

/* Repartir sólo el desnivel añadido por la excavación fluvial.
 * La pertenencia al agua y la rasante se conservan en ambos lados. */
static void repairRiverShoreSeams(const WorldHydrology *h, TerrainHeightfield *tf,
                                  const float *base, float *scratch, uint32_t *active,
                                  uint8_t *ring, bool wholeCorridor) {
    uint32_t n = tf->resolution, count = n * n;
    uint32_t tail = 0;
    memset(ring, 0, count * sizeof(*ring));
    uint32_t first = wholeCorridor ? 0u : 1u;
    uint32_t end = n - first;
    uint32_t seedEnd = wholeCorridor ? end : end - 1u;
    for (uint32_t z = first; z < seedEnd; ++z) {
        for (uint32_t x = first; x < seedEnd; ++x) {
            uint32_t i = z * n + x;
            const uint32_t neighbor[] = {i + 1u, i + n};
            const bool valid[] = {x + 1u < end, z + 1u < end};
            for (unsigned k = 0; k < 2; ++k) {
                if (!valid[k]) continue;
                uint32_t j = neighbor[k];
                if (fabsf(tf->heights[i] - tf->heights[j]) <= seamRiseLimit(base, i, j, tf->spacing))
                    continue;
                if (!wholeCorridor) {
                    bool wetI = isWetDomain(h, tf->heights, i), wetJ = isWetDomain(h, tf->heights, j);
                    if ((!wetI && !wetJ) ||
                        (wetI && wetJ && h->surfaceBodyId[i] == h->surfaceBodyId[j])) continue;
                } else if (h->surfaceBodyId[i] <= h->lakeCount && h->surfaceBodyId[j] <= h->lakeCount) {
                    continue;
                }
                if (!ring[i]) { ring[i] = 1; active[tail++] = i; }
                if (!ring[j]) { ring[j] = 1; active[tail++] = j; }
            }
        }
    }
    unsigned maxRing = (unsigned)fminf(12.0f, 1.0f + ceilf(2.2f * tf->invSpacing));
    for (uint32_t k = 0; k < tail; ++k) {
        uint32_t i = active[k], x = i % n, z = i / n;
        if (ring[i] >= maxRing) continue;
        const uint32_t neighbor[] = {i - 1u, i + 1u, i - n, i + n};
        const bool interior[] = {x > first, x + 1u < end, z > first, z + 1u < end};
        for (unsigned d = 0; d < 4; ++d) {
            uint32_t j = neighbor[d];
            if (!interior[d] || ring[j]) continue;
            ring[j] = ring[i] + 1u;
            active[tail++] = j;
        }
    }

    for (unsigned pass = 0; pass < 20u; ++pass) {
        for (uint32_t k = 0; k < tail; ++k) {
            uint32_t i = active[k];
            uint32_t x = i % n, z = i / n;
            float y = tf->heights[i];
            const uint32_t neighbor[] = {i - 1u, i + 1u, i - n, i + n};
            const bool valid[] = {x > 0u, x + 1u < n, z > 0u, z + 1u < n};
            float correction = 0.0f;
            for (unsigned d = 0; d < 4; ++d) {
                if (!valid[d]) continue;
                uint32_t j = neighbor[d];
                float delta = tf->heights[j] - y;
                float rise = seamRiseLimit(base, i, j, tf->spacing);
                correction += delta - fminf(rise, fmaxf(-rise, delta));
            }
            float changed = y + .22f * correction;
            if (h->surfaceBodyId[i]) {
                float limit = h->waterSurfaceY[i] - WORLD_WATER_DEPTH_EPSILON;
                if (isWetDomain(h, tf->heights, i)) {
                    /* No adelgazar por debajo de un tirante útil, salvo si la
                     * propia muestra ya pertenecía al borde muy somero. */
                    float minDepth = fmaxf(WORLD_WATER_DEPTH_EPSILON + 1e-5f,
                                           fminf(.035f, .25f * (h->waterSurfaceY[i] - y)));
                    changed = fminf(changed, fmaxf(y, h->waterSurfaceY[i] - minDepth));
                } else {
                    changed = fmaxf(changed, limit + 1e-5f);
                }
            }
            scratch[k] = fmaxf(fminf(.05f, y), changed);
        }
        for (uint32_t k = 0; k < tail; ++k) tf->heights[active[k]] = scratch[k];
    }
}

static void propagateShore(const WorldHydrology *h, const TerrainHeightfield *tf,
                           uint32_t *distance, uint32_t i, uint32_t other, unsigned band) {
    uint32_t next = distance[other];
    if ((next & 0xffffu) >= band || (next & 0xffffu) + 1u >= (distance[i] & 0xffffu)) return;
    uint16_t owner = (uint16_t)(next >> 16);
    uint16_t current = h->surfaceBodyId[i];
    if (current != owner && (current || owner > h->lakeCount ||
                             tf->heights[i] <= h->bodies[owner - 1u].surfaceY)) return;
    distance[i] = next + 1u;
}

/* Relajación local de ambas caras del contorno. La distancia y la identidad
 * extendida en tierra seca sólo existen en el búfer de generación. */
static void relaxShoreV3(WorldHydrology *h, TerrainHeightfield *tf,
                         float *scratch, uint32_t *distance) {
    uint32_t n = tf->resolution, count = n * n;
    unsigned band = (unsigned)fminf(12.0f, fmaxf(2.0f, ceilf(2.4f * tf->invSpacing)));
    const float epsilon = WORLD_WATER_DEPTH_EPSILON;
    for (uint32_t i = 0; i < count; ++i) distance[i] = UINT32_MAX;
    for (uint32_t z = 1; z + 1u < n; ++z) {
        for (uint32_t x = 1; x + 1u < n; ++x) {
            uint32_t i = z * n + x;
            uint16_t owner = h->surfaceBodyId[i];
            if (!owner) continue;
            float depth = h->waterSurfaceY[i] - tf->heights[i];
            if (fabsf(depth) <= .55f * tf->spacing) distance[i] = (uint32_t)owner << 16;
        }
    }
    for (uint32_t z = 1; z + 1u < n; ++z) {
        for (uint32_t x = 1; x + 1u < n; ++x) {
            uint32_t i = z * n + x;
            propagateShore(h, tf, distance, i, i - 1u, band);
            propagateShore(h, tf, distance, i, i - n, band);
        }
    }
    for (uint32_t z = n - 2u; z; --z) {
        for (uint32_t x = n - 2u; x; --x) {
            uint32_t i = z * n + x;
            propagateShore(h, tf, distance, i, i + 1u, band);
            propagateShore(h, tf, distance, i, i + n, band);
        }
    }

    for (unsigned pass = 0; pass < 4; ++pass) {
        memcpy(scratch, tf->heights, count * sizeof(*scratch));
        for (uint32_t z = 1; z + 1u < n; ++z) {
            for (uint32_t x = 1; x + 1u < n; ++x) {
                uint32_t i = z * n + x;
                unsigned steps = distance[i] & 0xffffu;
                if (steps >= band) continue;
                uint16_t body = (uint16_t)(distance[i] >> 16);
                bool lake = body <= h->lakeCount;
                float y = tf->heights[i];
                float water = lake ? h->bodies[body - 1u].surfaceY : h->waterSurfaceY[i];
                if (lake && fabsf(y - water) < .045f) continue; /* Cresta de vertido. */
                if (water - y > .58f) continue; /* Lecho central. */
                uint32_t p[] = {i - 1u, i + 1u, i - n, i + n};
                float sum = 0.0f;
                bool protect = false;
                for (unsigned k = 0; k < 4; ++k) {
                    uint16_t neighbor = h->surfaceBodyId[p[k]];
                    float delta = tf->heights[p[k]] - y;
                    protect |= (neighbor && neighbor != body) || fabsf(delta) > .90f * tf->spacing;
                    sum += delta;
                }
                if (protect) continue;
                float strength = 1.0f - (float)steps / (float)band;
                float limit = .12f * tf->spacing;
                float correction = .115f * sum;
                if (lake) {
                    float run = (steps + .55f) * tf->spacing;
                    float shelf = water + copysignf(.20f * run, y - water);
                    correction += .14f * (shelf - y);
                }
                float changed = y + fminf(limit, fmaxf(-limit, correction * strength));
                if (water - y > epsilon) changed = fminf(water - 1.01f * epsilon, changed);
                else changed = fmaxf(water - .99f * epsilon, changed);
                scratch[i] = changed;
            }
        }
        memcpy(tf->heights, scratch, count * sizeof(*scratch));
    }
}

bool WorldHydrology_GenerateTraced(WorldHydrology *hydro, TerrainHeightfield *tf,
                                  const WorldRecipe *recipe, uint32_t hydroSeed,
                                  WorldHydrologyTraceFn trace, void *context) {
    (void)hydroSeed;
    if (!hydro || !tf || !tf->heights || !recipe) return false;
    memset(hydro, 0, sizeof(*hydro));

    uint32_t res = tf->resolution;
    if (!allocHydroBuffers(hydro, res)) return false;
    const bool morphology = usesGradedBanks(recipe);

    if (recipe->flat || !recipe->hydrology.enabled ||
        (recipe->hydrology.riverDensity <= 1e-4f && recipe->hydrology.lakeDensity <= 1e-4f)) {
        hydro->enabled = false;
        hydro->drainageAcyclic = true;
        if (!morphology || TerrainHeightfield_RebuildDerived(tf))
            return true;
        WorldHydrology_Free(hydro);
        return false;
    }

    hydro->enabled = true;
    const bool shoreline = recipe->generatorVersion == WORLD_GENERATOR_VERSION_SHORELINE;
    const bool v3 = recipe->generatorVersion == WORLD_GENERATOR_VERSION_MORPHOLOGY;
    const bool continuous = morphology || recipe->generatorVersion == WORLD_GENERATOR_VERSION_CONTINUOUS;
    size_t count = (size_t)res * (size_t)res;
    if (continuous) {
        hydro->surfaceBodyId = calloc(count, sizeof(*hydro->surfaceBodyId));
        if (!hydro->surfaceBodyId) {
            WorldHydrology_Free(hydro);
            return false;
        }
        hydro->memoryBytes += count * sizeof(*hydro->surfaceBodyId);
    }
    float *filled = malloc(count * sizeof(float));
    int32_t *parent = malloc(count * sizeof(int32_t));
    uint32_t *drainOrder = malloc(count * sizeof(uint32_t));
    uint32_t *bfsQ = malloc(count * sizeof(uint32_t));
    uint8_t *compVis = calloc(count, 1u);
    if (!filled || !parent || !drainOrder || !bfsQ || !compVis) {
        free(filled); free(parent); free(drainOrder); free(bfsQ); free(compVis);
        WorldHydrology_Free(hydro);
        return false;
    }

    /* 1) Priority-Flood para elevaciones rellenadas y árbol de drenaje acíclico. */
    float *routing = v3 ? malloc(count * sizeof(*routing)) : NULL;
    if (routing) conditionRouting(tf, routing);
    if (trace) trace(context, "routing_dem_f32", routing ? routing : tf->heights, count, sizeof(float));
    bool routed = (!v3 || routing) &&
                  runPriorityFlood(routing ? routing : tf->heights, res, filled, parent, drainOrder);
    free(routing);
    if (!routed) {
        free(filled); free(parent); free(drainOrder); free(bfsQ); free(compVis);
        WorldHydrology_Free(hydro);
        return false;
    }
    hydro->drainageAcyclic = verifyAcyclicRouting(parent, (uint32_t)count);
    if (trace) {
        trace(context, "filled_dem_f32", filled, count, sizeof(*filled));
        trace(context, "drainage_parent_i32", parent, count, sizeof(*parent));
    }

    /* 2) Acumulación de drenaje en orden topológico aguas arriba -> aguas abajo. */
    float cellArea = tf->spacing * tf->spacing;
    for (size_t i = 0; i < count; ++i) hydro->drainageAccum[i] = cellArea;
    for (size_t k = count; k > 0u; --k) {
        uint32_t idx = drainOrder[k - 1u];
        int32_t p = parent[idx];
        if (p >= 0) {
            hydro->drainageAccum[p] += hydro->drainageAccum[idx];
        }
    }

    /* 3) Identificación de cuencas lacustres conectadas (filled > raw + eps). */
    float lakeDensity = recipe->hydrology.lakeDensity;
    if (lakeDensity > 1e-4f) {
        /* Umbral de área y profundidad mínima escalados según lakeDensity y tamaño del mapa. */
        float minLakeArea = fmaxf(10.0f, (1.15f - lakeDensity) * (tf->span * tf->span * 0.0022f));
        if (v3) minLakeArea *= lakeDensity >= .6f ? .95f : 1.65f;
        uint32_t minLakeCells = (uint32_t)fmaxf(12.0f, minLakeArea / fmaxf(0.01f, cellArea));
        float minLakePeakDepth = fmaxf(0.28f, 0.70f - 0.40f * lakeDensity);
        float maxWetFractionForLakes = 0.24f + 0.14f * lakeDensity;
        uint32_t maxLakeCellsTotal = (uint32_t)((float)count * maxWetFractionForLakes);
        uint32_t usedLakeCells = 0u;

        float joinDepth = continuous ? WORLD_WATER_DEPTH_EPSILON : 0.06f;
        for (uint32_t i = 0; i < (uint32_t)count; ++i) {
            if (compVis[i]) continue;
            if (filled[i] - tf->heights[i] <= 0.08f) continue;

            /* Extraer componente conexa de la depresión con elevación de vertido compatible. */
            float spillElev = filled[i];
            uint32_t qh = 0u, qt = 0u;
            bfsQ[qt++] = i;
            compVis[i] = 1u;
            float peakDepth = 0.0f;
            bool touchesBorder = false;

            while (qh < qt) {
                uint32_t c = bfsQ[qh++];
                float d = spillElev - tf->heights[c];
                if (d > peakDepth) peakDepth = d;
                int cx = (int)(c % res), cz = (int)(c / res);
                if (cx <= 1 || cz <= 1 || cx >= (int)res - 2 || cz >= (int)res - 2) touchesBorder = true;

                for (int k = 0; k < 4; ++k) {
                    int nx = cx + g_dx8[k], nz = cz + g_dz8[k];
                    if (nx < 0 || nz < 0 || nx >= (int)res || nz >= (int)res) continue;
                    uint32_t ni = (uint32_t)nz * res + (uint32_t)nx;
                    if (compVis[ni]) continue;
                    if (filled[ni] - tf->heights[ni] > joinDepth &&
                        fabsf(filled[ni] - spillElev) < (continuous ? 0.001f : 0.15f)) {
                        compVis[ni] = 1u;
                        bfsQ[qt++] = ni;
                    }
                }
            }

            if (!touchesBorder && qt >= minLakeCells && peakDepth >= minLakePeakDepth &&
                hydro->bodyCount < WORLD_MAX_WATER_BODIES &&
                usedLakeCells + qt <= maxLakeCellsTotal) {
                uint16_t bodyId = (uint16_t)(hydro->bodyCount + 1u);
                WorldWaterBody *b = &hydro->bodies[hydro->bodyCount++];
                initWaterBody(b, bodyId, WATER_BODY_LAKE, recipe);
                b->surfaceY = spillElev;
                ++hydro->lakeCount;
                usedLakeCells += qt;

                for (uint32_t k = 0; k < qt; ++k) {
                    uint32_t c = bfsQ[k];
                    float depth = spillElev - tf->heights[c];
                    if (continuous) hydro->surfaceBodyId[c] = bodyId;
                    if (depth > WORLD_WATER_DEPTH_EPSILON) {
                        hydro->waterBodyId[c] = bodyId;
                        hydro->waterSurfaceY[c] = spillElev;
                        hydro->waterDepth[c] = depth;
                    }
                }
            } else if (!morphology) {
                /* Rellenar micro-hoyos descartados para evitar pozos residuales sin salida. */
                for (uint32_t k = 0; k < qt; ++k) {
                    uint32_t c = bfsQ[k];
                    if (tf->heights[c] < filled[c]) {
                        tf->heights[c] = filled[c];
                    }
                }
            }
        }
    }

    if (trace) trace(context, "lake_owner_u16", hydro->waterBodyId, count, sizeof(*hydro->waterBodyId));

    /* 4) Extracción y excavación de red fluvial dirigida desde acumulación de drenaje. */
    float riverDensity = recipe->hydrology.riverDensity;
    if (riverDensity > 1e-4f && hydro->bodyCount < WORLD_MAX_WATER_BODIES) {
        float totalArea = tf->span * tf->span;
        /* Umbral de cuenca para iniciar un cauce permanente. */
        float accumThresh = totalArea * (0.048f - 0.036f * riverDensity);
        if (v3 && lakeDensity < .6f) accumThresh *= 1.23f;
        if (accumThresh < cellArea * 28.0f) accumThresh = cellArea * 28.0f;

        uint8_t *isChannel = calloc(count, 1u);
        float *riverSurface = calloc(count, sizeof(float));
        float *carveDelta = calloc(count, sizeof(float));
        if (!isChannel || !riverSurface || !carveDelta) {
            free(isChannel); free(riverSurface); free(carveDelta);
            free(filled); free(parent); free(drainOrder); free(bfsQ); free(compVis);
            WorldHydrology_Free(hydro);
            return false;
        }

        /* Marcar semillas fluviales y propagar aguas abajo hasta lago o salida del mapa. */
        for (uint32_t i = 0; i < (uint32_t)count; ++i) {
            if (hydro->drainageAccum[i] >= accumThresh && hydro->waterBodyId[i] == 0u) {
                int32_t cur = (int32_t)i;
                uint32_t guard = 0u;
                while (cur >= 0 && guard < (uint32_t)count) {
                    uint32_t u = (uint32_t)cur;
                    if (hydro->waterBodyId[u] != 0u) break; /* Desemboca en un lago */
                    if (isChannel[u]) break;
                    isChannel[u] = 1u;
                    cur = parent[u];
                    ++guard;
                }
            }
        }

        /* Garantizar al menos un río principal si riverDensity >= 0.25 y la cuenca no superó el umbral. */
        uint32_t channelCells = 0u;
        for (size_t i = 0; i < count; ++i) channelCells += isChannel[i];
        if (channelCells < 10u && riverDensity >= 0.25f) {
            /* Elegir la celda interior con mayor acumulación y trazarla aguas abajo. */
            uint32_t bestIdx = 0u;
            float bestAcc = -1.0f;
            for (uint32_t iz = 4u; iz + 4u < res; ++iz) {
                for (uint32_t ix = 4u; ix + 4u < res; ++ix) {
                    uint32_t idx = iz * res + ix;
                    if (hydro->waterBodyId[idx] != 0u) continue;
                    /* Preferir celdas con suficiente distancia a la salida para formar un cauce real. */
                    if (hydro->drainageAccum[idx] > bestAcc) {
                        bestAcc = hydro->drainageAccum[idx];
                        bestIdx = idx;
                    }
                }
            }
            /* Remontar brevemente hacia el tributario principal aguas arriba para longitud suficiente. */
            uint32_t head = bestIdx;
            for (uint32_t step = 0; step < res / 2u; ++step) {
                uint32_t bestUp = head;
                float bestUpAcc = 0.0f;
                int cx = (int)(head % res), cz = (int)(head / res);
                for (int d = 0; d < 8; ++d) {
                    int nx = cx + g_dx8[d], nz = cz + g_dz8[d];
                    if (nx <= 2 || nz <= 2 || nx >= (int)res - 3 || nz >= (int)res - 3) continue;
                    uint32_t ni = (uint32_t)nz * res + (uint32_t)nx;
                    if (parent[ni] == (int32_t)head && hydro->waterBodyId[ni] == 0u &&
                        hydro->drainageAccum[ni] > bestUpAcc) {
                        bestUpAcc = hydro->drainageAccum[ni];
                        bestUp = ni;
                    }
                }
                if (bestUp == head) break;
                head = bestUp;
            }
            int32_t cur = (int32_t)head;
            uint32_t guard = 0u;
            while (cur >= 0 && guard < (uint32_t)count) {
                uint32_t u = (uint32_t)cur;
                if (hydro->waterBodyId[u] != 0u) break;
                isChannel[u] = 1u;
                cur = parent[u];
                ++guard;
            }
        }

        if (trace) trace(context, "channel_u8", isChannel, count, sizeof(*isChannel));
        if (continuous) {
            /* Propagar desde las salidas hacia las fuentes. La cota del lago
             * se conserva en la desembocadura y cada enlace desciende hacia su padre. */
            for (size_t k = 0; k < count; ++k) {
                uint32_t i = drainOrder[k];
                if (!isChannel[i]) continue;
                int32_t p = parent[i];
                float downstream = filled[i];
                if (p >= 0) downstream = isChannel[p] ? riverSurface[p] : hydro->waterSurfaceY[p];
                riverSurface[i] = fmaxf(filled[i], downstream);
            }
            /* Priority-Flood ya ha entregado las cotas del grafo. Retener el
             * relieve físico original para reconocer los saltos excavados. */
            if (morphology) memcpy(filled, tf->heights, count * sizeof(*filled));
            if (!carveContinuousRivers(hydro, tf, recipe, parent, drainOrder, isChannel,
                                        riverSurface, accumThresh, carveDelta, trace, context)) {
                free(isChannel); free(riverSurface); free(carveDelta);
                free(filled); free(parent); free(drainOrder); free(bfsQ); free(compVis);
                WorldHydrology_Free(hydro);
                return false;
            }
            if (morphology) {
                if (trace) trace(context, "river_ground_f32", tf->heights, count, sizeof(*tf->heights));
                repairRiverShoreSeams(hydro, tf, filled, carveDelta, bfsQ, compVis, shoreline);
            }
        } else {
            /* Asignar elevación de lámina de río no-creciente aguas abajo (orden topológico)
             * y vector de flujo unitario*caudal hacia el padre. */
            for (size_t k = count; k > 0u; --k) {
                uint32_t idx = drainOrder[k - 1u];
                if (!isChannel[idx]) continue;
                float baseSurf = filled[idx];
                if (riverSurface[idx] <= 0.0f || baseSurf < riverSurface[idx]) {
                    riverSurface[idx] = baseSurf;
                }
            }
            /* Paso de monotonicidad estricta aguas abajo: parent.surface <= child.surface. */
            for (size_t k = count; k > 0u; --k) {
                uint32_t idx = drainOrder[k - 1u];
                if (!isChannel[idx]) continue;
                int32_t p = parent[idx];
                if (p >= 0) {
                    if (hydro->waterBodyId[p] != 0u) {
                        /* Compatibilidad de nivel en desembocadura en lago. */
                        float lakeY = hydro->waterSurfaceY[p];
                        if (riverSurface[idx] < lakeY) riverSurface[idx] = lakeY;
                    } else if (isChannel[p]) {
                        if (riverSurface[p] > riverSurface[idx]) {
                            riverSurface[p] = riverSurface[idx];
                        }
                    }
                }
            }

            /* Excavar cauce suave con sección parabólica/coseno y ribera continua. */
            uint16_t riverBodyId = (uint16_t)(hydro->bodyCount + 1u);
            bool anyRiverWet = false;
            float wScale = recipe->hydrology.riverWidthScale;
            float dScale = recipe->hydrology.riverDepthScale;

            for (uint32_t idx = 0; idx < (uint32_t)count; ++idx) {
                if (!isChannel[idx]) continue;
                int cx = (int)(idx % res), cz = (int)(idx / res);
                float accNorm = sqrtf(fmaxf(1.0f, hydro->drainageAccum[idx] / fmaxf(1.0f, accumThresh * 0.25f)));
                /* Semiancho en unidades del mundo acotado para permitir vados y cruces realistas. */
                float halfW = fminf(5.5f * wScale, (1.15f + 0.55f * logf(1.0f + accNorm)) * wScale);
                float bankW = halfW + fmaxf(1.5f, halfW * 0.85f);
                float targetDepth = fminf(2.4f * dScale, (0.48f + 0.24f * logf(1.0f + accNorm)) * dScale);
                float surfY = riverSurface[idx];

                /* Dirección de flujo hacia el padre. */
                float fx = 0.0f, fz = 0.0f;
                int32_t p = parent[idx];
                if (p >= 0) {
                    int px = (int)((uint32_t)p % res), pz = (int)((uint32_t)p / res);
                    float dx = (float)(px - cx);
                    float dz = (float)(pz - cz);
                    float len = sqrtf(dx * dx + dz * dz);
                    if (len > 1e-4f) {
                        float speed = fminf(2.0f, 0.45f + 0.20f * logf(1.0f + accNorm));
                        fx = (dx / len) * speed;
                        fz = (dz / len) * speed;
                    }
                }

                int radCells = (int)ceilf(bankW * tf->invSpacing);
                if (radCells > 24) radCells = 24;
                for (int dz = -radCells; dz <= radCells; ++dz) {
                    int nz = cz + dz;
                    if (nz < 0 || nz >= (int)res) continue;
                    for (int dx = -radCells; dx <= radCells; ++dx) {
                        int nx = cx + dx;
                        if (nx < 0 || nx >= (int)res) continue;
                        float dist = sqrtf((float)(dx * dx + dz * dz)) * tf->spacing;
                        if (dist > bankW) continue;
                        uint32_t ni = (uint32_t)nz * res + (uint32_t)nx;
                        if (hydro->waterBodyId[ni] != 0u && hydro->waterBodyId[ni] != riverBodyId) {
                            continue; /* No sobreescribir lagos */
                        }

                        float hOld = tf->heights[ni];
                        if (dist <= halfW) {
                            /* Interior húmedo del cauce: perfil parabólico suave. */
                            float t = dist / fmaxf(0.1f, halfW);
                            float bedY = surfY - targetDepth * (1.0f - 0.82f * t * t);
                            if (bedY < 0.1f) bedY = 0.1f;
                            float drop = hOld - bedY;
                            if (drop > carveDelta[ni]) carveDelta[ni] = drop;
                            if (hydro->waterBodyId[ni] == 0u || surfY < hydro->waterSurfaceY[ni]) {
                                hydro->waterSurfaceY[ni] = surfY;
                            }
                            hydro->waterBodyId[ni] = riverBodyId;
                            hydro->flowX[ni] = fx;
                            hydro->flowZ[ni] = fz;
                            anyRiverWet = true;
                        } else {
                            /* Talud de ribera suave hacia el terreno adyacente (sin escalones verticales). */
                            float u = (dist - halfW) / fmaxf(0.1f, bankW - halfW);
                            float smooth = u * u * (3.0f - 2.0f * u);
                            float bankTarget = surfY + 0.18f + (hOld - (surfY + 0.18f)) * smooth;
                            float drop = hOld - bankTarget;
                            if (drop > carveDelta[ni]) carveDelta[ni] = drop;
                        }
                    }
                }
            }

            if (anyRiverWet) {
                WorldWaterBody *rb = &hydro->bodies[hydro->bodyCount++];
                initWaterBody(rb, riverBodyId, WATER_BODY_RIVER, recipe);
                ++hydro->riverCount;
                for (size_t i = 0; i < count; ++i) {
                    if (carveDelta[i] > 0.0f) {
                        float nh = tf->heights[i] - carveDelta[i];
                        tf->heights[i] = nh > 0.05f ? nh : 0.05f;
                    }
                }
            }
        }

        free(isChannel);
        free(riverSurface);
        free(carveDelta);
    }

    if (continuous) {
        for (uint32_t j = 0; j < (uint32_t)count; ++j) {
            if (hydro->surfaceBodyId[j]) continue;
            uint16_t owner = 0;
            float bestMargin = FLT_MAX;
            int x = (int)(j % res), z = (int)(j / res);
            for (int d = 0; d < 8; ++d) {
                int nx = x + g_dx8[d], nz = z + g_dz8[d];
                if (nx < 0 || nz < 0 || nx >= (int)res || nz >= (int)res) continue;
                uint16_t near = hydro->waterBodyId[(uint32_t)nz * res + (uint32_t)nx];
                if (!near || near > hydro->lakeCount) continue;
                float margin = tf->heights[j] - hydro->bodies[near - 1u].surfaceY;
                if (margin >= -WORLD_WATER_DEPTH_EPSILON && margin < bestMargin) {
                    owner = near;
                    bestMargin = margin;
                }
            }
            if (owner) {
                hydro->surfaceBodyId[j] = owner;
                hydro->waterSurfaceY[j] = hydro->bodies[owner - 1u].surfaceY;
            }
        }
    }

    if (trace) trace(context, "carved_ground_f32", tf->heights, count, sizeof(*tf->heights));
    if (morphology) relaxShoreV3(hydro, tf, filled, bfsQ);

    if (continuous && !morphology && hydro->lakeCount) {
        /* Filtrar el lecho somero cerca del talud con un ajuste simétrico y
         * acotado. Mantener el lado de la cota, islas, vertederos y acantilados. */
        memcpy(filled, tf->heights, count * sizeof(*filled));
        for (uint32_t z = 1; z + 1u < res; ++z) {
            for (uint32_t x = 1; x + 1u < res; ++x) {
                uint32_t j = z * res + x;
                uint16_t lake = hydro->waterBodyId[j];
                if (!lake || lake > hydro->lakeCount) continue;
                float level = hydro->bodies[lake - 1u].surfaceY;
                float depth = level - filled[j];
                if (depth <= WORLD_WATER_DEPTH_EPSILON + 0.003f || depth >= 0.48f) continue;
                uint32_t neighbors[] = {j - 1u, j + 1u, j - res, j + res};
                float sum = 0.0f;
                unsigned countWet = 0;
                bool edge = false, cliff = false;
                for (unsigned k = 0; k < 4; ++k) {
                    uint32_t n = neighbors[k];
                    cliff |= fabsf(filled[j] - filled[n]) > 0.8f * tf->spacing;
                    if (hydro->waterBodyId[n] == lake) {
                        sum += filled[n];
                        ++countWet;
                    } else {
                        edge = true;
                    }
                }
                if (!edge || cliff || countWet < 2u) continue;
                float correction = 0.20f * (sum / (float)countWet - filled[j]);
                float limit = fminf(0.08f, 0.16f * tf->spacing);
                tf->heights[j] = fminf(level - (WORLD_WATER_DEPTH_EPSILON + 0.003f),
                                        filled[j] + fminf(limit, fmaxf(-limit, correction)));
            }
        }
    }

    if (trace) trace(context, "relaxed_ground_f32", tf->heights, count, sizeof(*tf->heights));

    /* 5) Recalcular normales/pendientes finales del terreno y validar coherencia física de agua. */
    if (!TerrainHeightfield_RebuildDerived(tf)) {
        free(filled); free(parent); free(drainOrder); free(bfsQ); free(compVis);
        WorldHydrology_Free(hydro);
        return false;
    }

    /* Recomputar waterDepth exacto = max(0, waterSurfaceY - groundY) y estadísticas por cuerpo. */
    hydro->totalWetCells = 0u;
    for (uint32_t b = 0; b < hydro->bodyCount; ++b) {
        hydro->bodies[b].wetCellCount = 0u;
        hydro->bodies[b].maxDepth = 0.0f;
        hydro->bodies[b].bounds = AABB_Empty();
        hydro->bodies[b].centroid = Vec3_Zero();
        hydro->bodies[b].minSurfaceY = FLT_MAX;
        hydro->bodies[b].maxSurfaceY = -FLT_MAX;
    }
    double *sumX = calloc(hydro->bodyCount + 1u, sizeof(double));
    double *sumZ = calloc(hydro->bodyCount + 1u, sizeof(double));
    double *sumY = calloc(hydro->bodyCount + 1u, sizeof(double));

    for (uint32_t iz = 0; iz < res; ++iz) {
        float wz = tf->originZ + (float)iz * tf->spacing;
        for (uint32_t ix = 0; ix < res; ++ix) {
            uint32_t idx = iz * res + ix;
            uint16_t bid = hydro->waterBodyId[idx];
            if (bid == 0u || bid > hydro->bodyCount) {
                hydro->waterBodyId[idx] = 0u;
                hydro->waterDepth[idx] = 0.0f;
                if (!continuous || !hydro->surfaceBodyId[idx]) {
                    hydro->waterSurfaceY[idx] = 0.0f;
                    hydro->flowX[idx] = 0.0f;
                    hydro->flowZ[idx] = 0.0f;
                }
                continue;
            }
            float depth = hydro->waterSurfaceY[idx] - tf->heights[idx];
            if (depth <= WORLD_WATER_DEPTH_EPSILON) {
                hydro->waterBodyId[idx] = 0u;
                hydro->waterDepth[idx] = 0.0f;
                if (!continuous) {
                    hydro->waterSurfaceY[idx] = 0.0f;
                    hydro->flowX[idx] = 0.0f;
                    hydro->flowZ[idx] = 0.0f;
                }
                continue;
            }
            hydro->waterDepth[idx] = depth;
            ++hydro->totalWetCells;
            float wx = tf->originX + (float)ix * tf->spacing;
            WorldWaterBody *b = &hydro->bodies[bid - 1u];
            ++b->wetCellCount;
            if (depth > b->maxDepth) b->maxDepth = depth;
            if (hydro->waterSurfaceY[idx] < b->minSurfaceY) b->minSurfaceY = hydro->waterSurfaceY[idx];
            if (hydro->waterSurfaceY[idx] > b->maxSurfaceY) b->maxSurfaceY = hydro->waterSurfaceY[idx];
            AABB_ExpandPoint(&b->bounds, Vec3_Create(wx, tf->heights[idx], wz));
            AABB_ExpandPoint(&b->bounds, Vec3_Create(wx, hydro->waterSurfaceY[idx], wz));
            if (sumX && sumZ && sumY) {
                sumX[bid] += (double)wx;
                sumZ[bid] += (double)wz;
                sumY[bid] += (double)hydro->waterSurfaceY[idx];
            }
        }
    }
    for (uint32_t b = 0; b < hydro->bodyCount; ++b) {
        WorldWaterBody *wb = &hydro->bodies[b];
        if (wb->wetCellCount > 0u && sumX && sumZ && sumY) {
            double invN = 1.0 / (double)wb->wetCellCount;
            wb->centroid = Vec3_Create((float)(sumX[wb->id] * invN),
                                       (float)(sumY[wb->id] * invN),
                                       (float)(sumZ[wb->id] * invN));
            if (wb->type == WATER_BODY_RIVER) {
                wb->surfaceY = wb->centroid.y;
            }
        }
    }
    free(sumX); free(sumZ); free(sumY);
    hydro->wetAreaFraction = (float)hydro->totalWetCells / (float)count;

    free(filled);
    free(parent);
    free(drainOrder);
    free(bfsQ);
    free(compVis);

    /* 6) Extraer sitios de orilla iniciales (se refinarán nuevamente tras colocar rocas si es preciso). */
    return WorldHydrology_RebuildShoreSites(hydro, tf, recipe, NULL, NULL);
}

bool WorldHydrology_Generate(WorldHydrology *hydro, TerrainHeightfield *tf,
                             const WorldRecipe *recipe, uint32_t hydroSeed) {
    return WorldHydrology_GenerateTraced(hydro, tf, recipe, hydroSeed, NULL, NULL);
}

static void addShoreSite(WorldHydrology *hydro, const TerrainHeightfield *tf,
                         uint16_t bid, Vector3 wetContact, float outX, float outZ,
                         float minSpacingSq, float approachOffset, bool morphology,
                         bool (*extraBlockedFn)(const void *ctx, Vector3 pos, float radius),
                         const void *extraBlockedCtx) {
    float wx = wetContact.x, wz = wetContact.z, surfY = wetContact.y;
    const float maxSlopeRad = 0.60f;
    /* Espaciado mínimo contra estaciones ya aceptadas del mismo entorno. */
    bool tooClose = false;
    for (uint32_t s = 0; s < hydro->shoreSiteCount; ++s) {
        float dx = hydro->shoreSites[s].wetContactPoint.x - wetContact.x;
        float dz = hydro->shoreSites[s].wetContactPoint.z - wetContact.z;
        const WorldShoreSite *site = &hydro->shoreSites[s];
        bool facing = outX * site->bankNormal.x + outZ * site->bankNormal.z > -.25f;
        if (dx * dx + dz * dz < (morphology && !facing ? 3.24f : minSpacingSq)) {
            tooClose = true;
            break;
        }
    }
    if (tooClose) return;

    /* Validar punto de aproximación en tierra/vado bajo en dirección bankNormal. */
    float ax = wx + outX * approachOffset;
    float az = wz + outZ * approachOffset;
    float margin = 1.6f;
    if (ax < tf->originX + margin || ax > tf->originX + tf->span - margin ||
        az < tf->originZ + margin || az > tf->originZ + tf->span - margin) {
        return;
    }
    SurfaceHit bankHit;
    if (!Terrain_SampleGround(tf, ax, az, &bankHit)) return;
    float bankSlope = acosf(fmaxf(0.0f, fminf(1.0f, bankHit.normal.y)));
    if (bankSlope > maxSlopeRad) return;
    if (morphology) {
        float previous = bankHit.position.y;
        for (unsigned k = 1; k <= 3; ++k) {
            float distance = approachOffset * (1.0f - .225f * (float)k);
            float px = wx + outX * distance, pz = wz + outZ * distance;
            SurfaceHit traversed;
            WaterSample water;
            if (!Terrain_SampleGround(tf, px, pz, &traversed) ||
                traversed.normal.y < .79f || fabsf(previous - traversed.position.y) > .28f ||
                (WorldHydrology_SampleWater(hydro, tf, px, pz, &water) && water.depth > .3f))
                return;
            previous = traversed.position.y;
        }
    }

    WaterSample bankWater;
    if (WorldHydrology_SampleWater(hydro, tf, ax, az, &bankWater) && bankWater.wet &&
        bankWater.depth > 0.30f) {
        return;
    }
    /* Comprobar que el agua esté dentro de alcance vertical plausible de la boca. */
    float verticalDelta = bankHit.position.y - surfY;
    if (verticalDelta < -0.35f || verticalDelta > 1.15f) return;

    if (extraBlockedFn && extraBlockedFn(extraBlockedCtx, bankHit.position, 1.05f)) {
        return;
    }

    /* Comprobar visibilidad directa desde boca típica en bankApproachPoint hasta wetContact. */
    Vector3 mouthApprox = Vec3_Add(bankHit.position, Vec3_Create(0.0f, 0.55f, 0.0f));
    if (Terrain_RaycastSegment(tf, mouthApprox, wetContact, 0.10f, NULL, NULL)) {
        return;
    }

    WorldShoreSite *site = &hydro->shoreSites[hydro->shoreSiteCount++];
    site->siteId = hydro->shoreSiteCount;
    site->waterBodyId = bid;
    site->wetContactPoint = wetContact;
    site->bankApproachPoint = bankHit.position;
    site->bankNormal = Vec3_Create(outX, 0.0f, outZ);
    site->waterSurfaceY = surfY;
    site->bankSlopeRadians = bankSlope;
    site->maxSafeFootprint = (bankSlope < 0.42f) ? 1.25f : 0.90f;
    site->accessible = true;
}

static void rebuildContinuousShores(WorldHydrology *hydro, const TerrainHeightfield *tf,
                                    float minSpacingSq, bool morphology,
                                    bool (*blocked)(const void *, Vector3, float), const void *ctx) {
    double area = 0.0, totalLength = 0.0;
    float minLength = tf->spacing * 0.03f;
    for (uint32_t z = 0; z + 1u < tf->resolution; ++z) {
        for (uint32_t x = 0; x + 1u < tf->resolution; ++x) {
            for (unsigned triangle = 0; triangle < 2; ++triangle) {
                WorldWaterPatch patches[3];
                unsigned count = WorldHydrology_GetWaterPatches(hydro, tf, x, z, triangle, patches);
                for (unsigned k = 0; k < count; ++k) {
                    WorldWaterVertex v[WORLD_WATER_PATCH_VERTICES];
                    unsigned n = WorldHydrology_ClipWaterPatch(tf, &patches[k], v);
                    Vector3 center = Vec3_Zero();
                    for (unsigned i = 0; i < n; ++i) {
                        center = Vec3_Add(center, v[i].position);
                        if (i >= 2) {
                            Vector3 a = Vec3_Sub(v[i - 1u].position, v[0].position);
                            Vector3 b = Vec3_Sub(v[i].position, v[0].position);
                            area += 0.5 * ((double)a.z * b.x - (double)a.x * b.z);
                        }
                    }
                    if (n < 3) continue;
                    center = Vec3_Scale(center, 1.0f / (float)n);
                    for (unsigned i = 0; i < n; ++i) {
                        const WorldWaterVertex *a = &v[i], *b = &v[(i + 1u) % n];
                        if (!(a->boundary & b->boundary)) continue;
                        float dx = b->position.x - a->position.x;
                        float dz = b->position.z - a->position.z;
                        float length = hypotf(dx, dz);
                        totalLength += length;
                        if (length < minLength || hydro->shoreSiteCount >= WORLD_MAX_SHORE_SITES) continue;
                        Vector3 contact = Vec3_Lerp(Vec3_Lerp(a->position, b->position, 0.5f), center, 0.28f);
                        WaterSample water;
                        if (!WorldHydrology_SampleWater(hydro, tf, contact.x, contact.z, &water) ||
                            water.waterBodyId != patches[k].bodyId) continue;
                        contact.y = water.surfaceY;
                        const float offsets[] = {1.15f, .82f, 1.50f};
                        unsigned previous = hydro->shoreSiteCount;
                        for (unsigned j = 0; j < (morphology ? 3u : 1u) && hydro->shoreSiteCount == previous; ++j)
                            addShoreSite(hydro, tf, water.waterBodyId, contact, -dz / length, dx / length,
                                         minSpacingSq, offsets[j], morphology, blocked, ctx);
                    }
                }
            }
        }
    }
    hydro->wetAreaFraction = (float)(area / ((double)tf->span * tf->span));
    hydro->shorelineLength = (float)totalLength;
}

bool WorldHydrology_RebuildShoreSites(WorldHydrology *hydro, const TerrainHeightfield *tf,
                                      const WorldRecipe *recipe,
                                      bool (*extraBlockedFn)(const void *ctx, Vector3 pos, float radius),
                                      const void *extraBlockedCtx) {
    bool morphology = recipe && usesGradedBanks(recipe);
    if (!hydro || !tf || !hydro->shoreSites) return false;
    hydro->shoreSiteCount = 0u;
    if (!hydro->enabled || hydro->bodyCount == 0u || hydro->totalWetCells == 0u) return true;

    uint32_t res = tf->resolution;
    /* Espaciado adaptativo para cubrir ambas orillas de ríos y perímetros de lagos
     * sin saturar la memoria perceptiva de 32 entradas. */
    float minSpacing = fmaxf(4.5f, fminf(11.0f, tf->span * 0.032f));
    float minSpacingSq = minSpacing * minSpacing;
    if (hydro->surfaceBodyId) {
        rebuildContinuousShores(hydro, tf, minSpacingSq, morphology, extraBlockedFn, extraBlockedCtx);
        return true;
    }

    for (uint32_t iz = 2u; iz + 2u < res && hydro->shoreSiteCount < WORLD_MAX_SHORE_SITES; ++iz) {
        for (uint32_t ix = 2u; ix + 2u < res && hydro->shoreSiteCount < WORLD_MAX_SHORE_SITES; ++ix) {
            uint32_t idx = iz * res + ix;
            uint16_t bid = hydro->waterBodyId[idx];
            if (bid == 0u || hydro->waterDepth[idx] < 0.05f) continue;

            /* Buscar dirección saliente desde agua hacia tierra seca adyacente. */
            float outX = 0.0f, outZ = 0.0f;
            bool touchesDry = false;
            for (int d = 0; d < 8; ++d) {
                uint32_t ni = (uint32_t)((int)iz + g_dz8[d]) * res + (uint32_t)((int)ix + g_dx8[d]);
                if (hydro->waterBodyId[ni] == 0u) {
                    touchesDry = true;
                    outX += (float)g_dx8[d] / g_dist8[d];
                    outZ += (float)g_dz8[d] / g_dist8[d];
                }
            }
            if (!touchesDry) continue;
            float outLen = sqrtf(outX * outX + outZ * outZ);
            if (outLen < 0.25f) continue;
            outX /= outLen;
            outZ /= outLen;

            float wx = tf->originX + (float)ix * tf->spacing;
            float wz = tf->originZ + (float)iz * tf->spacing;
            float surfY = hydro->waterSurfaceY[idx];
            Vector3 wetContact = Vec3_Create(wx, surfY, wz);

            addShoreSite(hydro, tf, bid, wetContact, outX, outZ, minSpacingSq, 1.15f, false,
                         extraBlockedFn, extraBlockedCtx);
        }
    }
    return true;
}

bool WorldHydrology_SampleWater(const WorldHydrology *hydro, const TerrainHeightfield *tf,
                                float x, float z, WaterSample *out) {
    if (!out) return false;
    memset(out, 0, sizeof(*out));
    if (!hydro || !tf || !Terrain_InBounds(tf, x, z)) return false;

    float groundY = 0.0f;
    if (!Terrain_SampleHeight(tf, x, z, &groundY)) return false;
    out->groundY = groundY;
    if (!hydro->enabled || !hydro->waterBodyId) return true;

    float fx = (x - tf->originX) * tf->invSpacing;
    float fz = (z - tf->originZ) * tf->invSpacing;
    uint32_t res = tf->resolution;
    uint32_t maxCell = res - 2u;
    int ix = (int)floorf(fx);
    int iz = (int)floorf(fz);
    if (ix < 0) ix = 0; else if ((uint32_t)ix > maxCell) ix = (int)maxCell;
    if (iz < 0) iz = 0; else if ((uint32_t)iz > maxCell) iz = (int)maxCell;
    float u = fx - (float)ix;
    float v = fz - (float)iz;
    if (u < 0.0f) u = 0.0f; else if (u > 1.0f) u = 1.0f;
    if (v < 0.0f) v = 0.0f; else if (v > 1.0f) v = 1.0f;

    /* Usar exactamente los 3 vértices del triángulo autoritativo (T0 o T1). */
    size_t i00 = (size_t)iz * res + (size_t)ix;
    size_t ia = i00, ib, ic;
    float wa, wb, wc;
    if (u >= v) {
        ib = i00 + 1u;
        ic = i00 + res + 1u;
        wa = 1.0f - u;
        wb = u - v;
        wc = v;
    } else {
        ib = i00 + res;
        ic = i00 + res + 1u;
        wa = 1.0f - v;
        wb = v - u;
        wc = u;
    }

    if (hydro->surfaceBodyId) {
        const uint16_t *owners = hydro->surfaceBodyId;
        if (!(owners[ia] || owners[ib] || owners[ic])) return true;
        unsigned triangle = u >= v ? 0u : 1u;
        const float w[3] = {wa, triangle ? wb : wc, triangle ? wc : wb};
        WorldWaterPatch patches[3];
        unsigned n = WorldHydrology_GetWaterPatches(hydro, tf, (uint32_t)ix, (uint32_t)iz,
                                                      triangle, patches);
        for (unsigned k = 0; k < n; ++k) {
            const WorldWaterPatch *p = &patches[k];
            if (out->wet && p->bodyId >= out->waterBodyId) continue;
            float domain = p->domain[0] * w[0] + p->domain[1] * w[1] + p->domain[2] * w[2];
            float depth = p->depth[0] * w[0] + p->depth[1] * w[1] + p->depth[2] * w[2];
            if (domain < 0.0f || depth <= WORLD_WATER_DEPTH_EPSILON) continue;
            const WorldWaterBody *body = WorldHydrology_GetBody(hydro, p->bodyId);
            out->wet = true;
            out->waterBodyId = p->bodyId;
            out->type = body->type;
            out->surfaceY = body->type == WATER_BODY_LAKE ? body->surfaceY :
                p->surface[0] * w[0] + p->surface[1] * w[1] + p->surface[2] * w[2];
            out->depth = depth;
            out->flow.x = p->flowX[0] * w[0] + p->flowX[1] * w[1] + p->flowX[2] * w[2];
            out->flow.z = p->flowZ[0] * w[0] + p->flowZ[1] * w[1] + p->flowZ[2] * w[2];
        }
        return true;
    }

    uint16_t ba = hydro->waterBodyId[ia];
    uint16_t bb = hydro->waterBodyId[ib];
    uint16_t bc = hydro->waterBodyId[ic];
    if (ba == 0u && bb == 0u && bc == 0u) return true;

    /* Interpolar superficie únicamente a partir de los vértices húmedos del triángulo
     * y verificar profundidad física positiva contra groundY. */
    float wSum = 0.0f;
    float surfSum = 0.0f;
    float flowXSum = 0.0f, flowZSum = 0.0f;
    uint16_t bestBody = 0u;
    float bestWeight = -1.0f;

    const size_t ids[3] = {ia, ib, ic};
    const uint16_t bds[3] = {ba, bb, bc};
    const float ws[3] = {wa, wb, wc};
    for (int k = 0; k < 3; ++k) {
        if (bds[k] != 0u) {
            wSum += ws[k];
            surfSum += ws[k] * hydro->waterSurfaceY[ids[k]];
            flowXSum += ws[k] * hydro->flowX[ids[k]];
            flowZSum += ws[k] * hydro->flowZ[ids[k]];
            if (ws[k] > bestWeight) { bestWeight = ws[k]; bestBody = bds[k]; }
        }
    }
    /* Exigir cobertura húmeda real en el triángulo para evitar láminas flotando sobre suelo seco. */
    if (wSum < 0.25f || bestBody == 0u) return true;

    float surfaceY = surfSum / wSum;
    float depth = surfaceY - groundY;
    if (depth <= 0.01f) return true;

    out->wet = true;
    out->waterBodyId = bestBody;
    const WorldWaterBody *wbObj = WorldHydrology_GetBody(hydro, bestBody);
    out->type = wbObj ? wbObj->type : WATER_BODY_RIVER;
    out->surfaceY = surfaceY;
    out->depth = depth;
    out->flow = Vec3_Create(flowXSum / wSum, 0.0f, flowZSum / wSum);
    return true;
}

const WorldWaterBody *WorldHydrology_GetBody(const WorldHydrology *hydro, uint16_t waterBodyId) {
    if (!hydro || !hydro->bodies || waterBodyId == 0u || waterBodyId > hydro->bodyCount) return NULL;
    return &hydro->bodies[waterBodyId - 1u];
}

const WorldShoreSite *WorldHydrology_FindNearestShore(const WorldHydrology *hydro, Vector3 pos,
                                                      float maxRadius, uint16_t filterBodyId) {
    if (!hydro || !hydro->shoreSites || hydro->shoreSiteCount == 0u) return NULL;
    float bestD2 = (maxRadius > 0.0f) ? (maxRadius * maxRadius) : FLT_MAX;
    const WorldShoreSite *best = NULL;
    for (uint32_t i = 0; i < hydro->shoreSiteCount; ++i) {
        const WorldShoreSite *s = &hydro->shoreSites[i];
        if (!s->accessible) continue;
        if (filterBodyId != 0u && s->waterBodyId != filterBodyId) continue;
        float dx = s->wetContactPoint.x - pos.x;
        float dz = s->wetContactPoint.z - pos.z;
        float d2 = dx * dx + dz * dz;
        if (d2 < bestD2) {
            bestD2 = d2;
            best = s;
        }
    }
    return best;
}

uint64_t WorldHydrology_Fingerprint(const WorldHydrology *hydro) {
    if (!hydro) return 0u;
    uint64_t h = UINT64_C(14695981039346656037);
    fnv1aUpdate(&h, &hydro->resolution, sizeof(hydro->resolution));
    fnv1aUpdate(&h, &hydro->bodyCount, sizeof(hydro->bodyCount));
    fnv1aUpdate(&h, &hydro->shoreSiteCount, sizeof(hydro->shoreSiteCount));
    size_t count = (size_t)hydro->resolution * (size_t)hydro->resolution;
    if (hydro->waterBodyId && hydro->waterDepth) {
        for (size_t i = 0; i < count; ++i) {
            if (hydro->waterBodyId[i] != 0u) {
                uint32_t idx = (uint32_t)i;
                int32_t qd = (int32_t)llround((double)hydro->waterDepth[i] * 1000.0);
                fnv1aUpdate(&h, &idx, sizeof(idx));
                fnv1aUpdate(&h, &hydro->waterBodyId[i], sizeof(hydro->waterBodyId[i]));
                fnv1aUpdate(&h, &qd, sizeof(qd));
            }
        }
    }
    if (hydro->surfaceBodyId) {
        for (size_t i = 0; i < count; ++i) {
            uint16_t owner = hydro->surfaceBodyId[i];
            fnv1aUpdate(&h, &owner, sizeof(owner));
            if (owner) {
                int32_t field[3] = {
                    (int32_t)llround((double)hydro->waterSurfaceY[i] * 1000.0),
                    (int32_t)llround((double)hydro->flowX[i] * 1000.0),
                    (int32_t)llround((double)hydro->flowZ[i] * 1000.0),
                };
                fnv1aUpdate(&h, field, sizeof(field));
            }
        }
    }
    for (uint32_t i = 0; i < hydro->shoreSiteCount; ++i) {
        const WorldShoreSite *s = &hydro->shoreSites[i];
        int32_t qx = (int32_t)llround((double)s->wetContactPoint.x * 1000.0);
        int32_t qy = (int32_t)llround((double)s->wetContactPoint.y * 1000.0);
        int32_t qz = (int32_t)llround((double)s->wetContactPoint.z * 1000.0);
        fnv1aUpdate(&h, &s->waterBodyId, sizeof(s->waterBodyId));
        fnv1aUpdate(&h, &qx, sizeof(qx));
        fnv1aUpdate(&h, &qy, sizeof(qy));
        fnv1aUpdate(&h, &qz, sizeof(qz));
    }
    return h;
}
