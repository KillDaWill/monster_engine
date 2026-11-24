/**
 * @file test_molecular_metabolism.c
 * @brief Pruebas unitarias e integración de registros moleculares, IO estricto y digestión progresiva.
 */
#include "test_utils.h"
#include "MetabolismIO.h"
#include "CreatureMetabolism.h"
#include "EcosystemResources.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static void test_registry_loading_and_validation(void) {
    MetabolismDatabase db;
    char err[256] = {0};
    TEST_ASSERT(MetabolismIO_LoadDatabase("metabolism", &db, err, sizeof(err)),
                "carga completa de metabolism/");
    TEST_ASSERT(db.loaded && db.frozen, "base metabólica cargada y congelada");
    TEST_ASSERT(db.molecules.count >= 9u, "al menos 9 moléculas base");
    TEST_ASSERT(db.substances.count >= 8u, "al menos 8 sustancias base");
    TEST_ASSERT(db.profiles.count >= 3u, "perfiles brumacerno, velkar y omnivore_test");

    TEST_ASSERT(MoleculeRegistry_Find(&db.molecules, "water") != MOLECULE_INVALID_HANDLE, "water");
    TEST_ASSERT(MoleculeRegistry_Find(&db.molecules, "cellulose") != MOLECULE_INVALID_HANDLE, "cellulose");
    TEST_ASSERT(MoleculeRegistry_Find(&db.molecules, "organic_toxins") != MOLECULE_INVALID_HANDLE, "organic_toxins");
    TEST_ASSERT(SubstanceRegistry_Find(&db.substances, "grass") != SUBSTANCE_INVALID_HANDLE, "grass");
    TEST_ASSERT(SubstanceRegistry_Find(&db.substances, "fresh_meat") != SUBSTANCE_INVALID_HANDLE, "fresh_meat");
    TEST_ASSERT(SubstanceRegistry_Find(&db.substances, "clean_water") != SUBSTANCE_INVALID_HANDLE, "clean_water");
    TEST_ASSERT(SubstanceRegistry_Find(&db.substances, "fruit") != SUBSTANCE_INVALID_HANDLE, "fruit");
    TEST_ASSERT(SubstanceRegistry_Find(&db.substances, "rotten_meat") != SUBSTANCE_INVALID_HANDLE, "rotten_meat");
    TEST_ASSERT(SubstanceRegistry_Find(&db.substances, "contaminated_water") != SUBSTANCE_INVALID_HANDLE, "contaminated_water");
    TEST_ASSERT(MetabolicProfileRegistry_Find(&db.profiles, "brumacerno") != METABOLIC_PROFILE_INVALID_HANDLE, "brumacerno");
    TEST_ASSERT(MetabolicProfileRegistry_Find(&db.profiles, "velkar") != METABOLIC_PROFILE_INVALID_HANDLE, "velkar");
    TEST_ASSERT(MetabolicProfileRegistry_Find(&db.profiles, "omnivore_test") != METABOLIC_PROFILE_INVALID_HANDLE, "omnivore_test");

    /* Registro congelado rechaza mutaciones. */
    MoleculeDef extra = {0};
    snprintf(extra.id, sizeof(extra.id), "extra_mol");
    TEST_ASSERT(!MoleculeRegistry_Register(&db.molecules, &extra, NULL, err, sizeof(err)),
                "registro congelado rechaza nuevas moléculas");

    /* Errores estrictos: versión, duplicado, referencia desconocida, suma != 1, JSON malformado. */
    MoleculeRegistry mr;
    MoleculeRegistry_Init(&mr);
    const char *badVer = "{\"version\":99,\"molecules\":[]}";
    TEST_ASSERT(!MetabolismIO_LoadMoleculesJSON(badVer, strlen(badVer), &mr, err, sizeof(err)),
                "rechaza versión no soportada");

    const char *dupMol = "{\"version\":1,\"molecules\":["
                         "{\"id\":\"a\",\"sensory_signature\":{\"moisture\":0.5}},"
                         "{\"id\":\"a\",\"sensory_signature\":{\"moisture\":0.5}}]}";
    TEST_ASSERT(!MetabolismIO_LoadMoleculesJSON(dupMol, strlen(dupMol), &mr, err, sizeof(err)),
                "rechaza IDs moleculares duplicados");

    const char *malformed = "{\"version\":1,\"molecules\":[{";
    TEST_ASSERT(!MetabolismIO_LoadMoleculesJSON(malformed, strlen(malformed), &mr, err, sizeof(err)),
                "rechaza JSON malformado");

    const char *goodMols = "{\"version\":1,\"molecules\":["
                           "{\"id\":\"water\",\"sensory_signature\":{\"moisture\":1.0}},"
                           "{\"id\":\"chitin\",\"name\":\"Chitin\",\"sensory_signature\":{\"organic_aroma\":0.5}}]}";
    TEST_ASSERT(MetabolismIO_LoadMoleculesJSON(goodMols, strlen(goodMols), &mr, err, sizeof(err)),
                "carga molécula nueva sin recompilar (chitin)");
    TEST_ASSERT(MoleculeRegistry_Find(&mr, "chitin") != MOLECULE_INVALID_HANDLE,
                "chitin registrada dinámicamente");

    SubstanceRegistry sr;
    SubstanceRegistry_Init(&sr, &mr);
    const char *badRef = "{\"version\":1,\"id\":\"s1\",\"composition\":{\"unknown_mol\":1.0}}";
    TEST_ASSERT(!MetabolismIO_LoadSubstanceJSON(badRef, strlen(badRef), &sr, NULL, err, sizeof(err)),
                "rechaza referencia molecular desconocida");

    const char *badSum = "{\"version\":1,\"id\":\"s2\",\"composition\":{\"water\":0.5,\"chitin\":0.2}}";
    TEST_ASSERT(!MetabolismIO_LoadSubstanceJSON(badSum, strlen(badSum), &sr, NULL, err, sizeof(err)),
                "rechaza fracciones cuya suma difiere de 1");

    const char *goodSub = "{\"version\":1,\"id\":\"beetle_shell\",\"composition\":{\"water\":0.3,\"chitin\":0.7},"
                          "\"physical_properties\":{\"phase\":\"solid\",\"hardness\":0.8},"
                          "\"presentation\":{\"model\":\"generic\",\"color\":[0.2,0.2,0.1]}}";
    SubstanceHandle beetleH = SUBSTANCE_INVALID_HANDLE;
    TEST_ASSERT(MetabolismIO_LoadSubstanceJSON(goodSub, strlen(goodSub), &sr, &beetleH, err, sizeof(err)),
                "carga sustancia nueva con molécula nueva sin recompilar");
    const SubstanceDef *beetle = SubstanceRegistry_Get(&sr, beetleH);
    TEST_ASSERT(beetle != NULL && beetle->componentCount == 2u, "sustancia compilada");
    TEST_ASSERT(FLOAT_NEAR(beetle->cachedSignature.channels[SENSORY_MOISTURE], 0.3f),
                "firma sensorial moisture ponderada");
    TEST_ASSERT(FLOAT_NEAR(beetle->cachedSignature.channels[SENSORY_ORGANIC_AROMA], 0.35f),
                "firma sensorial organic_aroma ponderada");
}

