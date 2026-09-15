/** @file benchmark_head_morph.c
 * @brief Residuo SDF de la cabeza deformada entre keyframes de distintas edades.
 */
#include "MonsterVisualAsync.h"
#include "MonsterAger.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
static int Compare(const void* a,const void* b){float x=*(const float*)a,y=*(const float*)b;return (x>y)-(x<y);}
int main(void){
 Monster young=Monster_Create(),adult=Monster_Create();LizardPhenotype a=LizardPreset_Larva(),b=LizardPreset_Adult();
 Lizard_BuildMonster(&young,&a);Lizard_BuildMonster(&adult,&b);MonsterAger ager=MonsterAger_Create(&young,&adult,0);
 puts("reference_age,target_age,head_samples,mean_sdf_residual,p95_sdf_residual,max_sdf_residual");
 for(unsigned i=0;i<5;++i){
  float ref=i*.2f;MonsterAger_SetPerc(&ager,ref);
  MonsterVisualAsync* v=MonsterVisualAsync_Create(MonsterVisualAsync_DefaultConfig());MonsterVisualAsync_SetMorphMode(v,true);MonsterVisualAsync_SetContinuousMotion(v,true);
  MonsterVisualAsync_Update(v,&ager.result,0);MonsterVisualAsync_Flush(v);
  const HeadAnatomy reference=ager.result.head.anatomy;Vector3 sourceOrigin=ager.result.bodyParts[reference.attachmentBodyPartIndex].positionRender;
  float* errors=malloc(v->displayMesh.vertexCount*sizeof(float));
  for(unsigned step=0;step<=3;++step){
   float target=ref+step*.025f;MonsterAger_SetPerc(&ager,target);const Monster* m=&ager.result;
   LizardMorph_Deform(v->displayMorph,&m->anatomyGraph,&v->displayMesh);
   HeadMorph_Deform(v->displayHeadMorph,&v->displayMesh,&m->head.anatomy,m->bodyParts[m->head.anatomy.attachmentBodyPartIndex].positionRender);
   MonsterSDF sdf=MonsterSDF_Create();MonsterSDF_Build(&sdf,m,MonsterSDF_DefaultConfig());
   size_t count=0;double sum=0;
   for(size_t k=0;k<v->displayMesh.vertexCount;k+=4){
    Vector3 rest=v->displayMorph->basePositions[k];
    if(rest.z<sourceOrigin.z+reference.landmarks.neckAttachment.z || fabsf(rest.x)>reference.surface.craniumRadii.x*1.5f || rest.y<sourceOrigin.y-reference.surface.craniumRadii.y*1.4f)continue;
    float error=fabsf(MonsterSDF_EvaluateDistance(&sdf,v->displayMesh.vertices[k].position));errors[count++]=error;sum+=error;
   }
   qsort(errors,count,sizeof(float),Compare);
   printf("%.3f,%.3f,%zu,%.6f,%.6f,%.6f\n",ref,target,count,count?sum/count:0,count?errors[(size_t)(count*.95f)]:0,count?errors[count-1]:0);
   MonsterSDF_Free(&sdf);
  }
  free(errors);MonsterVisualAsync_Free(v);
 }
 MonsterAger_Free(&ager);Monster_Free(&young);Monster_Free(&adult);return 0;
}
