#include "ai_test_helpers.h"
void run_creature_needs_tests(void) {
    CreatureNeeds a = {0}, b = {0};
    const BrainProfile *p = BrainProfile_Find("balanced");
    for (int i = 0; i < 600; ++i)
        CreatureNeeds_Update(&a, p, 1.f / 60);
    CreatureNeeds_Update(&b, p, 10);
    TEST_ASSERT(fabsf(a.urgency[0] - b.urgency[0]) < 1e-5f,
                "tiempo fisiológico independiente de FPS");
    float old = a.urgency[0];
    CreatureNeeds_Update(&a, p, 0);
    TEST_ASSERT(a.urgency[0] == old, "pausa");
    CreatureNeeds_Satisfy(&a, p, NEED_HYDRATION, .1f);
    TEST_ASSERT(a.urgency[0] < old, "satisfacción progresiva");
    for (int i = 0; i < 1000; ++i)
        CreatureNeeds_Update(&a, BrainProfile_Find("waterless"), 1);
    TEST_ASSERT(a.urgency[0] == 0 && a.urgency[NEED_REST] > 0,
                "hidratación desactivada otras necesidades activas");
    TEST_ASSERT(CreatureNeeds_Utility(&a, BrainProfile_Find("waterless"), NEED_HYDRATION) == 0,
                "sin prioridad oculta");
}