static void simulateDigestion(const MetabolicProfile *prof, const SubstanceDef *sub,
                              float massToIngest, float simSeconds,
                              CreaturePhysiology *outPhys, MetabolicState *outState) {
    PhysiologyProfile pp = CreaturePhysiology_Default();
    CreaturePhysiology phys = CreaturePhysiology_Create(200.0, 0.30f);
    phys.hydration = 0.40f;
    MetabolicState st;
    CreatureMetabolism_Init(&st, prof, 0.40f);

    /* Ingerir en pasos pequeños respetando la tasa física máxima (0 recompensa inmediata). */
    float dt = 1.0f / 60.0f;
    float remaining = massToIngest;
    float energyBeforeIngest = phys.energy;
    float hydBeforeIngest = phys.hydration;

    while (remaining > 1e-5f) {
        float acc = CreatureMetabolism_MaxAcceptableMass(&st, prof, remaining, dt);
        if (acc <= 1e-7f) break;
        CreatureMetabolism_Ingest(&st, prof, sub->handle, sub->components, sub->componentCount, acc, 0.0);
        remaining -= acc;
        /* Comprobar que la ingestión pura NO otorga energía ni hidratación inmediata. */
        if (remaining > massToIngest - 0.1f) {
            TEST_ASSERT(FLOAT_NEAR(phys.energy, energyBeforeIngest),
                        "ingestión no entrega energía instantánea");
            TEST_ASSERT(FLOAT_NEAR(phys.hydration, hydBeforeIngest),
                        "ingestión no entrega hidratación instantánea");
        }
    }

    int steps = (int)(simSeconds / dt);
    for (int i = 0; i < steps && phys.cause == DEATH_NONE; ++i) {
        CreatureMetabolism_Step(&st, prof, &phys, &pp, dt, NULL);
    }
    *outPhys = phys;
    *outState = st;
}

