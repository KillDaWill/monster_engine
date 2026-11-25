/**
 * @file demo_brain_evolution_lab.c
 * @brief Aplicación gráfica del Brain Evolution Lab: entrenamiento evolutivo en segundo plano,
 *        gráficas de fitness/supervivencia/diversidad, inspector de candidatos, checkpoints
 *        verificados por huella, exportación de cerebros y viewport 3D independiente de replay.
 */
#include "BrainEvolutionLab.h"
#include "EvolutionCheckpointIO.h"
#include "EvolutionEnvironment.h"
#include "EcosystemVisual.h"
#include "EnvironmentRenderer.h"
#include "CreatureRecipeRegistry.h"
#include "NeuralBrainIO.h"
#include "OpenGLRenderer.h"
#include "PrimitiveMesh.h"
#include <math.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

#define LAB_WIN_DEFAULT_W 1440
#define LAB_WIN_DEFAULT_H 860
#define LAB_MAX_FPS 60.0

typedef enum LabCandidateSlot {
    CAND_CHAMPION = 0,
    CAND_BEST_TRAIN = 1,
    CAND_BEST_VAL = 2,
    CAND_ELITE_2 = 3,
    CAND_RANDOM = 4,
    CAND_SLOT_COUNT = 5
} LabCandidateSlot;

typedef struct LabUiButton {
    float x, y, w, h;
    const char *label;
} LabUiButton;

typedef struct LabCandidateSummary {
    bool valid;
    char title[32];
    uint64_t fingerprint;
    uint32_t generation;
    EvolutionAggregateMetrics metrics;
    NeuralGenome genome;
} LabCandidateSummary;

typedef struct LabUiSnapshot {
    BrainLabStatus status;
    uint32_t currentGeneration;
    unsigned currentStage;
    unsigned stageCount;
    char currentEnvName[64];
    char speciesName[64];
    char profileName[64];
    uint32_t recipeId;
    unsigned populationSize;
    unsigned eliteCount;
    unsigned workerCount;
    uint64_t totalEpisodesEvaluated;
    uint64_t totalTicksSimulated;
    double elapsedTrainingSeconds;
    float neuralDiversity;
    float bestTrainFitness;
    float meanTrainFitness;
    float medianTrainFitness;
    float bestTrainSurvival;
    bool hasValidation;
    float bestValFitness;
    float bestValSurvival;
    float bestValHealth;
    float bestValEnergy;
    float bestValHydration;
    double lastEpsPerSec;
    double lastTicksPerSec;
    uint64_t lastTotalKills;
    uint64_t lastTotalCarcasses;
    unsigned historyCount;
    BrainLabGenerationRecord history[BRAIN_LAB_MAX_HISTORY];
    LabCandidateSummary candidates[CAND_SLOT_COUNT];
    bool trainingActive;
    char statusMessage[220];
    bool statusError;
    uint64_t revision;
} LabUiSnapshot;

typedef struct LabAsyncController {
    pthread_mutex_t mutex;
    pthread_t thread;
    bool threadStarted;
    volatile bool quitRequested;
    volatile bool stopFlag;

    bool trainingActive;
    bool stepOneRequested;
    bool validateRequested;
    bool saveCkptRequested;
    bool loadCkptRequested;
    bool exportRequested;
    char ckptPath[256];
    char exportPath[256];

    BrainEvolutionLab lab;
    LabUiSnapshot ui;
} LabAsyncController;

typedef struct ReplayViewer {
    bool initialized;
    bool paused;
    EcosystemWorld world;
    EcosystemVisual visual;
    AIObjectId focalId;
    uint32_t episodeSeed;
    bool isRecordedEvalSeed;
    double achievedSpeed;
    uint64_t loadedFingerprint;
    LabCandidateSlot loadedSlot;
    char loadedLabel[48];
    double simAccumulator;
} ReplayViewer;

static void ensureDirPath(const char *path) {
    if (!path || !path[0]) return;
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", path);
    char *slash = strrchr(tmp, '/');
    if (!slash || slash == tmp) return;
    *slash = '\0';
    for (char *p = tmp + 1; *p; ++p) {
        if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
    }
    mkdir(tmp, 0755);
}

static bool hitBtn(int mx, int my, const LabUiButton *b) {
    return (float)mx >= b->x && (float)mx <= b->x + b->w &&
           (float)my >= b->y && (float)my <= b->y + b->h;
}

static void drawBtn(const LabUiButton *b, bool active, bool accent) {
    Color bg = active ? Color_Create(26, 98, 114, 240)
                      : (accent ? Color_Create(20, 56, 70, 232) : Color_Create(16, 28, 38, 232));
    Color bd = active ? Color_Create(92, 236, 250, 255) : Color_Create(52, 138, 158, 205);
    Color tx = active ? Color_Create(244, 254, 255, 255) : Color_Create(206, 232, 240, 255);
    OpenGLRenderer_DrawRect2D(b->x, b->y, b->w, b->h, bg, bd);
    OpenGLRenderer_DrawText2D(b->x + 6.0f, b->y + 6.0f, 1.12f, tx, b->label);
}

static void drawBar2D(float x, float y, float w, float h, const char *label,
                      float value, Color fillCol) {
    float v = fmaxf(0.0f, fminf(1.0f, value));
    OpenGLRenderer_DrawText2D(x, y + 1.0f, 1.08f, Color_Create(166, 198, 210, 255), label);
    float bx = x + 56.0f, bw = w - 96.0f;
    OpenGLRenderer_DrawRect2D(bx, y + 1.0f, bw, h - 2.0f,
                              Color_Create(14, 24, 34, 240), Color_Create(42, 82, 98, 180));
    if (v > 0.005f) {
        OpenGLRenderer_DrawRect2D(bx + 1.0f, y + 2.0f, (bw - 2.0f) * v, h - 4.0f,
                                  fillCol, Color_Create(0, 0, 0, 0));
    }
    char num[16];
    snprintf(num, sizeof(num), "%3.0f%%", v * 100.0f);
    OpenGLRenderer_DrawText2D(bx + bw + 5.0f, y + 1.0f, 1.08f, Color_Create(224, 238, 244, 255), num);
}

static Color colorFromFloat3(const float rgb[3]) {
    return Color_Create((unsigned char)fmaxf(0.0f, fminf(255.0f, rgb[0] * 255.0f)),
                        (unsigned char)fmaxf(0.0f, fminf(255.0f, rgb[1] * 255.0f)),
                        (unsigned char)fmaxf(0.0f, fminf(255.0f, rgb[2] * 255.0f)), 255);
}

static Color labActivationFill(float val) {
    float mag = fminf(1.0f, fabsf(val));
    if (val >= 0.0f) {
        return Color_Create((unsigned char)(18 + 44 * mag),
                              (unsigned char)(42 + 178 * mag),
                              (unsigned char)(54 + 186 * mag), 245);
    }
    return Color_Create((unsigned char)(56 + 184 * mag),
                          (unsigned char)(28 + 64 * mag),
                          (unsigned char)(32 + 54 * mag), 245);
}

static Color labWeightColor(float w, float alphaScale, bool recurrent) {
    float mag = fminf(1.0f, fabsf(w) / 2.2f);
    unsigned char a = (unsigned char)fmaxf(26.0f, fminf(235.0f, (36.0f + 195.0f * mag) * alphaScale));
    if (recurrent) {
        return w >= 0.0f ? Color_Create(246, 202, 76, a) : Color_Create(228, 126, 54, a);
    }
    return w >= 0.0f ? Color_Create(58, 214, 228, a) : Color_Create(232, 92, 86, a);
}

