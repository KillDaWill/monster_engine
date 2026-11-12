#include "Creature.h"
#include "CreatureRecipes.h"
#include "MonsterAger.h"
#include "MonsterSDF.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static size_t samples;
static void sample(const MonsterSDF* sdf, Vector3 p) {
    float d=MonsterSDF_EvaluateDistance(sdf,p);
    SDFSample s=MonsterSDF_Evaluate(sdf,p);
    if(!isfinite(d)||!isfinite(s.distance))exit(2);
    printf("%a %a %u %u %u %u %d\n",(double)d,(double)s.distance,
        s.color.r,s.color.g,s.color.b,s.color.a,(int)s.material);
    ++samples;
}

int main(void) {
    Monster dog=Monster_Create(),lizard=Monster_Create();
    const CreatureRecipe* a=CreatureRecipes_Dog();
    const CreatureRecipe* b=CreatureRecipes_Lizard();
    if(!Creature_BuildMonster(&dog,a,&a->adult)||
       !Creature_BuildMonster(&lizard,b,&b->adult))return 2;
    Monster_SetHeadOpenFactor(&dog,0);
    Monster_SetHeadOpenFactor(&lizard,.08f);
    MonsterAger ager=MonsterAger_Create(&dog,&lizard,0);
    MonsterAger_SetGeometrySteps(&ager,20);
    MonsterSDF sdf=MonsterSDF_Create();
    const float ages[]={0,.5f,1,.5f,0};
    for(size_t k=0;k<sizeof(ages)/sizeof(*ages);++k) {
        MonsterAger_SetPerc(&ager,ages[k]);
        const Monster* m=MonsterAger_GetResultConst(&ager);
        if(!MonsterSDF_Build(&sdf,m,MonsterSDF_DefaultConfig()))return 2;
        printf("age=%a connectors=%zu\n",(double)ages[k],sdf.connectorCount);
        Vector3 span=Vec3_Sub(sdf.bounds.end,sdf.bounds.start);
        for(int x=0;x<9;++x)for(int y=0;y<9;++y)for(int z=0;z<9;++z)
            sample(&sdf,Vec3_Add(sdf.bounds.start,Vec3_Create(
                span.x*x/8,span.y*y/8,span.z*z/8)));
        /* Centros y superficies de nodos, incluidos cabeza y cuello. */
        for(size_t n=0;n<m->anatomyGraph.nodeCount;++n) {
            const AnatomyNode* node=&m->anatomyGraph.nodes[n];
            for(int side=-1;side<=1;++side) {
                sample(&sdf,Vec3_Add(node->center,Vec3_Create(side*node->widthRadius,0,0)));
                sample(&sdf,Vec3_Add(node->center,Vec3_Create(0,side*node->heightRadius,0)));
            }
        }
        /* Interior y borde de cada unión; no sólo la rejilla global. */
        for(size_t i=0;i<sdf.connectorCount;++i) {
            const MonsterSDFConnector* c=&sdf.connectors[i];
            for(int j=0;j<5;++j) {
                float t=j/4.0f;
                Vector3 p=Vec3_Lerp(c->a,c->b,t);
                float r=c->widthA+(c->widthB-c->widthA)*t;
                sample(&sdf,p);
                sample(&sdf,Vec3_Add(p,Vec3_Create(r,0,0)));
                sample(&sdf,Vec3_Add(p,Vec3_Create(-r,0,0)));
            }
        }
    }
    fprintf(stderr,"PASS: %zu finite full samples + distances; ages 0,.5,1,.5,0\n",samples);
    MonsterSDF_Free(&sdf);MonsterAger_Free(&ager);
    Monster_Free(&dog);Monster_Free(&lizard);
    return 0;
}
