/**
 * @file CreatureNavigation.c
 * @brief A* topográfico 2.5D con resolución física preservada en mapas grandes (hasta 512x512),
 *        caché inmutable compartida, scratch dinámico por hilo libre de fugas y prevención de
 *        corte diagonal de esquinas.
 */
#include "CreatureNavigation.h"
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#define NAV_MAX_DIM 512
#define NAV_MAX_EXPANSIONS_FLAT 4096
#define NAV_MAX_EXPANSIONS_TERRAIN 8192

/* Almacenamiento contiguo único: free(w->navCache) libera toda la caché sin fugas. */
typedef struct NavStaticCache {
    unsigned revision;
    uint64_t worldFingerprint;
    float halfSize;
    float cellSize;
    int dim;
    unsigned cells;
    float data[]; /* [0..cells-1]=landClearance, [cells..2*cells-1]=swimClearance */
} NavStaticCache;

typedef struct NavThreadScratch {
    uint32_t capacity;
    uint32_t gen;
    uint32_t *visitGen;
    uint8_t *state; /* 1 = abierto, 2 = cerrado */
    float *g;
    int *parent;
    int *heap;
    float *heapF;
    int *heapPos;
} NavThreadScratch;

static pthread_key_t g_navScratchKey;
static pthread_once_t g_navKeyOnce = PTHREAD_ONCE_INIT;

static void destroyNavScratch(void *ptr) {
    NavThreadScratch *sc = (NavThreadScratch *)ptr;
    if (!sc) return;
    free(sc->visitGen);
    free(sc->state);
    free(sc->g);
    free(sc->parent);
    free(sc->heap);
    free(sc->heapF);
    free(sc->heapPos);
    free(sc);
}

static void initNavKey(void) {
    pthread_key_create(&g_navScratchKey, destroyNavScratch);
}

static NavThreadScratch *acquireScratch(uint32_t neededCells) {
    pthread_once(&g_navKeyOnce, initNavKey);
    NavThreadScratch *sc = (NavThreadScratch *)pthread_getspecific(g_navScratchKey);
    if (!sc) {
        sc = calloc(1, sizeof(*sc));
        if (!sc) return NULL;
        pthread_setspecific(g_navScratchKey, sc);
    }
    if (sc->capacity < neededCells) {
        uint32_t cap = neededCells < 4096u ? 4096u : neededCells;
        uint32_t *vg = calloc(cap, sizeof(uint32_t));
        uint8_t *st = calloc(cap, sizeof(uint8_t));
        float *g = malloc(cap * sizeof(float));
        int *par = malloc(cap * sizeof(int));
        int *hp = malloc(cap * sizeof(int));
        float *hf = malloc(cap * sizeof(float));
        int *hpos = malloc(cap * sizeof(int));
        if (!vg || !st || !g || !par || !hp || !hf || !hpos) {
            free(vg); free(st); free(g); free(par); free(hp); free(hf); free(hpos);
            return NULL;
        }
        free(sc->visitGen); free(sc->state); free(sc->g); free(sc->parent);
        free(sc->heap); free(sc->heapF); free(sc->heapPos);
        sc->visitGen = vg;
        sc->state = st;
        sc->g = g;
        sc->parent = par;
        sc->heap = hp;
        sc->heapF = hf;
        sc->heapPos = hpos;
        sc->capacity = cap;
        sc->gen = 0u;
    }
    if (++sc->gen == 0u) {
        memset(sc->visitGen, 0, sc->capacity * sizeof(uint32_t));
        sc->gen = 1u;
    }
    return sc;
}

static int computeGridDim(const AIWorld *w) {
    if (w && w->worldStatic && !w->worldStatic->isFlat && w->worldStatic->navBase.dim >= 48u) {
        return (int)w->worldStatic->navBase.dim;
    }
    float span = 2.0f * (w ? w->halfSize : 20.0f);
    int dim = (int)ceilf(span / 1.0f);
    if (dim < 48) dim = 48;
    if (dim > NAV_MAX_DIM) dim = NAV_MAX_DIM;
    return dim;
}