static void drawLabNeuralGraph(int winW, int winH, ReplayViewer *rv,
                               float edgeThreshold,
                               LabUiButton *btnWMinus, LabUiButton *btnWPlus) {
    const float px = 374.0f, py = 54.0f;
    const float pw = fmaxf(520.0f, (float)winW - 374.0f - 438.0f);
    const float ph = fminf(680.0f, (float)winH - 98.0f);

    OpenGLRenderer_DrawRect2D(px, py, pw, ph, Color_Create(7, 13, 20, 228), Color_Create(64, 194, 216, 235));
    OpenGLRenderer_DrawRect2D(px, py, pw, 26.0f, Color_Create(13, 29, 42, 245), Color_Create(64, 194, 216, 235));

    if (!rv || !rv->initialized) {
        OpenGLRenderer_DrawText2D(px + 10.0f, py + 7.0f, 1.22f, Color_Create(84, 226, 240, 255),
                                  "NEURAL NETWORK V2 GRAPH [N]  (NO REPLAY ACTIVE)");
        return;
    }

    const EcosystemEntity *fe = EcosystemWorld_Get(&rv->world, rv->focalId);
    if (!fe || !fe->mind.neuralBrain.genome.valid ||
        fe->mind.neuralBrain.genome.architecture != NEURAL_ARCH_CANDIDATE_POLICY) {
        OpenGLRenderer_DrawText2D(px + 10.0f, py + 7.0f, 1.22f, Color_Create(248, 180, 120, 255),
                                  "NEURAL NETWORK V2 GRAPH [N]  (FOCAL CREATURE DEAD OR NO V2 BRAIN)");
        return;
    }

    const NeuralGenome *g = &fe->mind.neuralBrain.genome;
    const NeuralBrainV2Runtime *rt = &fe->mind.neuralBrain.runtime.v2;
    const NeuralV2InspectorState *v2 = &fe->mind.neuralState.v2;

    char hdr[160];
    snprintf(hdr, sizeof(hdr), "NEURAL V2 GRAPH [N]  FP:%08llX  STEPS:%llu  1416P",
             (unsigned long long)(g->metadata.fingerprint & 0xFFFFFFFFu),
             (unsigned long long)fe->mind.neuralBrain.runtime.stepCount);
    OpenGLRenderer_DrawText2D(px + 10.0f, py + 7.0f, 1.18f, Color_Create(84, 232, 246, 255), hdr);

    *btnWMinus = (LabUiButton){px + pw - 144.0f, py + 3.0f, 32.0f, 20.0f, "W-"};
    *btnWPlus  = (LabUiButton){px + pw - 108.0f, py + 3.0f, 32.0f, 20.0f, "W+"};
    drawBtn(btnWMinus, false, false);
    drawBtn(btnWPlus, false, false);
    char thBuf[24];
    snprintf(thBuf, sizeof(thBuf), "T:%.2f", edgeThreshold);
    OpenGLRenderer_DrawText2D(px + pw - 70.0f, py + 7.0f, 1.05f, Color_Create(194, 228, 238, 255), thBuf);

    /* Posiciones de capas del grafo principal: 24 Sensores -> 16 Ocultas Recurrentes -> Salidas */
    const float graphTop = py + 42.0f;
    const float graphBot = py + ph - 208.0f;
    const float xIn = px + 80.0f;
    const float xHid = px + pw * 0.44f;
    const float xOut = px + pw * 0.67f;

    float inY[NEURAL_V2_CONTEXT_INPUTS];
    float hidY[NEURAL_V2_CONTEXT_HIDDEN];
    for (unsigned i = 0; i < NEURAL_V2_CONTEXT_INPUTS; ++i) {
        inY[i] = graphTop + (graphBot - graphTop) * (float)i / (float)(NEURAL_V2_CONTEXT_INPUTS - 1u);
    }
    for (unsigned h = 0; h < NEURAL_V2_CONTEXT_HIDDEN; ++h) {
        hidY[h] = graphTop + 12.0f + (graphBot - graphTop - 24.0f) * (float)h / (float)(NEURAL_V2_CONTEXT_HIDDEN - 1u);
    }
    float globY[NEURAL_V2_GLOBAL_ACTIONS];
    for (unsigned o = 0; o < NEURAL_V2_GLOBAL_ACTIONS; ++o) {
        globY[o] = graphTop + 18.0f + (float)o * 36.0f;
    }

    /* 1) Conexiones reales ctx_w_xh (24 -> 16) filtradas por |w| >= edgeThreshold */
    for (unsigned h = 0; h < NEURAL_V2_CONTEXT_HIDDEN; ++h) {
        for (unsigned i = 0; i < NEURAL_V2_CONTEXT_INPUTS; ++i) {
            float w = g->v2.ctx_w_xh[h * NEURAL_V2_CONTEXT_INPUTS + i];
            if (fabsf(w) < edgeThreshold) continue;
            float lw = 1.0f + fminf(2.2f, fabsf(w) * 0.8f);
            OpenGLRenderer_DrawLine2D(xIn + 5.0f, inY[i], xHid - 9.0f, hidY[h],
                                      lw, labWeightColor(w, 0.58f, false));
        }
    }

    /* 2) Conexiones recurrentes ctx_w_hh (16 -> 16) en ámbar */
    for (unsigned h = 0; h < NEURAL_V2_CONTEXT_HIDDEN; ++h) {
        for (unsigned k = 0; k < NEURAL_V2_CONTEXT_HIDDEN; ++k) {
            float w = g->v2.ctx_w_hh[h * NEURAL_V2_CONTEXT_HIDDEN + k];
            if (fabsf(w) < edgeThreshold) continue;
            Color col = labWeightColor(w, 0.78f, true);
            float lw = 1.0f + fminf(2.2f, fabsf(w) * 0.75f);
            if (h == k) {
                OpenGLRenderer_DrawCircle2D(xHid + 12.0f, hidY[h], 6.0f, Color_Create(0, 0, 0, 0), col);
            } else {
                float arcX = xHid + 22.0f + fabsf((float)h - (float)k) * 2.2f;
                float midY = 0.5f * (hidY[h] + hidY[k]);
                OpenGLRenderer_DrawLine2D(xHid + 8.0f, hidY[k], arcX, midY, lw, col);
                OpenGLRenderer_DrawLine2D(arcX, midY, xHid + 8.0f, hidY[h], lw, col);
            }
        }
    }

    /* 3) Conexiones glob_w (16 -> 3 acciones globales) */
    for (unsigned o = 0; o < NEURAL_V2_GLOBAL_ACTIONS; ++o) {
        for (unsigned h = 0; h < NEURAL_V2_CONTEXT_HIDDEN; ++h) {
            float w = g->v2.global_w[o * NEURAL_V2_CONTEXT_HIDDEN + h];
            if (fabsf(w) < edgeThreshold) continue;
            float lw = 1.0f + fminf(2.4f, fabsf(w) * 0.85f);
            OpenGLRenderer_DrawLine2D(xHid + 9.0f, hidY[h], xOut - 8.0f, globY[o],
                                      lw, labWeightColor(w, 0.75f, false));
        }
    }

    /* 4) Nodos de entrada (24 sensores de contexto v2) */
    for (unsigned i = 0; i < NEURAL_V2_CONTEXT_INPUTS; ++i) {
        float v = rt->contextInputs[i];
        Color bd = v >= 0.0f ? Color_Create(78, 212, 226, 225) : Color_Create(232, 102, 92, 225);
        OpenGLRenderer_DrawCircle2D(xIn, inY[i], 4.2f, labActivationFill(v), bd);
        char lbl[16];
        snprintf(lbl, sizeof(lbl), "%-9.9s", NeuralV2ContextSensor_Name(i));
        OpenGLRenderer_DrawText2D(px + 8.0f, inY[i] - 3.5f, 0.92f, Color_Create(174, 206, 218, 240), lbl);
    }

    /* 5) Nodos ocultos recurrentes (16 neuronas C0..C15) */
    for (unsigned h = 0; h < NEURAL_V2_CONTEXT_HIDDEN; ++h) {
        float v = rt->context[h];
        Color bd = v >= 0.0f ? Color_Create(84, 232, 246, 245) : Color_Create(242, 108, 96, 245);
        OpenGLRenderer_DrawCircle2D(xHid, hidY[h], 8.5f, labActivationFill(v), bd);
        char hbuf[10];
        snprintf(hbuf, sizeof(hbuf), "C%u", h);
        OpenGLRenderer_DrawText2D(xHid - 7.5f, hidY[h] - 3.5f, 0.88f, Color_Create(242, 252, 255, 255), hbuf);
    }

    /* 6) Salidas globales (IDLE / EXPLORE / REST) */
    static const char *gNames[NEURAL_V2_GLOBAL_ACTIONS] = {"IDLE", "EXPLORE", "REST"};
    for (unsigned o = 0; o < NEURAL_V2_GLOBAL_ACTIONS; ++o) {
        float sc = v2->globalScores[o];
        bool win = (v2->selectedAction == (NeuralV2ActionType)o);
        float norm = 0.5f + 0.5f * tanhf(sc * 0.5f);
        OpenGLRenderer_DrawCircle2D(xOut, globY[o], win ? 8.5f : 6.5f, labActivationFill(tanhf(sc * 0.5f)),
                                    win ? Color_Create(88, 244, 255, 255) : Color_Create(62, 132, 150, 220));
        float bx0 = xOut + 12.0f, bw = px + pw - bx0 - 10.0f;
        OpenGLRenderer_DrawRect2D(bx0, globY[o] - 9.0f, bw, 18.0f,
                                  win ? Color_Create(22, 70, 86, 235) : Color_Create(13, 22, 30, 220),
                                  win ? Color_Create(84, 236, 250, 245) : Color_Create(40, 74, 88, 165));
        OpenGLRenderer_DrawRect2D(bx0 + 1.0f, globY[o] - 8.0f, (bw - 2.0f) * norm, 16.0f,
                                  win ? Color_Create(44, 174, 196, 170) : Color_Create(36, 98, 118, 120),
                                  Color_Create(0, 0, 0, 0));
        char obuf[40];
        snprintf(obuf, sizeof(obuf), "%-7s %+5.2f", gNames[o], sc);
        OpenGLRenderer_DrawText2D(bx0 + 5.0f, globY[o] - 4.0f, 1.02f,
                                  win ? Color_Create(244, 255, 255, 255) : Color_Create(196, 222, 232, 245), obuf);
    }

    /* 7) Evaluador compartido de recursos y criaturas (columna derecha inferior del grafo) */
    float subX = xOut - 12.0f, subY = globY[2] + 32.0f, subW = px + pw - subX - 10.0f;
    OpenGLRenderer_DrawRect2D(subX, subY, subW, graphBot - subY + 8.0f,
                              Color_Create(10, 18, 26, 225), Color_Create(44, 122, 142, 190));
    OpenGLRenderer_DrawText2D(subX + 6.0f, subY + 5.0f, 0.96f, Color_Create(124, 232, 182, 255),
                              "SHARED EVALUATORS (RES/CRE)");
    float ry = subY + 20.0f;
    unsigned maxResShow = v2->resourceCount < 6u ? v2->resourceCount : 6u;
    for (unsigned r = 0; r < maxResShow && ry + 18.0f < graphBot; ++r) {
        AIObjectId rid = v2->resourceIds[r];
        float sc = v2->resourceScores[r];
        bool win = (v2->selectedAction == NEURAL_V2_ACTION_CONSUME_TARGET && v2->selectedTargetId == rid);
        const char *sname = "resource";
        Color swatch = Color_Create(110, 180, 130, 255);
        for (unsigned k = 0; k < rv->world.activeResourceCount; ++k) {
            const EcosystemResource *er = &rv->world.resources[rv->world.activeResourceSlots[k]];
            if (er->id == rid) {
                if (er->molecular && rv->world.metabolismDb) {
                    const SubstanceDef *sd = SubstanceRegistry_Get(&rv->world.metabolismDb->substances, er->substanceHandle);
                    if (sd) sname = sd->name;
                    swatch = colorFromFloat3(er->color);
                }
                break;
            }
        }
        OpenGLRenderer_DrawRect2D(subX + 5.0f, ry, subW - 10.0f, 16.0f,
                                  win ? Color_Create(24, 78, 66, 235) : Color_Create(14, 24, 32, 215),
                                  win ? Color_Create(104, 246, 188, 245) : Color_Create(38, 72, 86, 155));
        OpenGLRenderer_DrawRect2D(subX + 8.0f, ry + 3.0f, 8.0f, 10.0f, swatch, Color_Create(220, 236, 244, 140));
        char rb[48];
        snprintf(rb, sizeof(rb), "#%-3u %-10.10s %+5.2f", rid, sname, sc);
        OpenGLRenderer_DrawText2D(subX + 20.0f, ry + 3.0f, 0.92f,
                                  win ? Color_Create(244, 255, 248, 255) : Color_Create(196, 222, 234, 240), rb);
        ry += 18.0f;
    }
    if (v2->creatureCount > 0u && ry + 26.0f < graphBot) {
        ry += 4.0f;
        OpenGLRenderer_DrawText2D(subX + 6.0f, ry, 0.90f, Color_Create(246, 166, 128, 255), "CREATURES (ATT/FLE/FOL)");
        ry += 13.0f;
        for (unsigned c = 0; c < v2->creatureCount && c < 3u && ry + 16.0f < graphBot; ++c) {
            AIObjectId cid = v2->creatureIds[c];
            bool win = (v2->selectedTargetId == cid && v2->selectedAction >= NEURAL_V2_ACTION_ATTACK_TARGET);
            OpenGLRenderer_DrawRect2D(subX + 5.0f, ry, subW - 10.0f, 15.0f,
                                      win ? Color_Create(76, 36, 34, 235) : Color_Create(14, 24, 32, 215),
                                      win ? Color_Create(250, 146, 112, 245) : Color_Create(38, 72, 86, 155));
            char cb[48];
            snprintf(cb, sizeof(cb), "#%-3u %+4.1f %+4.1f %+4.1f",
                     cid, v2->creatureScores[c][0], v2->creatureScores[c][1], v2->creatureScores[c][2]);
            OpenGLRenderer_DrawText2D(subX + 8.0f, ry + 3.0f, 0.90f, Color_Create(224, 236, 242, 245), cb);
            ry += 17.0f;
        }
    }

    /* 8) Panel inferior: Decisión ganadora + Retroalimentación metabólica + Leyenda */
    float botY = graphBot + 16.0f;
    OpenGLRenderer_DrawRect2D(px + 8.0f, botY, pw - 16.0f, ph - (botY - py) - 8.0f,
                              Color_Create(11, 20, 28, 235), Color_Create(48, 142, 164, 205));
    char dec[160];
    snprintf(dec, sizeof(dec), "DECISION: %-14s  TARGET:#%-4u  SCORE:%+6.2f  OUT:%s",
             NeuralV2Action_Name(v2->selectedAction), v2->selectedTargetId, v2->selectedScore,
             NeuralInteractionOutcome_Name(fe->mind.neuralState.lastOutcome));
    OpenGLRenderer_DrawText2D(px + 14.0f, botY + 8.0f, 1.06f, Color_Create(236, 252, 255, 255), dec);

    const NeuralMetabolicFeedback *fb = &v2->lastFeedback;
    char fbBuf[256];
    snprintf(fbBuf, sizeof(fbBuf),
             "FEEDBACK  ING:%+.3f  E_ABS:%+.3f  W_ABS:%+.3f  NUT:%+.3f\n"
             "          TOX_D:%+.3f  DMG:%+.3f  FAIL:%+.3f\n"
             "COUNTS    IDL:%llu EXP:%llu RST:%llu CON:%llu ATT:%llu FLE:%llu FOL:%llu MAT:%llu\n"
             "EDGES: CYAN:+W  RED:-W  AMBER:RECURRENT W_HH  (W-/W+ THRESHOLD)",
             fb->ingestedMass, fb->energyAbsorbed, fb->waterAbsorbed, fb->nutrientsAbsorbed,
             fb->toxicityDelta, fb->damageReceived, fb->interactionFailure,
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_IDLE],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_EXPLORE],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_REST],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_CONSUME_TARGET],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_ATTACK_TARGET],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_FLEE_TARGET],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_FOLLOW_TARGET],
             (unsigned long long)v2->actionCounts[NEURAL_V2_ACTION_MATE_TARGET]);
    OpenGLRenderer_DrawText2D(px + 14.0f, botY + 28.0f, 0.98f, Color_Create(188, 224, 236, 245), fbBuf);
}

