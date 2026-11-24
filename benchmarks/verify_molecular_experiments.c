/**
 * @file verify_molecular_experiments.c
 * @brief Verificación cuantitativa controlada de los Experimentos 1..5 y 8..10 (Sección 40).
 */
#include "BrainEvolutionLab.h"
#include "CreatureMetabolism.h"
#include "CreatureRecipeRegistry.h"
#include "EvolutionArena.h"
#include "EvolutionEvaluator.h"
#include "MetabolismIO.h"
#include "NeuralBrainIO.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void runSingleSubstanceDigestTest(const MetabolismDatabase *db,
                                         const char *subId, const char *profId,
                                         float doseMass, unsigned steps, float dt,
                                         double *outE, double *outH, double *outNut,
                                         double *outDmg, float *outMaxTox, float *outMaxWaste,
                                         float *outMaxDisc, CreatureDeathCause *outCause) {
    SubstanceHandle sh = SubstanceRegistry_Find(&db->substances, subId);
    MetabolicProfileHandle ph = MetabolicProfileRegistry_Find(&db->profiles, profId);
    const SubstanceDef *sub = SubstanceRegistry_Get(&db->substances, sh);
    const MetabolicProfile *prof = MetabolicProfileRegistry_Get(&db->profiles, ph);
    if (!sub || !prof) return;

    PhysiologyProfile pp = EcosystemSpecies_Default(1u, ECO_PLANT, 0u, 0u, 0u).physiology;
    CreaturePhysiology phys = CreaturePhysiology_Create(40.0, 0.50f);
    phys.energy = 0.50f;
    phys.hydration = 0.50f;
    phys.health = 1.00f;

    MetabolicState st;
    CreatureMetabolism_Init(&st, prof, 0.50f);
    CreatureMetabolism_Ingest(&st, prof, sub->handle, sub->components, sub->componentCount, doseMass, 0.0);

    double totE = 0.0, totH = 0.0, totN = 0.0, totD = 0.0;
    float maxTox = 0.0f, maxW = 0.0f, maxDisc = 0.0f;
    for (unsigned s = 0; s < steps; ++s) {
        MetabolicStepOutcome r = {0};
        CreatureMetabolism_Step(&st, prof, &phys, &pp, dt, &r);
        totE += (double)r.energyGenerated - (double)r.digestiveEnergyCost;
        totH += (double)r.hydrationGenerated;
        totN += (double)r.nutrientsAbsorbed;
        totD += (double)r.metabolicDamage;
        if (st.toxicity > maxTox) maxTox = st.toxicity;
        if (st.wasteMass > maxW) maxW = st.wasteMass;
        if (st.digestiveDiscomfort > maxDisc) maxDisc = st.digestiveDiscomfort;
    }
    *outE = totE; *outH = totH; *outNut = totN; *outDmg = totD;
    *outMaxTox = maxTox; *outMaxWaste = maxW; *outMaxDisc = maxDisc;
    *outCause = phys.cause;
}

static void writeTextFile(const char *path, const char *content) {
    FILE *f = fopen(path, "w");
    if (!f) return;
    fputs(content, f);
    fclose(f);
}