static inline Vector3 cellCenter(const AIWorld *w, int i, int dim, float half, float step) {
    float x = -half + ((float)(i % dim) + 0.5f) * step;
    float z = -half + ((float)(i / dim) + 0.5f) * step;
    float y = 0.0f;
    if (w && w->worldStatic && !w->worldStatic->isFlat) {
        const WorldNavBaseGrid *nb = &w->worldStatic->navBase;
        if (nb->cellHeight && (int)nb->dim == dim) {
            y = nb->cellHeight[i];
        } else {
            Terrain_SampleHeight(&w->worldStatic->terrain, x, z, &y);
        }
    }
    return Vec3_Create(x, y, z);
}

static inline int pointToCell(Vector3 p, int dim, float half, float invStep) {
    int x = (int)floorf((p.x + half) * invStep);
    int z = (int)floorf((p.z + half) * invStep);
    return (x < 0 || z < 0 || x >= dim || z >= dim) ? -1 : (z * dim + x);
}

static inline void heapSwap(NavThreadScratch *sc, int a, int b) {
    int na = sc->heap[a], nb = sc->heap[b];
    float fa = sc->heapF[a], fb = sc->heapF[b];
    sc->heap[a] = nb; sc->heap[b] = na;
    sc->heapF[a] = fb; sc->heapF[b] = fa;
    sc->heapPos[nb] = a;
    sc->heapPos[na] = b;
}

static inline bool heapLess(const NavThreadScratch *sc, int a, int b) {
    if (fabsf(sc->heapF[a] - sc->heapF[b]) > 1e-5f) return sc->heapF[a] < sc->heapF[b];
    return sc->heap[a] < sc->heap[b];
}

static void heapSiftUp(NavThreadScratch *sc, int idx) {
    while (idx > 0) {
        int p = (idx - 1) >> 1;
        if (!heapLess(sc, idx, p)) break;
        heapSwap(sc, idx, p);
        idx = p;
    }
}

static void heapSiftDown(NavThreadScratch *sc, int idx, int size) {
    for (;;) {
        int l = (idx << 1) + 1;
        int r = l + 1;
        int best = idx;
        if (l < size && heapLess(sc, l, best)) best = l;
        if (r < size && heapLess(sc, r, best)) best = r;
        if (best == idx) break;
        heapSwap(sc, idx, best);
        idx = best;
    }
}

static void heapPushOrDecrease(NavThreadScratch *sc, int *heapSize, int node, float f) {
    if (sc->visitGen[node] == sc->gen && sc->state[node] == 1) {
        int pos = sc->heapPos[node];
        sc->heapF[pos] = f;
        heapSiftUp(sc, pos);
        return;
    }
    int pos = (*heapSize)++;
    sc->heap[pos] = node;
    sc->heapF[pos] = f;
    sc->heapPos[node] = pos;
    sc->state[node] = 1;
    heapSiftUp(sc, pos);
}

static int heapPop(NavThreadScratch *sc, int *heapSize) {
    int top = sc->heap[0];
    int last = --(*heapSize);
    if (last > 0) {
        sc->heap[0] = sc->heap[last];
        sc->heapF[0] = sc->heapF[last];
        sc->heapPos[sc->heap[0]] = 0;
        heapSiftDown(sc, 0, last);
    }
    sc->state[top] = 2;
    return top;
}