static void labSyncSnapshotLocked(LabAsyncController *ctrl, const char *msg, bool isErr) {
    BrainEvolutionLab *lab = &ctrl->lab;
    LabUiSnapshot *u = &ctrl->ui;

    u->status = lab->status;
    u->currentGeneration = lab->currentGeneration;
    u->currentStage = lab->currentStage;
    u->stageCount = lab->stageCount;
    if (lab->stageCount > 0u && lab->currentStage < lab->stageCount) {
        snprintf(u->currentEnvName, sizeof(u->currentEnvName), "%.63s",
                 lab->stages[lab->currentStage].env.name);
    } else {
        snprintf(u->currentEnvName, sizeof(u->currentEnvName), "none");
    }
    snprintf(u->speciesName, sizeof(u->speciesName), "%.63s", lab->species.name);
    snprintf(u->profileName, sizeof(u->profileName), "%.63s", lab->species.metabolismProfileId);
    u->recipeId = lab->species.recipeId;
    u->populationSize = lab->populationSize;
    u->eliteCount = lab->config.eliteCount;
    u->workerCount = lab->workerCount;
    u->totalEpisodesEvaluated = lab->totalEpisodesEvaluated;
    u->totalTicksSimulated = lab->totalTicksSimulated;
    u->elapsedTrainingSeconds = lab->elapsedTrainingSeconds;
    u->neuralDiversity = BrainEvolutionLab_ComputeDiversity(lab);
    u->trainingActive = ctrl->trainingActive;

    u->historyCount = lab->historyCount < BRAIN_LAB_MAX_HISTORY ? lab->historyCount : BRAIN_LAB_MAX_HISTORY;
    if (u->historyCount > 0u) {
        memcpy(u->history, lab->history, u->historyCount * sizeof(BrainLabGenerationRecord));
        const BrainLabGenerationRecord *last = &u->history[u->historyCount - 1u];
        u->bestTrainFitness = last->bestTrainFitness;
        u->meanTrainFitness = last->meanTrainFitness;
        u->medianTrainFitness = last->medianTrainFitness;
        u->bestTrainSurvival = last->bestTrainSurvival;
        u->lastEpsPerSec = last->episodesPerSecond;
        u->lastTicksPerSec = last->ticksPerSecond;
        u->lastTotalKills = last->totalKills;
        u->lastTotalCarcasses = last->totalCarcassesConsumed;
    } else {
        u->bestTrainFitness = u->meanTrainFitness = u->medianTrainFitness = u->bestTrainSurvival = 0.0f;
        u->lastEpsPerSec = u->lastTicksPerSec = 0.0;
        u->lastTotalKills = u->lastTotalCarcasses = 0u;
    }

    u->hasValidation = lab->hasBestValidation;
    if (lab->hasBestValidation) {
        u->bestValFitness = lab->bestValidationMetrics.meanFitness;
        u->bestValSurvival = lab->bestValidationMetrics.survivalRate;
        u->bestValHealth = lab->bestValidationMetrics.meanHealth;
        u->bestValEnergy = lab->bestValidationMetrics.meanEnergy;
        u->bestValHydration = lab->bestValidationMetrics.meanHydration;
    } else {
        u->bestValFitness = u->bestValSurvival = u->bestValHealth = u->bestValEnergy = u->bestValHydration = 0.0f;
    }

    /* Poblar los 5 slots del inspector de candidatos. */
    for (unsigned s = 0; s < CAND_SLOT_COUNT; ++s) u->candidates[s].valid = false;

    const NeuralGenome *champ = BrainEvolutionLab_GetChampion(lab);
    if (champ && champ->valid) {
        LabCandidateSummary *cs = &u->candidates[CAND_CHAMPION];
        cs->valid = true;
        snprintf(cs->title, sizeof(cs->title), "CHAMPION");
        cs->genome = *champ;
        cs->fingerprint = champ->metadata.fingerprint;
        if (lab->hasBestValidation) {
            cs->generation = lab->bestValidationGeneration;
            cs->metrics = lab->bestValidationMetrics;
        } else if (lab->hasBestTraining) {
            cs->generation = lab->bestTrainingGeneration;
            cs->metrics = lab->bestTrainingMetrics;
        } else if (lab->populationSize > 0u && lab->rankOrder && lab->popMetrics) {
            unsigned r0 = lab->rankOrder[0];
            if (r0 < lab->populationSize) {
                cs->generation = lab->currentGeneration;
                cs->metrics = lab->popMetrics[r0];
            }
        }
    }
    if (lab->hasBestTraining && lab->bestTrainingGenome.valid) {
        LabCandidateSummary *cs = &u->candidates[CAND_BEST_TRAIN];
        cs->valid = true;
        snprintf(cs->title, sizeof(cs->title), "BEST TRAIN");
        cs->genome = lab->bestTrainingGenome;
        cs->fingerprint = lab->bestTrainingGenome.metadata.fingerprint;
        cs->generation = lab->bestTrainingGeneration;
        cs->metrics = lab->bestTrainingMetrics;
    }
    if (lab->hasBestValidation && lab->bestValidationGenome.valid) {
        LabCandidateSummary *cs = &u->candidates[CAND_BEST_VAL];
        cs->valid = true;
        snprintf(cs->title, sizeof(cs->title), "BEST VAL");
        cs->genome = lab->bestValidationGenome;
        cs->fingerprint = lab->bestValidationGenome.metadata.fingerprint;
        cs->generation = lab->bestValidationGeneration;
        cs->metrics = lab->bestValidationMetrics;
    }
    if (lab->populationSize >= 2u && lab->rankOrder && lab->population && lab->popMetrics) {
        unsigned idx2 = lab->rankOrder[1];
        if (idx2 < lab->populationSize && lab->population[idx2].valid) {
            LabCandidateSummary *cs = &u->candidates[CAND_ELITE_2];
            cs->valid = true;
            snprintf(cs->title, sizeof(cs->title), "ELITE #2");
            cs->genome = lab->population[idx2];
            cs->fingerprint = lab->population[idx2].metadata.fingerprint;
            cs->generation = lab->currentGeneration;
            cs->metrics = lab->popMetrics[idx2];
        }
        unsigned idxRand = lab->rankOrder[lab->populationSize / 2u];
        if (idxRand < lab->populationSize && lab->population[idxRand].valid) {
            LabCandidateSummary *cs = &u->candidates[CAND_RANDOM];
            cs->valid = true;
            snprintf(cs->title, sizeof(cs->title), "MEDIAN/POP");
            cs->genome = lab->population[idxRand];
            cs->fingerprint = lab->population[idxRand].metadata.fingerprint;
            cs->generation = lab->currentGeneration;
            cs->metrics = lab->popMetrics[idxRand];
        }
    }

    if (msg) {
        snprintf(u->statusMessage, sizeof(u->statusMessage), "%.219s", msg);
        u->statusError = isErr;
    }
    ++u->revision;
}

static void *labWorkerMain(void *arg) {
    LabAsyncController *ctrl = (LabAsyncController *)arg;
    char err[256];
    while (!ctrl->quitRequested) {
        bool doStep = false, doVal = false, doSave = false, doLoad = false, doExport = false;
        char ckptPath[256] = {0}, exportPath[256] = {0};

        pthread_mutex_lock(&ctrl->mutex);
        if (ctrl->saveCkptRequested) {
            doSave = true;
            ctrl->saveCkptRequested = false;
            snprintf(ckptPath, sizeof(ckptPath), "%s", ctrl->ckptPath);
        } else if (ctrl->loadCkptRequested) {
            doLoad = true;
            ctrl->loadCkptRequested = false;
            snprintf(ckptPath, sizeof(ckptPath), "%s", ctrl->ckptPath);
        } else if (ctrl->exportRequested) {
            doExport = true;
            ctrl->exportRequested = false;
            snprintf(exportPath, sizeof(exportPath), "%s", ctrl->exportPath);
        } else if (ctrl->validateRequested) {
            doVal = true;
            ctrl->validateRequested = false;
        } else if (ctrl->stepOneRequested) {
            doStep = true;
            ctrl->stepOneRequested = false;
        } else if (ctrl->trainingActive &&
                   (ctrl->lab.status == LAB_STATUS_READY || ctrl->lab.status == LAB_STATUS_RUNNING)) {
            doStep = true;
        }
        pthread_mutex_unlock(&ctrl->mutex);

        if (doSave) {
            err[0] = '\0';
            ensureDirPath(ckptPath);
            bool ok = EvolutionCheckpoint_Save(&ctrl->lab, ckptPath, err, sizeof(err));
            char msg[220];
            if (ok) snprintf(msg, sizeof(msg), "CHECKPOINT SAVED: %.180s (GEN %u)", ckptPath, ctrl->lab.currentGeneration);
            else snprintf(msg, sizeof(msg), "CKPT SAVE ERR: %.180s", err);
            pthread_mutex_lock(&ctrl->mutex);
            labSyncSnapshotLocked(ctrl, msg, !ok);
            pthread_mutex_unlock(&ctrl->mutex);
            continue;
        }
        if (doLoad) {
            err[0] = '\0';
            bool ok = EvolutionCheckpoint_Load(&ctrl->lab, ckptPath, false, err, sizeof(err));
            char msg[220];
            if (ok) snprintf(msg, sizeof(msg), "CHECKPOINT LOADED: %.165s (GEN %u, VERIFIED)", ckptPath, ctrl->lab.currentGeneration);
            else snprintf(msg, sizeof(msg), "CKPT LOAD REJECTED: %.180s", err);
            pthread_mutex_lock(&ctrl->mutex);
            ctrl->trainingActive = false;
            labSyncSnapshotLocked(ctrl, msg, !ok);
            pthread_mutex_unlock(&ctrl->mutex);
            continue;
        }
        if (doExport) {
            err[0] = '\0';
            bool ok = true;
            if (exportPath[0]) {
                ensureDirPath(exportPath);
                const NeuralGenome *champ = BrainEvolutionLab_GetChampion(&ctrl->lab);
                ok = champ && NeuralBrain_SaveFile(champ, exportPath, true, err, sizeof(err));
                if (ok) BrainEvolutionLab_ExportOutputs(&ctrl->lab, err, sizeof(err));
            } else {
                ok = BrainEvolutionLab_ExportOutputs(&ctrl->lab, err, sizeof(err));
            }
            char msg[220];
            if (ok) {
                const NeuralGenome *champ = BrainEvolutionLab_GetChampion(&ctrl->lab);
                snprintf(msg, sizeof(msg), "EXPORTED CHAMPION FP:%08llX -> %.140s",
                         (unsigned long long)(champ ? (champ->metadata.fingerprint & 0xFFFFFFFFu) : 0u),
                         exportPath[0] ? exportPath : ctrl->lab.config.outputDir);
            } else {
                snprintf(msg, sizeof(msg), "EXPORT ERR: %.180s", err);
            }
            pthread_mutex_lock(&ctrl->mutex);
            labSyncSnapshotLocked(ctrl, msg, !ok);
            pthread_mutex_unlock(&ctrl->mutex);
            continue;
        }
        if (doVal) {
            err[0] = '\0';
            EvolutionAggregateMetrics vm = {0};
            bool ok = BrainEvolutionLab_RunValidation(&ctrl->lab, &vm, err, sizeof(err));
            char msg[220];
            if (ok) {
                snprintf(msg, sizeof(msg), "VALIDATION COMPLETE: FIT %.4f  SURV %.1f%%  HP %.2f E %.2f H %.2f",
                         vm.meanFitness, vm.survivalRate * 100.0f, vm.meanHealth, vm.meanEnergy, vm.meanHydration);
            } else {
                snprintf(msg, sizeof(msg), "VALIDATION ERR: %.180s", err);
            }
            pthread_mutex_lock(&ctrl->mutex);
            labSyncSnapshotLocked(ctrl, msg, !ok);
            pthread_mutex_unlock(&ctrl->mutex);
            continue;
        }
        if (doStep) {
            err[0] = '\0';
            bool ok = BrainEvolutionLab_StepGeneration(&ctrl->lab, err, sizeof(err));
            char msg[220];
            if (!ok) {
                snprintf(msg, sizeof(msg), "STEP ERROR: %.180s", err);
            } else if (ctrl->lab.status != LAB_STATUS_RUNNING && ctrl->lab.status != LAB_STATUS_READY) {
                snprintf(msg, sizeof(msg), "FINISHED: %s AT GEN %u (BEST %.4f)",
                         BrainLabStatus_Name(ctrl->lab.status), ctrl->lab.currentGeneration,
                         ctrl->lab.hasBestTraining ? ctrl->lab.bestTrainingMetrics.meanFitness : 0.0f);
            } else {
                snprintf(msg, sizeof(msg), "GEN %u DONE  STAGE %u/%u  BEST %.4f  DIV %.3f",
                         ctrl->lab.currentGeneration, ctrl->lab.currentStage + 1u, ctrl->lab.stageCount,
                         ctrl->lab.hasBestTraining ? ctrl->lab.bestTrainingMetrics.meanFitness : 0.0f,
                         BrainEvolutionLab_ComputeDiversity(&ctrl->lab));
            }
            pthread_mutex_lock(&ctrl->mutex);
            if (!ok || (ctrl->lab.status != LAB_STATUS_RUNNING && ctrl->lab.status != LAB_STATUS_READY)) {
                ctrl->trainingActive = false;
            }
            labSyncSnapshotLocked(ctrl, msg, !ok);
            pthread_mutex_unlock(&ctrl->mutex);
            continue;
        }

        struct timespec ts = {.tv_sec = 0, .tv_nsec = 15000000L}; /* 15 ms idle */
        nanosleep(&ts, NULL);
    }
    return NULL;
}

static void replayShutdown(ReplayViewer *rv) {
    if (!rv || !rv->initialized) return;
    EcosystemVisual_Shutdown(&rv->visual);
    EcosystemWorld_Free(&rv->world);
    memset(rv, 0, sizeof(*rv));
}