static void test_species_digestive_divergence_and_mixed_substances(void) {
    MetabolismDatabase db;
    char err[256] = {0};
    TEST_ASSERT(MetabolismIO_LoadDatabase("metabolism", &db, err, sizeof(err)), "cargar db");

    const MetabolicProfile *brum = MetabolicProfileRegistry_Get(
        &db.profiles, MetabolicProfileRegistry_Find(&db.profiles, "brumacerno"));
    const MetabolicProfile *velk = MetabolicProfileRegistry_Get(
        &db.profiles, MetabolicProfileRegistry_Find(&db.profiles, "velkar"));
    const SubstanceDef *grass = SubstanceRegistry_Get(
        &db.substances, SubstanceRegistry_Find(&db.substances, "grass"));
    const SubstanceDef *meat = SubstanceRegistry_Get(
        &db.substances, SubstanceRegistry_Find(&db.substances, "fresh_meat"));
    const SubstanceDef *rotten = SubstanceRegistry_Get(
        &db.substances, SubstanceRegistry_Find(&db.substances, "rotten_meat"));
    const SubstanceDef *bark = SubstanceRegistry_Get(
        &db.substances, SubstanceRegistry_Find(&db.substances, "woody_bark"));
    const SubstanceDef *contWater = SubstanceRegistry_Get(
        &db.substances, SubstanceRegistry_Find(&db.substances, "contaminated_water"));

    /* Experimento 1: idéntica cantidad de hierba (1.5 u_m). */
    CreaturePhysiology brumGrassP, velkGrassP;
    MetabolicState brumGrassS, velkGrassS;
    simulateDigestion(brum, grass, 1.5f, 40.0f, &brumGrassP, &brumGrassS);
    simulateDigestion(velk, grass, 1.5f, 40.0f, &velkGrassP, &velkGrassS);
    TEST_ASSERT(brumGrassS.totalEnergyGenerated > velkGrassS.totalEnergyGenerated * 3.0,
                "Brumacerno extrae sustancialmente más energía de grass que Velkar");
    TEST_ASSERT(velkGrassS.totalHydrationGenerated > 0.25,
                "Velkar aún absorbe agua útil contenida en grass");

    /* Experimento 2: idéntica cantidad de carne fresca (1.5 u_m). */
    CreaturePhysiology brumMeatP, velkMeatP;
    MetabolicState brumMeatS, velkMeatS;
    simulateDigestion(brum, meat, 1.5f, 40.0f, &brumMeatP, &brumMeatS);
    simulateDigestion(velk, meat, 1.5f, 40.0f, &velkMeatP, &velkMeatS);
    TEST_ASSERT(velkMeatS.totalEnergyGenerated > brumMeatS.totalEnergyGenerated * 4.0,
                "Velkar extrae sustancialmente más energía de fresh_meat que Brumacerno");
    TEST_ASSERT(brumMeatS.totalIngestedMass > 1.4f,
                "Brumacerno pudo ingerir carne físicamente sin rechazo categórico");

    /* Experimento 3: sustancia mixta (rotten_meat) produce beneficios y efectos tóxicos simultáneos. */
    CreaturePhysiology brumRotP, velkRotP;
    MetabolicState brumRotS, velkRotS;
    simulateDigestion(brum, rotten, 1.2f, 30.0f, &brumRotP, &brumRotS);
    simulateDigestion(velk, rotten, 1.2f, 30.0f, &velkRotP, &velkRotS);
    TEST_ASSERT(velkRotS.totalEnergyGenerated > 0.10 && velkRotS.totalToxinExposure > 0.05,
                "sustancia mixta produce simultáneamente energía y toxicidad");
    TEST_ASSERT(brumRotS.totalToxinExposure > velkRotS.totalToxinExposure,
                "Brumacerno sufre mayor carga tóxica por rotten_meat que Velkar");

    /* Material indigestible (woody_bark) no bloquea permanentemente el estómago. */
    CreaturePhysiology barkP;
    MetabolicState barkS;
    simulateDigestion(velk, bark, 1.0f, 60.0f, &barkP, &barkS);
    TEST_ASSERT(barkS.digestiveMass < 0.05f && barkS.wasteMass < 0.05f,
                "material indigestible transita y se elimina como residuo sin bloquear estómago");
    TEST_ASSERT(barkS.totalWasteEliminated > 0.6f, "residuos eliminados registrados");

    /* Toxicidad letal produce DEATH_TOXICITY (no DEATH_STARVATION). */
    PhysiologyProfile pp = CreaturePhysiology_Default();
    CreaturePhysiology toxPhys = CreaturePhysiology_Create(200.0, 0.90f);
    toxPhys.hydration = 0.90f;
    MetabolicState toxState;
    CreatureMetabolism_Init(&toxState, brum, 0.80f);
    float dt = 1.0f / 60.0f;
    for (int i = 0; i < 60 * 120 && toxPhys.cause == DEATH_NONE; ++i) {
        if (i % 15 == 0) {
            float acc = CreatureMetabolism_MaxAcceptableMass(&toxState, brum, 0.35f, dt * 15.0f);
            if (acc > 0.0f) {
                CreatureMetabolism_Ingest(&toxState, brum, contWater->handle,
                                          contWater->components, contWater->componentCount, acc, i * dt);
            }
        }
        CreatureMetabolism_Step(&toxState, brum, &toxPhys, &pp, dt, NULL);
    }
    TEST_ASSERT(toxPhys.cause == DEATH_TOXICITY,
                "intoxicación severa clasifica causa como DEATH_TOXICITY y no starvation");
    TEST_ASSERT(toxState.totalMetabolicDamage >= 0.99, "daño metabólico acumulado registrado");
}