bool CreatureNavigation_EnsureCache(AIWorld *w) {
    if (!w || !isfinite(w->halfSize) || w->halfSize <= 0.0f) return false;
    uint64_t worldFp = (w->worldStatic && !w->worldStatic->isFlat) ? w->worldStatic->dataFingerprint : 0u;
    NavStaticCache *cache = (NavStaticCache *)w->navCache;
    if (cache && cache->revision == w->revision && cache->worldFingerprint == worldFp &&
        fabsf(cache->halfSize - w->halfSize) < 1e-4f) {
        return true;
    }

    int dim = computeGridDim(w);
    unsigned cells = (unsigned)(dim * dim);
    if (!cache || cache->cells != cells) {
        free(w->navCache);
        w->navCache = NULL;
        size_t bytes = sizeof(NavStaticCache) + (size_t)cells * 2u * sizeof(float);
        cache = malloc(bytes);
        if (!cache) return false;
        w->navCache = cache;
    }

    float step = (2.0f * w->halfSize) / (float)dim;
    cache->revision = w->revision;
    cache->worldFingerprint = worldFp;
    cache->halfSize = w->halfSize;
    cache->cellSize = step;
    cache->dim = dim;
    cache->cells = cells;

    float *landClear = cache->data;
    float *swimClear = cache->data + cells;

    /* Recolectar únicamente bloqueadores estáticos de AIWorld (obstáculos cilíndricos y estanques). */
    typedef struct StaticDisc { float x, z, r; bool isWater; } StaticDisc;
    StaticDisc stackDiscs[256];
    StaticDisc *discs = stackDiscs;
    unsigned discCap = 256u, discCount = 0u;
    for (unsigned i = 0; i < w->capacity; ++i) {
        const AIWorldObject *o = AIWorld_At(w, i);
        if (!o || !o->active || o->walkable || o->type == AI_CREATURE) continue;
        if (discCount == discCap) {
            unsigned nextCap = discCap * 2u;
            StaticDisc *grown = malloc(nextCap * sizeof(*grown));
            if (!grown) break;
            memcpy(grown, discs, discCount * sizeof(*discs));
            if (discs != stackDiscs) free(discs);
            discs = grown;
            discCap = nextCap;
        }
        discs[discCount++] = (StaticDisc){o->position.x, o->position.z, o->radius, o->type == AI_WATER};
    }

    const WorldNavBaseGrid *nb = (w->worldStatic && !w->worldStatic->isFlat &&
                                  (int)w->worldStatic->navBase.dim == dim)
                                     ? &w->worldStatic->navBase : NULL;

    for (unsigned idx = 0; idx < cells; ++idx) {
        float cx = -w->halfSize + ((float)(idx % (unsigned)dim) + 0.5f) * step;
        float cz = -w->halfSize + ((float)(idx / (unsigned)dim) + 0.5f) * step;
        float wallClear = nb ? nb->cellRockClearance[idx]
                             : fminf(w->halfSize - fabsf(cx), w->halfSize - fabsf(cz));
        float landMin = wallClear;
        float swimMin = wallClear;
        for (unsigned d = 0; d < discCount; ++d) {
            float dx = cx - discs[d].x;
            float dz = cz - discs[d].z;
            float dist = sqrtf(dx * dx + dz * dz) - discs[d].r;
            if (dist < landMin) landMin = dist;
            if (!discs[d].isWater && dist < swimMin) swimMin = dist;
        }
        landClear[idx] = landMin;
        swimClear[idx] = swimMin;
    }
    if (discs != stackDiscs) free(discs);
    return true;
}

static inline bool cellPassable(const AIWorld *w, const NavStaticCache *cache,
                                const WorldNavBaseGrid *nb, int idx, int dim,
                                float half, float step, const TraversalProfile *prof) {
    float r = fmaxf(0.05f, prof->footprintRadius);
    if (nb) {
        if (nb->cellMaxSlope[idx] > prof->maxSlopeRadians + 0.04f) return false;
        float maxDepth = prof->canWade ? fmaxf(0.0f, prof->maxWadingDepth) : 0.05f;
        if (nb->cellMaxWaterDepth[idx] > maxDepth + 0.02f) return false;
        if (nb->cellRockClearance[idx] < r + 1e-4f) return false;
    }
    if (cache) {
        const float *arr = prof->canSwim ? (cache->data + cache->cells) : cache->data;
        return arr[idx] >= r + 1e-4f;
    }
    return AIWorld_CanTraverse(w, cellCenter(w, idx, dim, half, step), prof);
}

static inline float edgeTerrainCost(const WorldNavBaseGrid *nb, int a, int b, float stepDist,
                                    const TraversalProfile *prof) {
    if (!nb) return stepDist;
    float dh = nb->cellHeight[b] - nb->cellHeight[a];
    float uphill = dh > 0.0f ? (dh / fmaxf(0.05f, stepDist)) : 0.0f;
    float slope = nb->cellMaxSlope[b];
    float rough = nb->cellRoughness[b];
    float depth = nb->cellMaxWaterDepth[b];
    float maxWade = fmaxf(0.10f, prof->maxWadingDepth);

    float slopePen = uphill * 1.40f + slope * 0.35f;
    float roughPen = rough * 0.30f;
    float wadePen = (depth > 0.04f) ? (fminf(1.0f, depth / maxWade) * 1.15f) : 0.0f;
    return stepDist * (1.0f + slopePen + roughPen + wadePen);
}

