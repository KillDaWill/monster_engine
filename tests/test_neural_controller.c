#include "CreatureMind.h"
#include "CreatureActions.h"
#include "EcosystemResources.h"
#include "test_utils.h"
#define NEAR_EPS(a, b, eps) (fabsf((a) - (b)) < (eps))
#include <math.h>
#include <string.h>

static void test_sensor_encoding_privacy_confidence_and_bearing(void) {
    AIWorld world;
    AIWorld_Init(&world, 24.0f);
    AIObjectId selfId = AIWorld_Add(&world, (AIWorldObject){
        .type = AI_CREATURE, .position = {0, 0, 0}, .visible = true, .walkable = true
    });
    AIWorld_SetObservation(&world, selfId, 1, 1, 0, true, true);

    /* Planta visible al frente (+Z) y planta oculta fuera de rango (no debe filtrarse). */
    AIObjectId plantNear = AIWorld_Add(&world, (AIWorldObject){
        .type = AI_FOOD, .position = {0, 0, 4}, .radius = 0.5f, .visible = true, .walkable = true
    });
    AIWorld_SetObservation(&world, plantNear, 0, 0, ECO_PLANT, true, false);
    AIObjectId meatFar = AIWorld_Add(&world, (AIWorldObject){
        .type = AI_FOOD, .position = {0, 0, 22}, .radius = 0.5f, .visible = true, .walkable = true
    });
    AIWorld_SetObservation(&world, meatFar, 0, 0, ECO_MEAT, true, false);

    /* Agua a la derecha (+X) con yaw=0 -> sin(bearing) = +1, cos(bearing) = 0. */
    AIObjectId waterRight = AIWorld_Add(&world, (AIWorldObject){
        .type = AI_WATER, .position = {5, 0, 0}, .radius = 1.0f, .visible = true, .drinkable = true
    });
    AIWorld_SetObservation(&world, waterRight, 0, 0, ECO_WATER, true, false);

    /* Pareja conspecífica compatible visible. */
    AIObjectId mate = AIWorld_Add(&world, (AIWorldObject){
        .type = AI_CREATURE, .group = 1, .position = {0, 0, 6}, .visible = true, .walkable = true
    });
    AIWorld_SetObservation(&world, mate, 1, 1, 0, true, true);
    AIWorld_SetAgentState(&world, mate, 1, 2.0f);

    PerceptionProfile pp = {.range = 10.0f, .horizontalFOV = 6.28f, .verticalFOV = 3.14f,
                            .interval = 0.1f, .memoryDuration = 20.0f};
    CreaturePerception perc = {0};
    CreaturePerception_Update(&perc, &pp, &world, selfId, Vec3_Create(0, 0.5f, 0), 0.0f, 1.0);

    CreatureBodyState body = {0};
    body.position = Vec3_Zero();
    body.yaw = 0.0f;
    body.species = 1;
    body.reproductiveRole = 0;
    body.mateReady = true;
    body.energy = 0.6f;
    body.hydration = 0.7f;
    body.fatigue = 0.2f;
    body.health = 0.9f;
    body.normalizedAge = 0.25f;
    body.canAttack = false;
    body.capabilities.waterInteraction = true;
    body.capabilities.maximumSpeed = 2.0f;

    float fb[3] = {0.5f, 0.25f, 0.75f};
    float inputs[NEURAL_RNN_INPUTS];
    NeuralSensorCandidates cands = {0};
    NeuralController_EncodeSensors(&perc, &pp, &body, 1.0, fb, inputs, &cands);

    TEST_ASSERT(NEAR_EPS(inputs[0], 0.6f, 1e-5f), "sensor 00 energía");
    TEST_ASSERT(NEAR_EPS(inputs[1], 0.7f, 1e-5f), "sensor 01 hidratación");
    TEST_ASSERT(NEAR_EPS(inputs[5], 1.0f, 1e-5f), "sensor 05 mateReady");
    TEST_ASSERT(NEAR_EPS(inputs[7], 0.0f, 1e-5f), "sensor 07 canAttack");
    TEST_ASSERT(NEAR_EPS(inputs[9], 0.5f, 1e-5f) && NEAR_EPS(inputs[10], 0.25f, 1e-5f) &&
                NEAR_EPS(inputs[11], 0.75f, 1e-5f), "sensores 09..11 feedback real");

    /* Planta visible al frente: conf=1, dist=0.4, sin=0, cos=1. */
    TEST_ASSERT(cands.plantId == plantNear, "candidato vegetal percibido");
    TEST_ASSERT(NEAR_EPS(inputs[12], 1.0f, 1e-5f), "confianza planta visible");
    TEST_ASSERT(NEAR_EPS(inputs[13], 0.4f, 1e-5f), "distancia planta normalizada");
    TEST_ASSERT(NEAR_EPS(inputs[14], 0.0f, 1e-5f) && NEAR_EPS(inputs[15], 1.0f, 1e-5f), "rumbo relativo planta");

    /* Carne fuera de rango: ausente (no filtrada del mundo global). */
    TEST_ASSERT(cands.meatId == 0u && NEAR_EPS(inputs[16], 0.0f, 1e-5f), "recurso fuera de percepción no visible");

    /* Agua a la derecha: sin=+1, cos=0. */
    TEST_ASSERT(cands.waterId == waterRight, "candidato agua");
    TEST_ASSERT(NEAR_EPS(inputs[20], 1.0f, 1e-5f) && NEAR_EPS(inputs[22], 1.0f, 1e-5f) &&
                NEAR_EPS(inputs[23], 0.0f, 1e-5f), "rumbo relativo agua derecha");

    /* Pareja compatible detectada. */
    TEST_ASSERT(cands.mateCandidateId == mate && cands.conspecificId == mate, "candidato pareja compatible");

    /* Al dejar de estar visible en la siguiente observación, la confianza cae a recuerdo (0.1..0.5). */
    for (unsigned i = 0; i < perc.count; ++i) perc.entries[i].visible = false;
    NeuralController_EncodeSensors(&perc, &pp, &body, 6.0, fb, inputs, &cands);
    TEST_ASSERT(inputs[12] > 0.1f && inputs[12] < 0.5f, "observación recordada tiene confianza reducida");
}