static void test_explicit_transformation_mass_conservation(void) {
    MoleculeRegistry mr;
    MoleculeRegistry_Init(&mr);
    char err[256] = {0};
    const char *mols = "{\"version\":1,\"molecules\":["
                       "{\"id\":\"starch\",\"sensory_signature\":{\"plant_aroma\":0.5}},"
                       "{\"id\":\"glucose\",\"sensory_signature\":{\"sweetness\":0.9}}]}";
    TEST_ASSERT(MetabolismIO_LoadMoleculesJSON(mols, strlen(mols), &mr, err, sizeof(err)), "cargar mols");
    MoleculeRegistry_Freeze(&mr);

    MetabolicProfileRegistry pr;
    MetabolicProfileRegistry_Init(&pr, &mr);

    /* Regla que viola conservación de masa (> 1.0) debe rechazarse. */
    const char *badProf = "{\"version\":1,\"id\":\"bad_mass\",\"processing_rules\":["
                          "{\"molecule\":\"starch\",\"rate\":0.5,\"efficiency\":1.0,\"waste_fraction\":0.4,"
                          "\"outputs\":{\"glucose\":0.8}}]}";
    TEST_ASSERT(!MetabolismIO_LoadProfileJSON(badProf, strlen(badProf), &pr, NULL, err, sizeof(err)),
                "rechaza transformación que crea masa");

    /* Regla válida: starch -> 0.8 glucose + 0.2 waste, luego glucose -> energía. */
    const char *goodProf = "{\"version\":1,\"id\":\"good_mass\",\"processing_rules\":["
                           "{\"molecule\":\"starch\",\"rate\":0.5,\"efficiency\":1.0,\"waste_fraction\":0.2,"
                           "\"outputs\":{\"glucose\":0.8}},"
                           "{\"molecule\":\"glucose\",\"rate\":0.5,\"efficiency\":1.0,"
                           "\"effects\":{\"energy\":0.5}}]}";
    MetabolicProfileHandle ph = METABOLIC_PROFILE_INVALID_HANDLE;
    TEST_ASSERT(MetabolismIO_LoadProfileJSON(goodProf, strlen(goodProf), &pr, &ph, err, sizeof(err)),
                "acepta transformación conservativa de masa");
    const MetabolicProfile *prof = MetabolicProfileRegistry_Get(&pr, ph);
    MoleculeHandle hStarch = MoleculeRegistry_Find(&mr, "starch");
    SubstanceComponent comp = {.molecule = hStarch, .fraction = 1.0f};

    PhysiologyProfile pp = CreaturePhysiology_Default();
    CreaturePhysiology phys = CreaturePhysiology_Create(200.0, 0.2f);
    MetabolicState st;
    CreatureMetabolism_Init(&st, prof, 0.5f);
    CreatureMetabolism_Ingest(&st, prof, 0u, &comp, 1u, 1.0f, 0.0);
    for (int i = 0; i < 60 * 10; ++i) {
        CreatureMetabolism_Step(&st, prof, &phys, &pp, 1.0f / 60.0f, NULL);
    }
    TEST_ASSERT(fabsf((float)st.totalEnergyGenerated - 0.4f) < 0.01f,
                "starch (1.0) -> 0.8 glucose -> 0.4 energía con conservación exacta");
    TEST_ASSERT(st.digestiveMass < 1e-4f, "todo el sustrato e intermediario procesados");
}