static uint32_t labReplayMixSeed(uint32_t a, uint32_t b, uint32_t c) {
    uint32_t x = a ^ (b * 2654435761u) ^ (c * 2246822519u);
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x ? x : 1u;
}

static bool parseLabTerrainDebug(const char *s, TerrainDebugMode *out) {
    if (!s || !out) return false;
    if (!strcmp(s, "natural")) *out = TERRAIN_DEBUG_NATURAL;
    else if (!strcmp(s, "elevation") || !strcmp(s, "height")) *out = TERRAIN_DEBUG_ELEVATION;
    else if (!strcmp(s, "slope")) *out = TERRAIN_DEBUG_SLOPE;
    else if (!strcmp(s, "hydrology") || !strcmp(s, "hydro")) *out = TERRAIN_DEBUG_HYDROLOGY;
    else if (!strcmp(s, "depth") || !strcmp(s, "water")) *out = TERRAIN_DEBUG_WATER_DEPTH;
    else if (!strcmp(s, "biome") || !strcmp(s, "fertility")) *out = TERRAIN_DEBUG_BIOME;
    else if (!strcmp(s, "nav") || !strcmp(s, "navigation")) *out = TERRAIN_DEBUG_NAVIGATION;
    else if (!strcmp(s, "collision") || !strcmp(s, "rocks")) *out = TERRAIN_DEBUG_COLLISION;
    else return false;
    return true;
}

static bool replayLoadCandidate(ReplayViewer *rv,
                                const EvolutionEnvironmentConfig *env,
                                const EvolutionSpeciesConfig *species,
                                const MetabolismDatabase *db,
                                const LabCandidateSummary *cand,
                                LabCandidateSlot slot,
                                uint32_t seed,
                                bool isRecordedEvalSeed,
                                char *err, size_t errSize) {
    if (!rv || !env || !species || !db || !cand || !cand->valid) return false;
    replayShutdown(rv);

    if (!EvolutionEnvironment_BuildEpisodeWorld(&rv->world, env, species, db,
                                                &cand->genome, seed, true,
                                                &rv->focalId, err, errSize)) {
        return false;
    }
    if (!EcosystemVisual_Init(&rv->visual, &rv->world)) {
        EcosystemWorld_Free(&rv->world);
        snprintf(err, errSize, "No se pudo inicializar EcosystemVisual para replay");
        return false;
    }
    EcosystemVisual_SetBudget(&rv->visual, 8u, 16u, 1.0f);
    rv->initialized = true;
    rv->paused = false;
    rv->episodeSeed = seed;
    rv->isRecordedEvalSeed = isRecordedEvalSeed;
    rv->achievedSpeed = 0.0;
    rv->loadedFingerprint = cand->fingerprint;
    rv->loadedSlot = slot;
    snprintf(rv->loadedLabel, sizeof(rv->loadedLabel), "%s (%s)",
             cand->title,
             cand->metrics.episodeCount == 0u ? "RANDOM/INIT" : "TRAINED");
    rv->simAccumulator = 0.0;
    return true;
}

static void drawHistoryPlot(float x, float y, float w, float h,
                            const BrainLabGenerationRecord *hist, unsigned count) {
    OpenGLRenderer_DrawRect2D(x, y, w, h, Color_Create(11, 18, 26, 236), Color_Create(44, 108, 126, 195));
    /* Rejilla horizontal 0.25 / 0.50 / 0.75 */
    for (unsigned g = 1u; g <= 3u; ++g) {
        float gy = y + h - h * ((float)g * 0.25f);
        OpenGLRenderer_DrawLine2D(x + 2.0f, gy, x + w - 2.0f, gy, 1.0f, Color_Create(34, 62, 74, 140));
    }
    if (count < 1u) {
        OpenGLRenderer_DrawText2D(x + 12.0f, y + h * 0.5f - 6.0f, 1.08f, Color_Create(136, 168, 182, 220),
                                  "NO GENERATIONS YET (PRESS SPACE OR [STEP])");
        return;
    }
    float maxY = 1.0f;
    for (unsigned i = 0; i < count; ++i) {
        if (hist[i].bestTrainFitness > maxY) maxY = hist[i].bestTrainFitness;
        if (hist[i].hasValidation && hist[i].validationFitness > maxY) maxY = hist[i].validationFitness;
    }
    float pad = 6.0f, pw = w - 2.0f * pad, ph = h - 2.0f * pad;
    unsigned denom = count > 1u ? (count - 1u) : 1u;

    /* Marcadores verticales de cambio de etapa de currículo */
    for (unsigned i = 1u; i < count; ++i) {
        if (hist[i].curriculumStage != hist[i - 1u].curriculumStage) {
            float sx = x + pad + pw * (float)i / (float)denom;
            OpenGLRenderer_DrawLine2D(sx, y + 3.0f, sx, y + h - 3.0f, 1.5f, Color_Create(250, 196, 76, 200));
        }
    }

    for (unsigned i = 1u; i < count; ++i) {
        float x0 = x + pad + pw * (float)(i - 1u) / (float)denom;
        float x1 = x + pad + pw * (float)i / (float)denom;
        /* Median (ámbar) */
        float my0 = y + h - pad - ph * fmaxf(0.0f, fminf(1.0f, hist[i - 1u].medianTrainFitness / maxY));
        float my1 = y + h - pad - ph * fmaxf(0.0f, fminf(1.0f, hist[i].medianTrainFitness / maxY));
        OpenGLRenderer_DrawLine2D(x0, my0, x1, my1, 1.3f, Color_Create(232, 168, 72, 205));
        /* Mean (verde) */
        float ay0 = y + h - pad - ph * fmaxf(0.0f, fminf(1.0f, hist[i - 1u].meanTrainFitness / maxY));
        float ay1 = y + h - pad - ph * fmaxf(0.0f, fminf(1.0f, hist[i].meanTrainFitness / maxY));
        OpenGLRenderer_DrawLine2D(x0, ay0, x1, ay1, 1.6f, Color_Create(98, 224, 148, 225));
        /* Best train (cian) */
        float by0 = y + h - pad - ph * fmaxf(0.0f, fminf(1.0f, hist[i - 1u].bestTrainFitness / maxY));
        float by1 = y + h - pad - ph * fmaxf(0.0f, fminf(1.0f, hist[i].bestTrainFitness / maxY));
        OpenGLRenderer_DrawLine2D(x0, by0, x1, by1, 2.2f, Color_Create(76, 232, 248, 250));
    }

    /* Puntos y línea de validación (dorado) */
    float prevVx = -1.0f, prevVy = -1.0f;
    for (unsigned i = 0u; i < count; ++i) {
        if (!hist[i].hasValidation) continue;
        float vx = x + pad + (count > 1u ? pw * (float)i / (float)denom : pw * 0.5f);
        float vy = y + h - pad - ph * fmaxf(0.0f, fminf(1.0f, hist[i].validationFitness / maxY));
        if (prevVx >= 0.0f) {
            OpenGLRenderer_DrawLine2D(prevVx, prevVy, vx, vy, 2.0f, Color_Create(255, 224, 88, 245));
        }
        OpenGLRenderer_DrawCircle2D(vx, vy, 3.5f, Color_Create(255, 228, 96, 255), Color_Create(255, 255, 255, 220));
        prevVx = vx; prevVy = vy;
    }
}

static void drawSurvivalDiversityPlot(float x, float y, float w, float h,
                                      const BrainLabGenerationRecord *hist, unsigned count) {
    OpenGLRenderer_DrawRect2D(x, y, w, h, Color_Create(11, 18, 26, 236), Color_Create(44, 108, 126, 195));
    OpenGLRenderer_DrawLine2D(x + 2.0f, y + h * 0.5f, x + w - 2.0f, y + h * 0.5f, 1.0f, Color_Create(34, 62, 74, 140));
    if (count < 2u) return;
    float pad = 5.0f, pw = w - 2.0f * pad, ph = h - 2.0f * pad;
    float maxDiv = 0.5f;
    for (unsigned i = 0; i < count; ++i) {
        if (hist[i].neuralDiversity > maxDiv) maxDiv = hist[i].neuralDiversity;
    }
    for (unsigned i = 1u; i < count; ++i) {
        float x0 = x + pad + pw * (float)(i - 1u) / (float)(count - 1u);
        float x1 = x + pad + pw * (float)i / (float)(count - 1u);
        float s0 = y + h - pad - ph * fmaxf(0.0f, fminf(1.0f, hist[i - 1u].bestTrainSurvival));
        float s1 = y + h - pad - ph * fmaxf(0.0f, fminf(1.0f, hist[i].bestTrainSurvival));
        OpenGLRenderer_DrawLine2D(x0, s0, x1, s1, 2.0f, Color_Create(92, 232, 158, 240));
        float d0 = y + h - pad - ph * fmaxf(0.0f, fminf(1.0f, hist[i - 1u].neuralDiversity / maxDiv));
        float d1 = y + h - pad - ph * fmaxf(0.0f, fminf(1.0f, hist[i].neuralDiversity / maxDiv));
        OpenGLRenderer_DrawLine2D(x0, d0, x1, d1, 1.6f, Color_Create(196, 134, 246, 230));
    }
}

static void printHelp(void) {
    puts("Uso: ./demos/demo_brain_evolution_lab [OPCIONES]\n"
         "  --species=PATH              Archivo *.species.json (def: species/brumacerno.species.json)\n"
         "  --environment=PATH          Archivo *.environment.json (def: environments/brumacerno_mixed.environment.json)\n"
         "  --curriculum=P1,P2,...      Lista separada por comas de entornos progresivos\n"
         "  --metabolism-dir=DIR        Directorio de metabolismo (def: metabolism)\n"
         "  --recipes-dir=DIR           Directorio de recetas (def: recipes)\n"
         "  --initial-brain=PATH        Cerebro v2 inicial opcional\n"
         "  --resume=CHECKPOINT.json    Reanudar checkpoint verificado por huellas\n"
         "  --output=DIR                Directorio de salida (def: build/brain-evolution/gui_session)\n"
         "  --seed=N  --population=N  --generations=N  --workers=N  --episode-seconds=S\n"
         "  --train-seeds=N  --val-seeds=N  --test-seeds=N  --val-interval=N  --ckpt-interval=N\n"
         "  --mutation-only  --mutation-rate=F  --mutation-sigma=F  --target-val-fitness=F\n"
         "  --auto-start                Iniciar entrenamiento en segundo plano al abrir\n"
         "  --pretrain-gens=N           Ejecutar N generaciones antes de renderizar (capturas)\n"
         "  --action-save-ckpt=PATH     Guardar checkpoint al inicio para verificación visual\n"
         "  --action-load-ckpt=PATH     Cargar checkpoint al inicio para verificación visual\n"
         "  --action-export=PATH        Exportar campeón al inicio para verificación visual\n"
         "  --replay-ticks=N            Avanzar N ticks el viewport 3D de replay antes de capturar\n"
         "  --replay-speed=1|2|4|8|16|32 Velocidad inicial del viewport 3D de replay (tecla F / [ ])\n"
         "  --replay-seed=N             Semilla exacta de episodio para el replay 3D\n"
         "  --terrain-debug=MODE        Modo de depuración del terreno (natural|elevation|slope|hydrology|depth|biome|nav|collision)\n"
         "  --show-neural               Abrir el visualizador gráfico de la red neuronal V2 (tecla N)\n"
         "  --width=W  --height=H  --frames=N  --capture=IMG.ppm");
}

