/** @file inspect_brumacerno.c
 * @brief Evidencia reproducible de landmarks, proporciones y continuidad del SDF. */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "Monster.h"
#include "MonsterSDF.h"
#include "Limb.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

static void VectorJSON(Vector3 p) {printf("[%.7g,%.7g,%.7g]",p.x,p.y,p.z);}
int main(int argc,char** argv) {
    if(argc!=2)return 2;
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* monster=malloc(sizeof(*monster));
    char error[256];if(!doc||!monster||!CreatureRecipeIO_LoadFile(doc,argv[1],error,sizeof(error))){fprintf(stderr,"%s\n",error);return 1;}
    printf("{\"recipe\":%u,\"stages\":[",doc->recipe.id);
    bool valid=true;
    for(unsigned stage=0;stage<4;++stage) {
        *monster=Monster_Create();
        const CreaturePhenotype* p=CreatureRecipe_GetStage(&doc->recipe,(CreatureStage)stage);
        if(!Creature_BuildMonster(monster,&doc->recipe,p))return 1;
        MonsterSDF sdf=MonsterSDF_Create();if(!MonsterSDF_Build(&sdf,monster,MonsterSDF_DefaultConfig()))return 1;
        const HeadAnatomy* h=&monster->head.anatomy;HeadResolvedMeasurements m=HeadAnatomy_Measure(h);
        unsigned gaps=0,samples=0;
        for(size_t i=0;i<sdf.connectorCount;++i) {
            const MonsterSDFConnector* c=&sdf.connectors[i];
            if(c->material!=SDF_MATERIAL_HORN&&c->material!=SDF_MATERIAL_HOOF&&c->region!=ANATOMY_REGION_TAIL)continue;
            for(unsigned j=0;j<=16;++j){float d=MonsterSDF_EvaluateDistance(&sdf,Vec3_Lerp(c->a,c->b,(float)j/16));++samples;if(!isfinite(d)||d>1e-5f)++gaps;}
        }
        unsigned validation=HeadAnatomy_Validate(h);valid=valid&&!validation&&!gaps;
        printf("%s{\"stage\":%u,\"scale\":%.7g,\"head_validation\":%u,\"field_samples\":%u,\"field_gaps\":%u,\"head_length\":%.7g,\"zygomatic_width\":%.7g,\"bridge_width\":%.7g,\"muzzle_width\":%.7g,\"inter_eye_distance\":%.7g,\"oral_slope\":%.7g,\"bounds_min\":",stage?",":"",stage,p->axial.totalScale,validation,samples,gaps,m.actualHeadLength,m.actualMaxZygomaticWidth,h->surface.faceMidRadii.x*2,m.muzzleTipWidth,m.interEyeDistance,h->surface.oralSlope);
        VectorJSON(sdf.bounds.start);printf(",\"bounds_max\":");VectorJSON(sdf.bounds.end);
        printf(",\"ear_shape\":");VectorJSON(h->surface.earShape);
        printf(",\"skull\":");VectorJSON(h->landmarks.skullCenter);
        printf(",\"left_eye\":");VectorJSON(h->landmarks.leftEyeCenter);
        printf(",\"left_nostril\":");VectorJSON(h->landmarks.leftNostril);
        printf(",\"oral_landmark\":");VectorJSON(h->landmarks.oralContactPoint);
        MonsterSDFJawField jawContext;SDFField jaw=MonsterSDF_GetJawField(&sdf,0,&jawContext);
        Vector3 contact=h->landmarks.oralContactPoint;
        float jawDistance=jaw.evaluate(jaw.context,contact).distance;
        float upperDistance=MonsterSDF_EvaluateDistance(&sdf,Vec3_Add(sdf.mouths[0].center,contact));
        printf(",\"contact_to_jaw\":%.7g,\"contact_to_upper\":%.7g",jawDistance,upperDistance);
        printf(",\"horns\":[");
        for(unsigned side=0;side<2;++side){const AnatomyNode* a=AnatomyGraph_FindModuleNode(&monster->anatomyGraph,30+side,1);const AnatomyNode* b=AnatomyGraph_FindModuleNode(&monster->anatomyGraph,30+side,3);if(side)printf(",");printf("{\"root\":");VectorJSON(a->center);printf(",\"tip\":");VectorJSON(b->center);printf("}");}
        printf("],\"axial_stations\":[");
        float reference=p->axial.withersHeight*p->axial.totalScale;
        for(unsigned station=1;station<=6;++station) {
            const AnatomyNode* node=AnatomyGraph_FindModuleNode(&monster->anatomyGraph,1,station);
            printf("%s{\"center\":",station>1?",":"");VectorJSON(node->center);
            printf(",\"width_H\":%.7g,\"depth_H\":%.7g}",2*node->widthRadius/reference,2*node->heightRadius/reference);
        }
        printf("],\"limbs\":[");
        for(unsigned leg=0;leg<4;++leg) {
            LimbRigDescriptor desc;Limb_BuildRigDescriptor(&p->limbs[leg],10+leg,&monster->anatomyGraph,&desc);
            printf("%s{\"sole_offset_H\":%.7g,\"joints\":[",leg?",":"",desc.soleHeight/reference);
            for(unsigned joint=1;joint<=4;++joint){if(joint>1)printf(",");VectorJSON(AnatomyGraph_FindModuleNode(&monster->anatomyGraph,10+leg,joint)->center);}
            printf("],\"claws\":[");unsigned count=0;
            for(size_t c=0;c<sdf.connectorCount;++c) {
                const MonsterSDFConnector* claw=&sdf.connectors[c];
                if(claw->moduleInstanceId!=10+leg||claw->material!=SDF_MATERIAL_HOOF)continue;
                printf("%s{\"width_H\":%.7g,\"height_H\":%.7g,\"length_H\":%.7g,\"sole_y\":%.7g}",count++?",":"",
                    2*claw->widthA/reference,2*claw->heightB/reference,
                    (claw->length+claw->widthA*.55f+claw->widthB*claw->toeRounding)/reference,claw->a.y-claw->heightA);
            }
            printf("]}");
        }
        printf("]}");MonsterSDF_Free(&sdf);Monster_Free(monster);
    }
    printf("],\"valid\":%s}\n",valid?"true":"false");free(monster);free(doc);return valid?0:1;
}