static void test_deplete_relocate_and_rarity(void) {
    MetabolismDatabase db;
    char err[256] = {0};
    TEST_ASSERT(MetabolismIO_LoadDatabase("metabolism", &db, err, sizeof(err)), "cargar db para relocalización");
    const SubstanceDef *fruit = SubstanceRegistry_Get(&db.substances, SubstanceRegistry_Find(&db.substances, "fruit"));
    TEST_ASSERT(fruit != NULL, "sustancia fruit disponible");

    EcosystemResource r = EcosystemResource_FromSubstance(
        fruit, 1.5f, 1.5f, RESOURCE_REGEN_DEPLETE_RELOCATE, 0.0f,
        RESOURCE_DECAY_NONE, 0.0, 0.0f, MOLECULE_INVALID_HANDLE);
    r.id = 77u;
    r.respawnDelaySeconds = 5.0f;
    r.rarityWeight = 1.0f;
    r.minRelocateDistance = 8.0f;

    /* No regenera in-situ mientras tiene masa parcial. */
    float taken = EcosystemResource_Consume(&r, 0.5f);
    TEST_ASSERT(fabsf(taken - 0.5f) < 1e-4f, "consumo parcial exacto");
    EcosystemResource_UpdateMolecular(&r, &db.molecules, 2.0f);
    TEST_ASSERT(fabsf(r.biomass - 1.0f) < 1e-4f, "deplete_relocate nunca regenera masa en el mismo sitio");

    /* Al agotar completamente: desaparece (Available == false) y arranca temporizador. */
    taken = EcosystemResource_Consume(&r, 1.5f);
    TEST_ASSERT(fabsf(taken - 1.0f) < 1e-4f, "consume resto exacto");
    TEST_ASSERT(r.depleted && !EcosystemResource_Available(&r), "recurso agotado deja de estar disponible");
    TEST_ASSERT(!r.needsRelocation, "no relocaliza antes de cumplir el retardo");

    EcosystemResource_UpdateMolecular(&r, &db.molecules, 3.0f);
    TEST_ASSERT(r.depleted && !r.needsRelocation, "sigue agotado a mitad del retardo");
    EcosystemResource_UpdateMolecular(&r, &db.molecules, 2.5f);
    TEST_ASSERT(r.needsRelocation, "solicita relocalización al vencer el retardo");
}

