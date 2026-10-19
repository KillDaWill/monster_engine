/** @file creature_editor_bridge.c
 * @brief Puente sin GL entre documentos del editor y geometría real del motor.
 */
#include "CreatureRecipeIO.h"
#include "CreatureRecipeRegistry.h"
#include "Creature.h"
#include "Monster.h"
#include "MonsterVisual.h"
#include "PigmentPattern.h"
#include "EyeTexture.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>

static void ExportMesh(const Mesh* mesh,const CreaturePhenotype* p,bool skin,const MonsterVisualEye* eye){
    unsigned char texture[128*128*4];
    if(eye&&!EyeTexture_Generate(&eye->appearance,128,texture,sizeof(texture)))eye=NULL;
    for(size_t i=0;i<mesh->vertexCount;++i){const MeshVertex* v=&mesh->vertices[i];Color c=v->color;
        if(skin&&v->material==SDF_MATERIAL_SKIN){
            c=Color_Lerp(p->surface.pigment.baseColor,p->surface.pigment.ventralColor,v->surface.ventral);
            Vector3 q=Vec3_Scale(v->position,1.f/fmaxf(.01f,p->axial.totalScale));
            float pattern=PigmentPattern_Sample(p->surface.pigment.bands>.5f?PIGMENT_PATTERN_BANDS:PIGMENT_PATTERN_NOISE,
                q,p->surface.pigment.patternScale,.6f,Vec3_Create(0,0,1),p->surface.pigment.seed);
            c=Color_Lerp(c,p->surface.pigment.secondaryColor,pattern*p->surface.pigment.patternStrength);
            for(size_t k=0;k<p->surface.pigment.layerCount;++k)
                c=PigmentPattern_EvaluateLayer(&p->surface.pigment.layers[k],q,c);
        }
        if(eye){Vector3 delta=Vec3_Sub(v->position,eye->center);
            if(Vec3_Dot(delta,eye->forward)>0){
                float u=.5f+.5f*Vec3_Dot(delta,eye->right)/fmaxf(.0001f,eye->scale.x);
                float w=.5f+.5f*Vec3_Dot(delta,eye->up)/fmaxf(.0001f,eye->scale.y);
                int x=(int)fminf(127,fmaxf(0,u*128)),y=(int)fminf(127,fmaxf(0,w*128));
                const unsigned char* pixel=&texture[(y*128+x)*4];c=Color_FromRGB(pixel[0],pixel[1],pixel[2]);
            }
        }
        float data[10]={v->position.x,v->position.y,v->position.z,v->normal.x,v->normal.y,v->normal.z,
            c.r/255.f,c.g/255.f,c.b/255.f,c.a/255.f};
        fwrite(data,sizeof(float),10,stdout);
    }
    fwrite(mesh->indices,sizeof(uint32_t),mesh->indexCount,stdout);
}
int main(int argc,char** argv){
    if(argc==2&&!strcmp(argv[1],"schema"))return CreatureRecipeIO_WriteSchema(stdout)?0:1;
    char catalogError[256]={0};
    if(!CreatureRecipeRegistry_LoadDirectory("recipes",catalogError,sizeof(catalogError))){fprintf(stderr,"%s\n",catalogError);return 1;}
    if(argc==3&&!strcmp(argv[1],"export")){
        const CreatureRecipe* r=CreatureRecipes_Find((CreatureRecipeId)strtoul(argv[2],NULL,10));
        return r&&CreatureRecipeIO_WriteJSON(stdout,r)?0:1;
    }
    if(argc<3){fprintf(stderr,"Uso: export ID | validate archivo | preview archivo etapa calidad\n");return 1;}
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));char error[256];
    if(!doc||!CreatureRecipeIO_LoadFile(doc,argv[2],error,sizeof(error))){fprintf(stderr,"%s\n",doc?error:"Sin memoria");free(doc);return 1;}
    if(!strcmp(argv[1],"validate")){bool ok=CreatureRecipeIO_WriteJSON(stdout,&doc->recipe);free(doc);return ok?0:1;}
    int stage=argc>3?atoi(argv[3]):3;
    if(strcmp(argv[1],"preview")||stage<0||stage>3){free(doc);return 1;}
    bool fine=argc>4&&!strcmp(argv[4],"fine");
    Monster* m=malloc(sizeof(*m));MonsterVisual* v=malloc(sizeof(*v));
    if(!m||!v){free(m);free(v);free(doc);return 1;}
    *m=Monster_Create();const CreaturePhenotype* p=CreatureRecipe_GetStage(&doc->recipe,(CreatureStage)stage);
    bool ok=Creature_ResolveAppearance(m,&doc->recipe,p);
    /* Pose neutra de edición; la demo conserva el control de su animación oral. */
    for(size_t i=0;i<m->mouthCount;++i)Mouth_SetOpenFactor(&m->mouths[i],0);
    SDFMesherConfig config=SDFMesher_DefaultConfig();config.voxelSize=fine?.065f:.12f;
    config.maxResolution=fine?160:96;config.maxCells=fine?650000:160000;config.adaptiveDetail=false;
    *v=MonsterVisual_Create(config);
    v->headMesher.config.voxelSize=fine?.025f:.055f;
    v->headMesher.config.maxCells=fine?350000:100000;
    v->headMesher.config.maxResolution=fine?160:96;
    ok=ok&&MonsterVisual_RebuildNow(v,m,MonsterSDF_DefaultConfig());
    if(ok){
        const Mesh* meshes[160];bool skin[160];const MonsterVisualEye* eyes[160]={0};size_t count=0;
#define ADD(mesh,isSkin) do {const Mesh* next=(mesh);if(next&&next->indexCount){meshes[count]=next;skin[count++]=(isSkin);}}while(0)
        ADD(&v->mesh,true);ADD(&v->headMesh,true);
        for(size_t i=0;i<v->mouthCount;++i){ADD(&v->mouths[i].jaw,true);ADD(&v->mouths[i].hinge,true);}
        for(size_t i=0;i<v->eyeCount;++i){size_t before=count;ADD(&v->eyes[i].globe,false);if(count>before)eyes[before]=&v->eyes[i];}
        FILE* meta=tmpfile();if(!meta)ok=false;
        else{
            fprintf(meta,"{\"meshes\":[");
            for(size_t i=0;i<count;++i){size_t materials[10]={0};
                for(size_t j=0;j<meshes[i]->vertexCount;++j){unsigned material=(unsigned)meshes[i]->vertices[j].material;if(material<10)materials[material]++;}
                fprintf(meta,"%s{\"vertices\":%zu,\"indices\":%zu,\"materialCounts\":{\"skin\":%zu,\"horn\":%zu,\"hoof\":%zu}}",i?",":"",meshes[i]->vertexCount,meshes[i]->indexCount,materials[0],materials[8],materials[9]);}
            fprintf(meta,"],\"nodes\":[");
            for(size_t i=0;i<m->anatomyGraph.nodeCount;++i){const AnatomyNode* n=&m->anatomyGraph.nodes[i];
                fprintf(meta,"%s{\"id\":%u,\"module\":%u,\"local\":%u,\"position\":[%.7g,%.7g,%.7g],\"radii\":[%.7g,%.7g],\"region\":%u,\"role\":%u}",i?",":"",n->id,n->moduleInstanceId,n->localNodeId,n->center.x,n->center.y,n->center.z,n->widthRadius,n->heightRadius,(unsigned)n->region,(unsigned)n->role);}
            fprintf(meta,"]}");long length=ftell(meta);rewind(meta);uint32_t size=(uint32_t)length;
            fwrite(&size,sizeof(size),1,stdout);int c;while((c=fgetc(meta))!=EOF)fputc(c,stdout);fclose(meta);
            for(size_t i=0;i<count;++i)ExportMesh(meshes[i],&m->phenotype,skin[i],eyes[i]);
        }
    }
    if(!ok)fprintf(stderr,"El motor no pudo construir la vista previa (cuerpo=%zu, cabeza=%zu, bocas=%zu).\n",v->stagingMesh.vertexCount,v->stagingHeadMesh.vertexCount,m->mouthCount);
    MonsterVisual_Free(v);Monster_Free(m);free(v);free(m);free(doc);
    return ok&&!ferror(stdout)?0:1;
}
