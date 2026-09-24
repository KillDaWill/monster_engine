#include "Creature.h"
#include "Limb.h"
#include "test_utils.h"
#include "Monster.h"
#include "../src/MonsterSDFRayBounds.h"
#include <string.h>

void run_sdf_ray_bounds_tests(void) {
    CreaturePhenotype larva=CreatureRecipes_Lizard()->larva,adult=CreatureRecipes_Lizard()->adult;
    unsigned checked=0;
    for(int phase=0;phase<=4;++phase) {
        Monster monster=Monster_Create();
        CreaturePhenotype phenotype=CreaturePhenotype_Interpolate(&larva,&adult,phase*.25f);
        TEST_ASSERT(Creature_BuildMonster(&monster,CreatureRecipes_Lizard(),&phenotype),"Resolver anatomía de cotas GPU");
        MonsterSDF sdf=MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf,&monster,MonsterSDF_DefaultConfig()),"Compilar cotas GPU");
        for(int swept=0;swept<2;++swept)for(size_t i=0;i<sdf.mouthCount;++i) {
            sdf.mouths[i].sweptSkull=swept!=0;
            MonsterSDFMouth before=sdf.mouths[i];
            float smooth=before.headBodySmoothness;
            MonsterSDFMouth packed=RayBounds_PackedMouth(&before,smooth,.001f);
            TEST_ASSERT(memcmp(&before,&sdf.mouths[i],sizeof(before))==0,"La cota GPU no debe modificar el campo CPU");
            MonsterSDFHeadField context;
            SDFField field=MonsterSDF_GetHeadField(&sdf,i,&context);
            // Todas las caras y esquinas, a varias distancias exteriores. El
            // campo cefálico no puede participar en una unión superficial aquí.
            for(int axis=0;axis<3;++axis)for(int side=0;side<2;++side)
            for(int u=0;u<=12;++u)for(int v=0;v<=12;++v)for(int shell=0;shell<3;++shell) {
                float lo[3]={packed.headBounds.start.x,packed.headBounds.start.y,packed.headBounds.start.z};
                float hi[3]={packed.headBounds.end.x,packed.headBounds.end.y,packed.headBounds.end.z};
                float p[3];p[axis]=(side?hi[axis]:lo[axis])+(side?1.f:-1.f)*(.0001f+shell*.4f);
                int a=(axis+1)%3,b=(axis+2)%3;
                p[a]=lo[a]+(hi[a]-lo[a])*u/12.f;p[b]=lo[b]+(hi[b]-lo[b])*v/12.f;
                float d=field.evaluateDistance(field.context,Vec3_Create(p[0],p[1],p[2]));
                TEST_ASSERT(isfinite(d)&&d>smooth+.002f,"Cota de subnivel cefálico insuficiente");
                ++checked;
            }
        }
        MonsterSDF_Free(&sdf);Monster_Free(&monster);
    }
    printf("[PASS] test_sdf_ray_bounds: %u muestras exteriores, cinco edades, dos recetas\n",checked);
}