bool CreatureNavigation_PlanWithProfile(CreatureNavigation *n, const AIWorld *w,
                                        Vector3 from, Vector3 goal,
                                        const TraversalProfile *profile) {
    if (!n || !w || !profile) return false;
    n->count = n->index = 0;
    n->progress = (NavigationProgress){0};
    n->goal = goal;
    n->clearance = profile->footprintRadius;
    n->swimming = profile->canSwim;
    n->revision = w->revision;
    n->failed = false;
    n->lastExpandedNodes = 0u;

    /* Proyectar inicio/meta a suelo si hay terreno procedural. */
    bool hasTerrain = (w->worldStatic && !w->worldStatic->isFlat);
    if (hasTerrain) {
        float fy = 0.0f, gy = 0.0f;
        if (Terrain_SampleHeight(&w->worldStatic->terrain, from.x, from.z, &fy)) from.y = fy;
        if (Terrain_SampleHeight(&w->worldStatic->terrain, goal.x, goal.z, &gy)) {
            goal.y = gy;
            n->goal = goal;
        }
    }

    /* 1) Camino rápido en línea recta validado exactamente contra terreno, agua y obstáculos. */
    if (AIWorld_SegmentTraversable(w, from, goal, profile, NULL)) {
        n->points[n->count++] = goal;
        return true;
    }

    const NavStaticCache *cache = (const NavStaticCache *)w->navCache;
    uint64_t worldFp = hasTerrain ? w->worldStatic->dataFingerprint : 0u;
    if (cache && (cache->revision != w->revision || cache->worldFingerprint != worldFp ||
                  fabsf(cache->halfSize - w->halfSize) > 1e-4f)) {
        cache = NULL;
    }
    int dim = cache ? cache->dim : computeGridDim(w);
    const WorldNavBaseGrid *nb = (hasTerrain && (int)w->worldStatic->navBase.dim == dim)
                                     ? &w->worldStatic->navBase : NULL;
    float step = cache ? cache->cellSize : (2.0f * w->halfSize) / (float)dim;
    float invStep = 1.0f / step;
    unsigned cells = (unsigned)(dim * dim);

    int start = pointToCell(from, dim, w->halfSize, invStep);
    int end = pointToCell(goal, dim, w->halfSize, invStep);
    if (start < 0 || end < 0 || !AIWorld_CanTraverse(w, goal, profile)) goto failed;

    int snapRad = hasTerrain ? 2 : 1;
    if (!cellPassable(w, cache, nb, start, dim, w->halfSize, step, profile) ||
        !AIWorld_SegmentTraversable(w, from, cellCenter(w, start, dim, w->halfSize, step), profile, NULL)) {
        int sx0 = start % dim, sz0 = start / dim, bestS = -1;
        float bestD2 = FLT_MAX;
        for (int dz = -snapRad; dz <= snapRad; ++dz) {
            for (int dx = -snapRad; dx <= snapRad; ++dx) {
                int nx = sx0 + dx, nz = sz0 + dz;
                if (nx < 0 || nz < 0 || nx >= dim || nz >= dim) continue;
                int ni = nz * dim + nx;
                if (!cellPassable(w, cache, nb, ni, dim, w->halfSize, step, profile)) continue;
                Vector3 c = cellCenter(w, ni, dim, w->halfSize, step);
                if (!AIWorld_SegmentTraversable(w, from, c, profile, NULL)) continue;
                float d2 = Vec3_LengthSq(Vec3_Sub(c, from));
                if (d2 < bestD2) { bestD2 = d2; bestS = ni; }
            }
        }
        if (bestS < 0) goto failed;
        start = bestS;
    }
    if (!cellPassable(w, cache, nb, end, dim, w->halfSize, step, profile) ||
        !AIWorld_SegmentTraversable(w, cellCenter(w, end, dim, w->halfSize, step), goal, profile, NULL)) {
        int ex0 = end % dim, ez0 = end / dim, bestE = -1;
        float bestD2 = FLT_MAX;
        for (int dz = -snapRad; dz <= snapRad; ++dz) {
            for (int dx = -snapRad; dx <= snapRad; ++dx) {
                int nx = ex0 + dx, nz = ez0 + dz;
                if (nx < 0 || nz < 0 || nx >= dim || nz >= dim) continue;
                int ni = nz * dim + nx;
                if (!cellPassable(w, cache, nb, ni, dim, w->halfSize, step, profile)) continue;
                Vector3 c = cellCenter(w, ni, dim, w->halfSize, step);
                if (!AIWorld_SegmentTraversable(w, c, goal, profile, NULL)) continue;
                float d2 = Vec3_LengthSq(Vec3_Sub(c, goal));
                if (d2 < bestD2) { bestD2 = d2; bestE = ni; }
            }
        }
        if (bestE < 0) goto failed;
        end = bestE;
    }

    NavThreadScratch *sc = acquireScratch(cells);
    if (!sc) goto failed;

    int sx = start % dim, sz = start / dim;
    int ex = end % dim, ez = end / dim;
    int pad = hasTerrain ? 44 : 28;
    int minX = (sx < ex ? sx : ex) - pad; if (minX < 0) minX = 0;
    int maxX = (sx > ex ? sx : ex) + pad; if (maxX >= dim) maxX = dim - 1;
    int minZ = (sz < ez ? sz : ez) - pad; if (minZ < 0) minZ = 0;
    int maxZ = (sz > ez ? sz : ez) + pad; if (maxZ >= dim) maxZ = dim - 1;

    int maxExp = hasTerrain ? NAV_MAX_EXPANSIONS_TERRAIN : NAV_MAX_EXPANSIONS_FLAT;
    int heapSize = 0;
    sc->visitGen[start] = sc->gen;
    sc->g[start] = 0.0f;
    sc->parent[start] = -1;
    sc->state[start] = 0;
    float h0 = hasTerrain ? (hypotf((float)(sx - ex), (float)(sz - ez)) * step)
                          : ((float)(abs(sx - ex) + abs(sz - ez)) * step);
    heapPushOrDecrease(sc, &heapSize, start, h0);

    static const int ndx[8] = {-1, 1, 0, 0, -1, 1, -1, 1};
    static const int ndz[8] = {0, 0, -1, 1, -1, -1, 1, 1};
    const float ndist[8] = {
        step, step, step, step,
        step * 1.41421356f, step * 1.41421356f, step * 1.41421356f, step * 1.41421356f
    };

    int expanded = 0;
    int dirCount = hasTerrain ? 8 : 4;
    while (heapSize > 0 && expanded < maxExp) {
        int best = heapPop(sc, &heapSize);
        ++expanded;
        if (best == end) {
            n->lastExpandedNodes = (uint64_t)expanded;
            /* Reconstruir cadena de celdas completa (puede superar CREATURE_PATH_CAPACITY en mapas grandes;
             * la simplificación por cuerda comprime a los waypoints esenciales). */
            int stackRev[1024];
            int *reverse = stackRev;
            int revCap = 1024;
            int count = 0;
            for (int i = end; i != start; i = sc->parent[i]) {
                if (i < 0) {
                    if (reverse != stackRev) free(reverse);
                    goto failed;
                }
                if (count >= revCap) {
                    int nextCap = revCap * 2;
                    int *grown = malloc((size_t)nextCap * sizeof(int));
                    if (!grown) {
                        if (reverse != stackRev) free(reverse);
                        goto failed;
                    }
                    memcpy(grown, reverse, (size_t)count * sizeof(int));
                    if (reverse != stackRev) free(reverse);
                    reverse = grown;
                    revCap = nextCap;
                }
                reverse[count++] = i;
            }

            Vector3 previous = from;
            for (int j = count - 1; j >= 0;) {
                int far = hasTerrain ? j : 0;
                /* Búsqueda de cuerda: elegir el punto más lejano alcanzable directamente. */
                int limit = (hasTerrain && j > 48) ? (j - 48) : 0;
                for (int k = limit; k <= j; ++k) {
                    Vector3 ck = cellCenter(w, reverse[k], dim, w->halfSize, step);
                    if (AIWorld_SegmentTraversable(w, previous, ck, profile, NULL)) {
                        far = k;
                        break;
                    }
                }
                previous = cellCenter(w, reverse[far], dim, w->halfSize, step);
                if (n->count < CREATURE_PATH_CAPACITY - 1u) {
                    n->points[n->count++] = previous;
                } else {
                    /* No declarar éxito silencioso con ruta truncada. */
                    if (reverse != stackRev) free(reverse);
                    goto failed;
                }
                j = far - 1;
            }
            if (reverse != stackRev) free(reverse);
            n->points[n->count++] = goal;
            return true;
        }

        int x = best % dim, z = best / dim;
        Vector3 cBest = cellCenter(w, best, dim, w->halfSize, step);

        for (int d = 0; d < dirCount; ++d) {
            int nx = x + ndx[d], nz = z + ndz[d];
            if (nx < minX || nx > maxX || nz < minZ || nz > maxZ) continue;
            int k = nz * dim + nx;
            bool visited = (sc->visitGen[k] == sc->gen);
            if (visited && sc->state[k] == 2) continue;
            if (!cellPassable(w, cache, nb, k, dim, w->halfSize, step, profile)) continue;

            /* Impedir corte diagonal de esquinas bloqueadas. */
            if (d >= 4) {
                int o1 = z * dim + nx;
                int o2 = nz * dim + x;
                if (!cellPassable(w, cache, nb, o1, dim, w->halfSize, step, profile) ||
                    !cellPassable(w, cache, nb, o2, dim, w->halfSize, step, profile)) {
                    continue;
                }
            }

            float stepCost = edgeTerrainCost(nb, best, k, ndist[d], profile);
            float nextG = sc->g[best] + stepCost;
            if (!visited || nextG < sc->g[k]) {
                /* Verificación exacta de arista sólo cuando hay transición empinada/escalón o sin caché. */
                if (nb) {
                    float dh = fabsf(nb->cellHeight[k] - nb->cellHeight[best]);
                    if (dh > profile->maxStepHeight && (dh / ndist[d]) > tanf(profile->maxSlopeRadians)) {
                        continue;
                    }
                } else {
                    Vector3 cK = cellCenter(w, k, dim, w->halfSize, step);
                    if (!AIWorld_SegmentTraversable(w, cBest, cK, profile, NULL)) continue;
                }

                if (!visited) {
                    sc->visitGen[k] = sc->gen;
                    sc->state[k] = 0;
                }
                sc->g[k] = nextG;
                sc->parent[k] = best;
                float h = hasTerrain ? (hypotf((float)(nx - ex), (float)(nz - ez)) * step)
                                     : ((float)(abs(nx - ex) + abs(nz - ez)) * step);
                heapPushOrDecrease(sc, &heapSize, k, nextG + h);
            }
        }
    }
    n->lastExpandedNodes = (uint64_t)expanded;
failed:
    n->failed = true;
    ++n->failures;
    return false;
}

