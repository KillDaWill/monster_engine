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

bool WorldHydrology_Generate(WorldHydrology *hydro, TerrainHeightfield *tf,
                             const WorldRecipe *recipe, uint32_t hydroSeed) {
    (void)hydroSeed;
    if (!hydro || !tf || !tf->heights || !recipe) return false;
    memset(hydro, 0, sizeof(*hydro));

    uint32_t res = tf->resolution;
    if (!allocHydroBuffers(hydro, res)) return false;

    if (recipe->flat || !recipe->hydrology.enabled ||
        (recipe->hydrology.riverDensity <= 1e-4f && recipe->hydrology.lakeDensity <= 1e-4f)) {
        hydro->enabled = false;
        hydro->drainageAcyclic = true;
        return true;
    }

    hydro->enabled = true;
    size_t count = (size_t)res * (size_t)res;
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
    if (!runPriorityFlood(tf->heights, res, filled, parent, drainOrder)) {
        free(filled); free(parent); free(drainOrder); free(bfsQ); free(compVis);
        WorldHydrology_Free(hydro);
        return false;
    }
    hydro->drainageAcyclic = verifyAcyclicRouting(parent, (uint32_t)count);

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
        uint32_t minLakeCells = (uint32_t)fmaxf(12.0f, minLakeArea / fmaxf(0.01f, cellArea));
        float minLakePeakDepth = fmaxf(0.28f, 0.70f - 0.40f * lakeDensity);
        float maxWetFractionForLakes = 0.24f + 0.14f * lakeDensity;
        uint32_t maxLakeCellsTotal = (uint32_t)((float)count * maxWetFractionForLakes);
        uint32_t usedLakeCells = 0u;

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
                    if (filled[ni] - tf->heights[ni] > 0.06f && fabsf(filled[ni] - spillElev) < 0.15f) {
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
                    if (depth > 0.01f) {
                        hydro->waterBodyId[c] = bodyId;
                        hydro->waterSurfaceY[c] = spillElev;
                        hydro->waterDepth[c] = depth;
                    }
                }
            } else {
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

    /* 4) Extracción y excavación de red fluvial dirigida desde acumulación de drenaje. */
    float riverDensity = recipe->hydrology.riverDensity;
    if (riverDensity > 1e-4f && hydro->bodyCount < WORLD_MAX_WATER_BODIES) {
        float totalArea = tf->span * tf->span;
        /* Umbral de cuenca para iniciar un cauce permanente. */
        float accumThresh = totalArea * (0.048f - 0.036f * riverDensity);
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

        free(isChannel);
        free(riverSurface);
        free(carveDelta);
    }

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
                hydro->waterSurfaceY[idx] = 0.0f;
                hydro->waterDepth[idx] = 0.0f;
                hydro->flowX[idx] = 0.0f;
                hydro->flowZ[idx] = 0.0f;
                continue;
            }
            float depth = hydro->waterSurfaceY[idx] - tf->heights[idx];
            if (depth <= 0.01f) {
                hydro->waterBodyId[idx] = 0u;
                hydro->waterSurfaceY[idx] = 0.0f;
                hydro->waterDepth[idx] = 0.0f;
                hydro->flowX[idx] = 0.0f;
                hydro->flowZ[idx] = 0.0f;
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

bool WorldHydrology_RebuildShoreSites(WorldHydrology *hydro, const TerrainHeightfield *tf,
                                      const WorldRecipe *recipe,
                                      bool (*extraBlockedFn)(const void *ctx, Vector3 pos, float radius),
                                      const void *extraBlockedCtx) {
    (void)recipe;
    if (!hydro || !tf || !hydro->shoreSites) return false;
    hydro->shoreSiteCount = 0u;
    if (!hydro->enabled || hydro->bodyCount == 0u || hydro->totalWetCells == 0u) return true;

    uint32_t res = tf->resolution;
    /* Espaciado adaptativo para cubrir ambas orillas de ríos y perímetros de lagos
     * sin saturar la memoria perceptiva de 32 entradas. */
    float minSpacing = fmaxf(4.5f, fminf(11.0f, tf->span * 0.032f));
    float minSpacingSq = minSpacing * minSpacing;
    float maxSlopeRad = 0.60f; /* ~34 grados máx en punto de apoyo de bebida */
    float approachOffset = 1.15f; /* Distancia desde el borde húmedo hacia tierra firme */

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

            /* Espaciado mínimo contra estaciones ya aceptadas del mismo entorno. */
            bool tooClose = false;
            for (uint32_t s = 0; s < hydro->shoreSiteCount; ++s) {
                float dx = hydro->shoreSites[s].wetContactPoint.x - wetContact.x;
                float dz = hydro->shoreSites[s].wetContactPoint.z - wetContact.z;
                if (dx * dx + dz * dz < minSpacingSq) {
                    tooClose = true;
                    break;
                }
            }
            if (tooClose) continue;

            /* Validar punto de aproximación en tierra/vado bajo en dirección bankNormal. */
            float ax = wx + outX * approachOffset;
            float az = wz + outZ * approachOffset;
            float margin = 1.6f;
            if (ax < tf->originX + margin || ax > tf->originX + tf->span - margin ||
                az < tf->originZ + margin || az > tf->originZ + tf->span - margin) {
                continue;
            }
            SurfaceHit bankHit;
            if (!Terrain_SampleGround(tf, ax, az, &bankHit)) continue;
            float bankSlope = acosf(fmaxf(0.0f, fminf(1.0f, bankHit.normal.y)));
            if (bankSlope > maxSlopeRad) continue;

            WaterSample bankWater;
            if (WorldHydrology_SampleWater(hydro, tf, ax, az, &bankWater) && bankWater.wet &&
                bankWater.depth > 0.30f) {
                continue;
            }
            /* Comprobar que el agua esté dentro de alcance vertical plausible de la boca. */
            float verticalDelta = bankHit.position.y - surfY;
            if (verticalDelta < -0.35f || verticalDelta > 1.15f) continue;

            if (extraBlockedFn && extraBlockedFn(extraBlockedCtx, bankHit.position, 1.05f)) {
                continue;
            }

            /* Comprobar visibilidad directa desde boca típica en bankApproachPoint hasta wetContact. */
            Vector3 mouthApprox = Vec3_Add(bankHit.position, Vec3_Create(0.0f, 0.55f, 0.0f));
            if (Terrain_RaycastSegment(tf, mouthApprox, wetContact, 0.10f, NULL, NULL)) {
                continue;
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