int main(void) {
    char err[256] = {0};
    CreatureRecipeRegistry_LoadDirectory("recipes", err, sizeof(err));

    MetabolismDatabase db;
    if (!MetabolismIO_LoadDatabase("metabolism", &db, err, sizeof(err))) {
        fprintf(stderr, "Error cargando metabolismo: %s\n", err);
        return 1;
    }

    printf("========================================================================\n");
    printf("EXP 1 & 2: IDENTICAL SUBSTANCES ACROSS METABOLIC PROFILES (dose=1.0, 20s)\n");
    printf("========================================================================\n");
    const char *subs12[2] = {"grass", "fresh_meat"};
    const char *profs12[3] = {"brumacerno", "velkar", "omnivore_test"};
    for (int s = 0; s < 2; ++s) {
        for (int p = 0; p < 3; ++p) {
            double e, h, n, d;
            float mTox, mW, mDisc;
            CreatureDeathCause cause;
            runSingleSubstanceDigestTest(&db, subs12[s], profs12[p], 1.0f, 400, 0.05f,
                                         &e, &h, &n, &d, &mTox, &mW, &mDisc, &cause);
            printf("sub=%-12s prof=%-14s | netE=%+.4f netH=%+.4f nut=%.4f dmg=%.4f | maxTox=%.3f maxWaste=%.3f maxDisc=%.3f cause=%d\n",
                   subs12[s], profs12[p], e, h, n, d, mTox, mW, mDisc, (int)cause);
        }
    }

    printf("\n========================================================================\n");
    printf("EXP 3: MIXED SUBSTANCES (SIMULTANEOUS BENEFIT + TOXICITY + DEATH_TOXICITY)\n");
    printf("========================================================================\n");
    const char *mixedSubs[3] = {"contaminated_water", "rotten_meat", "toxic_plant"};
    for (int s = 0; s < 3; ++s) {
        double e, h, n, d;
        float mTox, mW, mDisc;
        CreatureDeathCause cause;
        runSingleSubstanceDigestTest(&db, mixedSubs[s], "brumacerno", 2.0f, 500, 0.05f,
                                     &e, &h, &n, &d, &mTox, &mW, &mDisc, &cause);
        printf("single_dose sub=%-18s prof=brumacerno | netE=%+.4f netH=%+.4f dmg=%.4f maxTox=%.3f cause=%d\n",
               mixedSubs[s], e, h, d, mTox, (int)cause);
    }
    /* Ingestión repetida de contaminated_water hasta DEATH_TOXICITY */
    {
        const SubstanceDef *cw = SubstanceRegistry_Get(&db.substances, SubstanceRegistry_Find(&db.substances, "contaminated_water"));
        const MetabolicProfile *bp = MetabolicProfileRegistry_Get(&db.profiles, MetabolicProfileRegistry_Find(&db.profiles, "brumacerno"));
        PhysiologyProfile pp = EcosystemSpecies_Default(1u, ECO_PLANT, 0u, 0u, 0u).physiology;
        CreaturePhysiology phys = CreaturePhysiology_Create(40.0, 0.80f);
        phys.hydration = 0.25f;
        MetabolicState st;
        CreatureMetabolism_Init(&st, bp, 0.50f);
        double totH = 0.0, totDmg = 0.0;
        unsigned tickDeath = 0u;
        for (unsigned t = 0; t < 2000u && !phys.cause; ++t) {
            if (t % 20u == 0u) CreatureMetabolism_Ingest(&st, bp, cw->handle, cw->components, cw->componentCount, 0.6f, t * 0.05);
            MetabolicStepOutcome r = {0};
            CreatureMetabolism_Step(&st, bp, &phys, &pp, 0.05f, &r);
            totH += r.hydrationGenerated;
            totDmg += r.metabolicDamage;
            if (phys.cause) tickDeath = t;
        }
        printf("sustained_toxic contaminated_water -> hydrationGain=+%.3f metaDmg=%.3f finalTox=%.3f deathTick=%u cause=%d (DEATH_TOXICITY=%d)\n",
               totH, totDmg, st.toxicity, tickDeath, (int)phys.cause, (int)DEATH_TOXICITY);
    }

    printf("\n========================================================================\n");
    printf("EXP 4: EXTERNAL MOLECULE & SUBSTANCE EXTENSIBILITY WITHOUT RECOMPILING\n");
    printf("========================================================================\n");
    {
        const char *tmpDir = "/tmp/monster_exp4_metabolism";
        char cmd[512];
        snprintf(cmd, sizeof(cmd), "rm -rf %s && mkdir -p %s/substances %s/profiles && cp metabolism/substances/*.json %s/substances/ && cp metabolism/profiles/*.json %s/profiles/",
                 tmpDir, tmpDir, tmpDir, tmpDir, tmpDir);
        if (system(cmd) == 0) {
            /* Copiar molecules.json añadiendo nueva molécula 'chitin' sin recompilar nada */
            FILE *src = fopen("metabolism/molecules.json", "r");
            if (src) {
                char buf[16384];
                size_t n = fread(buf, 1, sizeof(buf) - 1, src);
                buf[n] = '\0';
                fclose(src);
                char *pos = strrchr(buf, ']');
                if (pos) {
                    *pos = '\0';
                    char outPath[512];
                    snprintf(outPath, sizeof(outPath), "%s/molecules.json", tmpDir);
                    FILE *dst = fopen(outPath, "w");
                    if (dst) {
                        fprintf(dst,
                                "%s,\n"
                                "    {\n"
                                "      \"id\": \"chitin\",\n"
                                "      \"name\": \"Chitin\",\n"
                                "      \"description\": \"External test structural polysaccharide\",\n"
                                "      \"sensory_signature\": { \"moisture\": 0.05, \"plant_aroma\": 0.15, \"organic_aroma\": 0.55, \"lipid_aroma\": 0.10, \"sweetness\": 0.05, \"decay_aroma\": 0.0 }\n"
                                "    }\n"
                                "  ]\n}\n", buf);
                        fclose(dst);
                    }
                }
            }
            char subPath[512];
            snprintf(subPath, sizeof(subPath), "%s/substances/chitin_pod.substance.json", tmpDir);
            writeTextFile(subPath,
                "{\n"
                "  \"version\": 1,\n"
                "  \"id\": \"chitin_pod\",\n"
                "  \"name\": \"Chitin Pod\",\n"
                "  \"composition\": { \"water\": 0.25, \"chitin\": 0.55, \"proteins\": 0.20 },\n"
                "  \"physical_properties\": { \"phase\": \"solid\", \"hardness\": 0.65 },\n"
                "  \"presentation\": { \"model\": \"generic\", \"color\": [0.62, 0.48, 0.72] }\n"
                "}\n");
            /* Actualizar catalog.txt o dejar que nftw descubra */
            MetabolismDatabase extDb;
            if (MetabolismIO_LoadDatabase(tmpDir, &extDb, err, sizeof(err))) {
                MoleculeHandle hChitin = MoleculeRegistry_Find(&extDb.molecules, "chitin");
                SubstanceHandle hPod = SubstanceRegistry_Find(&extDb.substances, "chitin_pod");
                const SubstanceDef *pod = SubstanceRegistry_Get(&extDb.substances, hPod);
                double eB = 0, hB = 0, nB = 0, dB = 0;
                float mToxB = 0, mWB = 0, mDiscB = 0;
                CreatureDeathCause cB = DEATH_NONE;
                runSingleSubstanceDigestTest(&extDb, "chitin_pod", "brumacerno", 1.0f, 300, 0.05f,
                                             &eB, &hB, &nB, &dB, &mToxB, &mWB, &mDiscB, &cB);
                printf("ext_load=OK molecules=%u (chitin_handle=%u) substances=%u (chitin_pod_handle=%u)\n",
                       extDb.molecules.count, hChitin,
                       extDb.substances.count, hPod);
                printf("chitin_pod on brumacerno (unlisted chitin -> default unprocessable): netE=%+.4f netH=%+.4f maxWaste=%.3f\n",
                       eB, hB, mWB);
                /* Confirmar inferencia v2 sin cambiar esquema */
                NeuralGenome g2;
                uint32_t rng = 99u;
                NeuralGenome_InitRandomV2(&g2, &rng, 0.35f, 4.0f, 1u);
                NeuralBrainRuntime rt = {0};
                rt.architecture = NEURAL_ARCH_CANDIDATE_POLICY;
                rt.valid = true;
                float ctx[NEURAL_V2_CONTEXT_INPUTS] = {0};
                float rf[1][NEURAL_V2_RES_FEAT] = {{0}};
                for (unsigned k = 0; k < SENSORY_SIGNATURE_CHANNELS; ++k) rf[0][5 + k] = pod->cachedSignature.channels[k];
                float gs[NEURAL_V2_GLOBAL_ACTIONS], rs[NEURAL_V2_MAX_RESOURCE_CANDIDATES], cs[NEURAL_V2_MAX_CREATURE_CANDIDATES][NEURAL_V2_CRE_ACTIONS];
                bool evalOk = NeuralBrain_EvaluateV2(&g2, &rt, ctx, (const float (*)[NEURAL_V2_RES_FEAT])rf, 1u, NULL, 0u, gs, rs, cs);
                printf("v2_eval_with_external_molecule=%s paramCount=%u consumeScore=%.4f\n",
                       evalOk ? "OK" : "FAIL", NeuralGenome_ParamCount(&g2), rs[0]);
            } else {
                printf("ext_load=FAIL err=%s\n", err);
            }
        }
    }

    /* Cargar cerebros evolucionados reales */
    NeuralGenome brumBrain = {0}, velkScav = {0}, velkHunt = {0};
    bool hasBrum = NeuralBrain_LoadFile("brains/brumacerno_forager_v2.brain.json", &brumBrain, err, sizeof(err));
    bool hasScav = NeuralBrain_LoadFile("brains/velkar_scavenger.brain.json", &velkScav, err, sizeof(err));
    bool hasHunt = NeuralBrain_LoadFile("brains/velkar_hunter_v2.brain.json", &velkHunt, err, sizeof(err));
    printf("\nLoaded evolved brains: brum=%d scav=%d hunt=%d\n", hasBrum, hasScav, hasHunt);

    EvolutionSpeciesConfig spBrum, spVelk;
    EvolutionEnvironmentConfig envBrumMix, envVelkHunt, envGen;
    if (!EvolutionSpeciesConfig_LoadFile("species/brumacerno.species.json", &spBrum, err, sizeof(err)) ||
        !EvolutionSpeciesConfig_LoadFile("species/velkar.species.json", &spVelk, err, sizeof(err)) ||
        !EvolutionEnvironment_LoadFile("environments/brumacerno_mixed.environment.json", &envBrumMix, err, sizeof(err)) ||
        !EvolutionEnvironment_ValidateAgainstDatabase(&envBrumMix, &db, err, sizeof(err)) ||
        !EvolutionEnvironment_LoadFile("environments/velkar_hunting.environment.json", &envVelkHunt, err, sizeof(err)) ||
        !EvolutionEnvironment_ValidateAgainstDatabase(&envVelkHunt, &db, err, sizeof(err)) ||
        !EvolutionEnvironment_LoadFile("environments/molecular_generalization.environment.json", &envGen, err, sizeof(err)) ||
        !EvolutionEnvironment_ValidateAgainstDatabase(&envGen, &db, err, sizeof(err))) {
        fprintf(stderr, "Error cargando especies/entornos: %s\n", err);
        return 1;
    }

    EvolutionFitnessConfig fcfg = EvolutionFitnessConfig_Default();

    printf("\n========================================================================\n");
    printf("EXP 5: HUNTING CAUSALITY (ZERO FREE ATTACK ENERGY + INDEPENDENT CARCASS)\n");
    printf("========================================================================\n");
    if (hasHunt) {
        EcosystemWorld hw;
        AIObjectId fid = 0u;
        if (EvolutionEnvironment_BuildEpisodeWorld(&hw, &envVelkHunt, &spVelk, &db, &velkHunt, 7001u, false, &fid, err, sizeof(err))) {
            unsigned attackHitEvents = 0u, carcassesSpawned = 0u, carcassIngestEvents = 0u;
            double maxDirectEnergyOnHit = 0.0;
            unsigned ticks = (unsigned)(envVelkHunt.episodeSeconds / hw.clock.fixedStep);
            for (unsigned t = 0; t < ticks; ++t) {
                const EcosystemEntity *pre = EcosystemWorld_Get(&hw, fid);
                float preE = pre ? pre->physiology.energy : 0.0f;
                unsigned preRes = hw.activeResourceCount;
                EcosystemWorld_Step(&hw);
                const EcosystemEntity *post = EcosystemWorld_Get(&hw, fid);
                if (!post || post->physiology.cause) break;
                if (post->mind.neuralState.lastOutcome == NEURAL_OUTCOME_ATTACK_HIT &&
                    fabs(post->mind.neuralState.lastOutcomeTime - hw.clock.seconds) < 0.06) {
                    ++attackHitEvents;
                    double dE = (double)post->physiology.energy - (double)preE;
                    if (dE > maxDirectEnergyOnHit) maxDirectEnergyOnHit = dE;
                }
                if (hw.activeResourceCount > preRes) {
                    for (unsigned r = 0; r < hw.activeResourceCount; ++r) {
                        const EcosystemResource *res = &hw.resources[hw.activeResourceSlots[r]];
                        if (res->killerCreatureId == fid && fabs(res->deathTime - hw.clock.seconds) < 0.06) {
                            ++carcassesSpawned;
                        }
                    }
                }
                if (post->metabolism.lastIngestedSubstance != SUBSTANCE_INVALID_HANDLE) {
                    const SubstanceDef *ls = SubstanceRegistry_Get(&db.substances, post->metabolism.lastIngestedSubstance);
                    if (ls && strstr(ls->id, "tissue")) ++carcassIngestEvents;
                }
            }
            const EcosystemEntity *endE = EcosystemWorld_Get(&hw, fid);
            printf("velkar_hunter seed=7001 | attackHits=%u maxDirectEnergyDeltaOnHit=%+.5f | carcassesSpawned=%u | tissueIngestTicks=%u | totalIngested=%.2f E_gen=%.2f alive=%d\n",
                   attackHitEvents, maxDirectEnergyOnHit, carcassesSpawned, carcassIngestEvents,
                   endE ? endE->metabolism.totalIngestedMass : 0.0,
                   endE ? endE->metabolism.totalEnergyGenerated : 0.0,
                   (endE && !endE->physiology.cause) ? 1 : 0);
            EcosystemWorld_Free(&hw);
        }
    }

    printf("\n========================================================================\n");
    printf("EXP 8: CROSS-SPECIES BRAIN TRANSFER (10 EPISODES EACH)\n");
    printf("========================================================================\n");
    if (hasBrum && hasHunt) {
        EvolutionEpisodeMetrics epNativeB[10], epXferVonB[10], epNativeV[10], epXferBonV[10];
        for (unsigned k = 0; k < 10u; ++k) {
            uint32_t sd = 9000u + k * 13u;
            EvolutionEvaluator_RunEpisode(&envBrumMix, &spBrum, &db, &brumBrain, &fcfg, sd, false, &epNativeB[k], err, sizeof(err));
            EvolutionEvaluator_RunEpisode(&envBrumMix, &spBrum, &db, &velkHunt,  &fcfg, sd, false, &epXferVonB[k], err, sizeof(err));
            EvolutionEvaluator_RunEpisode(&envVelkHunt, &spVelk, &db, &velkHunt, &fcfg, sd, false, &epNativeV[k], err, sizeof(err));
            EvolutionEvaluator_RunEpisode(&envVelkHunt, &spVelk, &db, &brumBrain,&fcfg, sd, false, &epXferBonV[k], err, sizeof(err));
        }
        EvolutionAggregateMetrics mNB, mVB, mNV, mBV;
        EvolutionFitness_Aggregate(epNativeB, 10u, &mNB);
        EvolutionFitness_Aggregate(epXferVonB, 10u, &mVB);
        EvolutionFitness_Aggregate(epNativeV, 10u, &mNV);
        EvolutionFitness_Aggregate(epXferBonV, 10u, &mBV);
        printf("Brumacerno body + Brumacerno brain (native) : fit=%.4f+-%.4f surv=%5.1f%% ing=%.2f E=%.2f tox=%.3f kills=%llu\n",
               mNB.meanFitness, mNB.ci95HalfWidth, mNB.survivalRate * 100.0f, mNB.meanIngestedMass, mNB.meanEnergyAbsorbed, mNB.meanToxicity, (unsigned long long)mNB.totalKills);
        printf("Brumacerno body + Velkar brain    (transfer): fit=%.4f+-%.4f surv=%5.1f%% ing=%.2f E=%.2f tox=%.3f kills=%llu\n",
               mVB.meanFitness, mVB.ci95HalfWidth, mVB.survivalRate * 100.0f, mVB.meanIngestedMass, mVB.meanEnergyAbsorbed, mVB.meanToxicity, (unsigned long long)mVB.totalKills);
        printf("Velkar body     + Velkar brain    (native)  : fit=%.4f+-%.4f surv=%5.1f%% ing=%.2f E=%.2f tox=%.3f kills=%llu carc=%llu\n",
               mNV.meanFitness, mNV.ci95HalfWidth, mNV.survivalRate * 100.0f, mNV.meanIngestedMass, mNV.meanEnergyAbsorbed, mNV.meanToxicity, (unsigned long long)mNV.totalKills, (unsigned long long)mNV.totalCarcassesConsumed);
        printf("Velkar body     + Brumacerno brain(transfer): fit=%.4f+-%.4f surv=%5.1f%% ing=%.2f E=%.2f tox=%.3f kills=%llu carc=%llu\n",
               mBV.meanFitness, mBV.ci95HalfWidth, mBV.survivalRate * 100.0f, mBV.meanIngestedMass, mBV.meanEnergyAbsorbed, mBV.meanToxicity, (unsigned long long)mBV.totalKills, (unsigned long long)mBV.totalCarcassesConsumed);
    }

    printf("\n========================================================================\n");
    printf("EXP 9: GENERALIZATION ON UNSEEN MIXED ENVIRONMENT (10 EPISODES)\n");
    printf("========================================================================\n");
    if (hasBrum && hasScav) {
        EvolutionEpisodeMetrics epGenB[10], epGenV[10], epBaseIdle[10], epBaseNear[10];
        for (unsigned k = 0; k < 10u; ++k) {
            uint32_t sd = 12000u + k * 17u;
            EvolutionEvaluator_RunEpisode(&envGen, &spBrum, &db, &brumBrain, &fcfg, sd, false, &epGenB[k], err, sizeof(err));
            EvolutionEvaluator_RunEpisode(&envGen, &spVelk, &db, &velkScav,  &fcfg, sd, false, &epGenV[k], err, sizeof(err));
            EvolutionEvaluator_RunBaselineEpisode(&envGen, &spBrum, &db, EVO_BASELINE_ALWAYS_IDLE, &fcfg, sd, false, &epBaseIdle[k], err, sizeof(err));
            EvolutionEvaluator_RunBaselineEpisode(&envGen, &spBrum, &db, EVO_BASELINE_CONSUME_NEAREST, &fcfg, sd, false, &epBaseNear[k], err, sizeof(err));
        }
        EvolutionAggregateMetrics mGB, mGV, mGI, mGN;
        EvolutionFitness_Aggregate(epGenB, 10u, &mGB);
        EvolutionFitness_Aggregate(epGenV, 10u, &mGV);
        EvolutionFitness_Aggregate(epBaseIdle, 10u, &mGI);
        EvolutionFitness_Aggregate(epBaseNear, 10u, &mGN);
        printf("molecular_generalization | Brumacerno evolved  : fit=%.4f+-%.4f surv=%5.1f%% HP=%.2f E=%.2f H=%.2f tox=%.3f\n",
               mGB.meanFitness, mGB.ci95HalfWidth, mGB.survivalRate * 100.0f, mGB.meanHealth, mGB.meanEnergy, mGB.meanHydration, mGB.meanToxicity);
        printf("molecular_generalization | Velkar scav evolved : fit=%.4f+-%.4f surv=%5.1f%% HP=%.2f E=%.2f H=%.2f tox=%.3f\n",
               mGV.meanFitness, mGV.ci95HalfWidth, mGV.survivalRate * 100.0f, mGV.meanHealth, mGV.meanEnergy, mGV.meanHydration, mGV.meanToxicity);
        printf("molecular_generalization | Baseline idle (Brum): fit=%.4f+-%.4f surv=%5.1f%% HP=%.2f E=%.2f H=%.2f tox=%.3f\n",
               mGI.meanFitness, mGI.ci95HalfWidth, mGI.survivalRate * 100.0f, mGI.meanHealth, mGI.meanEnergy, mGI.meanHydration, mGI.meanToxicity);
        printf("molecular_generalization | Baseline near (Brum): fit=%.4f+-%.4f surv=%5.1f%% HP=%.2f E=%.2f H=%.2f tox=%.3f\n",
               mGN.meanFitness, mGN.ci95HalfWidth, mGN.survivalRate * 100.0f, mGN.meanHealth, mGN.meanEnergy, mGN.meanHydration, mGN.meanToxicity);
    }

    printf("\n========================================================================\n");
    printf("EXP 10: EVOLUTION ARENA NATURAL SELECTION WITH EVOLVED V2 BRAINS (3 DAYS)\n");
    printf("========================================================================\n");
    if (hasBrum && hasHunt) {
        EvolutionArenaConfig ac = EvolutionArenaConfig_DefaultMolecular();
        ac.seed = 42u;
        ac.brumacernos = 20u;
        ac.velkares = 8u;
        ac.capacity = 80u;
        ac.arenaSize = 68.0f;
        ac.plants = 70u;
        ac.ponds = 5u;
        ac.includeMixedSubstances = true;
        ac.founderMode = EVOLUTION_FOUNDERS_IMPORTED;
        ac.brumacernoBrainPath = "brains/brumacerno_forager_v2.brain.json";
        ac.velkarBrainPath = "brains/velkar_hunter_v2.brain.json";
        EcosystemWorld aw;
        if (EvolutionArena_Create(&aw, &ac, err, sizeof(err))) {
            unsigned totalTicks = (unsigned)(8.0 * ac.day / ac.step);
            for (unsigned t = 0; t < totalTicks; ++t) EcosystemWorld_Step(&aw);
            unsigned bAlive = 0, vAlive = 0;
            for (unsigned i = 0; i < aw.count; ++i) {
                const EcosystemEntity *e = &aw.entities[aw.activeEntitySlots[i]];
                if (e->speciesIndex == 0u) ++bAlive;
                else ++vAlive;
            }
            printf("arena_8days | alive=%u (Brum=%u Velk=%u) | births=%llu deaths=%llu (starv=%llu dehyd=%llu pred=%llu tox=%llu) | maxGen=%u div=%.4f | ingested=%.1f\n",
                   aw.count, bAlive, vAlive,
                   (unsigned long long)aw.statistics.births,
                   (unsigned long long)aw.statistics.deaths,
                   (unsigned long long)aw.statistics.deathCauses[DEATH_STARVATION],
                   (unsigned long long)aw.statistics.deathCauses[DEATH_DEHYDRATION],
                   (unsigned long long)aw.statistics.deathCauses[DEATH_PREDATION],
                   (unsigned long long)aw.statistics.deathCauses[DEATH_TOXICITY],
                   aw.statistics.maximumGeneration, aw.statistics.neuralDiversity,
                   aw.statistics.totalIngestedMass);
            EcosystemWorld_Free(&aw);
        } else {
            printf("arena_create=FAIL err=%s\n", err);
        }
    }

    return 0;
}