bool CreatureNavigation_Plan(CreatureNavigation *n, const AIWorld *w, Vector3 from, Vector3 goal,
                             float r, bool swim) {
    TraversalProfile prof = TraversalProfile_DefaultLand(r);
    prof.canSwim = swim;
    return CreatureNavigation_PlanWithProfile(n, w, from, goal, &prof);
}

NavigationGoalStatus CreatureNavigation_Update(CreatureNavigation *n, Vector3 p, float yaw,
                                               float speed, float turn, float dt, Vector3 *out) {
    if (n->failed || !n->count)
        return NAVIGATION_STALLED;
    while (n->index + 1 < n->count) {
        Vector3 d = Vec3_Sub(n->points[n->index], p);
        d.y = 0.0f;
        if (Vec3_Length(d) >= 0.7f) break;
        ++n->index;
        n->progress = (NavigationProgress){0};
    }
    Vector3 target = n->points[n->index];
    if (out) *out = target;
    Vector3 pFlat = Vec3_Create(p.x, 0.0f, p.z);
    Vector3 tFlat = Vec3_Create(target.x, 0.0f, target.z);
    NavigationGoalStatus s =
        NavigationProgress_Update(&n->progress, pFlat, tFlat, yaw, speed, turn, 0.12f, dt);
    if (s == NAVIGATION_ARRIVED && n->index + 1 < n->count) {
        ++n->index;
        n->progress = (NavigationProgress){0};
        if (out) *out = n->points[n->index];
        return NAVIGATION_ADVANCING;
    }
    return s;
}