static void test_satiety_blocking_and_acute_lethal_toxin(void) {
    MetabolismDatabase db;
    char err[256] = {0};
    TEST_ASSERT(MetabolismIO_LoadDatabase("metabolism", &db, err, sizeof(err)), "cargar db con toxina aguda");
    const MetabolicProfile *brum = MetabolicProfileRegistry_Get(&db.profiles, MetabolicProfileRegistry_Find(&db.profiles, "brumacerno"));
    const SubstanceDef *grass = SubstanceRegistry_Get(&db.substances, SubstanceRegistry_Find(&db.substances, "grass"));
    const SubstanceDef *lethal = SubstanceRegistry_Get(&db.substances, SubstanceRegistry_Find(&db.substances, "deadly_nightshade"));
    TEST_ASSERT(brum && grass && lethal, "perfil brumacerno, grass y deadly_nightshade cargados");

    /* 1) Bloqueo duro de ingesta cuando el estómago alcanza >= 92% de saciedad. */
    MetabolicState st;
    CreatureMetabolism_Init(&st, brum, 0.5f);
    CreatureMetabolism_Ingest(&st, brum, grass->handle, grass->components, grass->componentCount,
                              brum->digestiveCapacity * 0.93f, 0.0);
    float acceptFull = CreatureMetabolism_MaxAcceptableMass(&st, brum, 0.5f, 1.0f);
    TEST_ASSERT(acceptFull == 0.0f, "bloquea ingesta cuando el estómago está lleno (>=92%)");

    /* 2) Sustancia altamente letal en pequeña dosis (0.40 de masa -> DEATH_TOXICITY). */
    PhysiologyProfile pp = CreaturePhysiology_Default();
    CreaturePhysiology phys = CreaturePhysiology_Create(200.0, 0.90f);
    phys.hydration = 0.90f;
    phys.health = 1.0f;
    CreatureMetabolism_Init(&st, brum, 0.5f);
    CreatureMetabolism_Ingest(&st, brum, lethal->handle, lethal->components, lethal->componentCount, 0.40f, 0.0);
    for (int i = 0; i < 60 * 60 && phys.cause == DEATH_NONE; ++i) {
        CreatureMetabolism_Step(&st, brum, &phys, &pp, 1.0f / 60.0f, NULL);
    }
    TEST_ASSERT(phys.cause == DEATH_TOXICITY,
                "una dosis pequeña (0.40) de deadly_nightshade provoca DEATH_TOXICITY");
}

void run_molecular_metabolism_tests(void) {
    test_registry_loading_and_validation();
    test_species_digestive_divergence_and_mixed_substances();
    test_explicit_transformation_mass_conservation();
    test_deplete_relocate_and_rarity();
    test_satiety_blocking_and_acute_lethal_toxin();
    printf("[PASS] molecular_metabolism: registros, JSON estricto, digestión progresiva, saciedad, veneno agudo y relocalización\n");
}
