/** @file test_navigation.c
 * @brief Regresiones de llegada, órbita y avance real hacia objetivos.
 */
#include "NavigationProgress.h"
#include "test_utils.h"
#include <math.h>
void run_navigation_tests(void) {
    NavigationProgress p={0};
    TEST_ASSERT(NavigationProgress_Update(&p,Vec3_Zero(),Vec3_Zero(),2,.7f,1,.5f,0)==NAVIGATION_ARRIVED,
        "Objetivo coincidente se alcanza incluso sin paso temporal");
    NavigationGoalStatus result=NAVIGATION_ADVANCING;
    for(int i=0;i<600&&result==NAVIGATION_ADVANCING;++i) {
        float angle=i/60.f;
        result=NavigationProgress_Update(&p,Vec3_Create(sinf(angle),0,cosf(angle)),
            Vec3_Zero(),angle,.7f,1,.5f,1.f/60);
    }
    TEST_ASSERT(result==NAVIGATION_STALLED&&p.elapsed<6,"Detectar órbita aunque se acumule recorrido");
    p=(NavigationProgress){0};
    for(int i=0;i<600;++i) {
        result=NavigationProgress_Update(&p,Vec3_Create(0,0,i/60.f),Vec3_Create(0,0,12),
            sinf(i*.1f),1,1,.5f,1.f/60);
        TEST_ASSERT(result==NAVIGATION_ADVANCING,"Avance hacia el objetivo renueva el plazo aunque cambie el rumbo");
    }
    TEST_ASSERT(NavigationProgress_Update(&p,Vec3_Create(0,0,11.6f),Vec3_Create(0,0,12),3,1,1,.5f,1.f/60)==NAVIGATION_ARRIVED,
        "Llegar antes de sobrepasar el objetivo y girar hacia atrás");
    p=(NavigationProgress){0};
    for(int i=0;i<400;++i)result=NavigationProgress_Update(&p,Vec3_Zero(),Vec3_Create(0,0,10),0,1,1,.5f,1.f/60);
    TEST_ASSERT(result==NAVIGATION_STALLED,"Detectar bloqueo sin rotación ni desplazamiento");
    p=(NavigationProgress){0};
    for(int i=0;i<180;++i) {
        result=NavigationProgress_Update(&p,Vec3_Zero(),Vec3_Create(0,0,-10),i/60.f,1,1,.5f,1.f/60);
        TEST_ASSERT(result==NAVIGATION_ADVANCING,"Permitir orientar el cuerpo inicialmente hacia un objetivo opuesto");
    }
    TEST_ASSERT(NavigationProgress_Update(&p,Vec3_Zero(),Vec3_Zero(),NAN,1,1,.5f,.01f)==NAVIGATION_STALLED,"Rechazar datos no finitos");
}