static void test_neural_only_decisions_no_utility_fallback_and_distinct_food(void) {
    AIWorld world;
    AIWorld_Init(&world, 24.0f);
    AIObjectId selfId = AIWorld_Add(&world, (AIWorldObject){
        .type = AI_CREATURE, .position = {0, 0, 0}, .visible = true, .walkable = true
    });
    AIWorld_SetObservation(&world, selfId, 1, 1, 0, true, false);
    AIObjectId plant = AIWorld_Add(&world, (AIWorldObject){
        .type = AI_FOOD, .position = {0, 0, 3}, .radius = 0.5f, .visible = true, .walkable = true
    });
    AIWorld_SetObservation(&world, plant, 0, 0, ECO_PLANT, true, false);
    AIObjectId meat = AIWorld_Add(&world, (AIWorldObject){
        .type = AI_FOOD, .position = {3, 0, 0}, .radius = 0.5f, .visible = true, .walkable = true
    });
    AIWorld_SetObservation(&world, meat, 0, 0, ECO_MEAT, true, false);
    AIObjectId predator = AIWorld_Add(&world, (AIWorldObject){
        .type = AI_CREATURE, .position = {0, 0, 2}, .visible = true, .walkable = true
    });
    AIWorld_SetObservation(&world, predator, 2, 2, 0, true, false);

    /* Perfil con todos los pesos legacy a cero y threatMask activo:
     * la red neuronal NO debe disparar huida automática ni bloquear acciones por pesos legacy. */
    BrainProfile profile = BrainProfile_Default();
    for (int i = 0; i < ACTION_COUNT; ++i) profile.weights[i] = 0.0f;
    profile.threatMask = 2u;
    profile.dangerDistance = 10.0f;
    profile.perception.range = 15.0f;
    profile.perception.horizontalFOV = 6.28f;

    CreatureMind mind;
    CreatureMind_Init(&mind, &profile, selfId, 77u);

    /* Genoma que siempre elige IDLE (sesgo fuerte en salida 0). */
    NeuralGenome idleGenome;
    NeuralGenome_Init(&idleGenome);
    idleGenome.b_y[ACTION_INTENT_IDLE] = 4.0f;
    NeuralGenome_UpdateFingerprint(&idleGenome);
    TEST_ASSERT(CreatureMind_AssignNeuralGenome(&mind, &idleGenome), "asignar genoma IDLE");

    CreatureBodyState body = {0};
    body.position = Vec3_Zero();
    body.eye = Vec3_Create(0, 0.5f, 0);
    body.mouth = Vec3_Create(0, 0.4f, 0.4f);
    body.mouthDirection = Vec3_Create(0, 0, 1);
    body.species = 1;
    body.energy = 0.05f;     /* casi muriendo de hambre */
    body.hydration = 0.05f;  /* casi muriendo de sed */
    body.health = 0.5f;
    body.capabilities.ground = true;
    body.capabilities.waterInteraction = true;
    body.capabilities.clearance = 0.5f;
    body.capabilities.maximumSpeed = 2.0f;
    body.capabilities.maximumTurn = 2.0f;
    body.capabilities.contactTolerance = 0.2f;
    body.capabilities.maximumOralReach = 1.0f;

    for (int i = 0; i < 30; ++i) CreatureMind_Update(&mind, &world, &body, 1.0f / 60.0f);
    TEST_ASSERT(mind.neuralState.selectedIntent == ACTION_INTENT_IDLE && mind.action == ACTION_IDLE,
                "sin respaldo oculto de utilidad: permanece IDLE incluso con depredador y hambre");

    /* Genoma que elige FEED_PLANT apunta específicamente a la planta aunque todos los weights legacy sean 0. */
    NeuralGenome plantGenome;
    NeuralGenome_Init(&plantGenome);
    plantGenome.b_y[ACTION_INTENT_FEED_PLANT] = 4.0f;
    NeuralGenome_UpdateFingerprint(&plantGenome);
    TEST_ASSERT(CreatureMind_AssignNeuralGenome(&mind, &plantGenome), "asignar genoma FEED_PLANT");
    for (int i = 0; i < 20; ++i) CreatureMind_Update(&mind, &world, &body, 1.0f / 60.0f);
    TEST_ASSERT(mind.action == ACTION_APPROACH_FOOD && mind.target == plant && mind.desiredFoodCategory == ECO_PLANT,
                "FEED_PLANT selecciona planta sin depender de weights legacy");

    /* Genoma que elige FEED_MEAT apunta específicamente a la carne (distinguible de FEED_PLANT). */
    NeuralGenome meatGenome;
    NeuralGenome_Init(&meatGenome);
    meatGenome.b_y[ACTION_INTENT_FEED_MEAT] = 4.0f;
    NeuralGenome_UpdateFingerprint(&meatGenome);
    TEST_ASSERT(CreatureMind_AssignNeuralGenome(&mind, &meatGenome), "asignar genoma FEED_MEAT");
    for (int i = 0; i < 20; ++i) CreatureMind_Update(&mind, &world, &body, 1.0f / 60.0f);
    TEST_ASSERT(mind.action == ACTION_APPROACH_FOOD && mind.target == meat && mind.desiredFoodCategory == ECO_MEAT,
                "FEED_MEAT selecciona carne de forma distinta a FEED_PLANT");

    CreatureMind_Reset(&mind);
    NeuralGenome_Destroy(&idleGenome);
    NeuralGenome_Destroy(&plantGenome);
    NeuralGenome_Destroy(&meatGenome);
}

void run_neural_controller_tests(void) {
    test_sensor_encoding_privacy_confidence_and_bearing();
    test_neural_only_decisions_no_utility_fallback_and_distinct_food();
    printf("[PASS] neural_controller: sensores privados, FEED_PLANT/FEED_MEAT distintos y cero fallback utilidad\n");
}