int main(int argc, char **argv) {
    BrainLabConfig cfg = BrainLabConfig_Default();
    snprintf(cfg.speciesPath, sizeof(cfg.speciesPath), "species/brumacerno.species.json");
    snprintf(cfg.environmentPath, sizeof(cfg.environmentPath), "environments/brumacerno_mixed.environment.json");
    snprintf(cfg.outputDir, sizeof(cfg.outputDir), "build/brain-evolution/gui_session");
    cfg.populationSize = 32u;
    cfg.maxGenerations = 40u;
    cfg.validationInterval = 5u;

    const char *recipesDir = "recipes";
    const char *resumePath = NULL;
    const char *capturePath = NULL;
    const char *actionSaveCkpt = NULL;
    const char *actionLoadCkpt = NULL;
    const char *actionExport = NULL;
    unsigned frames = 0u, pretrainGens = 0u, replayTicks = 0u;
    bool autoStart = false, showNeural = false;
    bool hasCustomReplaySeed = false;
    uint32_t customReplaySeed = 0u;
    TerrainDebugMode terrainDebugMode = TERRAIN_DEBUG_NATURAL;
    double initialReplaySpeed = 1.0;
    int winW = LAB_WIN_DEFAULT_W, winH = LAB_WIN_DEFAULT_H;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        if (!strcmp(a, "--help")) { printHelp(); return 0; }
        else if (!strncmp(a, "--species=", 10)) snprintf(cfg.speciesPath, sizeof(cfg.speciesPath), "%s", a + 10);
        else if (!strncmp(a, "--environment=", 14)) snprintf(cfg.environmentPath, sizeof(cfg.environmentPath), "%s", a + 14);
        else if (!strncmp(a, "--curriculum=", 13)) {
            cfg.useCurriculum = true;
            cfg.curriculumCount = 0u;
            const char *p = a + 13;
            while (*p && cfg.curriculumCount < BRAIN_LAB_MAX_CURRICULUM_STAGES) {
                const char *comma = strchr(p, ',');
                size_t len = comma ? (size_t)(comma - p) : strlen(p);
                if (len > 0 && len < sizeof(cfg.curriculumPaths[0])) {
                    memcpy(cfg.curriculumPaths[cfg.curriculumCount], p, len);
                    cfg.curriculumPaths[cfg.curriculumCount][len] = '\0';
                    ++cfg.curriculumCount;
                }
                if (!comma) break;
                p = comma + 1;
            }
        }
        else if (!strncmp(a, "--metabolism-dir=", 17)) snprintf(cfg.metabolismDir, sizeof(cfg.metabolismDir), "%s", a + 17);
        else if (!strncmp(a, "--recipes-dir=", 14)) recipesDir = a + 14;
        else if (!strncmp(a, "--initial-brain=", 16)) {
            snprintf(cfg.initialBrainPath, sizeof(cfg.initialBrainPath), "%s", a + 16);
            cfg.randomFounders = false;
        }
        else if (!strncmp(a, "--resume=", 9)) resumePath = a + 9;
        else if (!strncmp(a, "--output=", 9)) snprintf(cfg.outputDir, sizeof(cfg.outputDir), "%s", a + 9);
        else if (!strncmp(a, "--seed=", 7)) cfg.seed = (uint32_t)strtoul(a + 7, NULL, 10);
        else if (!strncmp(a, "--population=", 13)) cfg.populationSize = (unsigned)strtoul(a + 13, NULL, 10);
        else if (!strncmp(a, "--generations=", 14)) cfg.maxGenerations = (unsigned)strtoul(a + 14, NULL, 10);
        else if (!strncmp(a, "--workers=", 10)) cfg.workers = (unsigned)strtoul(a + 10, NULL, 10);
        else if (!strncmp(a, "--episode-seconds=", 18)) cfg.episodeSecondsOverride = strtod(a + 18, NULL);
        else if (!strncmp(a, "--train-seeds=", 14)) cfg.trainingSeedsPerGen = (unsigned)strtoul(a + 14, NULL, 10);
        else if (!strncmp(a, "--val-seeds=", 12)) cfg.validationSeeds = (unsigned)strtoul(a + 12, NULL, 10);
        else if (!strncmp(a, "--test-seeds=", 13)) cfg.testSeeds = (unsigned)strtoul(a + 13, NULL, 10);
        else if (!strncmp(a, "--val-interval=", 15)) cfg.validationInterval = (unsigned)strtoul(a + 15, NULL, 10);
        else if (!strncmp(a, "--ckpt-interval=", 16)) cfg.checkpointInterval = (unsigned)strtoul(a + 16, NULL, 10);
        else if (!strcmp(a, "--mutation-only")) cfg.mutationOnly = true;
        else if (!strncmp(a, "--mutation-rate=", 16)) cfg.mutation.mutationRate = strtof(a + 16, NULL);
        else if (!strncmp(a, "--mutation-sigma=", 17)) cfg.mutation.mutationSigma = strtof(a + 17, NULL);
        else if (!strncmp(a, "--target-val-fitness=", 21)) cfg.targetValidationFitness = strtof(a + 21, NULL);
        else if (!strcmp(a, "--auto-start")) autoStart = true;
        else if (!strncmp(a, "--pretrain-gens=", 16)) pretrainGens = (unsigned)strtoul(a + 16, NULL, 10);
        else if (!strncmp(a, "--action-save-ckpt=", 19)) actionSaveCkpt = a + 19;
        else if (!strncmp(a, "--action-load-ckpt=", 19)) actionLoadCkpt = a + 19;
        else if (!strncmp(a, "--action-export=", 16)) actionExport = a + 16;
        else if (!strncmp(a, "--replay-ticks=", 15)) replayTicks = (unsigned)strtoul(a + 15, NULL, 10);
        else if (!strncmp(a, "--replay-speed=", 15)) initialReplaySpeed = strtod(a + 15, NULL);
        else if (!strncmp(a, "--replay-seed=", 14)) {
            customReplaySeed = (uint32_t)strtoul(a + 14, NULL, 10);
            hasCustomReplaySeed = true;
        }
        else if (!strncmp(a, "--terrain-debug=", 16)) {
            if (!parseLabTerrainDebug(a + 16, &terrainDebugMode)) {
                fprintf(stderr, "Modo terrain-debug inválido: %s\n", a + 16);
                return 2;
            }
        }
        else if (!strcmp(a, "--show-neural")) showNeural = true;
        else if (!strncmp(a, "--width=", 8)) winW = (int)strtol(a + 8, NULL, 10);
        else if (!strncmp(a, "--height=", 9)) winH = (int)strtol(a + 9, NULL, 10);
        else if (!strncmp(a, "--frames=", 9)) frames = (unsigned)strtoul(a + 9, NULL, 10);
        else if (!strncmp(a, "--capture=", 10)) capturePath = a + 10;
        else { fprintf(stderr, "Opción desconocida: %s\n", a); return 2; }
    }

    char errBuf[256] = {0};
    if (!CreatureRecipeRegistry_LoadDirectory(recipesDir, errBuf, sizeof(errBuf))) {
        fprintf(stderr, "Error cargando recetas: %s\n", errBuf);
        return 1;
    }

    LabAsyncController *ctrl = calloc(1, sizeof(*ctrl));
    if (!ctrl) return 1;
    pthread_mutex_init(&ctrl->mutex, NULL);

    if (!BrainEvolutionLab_Init(&ctrl->lab, &cfg, errBuf, sizeof(errBuf))) {
        fprintf(stderr, "Error iniciando BrainEvolutionLab: %s\n", errBuf);
        pthread_mutex_destroy(&ctrl->mutex);
        free(ctrl);
        return 1;
    }

    char initMsg[220] = "LAB READY  (PRESS SPACE TO START TRAINING OR [G] STEP 1 GEN)";
    bool initErr = false;

    if (resumePath) {
        if (!EvolutionCheckpoint_Load(&ctrl->lab, resumePath, false, errBuf, sizeof(errBuf))) {
            fprintf(stderr, "Error cargando checkpoint %s: %s\n", resumePath, errBuf);
            BrainEvolutionLab_Destroy(&ctrl->lab);
            pthread_mutex_destroy(&ctrl->mutex);
            free(ctrl);
            return 1;
        }
        snprintf(initMsg, sizeof(initMsg), "RESUMED CHECKPOINT: %.150s (GEN %u)", resumePath, ctrl->lab.currentGeneration);
    }

    for (unsigned g = 0; g < pretrainGens; ++g) {
        if (!BrainEvolutionLab_StepGeneration(&ctrl->lab, errBuf, sizeof(errBuf))) {
            snprintf(initMsg, sizeof(initMsg), "PRETRAIN ERR: %.180s", errBuf);
            initErr = true;
            break;
        }
        if (ctrl->lab.status != LAB_STATUS_RUNNING && ctrl->lab.status != LAB_STATUS_READY) break;
    }
    if (pretrainGens > 0u && !initErr) {
        snprintf(initMsg, sizeof(initMsg), "TRAINED %u GENS  BEST:%.4f  VAL:%.4f  EPS/S:%.1f",
                 ctrl->lab.currentGeneration,
                 ctrl->lab.hasBestTraining ? ctrl->lab.bestTrainingMetrics.meanFitness : 0.0f,
                 ctrl->lab.hasBestValidation ? ctrl->lab.bestValidationMetrics.meanFitness : 0.0f,
                 ctrl->lab.historyCount ? ctrl->lab.history[ctrl->lab.historyCount - 1u].episodesPerSecond : 0.0);
    }

    if (actionSaveCkpt) {
        ensureDirPath(actionSaveCkpt);
        if (EvolutionCheckpoint_Save(&ctrl->lab, actionSaveCkpt, errBuf, sizeof(errBuf))) {
            snprintf(initMsg, sizeof(initMsg), "CHECKPOINT SAVED: %.160s (GEN %u)", actionSaveCkpt, ctrl->lab.currentGeneration);
        } else {
            snprintf(initMsg, sizeof(initMsg), "CKPT SAVE ERR: %.180s", errBuf);
            initErr = true;
        }
    }
    if (actionLoadCkpt) {
        if (EvolutionCheckpoint_Load(&ctrl->lab, actionLoadCkpt, false, errBuf, sizeof(errBuf))) {
            snprintf(initMsg, sizeof(initMsg), "CHECKPOINT VERIFIED & LOADED: %.140s (GEN %u)", actionLoadCkpt, ctrl->lab.currentGeneration);
        } else {
            snprintf(initMsg, sizeof(initMsg), "CKPT LOAD ERR: %.180s", errBuf);
            initErr = true;
        }
    }
    if (actionExport) {
        ensureDirPath(actionExport);
        const NeuralGenome *champ = BrainEvolutionLab_GetChampion(&ctrl->lab);
        if (champ && NeuralBrain_SaveFile(champ, actionExport, true, errBuf, sizeof(errBuf))) {
            BrainEvolutionLab_ExportOutputs(&ctrl->lab, errBuf, sizeof(errBuf));
            snprintf(initMsg, sizeof(initMsg), "EXPORTED BRAIN FP:%08llX -> %.140s",
                     (unsigned long long)(champ->metadata.fingerprint & 0xFFFFFFFFu), actionExport);
        } else {
            snprintf(initMsg, sizeof(initMsg), "EXPORT ERR: %.180s", errBuf);
            initErr = true;
        }
    }

    ctrl->trainingActive = autoStart;
    labSyncSnapshotLocked(ctrl, initMsg, initErr);

    if (pthread_create(&ctrl->thread, NULL, labWorkerMain, ctrl) == 0) {
        ctrl->threadStarted = true;
    }

    OpenGLDemoWindow *window = OpenGLDemoWindow_Create("Monster Engine - Brain Evolution Lab", winW, winH);
    if (!window) {
        ctrl->quitRequested = true;
        if (ctrl->threadStarted) pthread_join(ctrl->thread, NULL);
        BrainEvolutionLab_Destroy(&ctrl->lab);
        pthread_mutex_destroy(&ctrl->mutex);
        free(ctrl);
        return 1;
    }

    ICamera camera = {.position = {14, 14, 18}, .target = {0, 0, 0}, .up = {0, 1, 0},
                      .fov = 50.0f, .aspectRatio = (float)winW / (float)winH,
                      .nearPlane = 0.1f, .farPlane = 600.0f};
    Renderer3D renderer = OpenGLRenderer_Create(&camera);

    EnvironmentRenderer envRenderer = {0};
    if (!EnvironmentRenderer_Init(&envRenderer)) {
        fprintf(stderr, "Error inicializando EnvironmentRenderer en Brain Evolution Lab\n");
        ctrl->quitRequested = true;
        if (ctrl->threadStarted) pthread_join(ctrl->thread, NULL);
        OpenGLRenderer_Destroy(&renderer);
        OpenGLDemoWindow_Free(window);
        BrainEvolutionLab_Destroy(&ctrl->lab);
        pthread_mutex_destroy(&ctrl->mutex);
        free(ctrl);
        return 1;
    }
    EnvironmentRenderer_SetDebugMode(&envRenderer, terrainDebugMode);

    Mesh meshes[5];
    for (unsigned i = 0; i < 5u; ++i) meshes[i] = Mesh_Create();
    Color baseColors[] = {
        {30, 40, 42, 255},   /* Suelo */
        {42, 128, 168, 255}, /* Estanque */
        {64, 152, 84, 255},  /* Vegetación */
        {134, 86, 68, 255},  /* Cadáver/carne */
        {66, 78, 86, 255}    /* Obstáculo */
    };
    bool ok = true;
    ok = PrimitiveMesh_GenerateEllipsoid(&meshes[0], (Transform3D){.position = {0, -0.0001f, 0}, .scale = {1, 0.0001f, 1}}, 16, 6, baseColors[0]) && ok;
    ok = PrimitiveMesh_GenerateEllipsoid(&meshes[1], (Transform3D){.position = {0, 0, 0}, .scale = {1, 0.03f, 1}}, 16, 6, baseColors[1]) && ok;
    ok = PrimitiveMesh_GenerateEllipsoid(&meshes[2], (Transform3D){.position = {0, 0.25f, 0}, .scale = {0.28f, 0.24f, 0.48f}}, 12, 6, baseColors[2]) && ok;
    ok = PrimitiveMesh_GenerateEllipsoid(&meshes[3], (Transform3D){.position = {0, 0.22f, 0}, .scale = {0.30f, 0.22f, 0.48f}}, 12, 6, baseColors[3]) && ok;
    ok = PrimitiveMesh_GenerateEllipsoid(&meshes[4], (Transform3D){.position = {0, 0.65f, 0}, .scale = {1.0f, 0.95f, 1.0f}}, 14, 8, baseColors[4]) && ok;

    /* Copia local del estado UI para renderizar sin mantener bloqueado el mutex. */
    LabUiSnapshot *snap = calloc(1, sizeof(*snap));
    EcosystemVisualDrawPacket *packets = calloc(64u, sizeof(*packets));
    Mesh *cpuBodies = calloc(64u, sizeof(*cpuBodies));
    ReplayViewer replay = {0};
    LabCandidateSlot selectedSlot = CAND_CHAMPION;
    double camAngle = 0.65, camZoom = 22.0;
    /* Usar semilla exacta de evaluación registrada por defecto si existe, o semilla custom. */
    pthread_mutex_lock(&ctrl->mutex);
    *snap = ctrl->ui;
    const EvolutionEnvironmentConfig envCopy0 = ctrl->lab.stages[ctrl->lab.currentStage].env;
    const EvolutionSpeciesConfig spCopy0 = ctrl->lab.species;
    uint32_t recordedValSeed = labReplayMixSeed(ctrl->lab.config.seed,
                                                0x56414C00u + ctrl->lab.currentStage * 17u, 1u);
    pthread_mutex_unlock(&ctrl->mutex);

    uint32_t initReplaySeed = hasCustomReplaySeed ? customReplaySeed : recordedValSeed;
    bool initReplayIsEval = !hasCustomReplaySeed;
    uint32_t replaySeedCounter = initReplaySeed;

    if (snap->candidates[CAND_CHAMPION].valid) {
        replayLoadCandidate(&replay, &envCopy0, &spCopy0, &ctrl->lab.db,
                            &snap->candidates[CAND_CHAMPION], CAND_CHAMPION,
                            initReplaySeed, initReplayIsEval, errBuf, sizeof(errBuf));
        for (unsigned t = 0; t < replayTicks && replay.initialized; ++t) {
            EcosystemWorld_Step(&replay.world);
        }
    }

    LabUiButton btnStart = {0}, btnStep = {0}, btnStop = {0}, btnVal = {0};
    LabUiButton btnSave = {0}, btnLoad = {0}, btnExport = {0}, btnReplay = {0};
    LabUiButton btnNeural = {0}, btnSpeed = {0}, btnWMinus = {0}, btnWPlus = {0};
    LabUiButton btnSlots[CAND_SLOT_COUNT] = {{0}};

    static const double replaySpeeds[] = {1.0, 2.0, 4.0, 8.0, 16.0, 32.0};
    const unsigned replaySpeedCount = (unsigned)(sizeof(replaySpeeds) / sizeof(replaySpeeds[0]));
    unsigned replaySpeedIdx = 0u;
    for (unsigned si = 0u; si < replaySpeedCount; ++si) {
        if (initialReplaySpeed >= replaySpeeds[si] - 0.1) replaySpeedIdx = si;
    }
    float neuralEdgeThreshold = 0.28f;

    unsigned frame = 0u;
    double lastTime = OpenGLDemoWindow_Time(), displayedFps = 60.0;
    bool captured = false;

    while (ok && (!frames || frame < frames)) {
        double now = OpenGLDemoWindow_Time(), dt = fmin(0.1, fmax(0.001, now - lastTime));
        lastTime = now;
        if (frame > 0) displayedFps += (1.0 / dt - displayedFps) * 0.1;

        OpenGLDemoInput input = OpenGLDemoWindow_Poll(window);
        if (input.quit) break;

        /* Leer último snapshot UI desde el hilo entrenador. */
        pthread_mutex_lock(&ctrl->mutex);
        *snap = ctrl->ui;
        EvolutionEnvironmentConfig curEnv = ctrl->lab.stages[ctrl->lab.currentStage].env;
        EvolutionSpeciesConfig curSpecies = ctrl->lab.species;
        pthread_mutex_unlock(&ctrl->mutex);

        /* Manejo de botones y teclas */
        bool reqToggleRun = input.togglePause;
        bool reqStep = input.keyG;
        bool reqStop = input.keyX;
        bool reqVal = input.keyV;
        bool reqSave = false;
        bool reqLoad = false;
        bool reqExport = input.keyE;
        bool reqReplay = input.keyR;
        if (input.keyN) showNeural = !showNeural;
        if (input.toggleDebug && input.shiftHeld && EnvironmentRenderer_IsProcedural(&envRenderer)) {
            terrainDebugMode = (TerrainDebugMode)(((int)terrainDebugMode + 1) % TERRAIN_DEBUG_MODE_COUNT);
            EnvironmentRenderer_SetDebugMode(&envRenderer, terrainDebugMode);
        }
        if (input.keyF) replaySpeedIdx = (replaySpeedIdx + 1u) % replaySpeedCount;
        if (input.furLengthAdjust < 0 && replaySpeedIdx > 0u) --replaySpeedIdx;
        if (input.furLengthAdjust > 0 && replaySpeedIdx + 1u < replaySpeedCount) ++replaySpeedIdx;

        if (input.digitKey >= 0 && input.digitKey < (int)CAND_SLOT_COUNT) {
            selectedSlot = (LabCandidateSlot)input.digitKey;
        }
        if (input.mouseLeftPressed) {
            if (hitBtn(input.mouseX, input.mouseY, &btnStart)) reqToggleRun = true;
            else if (hitBtn(input.mouseX, input.mouseY, &btnStep)) reqStep = true;
            else if (hitBtn(input.mouseX, input.mouseY, &btnStop)) reqStop = true;
            else if (hitBtn(input.mouseX, input.mouseY, &btnVal)) reqVal = true;
            else if (hitBtn(input.mouseX, input.mouseY, &btnSave)) reqSave = true;
            else if (hitBtn(input.mouseX, input.mouseY, &btnLoad)) reqLoad = true;
            else if (hitBtn(input.mouseX, input.mouseY, &btnExport)) reqExport = true;
            else if (hitBtn(input.mouseX, input.mouseY, &btnReplay)) reqReplay = true;
            else if (hitBtn(input.mouseX, input.mouseY, &btnNeural)) showNeural = !showNeural;
            else if (hitBtn(input.mouseX, input.mouseY, &btnSpeed)) replaySpeedIdx = (replaySpeedIdx + 1u) % replaySpeedCount;
            else if (showNeural && hitBtn(input.mouseX, input.mouseY, &btnWMinus)) neuralEdgeThreshold = fmaxf(0.05f, neuralEdgeThreshold - 0.08f);
            else if (showNeural && hitBtn(input.mouseX, input.mouseY, &btnWPlus)) neuralEdgeThreshold = fminf(1.80f, neuralEdgeThreshold + 0.08f);
            else {
                for (unsigned s = 0; s < CAND_SLOT_COUNT; ++s) {
                    if (hitBtn(input.mouseX, input.mouseY, &btnSlots[s])) {
                        selectedSlot = (LabCandidateSlot)s;
                    }
                }
            }
        }
        if (input.mouseRightHeld) camAngle += input.mouseDeltaX * 0.008;
        if (input.mouseWheel != 0) camZoom = fmax(5.0, fmin(55.0, camZoom - input.mouseWheel * 1.2));

        if (reqToggleRun || reqStep || reqStop || reqVal || reqSave || reqLoad || reqExport) {
            pthread_mutex_lock(&ctrl->mutex);
            if (reqToggleRun) ctrl->trainingActive = !ctrl->trainingActive;
            if (reqStep) ctrl->stepOneRequested = true;
            if (reqStop) { ctrl->trainingActive = false; ctrl->lab.status = LAB_STATUS_MANUALLY_STOPPED; }
            if (reqVal) ctrl->validateRequested = true;
            if (reqSave) {
                snprintf(ctrl->ckptPath, sizeof(ctrl->ckptPath), "%.230s/checkpoint.json", cfg.outputDir);
                ctrl->saveCkptRequested = true;
            }
            if (reqLoad) {
                snprintf(ctrl->ckptPath, sizeof(ctrl->ckptPath), "%.230s/checkpoint.json", cfg.outputDir);
                ctrl->loadCkptRequested = true;
            }
            if (reqExport) {
                snprintf(ctrl->exportPath, sizeof(ctrl->exportPath), "%.230s/champion.brain.json", cfg.outputDir);
                ctrl->exportRequested = true;
            }
            pthread_mutex_unlock(&ctrl->mutex);
        }

        if (reqReplay && snap->candidates[selectedSlot].valid) {
            ++replaySeedCounter;
            replayLoadCandidate(&replay, &curEnv, &curSpecies, &ctrl->lab.db,
                                &snap->candidates[selectedSlot], selectedSlot,
                                replaySeedCounter, false, errBuf, sizeof(errBuf));
        }

        /* Avanzar mundo independiente de replay 3D usando world.clock.fixedStep exacto. */
        double effReplaySpeed = replaySpeeds[replaySpeedIdx];
        if (replay.initialized && !replay.paused) {
            double fixedStep = (replay.world.clock.fixedStep > 1e-4) ? replay.world.clock.fixedStep : 0.05;
            double frameWallDt = frames ? (1.0 / 60.0) : dt;
            replay.simAccumulator += frameWallDt * effReplaySpeed;
            unsigned maxSteps = (unsigned)ceil(effReplaySpeed * (frameWallDt / fixedStep) + 4.0);
            if (maxSteps > 80u) maxSteps = 80u;
            unsigned steps = 0u;
            double simBefore = replay.world.clock.seconds;
            while (replay.simAccumulator >= fixedStep && steps < maxSteps) {
                EcosystemWorld_Step(&replay.world);
                replay.simAccumulator -= fixedStep;
                ++steps;
            }
            double simDelta = replay.world.clock.seconds - simBefore;
            double instSpeed = frameWallDt > 1e-5 ? (simDelta / frameWallDt) : effReplaySpeed;
            replay.achievedSpeed = (frame == 0u) ? instSpeed : (replay.achievedSpeed * 0.85 + instSpeed * 0.15);

            EnvironmentRenderer_SyncWorld(&envRenderer, replay.world.staticWorld);
            EcosystemVisual_Sync(&replay.visual);
            const EcosystemEntity *focal = EcosystemWorld_Get(&replay.world, replay.focalId);
            Vector3 target = focal ? focal->body.position : Vec3_Zero();
            camera.target = target;
            float replayHalf = replay.world.observations.halfSize > 0.1f
                               ? replay.world.observations.halfSize : (curEnv.arenaSize * 0.5f);
            camera.farPlane = fmaxf(480.0f, replayHalf * 6.0f);
            camera.position = Vec3_Add(target, Vec3_Create(sinf((float)camAngle) * (float)camZoom,
                                                           (float)camZoom * 0.52f,
                                                           cosf((float)camAngle) * (float)camZoom));
            if (EnvironmentRenderer_IsProcedural(&envRenderer)) {
                float camGround = EnvironmentRenderer_SampleGroundHeight(&envRenderer, camera.position.x, camera.position.z);
                if (camera.position.y < camGround + 1.25f) camera.position.y = camGround + 1.25f;
            }
            for (unsigned i = 0; i < replay.world.count; ++i) {
                unsigned slot = replay.world.activeEntitySlots[i];
                EcosystemVisual_SetProjection(&replay.visual, replay.world.entities[slot].id,
                                              (EcosystemVisualProjection){.projectedPixels = 160.0f, .visible = true});
                EcosystemVisual_RequestTier(&replay.visual, replay.world.entities[slot].id, VISUAL_TIER_NEAR);
            }
            if (frame == 0u) {
                EcosystemVisual_PrewarmVisible(&replay.visual);
                double w0 = OpenGLDemoWindow_Time();
                while (OpenGLDemoWindow_Time() - w0 < 20.0) {
                    EcosystemVisual_Update(&replay.visual, 0.016f);
                    size_t pc = EcosystemVisual_GetDrawPackets(&replay.visual, packets, 64u);
                    bool allReady = true;
                    for (size_t k = 0; k < pc; ++k) if (!packets[k].ready) allReady = false;
                    if (allReady) break;
                }
            }
            EcosystemVisual_Update(&replay.visual, (float)fmin(0.25, dt * effReplaySpeed));
        }

        /* ================================================================== */
        /* Renderizado 3D del Viewport de Replay                              */
        /* ================================================================== */
        renderer.beginFrame(&renderer);
        OpenGLRenderer_SetupCameraOrtho(&camera, (float)camZoom, winW, winH);

        if (replay.initialized) {
            bool proceduralWorld = EnvironmentRenderer_IsProcedural(&envRenderer);
            float limit = replay.world.observations.halfSize > 0.1f
                        ? replay.world.observations.halfSize : (curEnv.arenaSize * 0.5f);
            if (proceduralWorld) {
                EnvironmentRenderer_RenderOpaque(&envRenderer, &renderer, &camera, limit, &meshes[0], 0.65f, 0.72f);
            } else {
                OpenGLRenderer_ModelTransform(Vec3_Create(0.0f, -0.03f, 0.0f), 0.0f, limit * 1.45f, Vec3_Zero());
                renderer.renderMesh(&renderer, &meshes[0]);
                OpenGLRenderer_PopModelMatrix();
            }

            /* Obstáculos y recursos moleculares del episodio de replay */
            for (unsigned i = 0; i < replay.world.obstacleCount; ++i) {
                const EcosystemObstacle *ob = &replay.world.obstacles[i];
                OpenGLRenderer_ModelTransform(ob->position, 0.0f, ob->radius, Vec3_Zero());
                renderer.renderMesh(&renderer, &meshes[4]);
                OpenGLRenderer_PopModelMatrix();
            }
            for (unsigned i = 0; i < replay.world.activeResourceCount; ++i) {
                const EcosystemResource *res = &replay.world.resources[replay.world.activeResourceSlots[i]];
                const AIWorldObject *obj = AIWorld_Get(&replay.world.observations, res->id);
                if (!obj || res->biomass <= 0.001f) continue;
                /* En modo procedural no dibujar discos artificiales para orillas naturales. */
                if (proceduralWorld && obj->waterBodyId != 0u && obj->shoreSiteIndex > 0u) continue;
                float frac = res->maximumBiomass > 1e-4f ? (res->biomass / res->maximumBiomass) : 1.0f;
                Color tint = res->molecular ? colorFromFloat3(res->color) : baseColors[2];
                OpenGLMeshInstance inst = {
                    .position = obj->position, .yaw = (float)(res->id % 13u) * 0.4f,
                    .scale = (res->presentationModel == SUBSTANCE_MODEL_WATER) ? obj->radius : (0.32f + 1.25f * sqrtf(frac)),
                    .scaleY = (res->presentationModel == SUBSTANCE_MODEL_WATER) ? 1.0f : (0.32f + 1.25f * sqrtf(frac)),
                    .tint = tint
                };
                const Mesh *rm = (res->presentationModel == SUBSTANCE_MODEL_WATER) ? &meshes[1]
                               : (res->presentationModel == SUBSTANCE_MODEL_CARCASS) ? &meshes[3]
                               : &meshes[2];
                OpenGLRenderer_RenderMeshInstanced(&renderer, rm, &inst, 1u, true);
            }

            /* Criaturas con anatomía procedural real y orientación 3D completa sobre relieve */
            size_t pc = EcosystemVisual_GetDrawPackets(&replay.visual, packets, 64u);
            for (size_t i = 0; i < pc; ++i) {
                EcosystemVisualDrawPacket *p = &packets[i];
                const Mesh *body = p->bodyMesh ? p->bodyMesh : EcosystemVisual_FallbackMesh(&replay.visual);
                if (!body) continue;
                OpenGLRenderer_SetFurQuality(&renderer, true, true, 0);
                if (proceduralWorld) {
                    OpenGLRenderer_ModelTransformQuat(p->position, p->orientation, p->scale, Vec3_Zero());
                } else {
                    OpenGLRenderer_ModelTransform(p->position, p->yaw * 57.29578f, p->scale, Vec3_Zero());
                }
                bool sk = p->deformer && p->skeleton && p->pose &&
                          OpenGLRenderer_SetMeshMorphSkinning(&renderer, body, p->deformer, p->skeleton,
                                                              p->pose, p->targetGraph, p->headJoint,
                                                              p->headScale, body->geometryGeneration);
                OpenGLRenderer_SetMeshSurfaceOverride(&renderer, &p->surfaceRecipe);
                if (sk || !p->ready) renderer.renderMesh(&renderer, body);
                else if (i < 64u && EcosystemVisual_DeformBody(p, &cpuBodies[i])) renderer.renderMesh(&renderer, &cpuBodies[i]);
                OpenGLRenderer_SetMeshSkinning(&renderer, NULL, NULL, NULL, NULL, 0);
                OpenGLRenderer_PushJointMorphTransform(p->restPose, p->pose, p->headJoint, p->headScale);
                if (p->headMesh && p->headMesh->vertexCount) renderer.renderMesh(&renderer, p->headMesh);
                if (renderer.renderEye) {
                    for (size_t e = 0; e < p->eyeCount; ++e) {
                        OpenGLRenderer_ModelTransform(p->eyes[e].center, 0, p->eyeScale, p->eyes[e].center);
                        renderer.renderEye(&renderer, &p->eyes[e]);
                        OpenGLRenderer_PopModelMatrix();
                    }
                }
                OpenGLRenderer_PopModelMatrix();
                OpenGLRenderer_PushJointMorphTransform(p->restPose, p->pose, p->jawJoint, p->jawScale);
                for (size_t m = 0; m < p->mouthCount; ++m)
                    if (p->mouths[m].jaw.vertexCount) renderer.renderMesh(&renderer, &p->mouths[m].jaw);
                OpenGLRenderer_PopModelMatrix();
                OpenGLRenderer_SetMeshSurfaceOverride(&renderer, NULL);
                OpenGLRenderer_PopModelMatrix();
            }

            if (proceduralWorld) {
                EnvironmentRenderer_RenderWater(&envRenderer, &camera, (float)replay.world.clock.seconds);
                EnvironmentRenderer_RenderOverlays(&envRenderer, &camera);
            }
        }

        /* ================================================================== */
        /* Interfaz 2D del Brain Evolution Lab (Sección 33)                   */
        /* ================================================================== */
        OpenGLRenderer_Begin2D(winW, winH);

        /* Barra Superior de Controles */
        OpenGLRenderer_DrawRect2D(10.0f, 8.0f, (float)winW - 20.0f, 38.0f,
                                  Color_Create(8, 15, 22, 238), Color_Create(48, 154, 178, 225));
        OpenGLRenderer_DrawText2D(20.0f, 20.0f, 1.38f, Color_Create(84, 232, 246, 255),
                                  "BRAIN EVOLUTION LAB");

        float bx = 250.0f, by = 14.0f, bh = 26.0f;
        char spdLbl[24];
        snprintf(spdLbl, sizeof(spdLbl), "[F] %.0fX", effReplaySpeed);
        btnStart  = (LabUiButton){bx,          by, 108.0f, bh, snap->trainingActive ? "[SPC]PAUSE" : "[SPC]START"};
        btnStep   = (LabUiButton){bx + 112.0f, by, 80.0f,  bh, "[G]STEP"};
        btnStop   = (LabUiButton){bx + 196.0f, by, 76.0f,  bh, "[X]STOP"};
        btnVal    = (LabUiButton){bx + 276.0f, by, 78.0f,  bh, "[V]VAL"};
        btnSave   = (LabUiButton){bx + 358.0f, by, 88.0f,  bh, "SAVE CK"};
        btnLoad   = (LabUiButton){bx + 450.0f, by, 88.0f,  bh, "LOAD CK"};
        btnExport = (LabUiButton){bx + 542.0f, by, 92.0f,  bh, "[E]EXPORT"};
        btnReplay = (LabUiButton){bx + 638.0f, by, 92.0f,  bh, "[R]REPLAY"};
        btnNeural = (LabUiButton){bx + 734.0f, by, 96.0f,  bh, "[N]NEURAL"};
        btnSpeed  = (LabUiButton){bx + 834.0f, by, 88.0f,  bh, spdLbl};
        drawBtn(&btnStart, snap->trainingActive, true);
        drawBtn(&btnStep, false, false);
        drawBtn(&btnStop, snap->status == LAB_STATUS_MANUALLY_STOPPED, false);
        drawBtn(&btnVal, false, false);
        drawBtn(&btnSave, false, false);
        drawBtn(&btnLoad, false, false);
        drawBtn(&btnExport, false, true);
        drawBtn(&btnReplay, false, true);
        drawBtn(&btnNeural, showNeural, true);
        drawBtn(&btnSpeed, replaySpeedIdx > 0u, true);

        char topBadge[96];
        snprintf(topBadge, sizeof(topBadge), "%s  FPS:%2.0f", BrainLabStatus_Name(snap->status), displayedFps);
        OpenGLRenderer_DrawText2D((float)winW - 256.0f, 21.0f, 1.15f,
                                  snap->status == LAB_STATUS_CONVERGED ? Color_Create(104, 246, 168, 255)
                                                                       : Color_Create(248, 214, 106, 255),
                                  topBadge);

        /* Panel Izquierdo 1: Configuración del Experimento */
        const float lx = 10.0f, lw = 356.0f;
        const float cfgY = 54.0f, cfgH = 264.0f;
        OpenGLRenderer_DrawRect2D(lx, cfgY, lw, cfgH, Color_Create(9, 15, 22, 232), Color_Create(44, 138, 160, 215));
        OpenGLRenderer_DrawRect2D(lx, cfgY, lw, 24.0f, Color_Create(14, 30, 42, 245), Color_Create(44, 138, 160, 215));
        OpenGLRenderer_DrawText2D(lx + 10.0f, cfgY + 6.0f, 1.18f, Color_Create(84, 226, 240, 255),
                                  "EXPERIMENT CONFIGURATION");
        char cfgTxt[640];
        snprintf(cfgTxt, sizeof(cfgTxt),
                 "SPECIES : %.24s (REC #%u)\n"
                 "METABOL : %.26s\n"
                 "ENVIRON : %.26s\n"
                 "CURRIC  : STAGE %u / %u\n"
                 "ARCH    : CAND_POLICY_V2 (1416 PARAMS)\n"
                 "FOUNDERS: %s\n"
                 "POP/ELT : %u / %u   TOURN: %u\n"
                 "MUTATION: RATE %.2f SIGMA %.2f (%s)\n"
                 "WORKERS : %u   EP_LEN: %.0fS\n"
                 "SEEDS   : TR:%u  VAL:%u  TEST:%u\n"
                 "BUDGET  : MAX_GEN %u  TGT_VAL %.2f\n"
                 "OUTPUT  : %.28s",
                 snap->speciesName, snap->recipeId,
                 snap->profileName,
                 snap->currentEnvName,
                 snap->currentStage + 1u, snap->stageCount,
                 cfg.initialBrainPath[0] ? "INITIAL_BRAIN" : "RANDOM",
                 snap->populationSize, snap->eliteCount, cfg.tournamentSize,
                 cfg.mutation.mutationRate, cfg.mutation.mutationSigma,
                 cfg.mutationOnly ? "MUT-ONLY" : "CROSS+MUT",
                 snap->workerCount, curEnv.episodeSeconds,
                 cfg.trainingSeedsPerGen, cfg.validationSeeds, cfg.testSeeds,
                 cfg.maxGenerations, cfg.targetValidationFitness,
                 cfg.outputDir);
        OpenGLRenderer_DrawText2D(lx + 10.0f, cfgY + 32.0f, 1.05f, Color_Create(206, 230, 238, 255), cfgTxt);

        /* Panel Izquierdo 2: Estadísticas de Entrenamiento en Vivo */
        const float stY = cfgY + cfgH + 8.0f, stH = 290.0f;
        OpenGLRenderer_DrawRect2D(lx, stY, lw, stH, Color_Create(9, 15, 22, 232), Color_Create(76, 198, 154, 215));
        OpenGLRenderer_DrawRect2D(lx, stY, lw, 24.0f, Color_Create(14, 34, 34, 245), Color_Create(76, 198, 154, 215));
        OpenGLRenderer_DrawText2D(lx + 10.0f, stY + 6.0f, 1.18f, Color_Create(116, 238, 186, 255),
                                  "LIVE TRAINING STATISTICS");
        char stTxt[640];
        snprintf(stTxt, sizeof(stTxt),
                 "STATUS      : %s\n"
                 "TRAINER GEN : %u / %u   STAGE %u/%u\n"
                 "EVALUATED   : %llu EPS  (%llu TICKS)\n"
                 "BEST TRAIN  : %.4f   SURV: %5.1f%%\n"
                 "MEAN / MED  : %.4f / %.4f\n"
                 "VALIDATION  : %s\n"
                 "VAL FIT/SURV: %.4f / %5.1f%%\n"
                 "VAL HP/E/H  : %.2f / %.2f / %.2f\n"
                 "DIVERSITY   : %.4f\n"
                 "THROUGHPUT  : %.1f EPS/S  %.0f TICKS/S\n"
                 "ELAPSED     : %.2fS\n"
                 "KILLS/CARC  : %llu / %llu",
                 BrainLabStatus_Name(snap->status),
                 snap->currentGeneration, cfg.maxGenerations, snap->currentStage + 1u, snap->stageCount,
                 (unsigned long long)snap->totalEpisodesEvaluated,
                 (unsigned long long)snap->totalTicksSimulated,
                 snap->bestTrainFitness, snap->bestTrainSurvival * 100.0f,
                 snap->meanTrainFitness, snap->medianTrainFitness,
                 snap->hasValidation ? "MEASURED (DISJOINT SEEDS)" : "PENDING",
                 snap->bestValFitness, snap->bestValSurvival * 100.0f,
                 snap->bestValHealth, snap->bestValEnergy, snap->bestValHydration,
                 snap->neuralDiversity,
                 snap->lastEpsPerSec, snap->lastTicksPerSec,
                 snap->elapsedTrainingSeconds,
                 (unsigned long long)snap->lastTotalKills,
                 (unsigned long long)snap->lastTotalCarcasses);
        OpenGLRenderer_DrawText2D(lx + 10.0f, stY + 32.0f, 1.05f, Color_Create(210, 236, 242, 255), stTxt);

        /* Panel Izquierdo 3: Estado de Replay 3D */
        const float rpY = stY + stH + 8.0f, rpH = (float)winH - rpY - 42.0f;
        if (rpH >= 120.0f) {
            OpenGLRenderer_DrawRect2D(lx, rpY, lw, rpH, Color_Create(9, 15, 22, 232), Color_Create(216, 174, 82, 215));
            OpenGLRenderer_DrawRect2D(lx, rpY, lw, 24.0f, Color_Create(30, 26, 16, 245), Color_Create(216, 174, 82, 215));
            OpenGLRenderer_DrawText2D(lx + 10.0f, rpY + 6.0f, 1.15f, Color_Create(248, 216, 114, 255),
                                      "CHAMPION REPLAY VIEWPORT (ISOLATED)");
            if (replay.initialized) {
                const EcosystemEntity *fe = EcosystemWorld_Get(&replay.world, replay.focalId);
                char rTxt[256];
                const char *wTag = (replay.world.staticWorld && !replay.world.staticWorld->recipe.flat)
                                   ? (replay.world.staticWorld->recipe.id[0] ? replay.world.staticWorld->recipe.id : "TERRAIN")
                                   : "FLAT";
                snprintf(rTxt, sizeof(rTxt), "%.12s %s S:%u(%s) %.1f/%.0fX",
                         replay.loadedLabel, wTag, replay.episodeSeed,
                         replay.isRecordedEvalSeed ? "VAL" : "DMO",
                         replay.achievedSpeed, effReplaySpeed);
                OpenGLRenderer_DrawText2D(lx + 10.0f, rpY + 28.0f, 0.90f, Color_Create(226, 240, 246, 255), rTxt);
                if (fe) {
                    drawBar2D(lx + 10.0f, rpY + 48.0f, lw - 20.0f, 14.0f, "HP   ", fe->physiology.health, Color_Create(96, 214, 140, 235));
                    drawBar2D(lx + 10.0f, rpY + 64.0f, lw - 20.0f, 14.0f, "ENG  ", fe->physiology.energy, Color_Create(64, 210, 224, 235));
                    drawBar2D(lx + 10.0f, rpY + 80.0f, lw - 20.0f, 14.0f, "HYD  ", fe->physiology.hydration, Color_Create(78, 162, 238, 235));
                    drawBar2D(lx + 10.0f, rpY + 96.0f, lw - 20.0f, 14.0f, "TOX  ", fe->metabolism.toxicity, Color_Create(232, 74, 136, 240));
                    char actTxt[160];
                    snprintf(actTxt, sizeof(actTxt), "ACT:%-12s TGT:#%-3u ING:%.2f FULL:%.0f%%",
                             NeuralV2Action_Name(fe->mind.neuralState.v2.selectedAction),
                             fe->mind.neuralState.v2.selectedTargetId,
                             fe->metabolism.totalIngestedMass,
                             MetabolicState_Fullness(&fe->metabolism) * 100.0f);
                    OpenGLRenderer_DrawText2D(lx + 10.0f, rpY + 114.0f, 1.00f, Color_Create(174, 224, 238, 255), actTxt);
                }
            }
        }

        /* Panel Derecho 1: Gráficas de Evolución (Fitness, Validación, Supervivencia, Diversidad) */
        const float rw = 420.0f, rx = (float)winW - rw - 10.0f;
        const float plY = 54.0f, plH = 368.0f;
        OpenGLRenderer_DrawRect2D(rx, plY, rw, plH, Color_Create(9, 15, 22, 234), Color_Create(46, 152, 174, 220));
        OpenGLRenderer_DrawRect2D(rx, plY, rw, 24.0f, Color_Create(14, 30, 42, 245), Color_Create(46, 152, 174, 220));
        OpenGLRenderer_DrawText2D(rx + 10.0f, plY + 6.0f, 1.18f, Color_Create(84, 226, 240, 255),
                                  "FITNESS, SURVIVAL & DIVERSITY HISTORY");
        OpenGLRenderer_DrawText2D(rx + 10.0f, plY + 30.0f, 0.98f, Color_Create(196, 224, 234, 245),
                                  "CYAN:BEST  GRN:MEAN  AMB:MED  GOLD:VAL  VERT:STAGE");
        drawHistoryPlot(rx + 10.0f, plY + 46.0f, rw - 20.0f, 184.0f, snap->history, snap->historyCount);
        OpenGLRenderer_DrawText2D(rx + 10.0f, plY + 238.0f, 0.98f, Color_Create(196, 224, 234, 245),
                                  "EMERALD:SURVIVAL RATE   VIOLET:NEURAL DIVERSITY");
        drawSurvivalDiversityPlot(rx + 10.0f, plY + 254.0f, rw - 20.0f, 102.0f, snap->history, snap->historyCount);

        /* Panel Derecho 2: Inspector de Población de Candidatos */
        const float ciY = plY + plH + 8.0f, ciH = (float)winH - ciY - 42.0f;
        OpenGLRenderer_DrawRect2D(rx, ciY, rw, ciH, Color_Create(9, 15, 22, 234), Color_Create(144, 124, 226, 215));
        OpenGLRenderer_DrawRect2D(rx, ciY, rw, 24.0f, Color_Create(22, 20, 40, 245), Color_Create(144, 124, 226, 215));
        OpenGLRenderer_DrawText2D(rx + 10.0f, ciY + 6.0f, 1.18f, Color_Create(198, 174, 255, 255),
                                  "CANDIDATE POPULATION INSPECTOR [1-5]");
        static const char *slotLabels[CAND_SLOT_COUNT] = {"1:CHMP", "2:BTRN", "3:BVAL", "4:ELT2", "5:MED"};
        float sbw = (rw - 28.0f) / (float)CAND_SLOT_COUNT;
        for (unsigned s = 0; s < CAND_SLOT_COUNT; ++s) {
            btnSlots[s] = (LabUiButton){rx + 10.0f + (float)s * (sbw + 2.0f), ciY + 30.0f, sbw, 22.0f, slotLabels[s]};
            drawBtn(&btnSlots[s], selectedSlot == (LabCandidateSlot)s, false);
        }
        const LabCandidateSummary *cs = &snap->candidates[selectedSlot];
        if (cs->valid) {
            char cTxt[640];
            snprintf(cTxt, sizeof(cTxt),
                     "CANDIDATE  : %s  (GEN %u)\n"
                     "FINGERPRINT: %016llX\n"
                     "FITNESS    : %.4f +- %.4f (CI95)\n"
                     "RANGE      : [%.4f .. %.4f] STD %.4f\n"
                     "SURVIVAL   : %5.1f%%  (N=%u EPISODES)\n"
                     "MEAN HP/E/H: %.3f / %.3f / %.3f\n"
                     "MEAN TOXIC : %.3f   INGESTED: %.2f\n"
                     "ABSORBED   : E:%.2f   W:%.2f\n"
                     "KILLS:%llu  CARC_CONS:%llu\n"
                     "ACT CON:%llu ATT:%llu EXP:%llu\n"
                     "PRESS [R] TO REPLAY CANDIDATE IN 3D",
                     cs->title, cs->generation,
                     (unsigned long long)cs->fingerprint,
                     cs->metrics.meanFitness, cs->metrics.ci95HalfWidth,
                     cs->metrics.minFitness, cs->metrics.maxFitness, cs->metrics.stdFitness,
                     cs->metrics.survivalRate * 100.0f, cs->metrics.episodeCount,
                     cs->metrics.meanHealth, cs->metrics.meanEnergy, cs->metrics.meanHydration,
                     cs->metrics.meanToxicity, cs->metrics.meanIngestedMass,
                     cs->metrics.meanEnergyAbsorbed, cs->metrics.meanHydrationAbsorbed,
                     (unsigned long long)cs->metrics.totalKills,
                     (unsigned long long)cs->metrics.totalCarcassesConsumed,
                     (unsigned long long)cs->metrics.totalActionCounts[NEURAL_V2_ACTION_CONSUME_TARGET],
                     (unsigned long long)cs->metrics.totalActionCounts[NEURAL_V2_ACTION_ATTACK_TARGET],
                     (unsigned long long)cs->metrics.totalActionCounts[NEURAL_V2_ACTION_EXPLORE]);
            OpenGLRenderer_DrawText2D(rx + 10.0f, ciY + 60.0f, 1.05f, Color_Create(214, 234, 244, 255), cTxt);
        } else {
            OpenGLRenderer_DrawText2D(rx + 10.0f, ciY + 64.0f, 1.08f, Color_Create(154, 182, 196, 225),
                                      "CANDIDATE NOT AVAILABLE YET (RUN 1+ GEN OR VALIDATE)");
        }

        /* Visualizador gráfico de la Red Neuronal V2 del individuo en replay [N] */
        if (showNeural) {
            drawLabNeuralGraph(winW, winH, &replay, neuralEdgeThreshold, &btnWMinus, &btnWPlus);
        }

        /* Barra Inferior de Estado y Confirmaciones (Checkpoint / Export) */
        OpenGLRenderer_DrawRect2D(10.0f, (float)winH - 34.0f, (float)winW - 20.0f, 26.0f,
                                  snap->statusError ? Color_Create(58, 18, 22, 238) : Color_Create(10, 20, 28, 235),
                                  snap->statusError ? Color_Create(236, 92, 86, 235) : Color_Create(48, 152, 174, 210));
        OpenGLRenderer_DrawText2D(18.0f, (float)winH - 26.0f, 1.12f,
                                  snap->statusError ? Color_Create(255, 168, 160, 255) : Color_Create(126, 236, 244, 255),
                                  snap->statusMessage);

        OpenGLRenderer_End2D();

        if (capturePath && frames && frame + 1u == frames) {
            ensureDirPath(capturePath);
            captured = OpenGLRenderer_SavePPM(capturePath, winW, winH);
            ok = captured && ok;
        }

        renderer.endFrame(&renderer);
        OpenGLDemoWindow_Swap(window);
        if (!frames) OpenGLDemoWindow_LimitFPS(now, LAB_MAX_FPS);
        ++frame;
        if (captured) break;
    }

    ctrl->quitRequested = true;
    ctrl->stopFlag = true;
    if (ctrl->threadStarted) pthread_join(ctrl->thread, NULL);

    replayShutdown(&replay);
    EnvironmentRenderer_Destroy(&envRenderer);
    for (unsigned i = 0; i < 64u; ++i) Mesh_Free(&cpuBodies[i]);
    free(cpuBodies);
    free(packets);
    free(snap);
    for (unsigned i = 0; i < 5u; ++i) Mesh_Free(&meshes[i]);
    OpenGLRenderer_Destroy(&renderer);
    OpenGLDemoWindow_Free(window);
    BrainEvolutionLab_Destroy(&ctrl->lab);
    pthread_mutex_destroy(&ctrl->mutex);
    free(ctrl);
    return ok ? 0 : 1;
}
