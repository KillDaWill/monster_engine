/** @file test_equine_anatomy.c
 * @brief Équidos reutilizables: cráneo, casco, apoyo, rig y desarrollo v1. */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include "CreatureMorph.h"
#include "MonsterAnimation.h"
#include "MonsterSDF.h"
#include "Limb.h"
#include "MonsterVisual.h"
#include "AnatomyDeformer.h"
#include "MonsterVisualAsync.h"
#include "test_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void CranialComposition(void) {
    TEST_ASSERT(HEAD_ARCHETYPE_BOVID==3&&HEAD_ARCHETYPE_EQUINE==4,"Conservar identidades de arquetipos antiguos");
    for(uint32_t seed=1;seed<=180;++seed) {
        HeadPhenotype p=HeadPhenotype_RandomValid(HEAD_ARCHETYPE_EQUINE,seed);
        HeadAnatomy h;
        TEST_ASSERT(HeadAnatomy_Resolve(&p,0,Vec3_Create(.5f,.45f,.5f),&h),"Variaciones cefálicas équidas legales");
        TEST_ASSERT(HeadAnatomy_Validate(&h)==HEAD_VALID,"Simetría, finitud y orden cefálico");
        TEST_ASSERT(h.surface.faceTipRadii.x>h.surface.faceMidRadii.x,"Labios más anchos que el puente nasal");
        TEST_ASSERT(h.surface.faceTip.y<h.surface.faceRoot.y&&h.surface.oralSlope<0,"Cara y labios inclinados juntos");
        TEST_ASSERT(!h.surface.hasNasalPad&&h.surface.hasEars,"Sin trufa canina; conservar pabellones");
        TEST_ASSERT(h.landmarks.leftOrbitNormal.x>h.landmarks.leftOrbitNormal.z,"Órbitas laterales de herbívoro");
        HeadPhenotype q=HeadPhenotype_RandomValid(HEAD_ARCHETYPE_EQUINE,seed);
        TEST_ASSERT(FLOAT_NEAR(p.muzzleLength,q.muzzleLength)&&p.archetype==q.archetype,"Variación determinista por semilla");
    }
    HeadPhenotype p=HeadPhenotype_EquinePreset();p.muzzleLength=NAN;p.earHeightRatio=INFINITY;
    HeadPhenotype_Normalize(&p);
    TEST_ASSERT(isfinite(p.muzzleLength)&&isfinite(p.earHeightRatio)&&p.archetype==HEAD_ARCHETYPE_EQUINE,"Normalizar parámetros inválidos sin perder familia");
}

static void MuscleCohesion(Monster* m,const MonsterSDF* sdf) {
    Mesh mesh=Mesh_Create();
    TEST_ASSERT(Mesh_ReserveVertices(&mesh,40),"Reservar sondas musculares de piel");
    float scale=m->phenotype.axial.totalScale;
    for(unsigned leg=0;leg<4;++leg) {
        unsigned module=10+leg;
        const AnatomyNode* root=AnatomyGraph_FindModuleNode(&m->anatomyGraph,module,1);
        const AnatomyNode* anchor=AnatomyGraph_FindModuleNode(&m->anatomyGraph,module,5);
        TEST_ASSERT(anchor->center.y>root->center.y,"Envolvente orientada desde escápula/pelvis hacia miembro");
        TEST_ASSERT(anchor->widthRadius<anchor->heightRadius,"Sección proximal aplanada, no esfera");
        float hostDistance=SDF_EllipticalSweepZ(anchor->center,sdf->axialStations,sdf->axialStationCount);
        if(hostDistance>=0)fprintf(stderr,"muscle anchor module=%u scale=%g host=%g\n",module,scale,hostDistance);
        TEST_ASSERT(hostDistance<0,"Origen muscular dentro del anfitrión");
        MonsterSDF local,opposite;MonsterSDFConnector lc[100],rc[100];
        TEST_ASSERT(MonsterSDF_BuildModuleView(sdf,module,&local,lc,100)&&
            MonsterSDF_BuildModuleView(sdf,10+(leg^1),&opposite,rc,100),"Campos de ambos miembros sin mezclar el orden global");
        for(unsigned local=1;local<=10;++local) {
            const AnatomyNode* node=AnatomyGraph_FindModuleNode(&m->anatomyGraph,module,local);
            mesh.vertices[mesh.vertexCount++]=(MeshVertex){.position=node->center,.normal={0,1,0}};
        }
        for(size_t i=0;i<sdf->connectorCount;++i) {
            const MonsterSDFConnector* c=&sdf->connectors[i];
            if(c->moduleInstanceId!=module||c->material==SDF_MATERIAL_HOOF)continue;
            for(int j=0;j<=12;++j) {
                Vector3 p=Vec3_Lerp(c->a,c->b,j/12.f),mirror=p;mirror.x=-mirror.x;
                float a=MonsterSDF_EvaluateDistance(&local,p),b=MonsterSDF_EvaluateDistance(&opposite,mirror);
                TEST_ASSERT(MonsterSDF_EvaluateDistance(sdf,p)<0,"Tejido continuo en la cadena muscular, sin centros externos aislados");
                TEST_ASSERT(fabsf(a-b)<.0001f*scale,"Simetría del campo muscular sin constantes por lado");
            }
        }
    }
    for(size_t i=0;i<sdf->connectorCount;++i) {
        const MonsterSDFConnector* c=&sdf->connectors[i];
        if(c->kind!=BODY_CONNECTION_AXIAL_LOFT||c->axialSweepMember)continue;
        const AnatomyNode* a=AnatomyGraph_FindNode(&m->anatomyGraph,c->fromId);
        const AnatomyNode* b=AnatomyGraph_FindNode(&m->anatomyGraph,c->toId);
        if(a->region!=ANATOMY_REGION_TAIL&&b->region==ANATOMY_REGION_TAIL)
            TEST_ASSERT(FLOAT_NEAR(c->widthA,b->widthRadius)&&FLOAT_NEAR(c->heightA,b->heightRadius),"El pedículo caudal no duplica la pelvis");
    }
    AnatomyDeformer* d=AnatomyDeformer_Create();
    TEST_ASSERT(d&&AnatomyDeformer_BindSkeleton(d,&mesh,&m->anatomyGraph,&m->animation->rig.skeleton),"Vincular estaciones de piel y envolventes cíclicas al árbol IK");
    SkeletonPose rest;
    TEST_ASSERT(SkeletonPose_Init(&rest,&m->animation->rig.skeleton)&&AnatomyDeformer_DeformPose(d,&m->animation->rig.skeleton,&rest,&mesh),"Deformación de reposo");
    for(size_t i=0;i<mesh.vertexCount;++i)
        TEST_ASSERT(Vec3_Distance(mesh.vertices[i].position,d->basePositions[i])<.0001f*scale,"Vinculación conserva posiciones de reposo");
    float maxDisplacement=0;
    for(int frame=0;frame<30;++frame) {
        m->animation->animator.desiredVelocity=Vec3_Create(0,0,.8f*scale);
        TEST_ASSERT(MonsterAnimation_Update(m,1.f/60)&&AnatomyDeformer_DeformPose(d,&m->animation->rig.skeleton,&m->animation->pose,&mesh),"Treinta poses musculares sin fallo de vinculación");
        for(size_t i=0;i<mesh.vertexCount;++i) {
            TEST_ASSERT(isfinite(Vec3_LengthSq(mesh.vertices[i].position)),"Posiciones finitas durante marcha");
            maxDisplacement=fmaxf(maxDisplacement,Vec3_Distance(mesh.vertices[i].position,d->basePositions[i]));
        }
    }
    TEST_ASSERT(maxDisplacement>.001f*scale,"La piel vinculada sigue la pose, no queda congelada");
    AnatomyDeformer_Free(d);Mesh_Free(&mesh);
}

static void MuzzlePigment(Monster* m) {
    const AnatomyNode* head=AnatomyGraph_FindFirstRegion(&m->anatomyGraph,ANATOMY_REGION_HEAD);
    float scale=m->phenotype.axial.totalScale;
    SurfaceCoordinate c={.position=Vec3_Scale(Vec3_Add(head->center,m->head.anatomy.landmarks.muzzleTip),1/scale),
        .region=SURFACE_REGION_HEAD,.secondaryRegion=SURFACE_REGION_HEAD,.integumentMask=1};
    TEST_ASSERT(m->surface.pigment.layerCount==2,"Hocico con capa perinasal y labios");
    const PigmentLayer* l=&m->surface.pigment.layers[0];
    TEST_ASSERT(l->domain.anchorNode==0&&m->phenotype.surface.pigment.layers[0].domain.anchorNode==head->id,"Ancla original conservada y runtime resuelto");
    TEST_ASSERT(PigmentDomain_Weight(&l->domain,c.position,l->seed)>.5f,"Pigmento sigue hocico real durante crecimiento");
    SurfaceLocalMaterial local=Surface_EvaluateLocalMaterial(&m->surface,&c,.65f);
    TEST_ASSERT(local.furCoverage<.99f&&local.roughness<.65f,"Hocico con respuesta distinta del manto");
    c.position=Vec3_Scale(head->center,1/scale);
    local=Surface_EvaluateLocalMaterial(&m->surface,&c,.65f);
    TEST_ASSERT(local.furCoverage==1&&local.roughness==.65f,"Cráneo sin cambios colaterales");
    SurfaceRecipe before=SurfaceRecipe_Compile(&m->surface);
    if(m->animation){m->animation->animator.desiredVelocity=Vec3_Create(0,0,.8f);MonsterAnimation_Update(m,.05f);}
    SurfaceRecipe after=SurfaceRecipe_Compile(&m->surface);
    TEST_ASSERT(!memcmp(&before,&after,sizeof(before)),"Animación no desplaza máscara en reposo");
}

static void AsyncMuzzleDomain(const CreatureRecipe* recipe) {
    Monster young=Monster_Create(),adult=Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&young,recipe,&recipe->juvenile)&&Creature_BuildMonster(&adult,recipe,&recipe->adult),"Extremos pigmentarios async");
    MonsterVisualAsyncConfig config=MonsterVisualAsync_DefaultConfig();
    config.interactiveMesherConfig.voxelSize=.16f;config.settledDelaySec=1000;
    MonsterVisualAsync* visual=MonsterVisualAsync_Create(config);TEST_ASSERT(visual,"Crear async pigmentario");
    MonsterVisualAsync_Update(visual,&young,.016f);MonsterVisualAsync_Flush(visual);
    MonsterVisualAsync_Update(visual,&young,.016f);
    const SurfaceRecipe* actual=&visual->displayMesh.surfaceRecipe;
    SurfaceRecipe expected=SurfaceRecipe_Compile(&adult.surface);
    TEST_ASSERT(actual->data[SURFACE_RECIPE_METADATA_ROW][1]==1,"Productor declara marco canónico adulto");
    for(int axis=0;axis<3;++axis)
        TEST_ASSERT(FLOAT_NEAR(actual->data[SURFACE_RECIPE_DOMAIN_BASE][axis],expected.data[SURFACE_RECIPE_DOMAIN_BASE][axis]),"Máscara y malla remallada usan el mismo marco adulto");
    uint64_t requests=MonsterVisualAsync_GetStats(visual).requestCount;
    young.surface.pigment.layers[0].color.r++;
    MonsterVisualAsync_Update(visual,&young,.016f);
    TEST_ASSERT(MonsterVisualAsync_GetStats(visual).requestCount==requests,"Editar hocico async no remalla SDF");
    MonsterVisualAsync_Free(visual);Monster_Free(&young);Monster_Free(&adult);
}

static void FeetAndStages(const char* path) {
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* m=malloc(sizeof(*m));char error[256];
    TEST_ASSERT(doc&&m,"Reservar receta équida");
    TEST_ASSERT(CreatureRecipeIO_LoadFile(doc,path,error,sizeof(error)),"Cargar documento équido v1");
    const CreatureRecipe* r=&doc->recipe;
    AsyncMuzzleDomain(r);
    for(unsigned stage=0;stage<4;++stage) {
        const CreaturePhenotype* p=CreatureRecipe_GetStage(r,(CreatureStage)stage);
        *m=Monster_Create();TEST_ASSERT(Creature_BuildMonster(m,r,p),"Resolver cuatro etapas por la tubería común");
        TEST_ASSERT(m->animation&&m->animation->rig.limbCount==4,"Cuatro cadenas locomotoras ungulígradas");
        TEST_ASSERT(HeadAnatomy_Validate(&m->head.anatomy)==HEAD_VALID,"Cráneo de receta válido");
        MonsterSDF sdf=MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf,m,MonsterSDF_DefaultConfig()),"Compilar campo y materiales");
        MuscleCohesion(m,&sdf);
        MuzzlePigment(m);
        TEST_ASSERT(m->mouths[0].openFactor==0,"Équido neutro con boca cerrada, sin heredar apertura 0.5");
        const MonsterSDFMouth* mouth=&sdf.mouths[0];
        TEST_ASSERT(mouth->independentMandible&&mouth->jawPosteriorZ<=mouth->hingeCenterLocal.z,"La rama alcanza el cóndilo, no la comisura");
        MonsterSDFJawField context;SDFField jaw=MonsterSDF_GetJawField(&sdf,0,&context);
        MonsterSDFHeadField hc;SDFField upper=MonsterSDF_GetHeadField(&sdf,0,&hc);
        for(int j=0;j<3;++j) {
            float z=mouth->faceTipLocal.z+mouth->faceCapDepth*(j*.35f);
            float lip=mouth->entranceCenterLocal.y+mouth->oralSlope*(z-mouth->entranceCenterLocal.z);
            float tolerance=mouth->entranceHalfExtents.y*5;
            Vector3 lower={0,lip-tolerance,z},higher={0,lip+tolerance,z};
            higher=Vec3_Add(mouth->center,Transform3D_RotateVector(m->mouths[0].rotation,higher));
            if(jaw.evaluateDistance(jaw.context,lower)>=0||upper.evaluateDistance(upper.context,higher)>=0)
                fprintf(stderr,"lip %s stage=%u j=%d lower=%g upper=%g z=%g y=%g tol=%g\n",path,stage,j,jaw.evaluateDistance(jaw.context,lower),upper.evaluateDistance(upper.context,higher),z,lip,tolerance);
            TEST_ASSERT(jaw.evaluateDistance(jaw.context,lower)<0&&upper.evaluateDistance(upper.context,higher)<0,"Contacto labial cerrado sin sobremordida hueca");
        }
        for(int j=0;j<6;++j) {
            Vector3 p=mouth->jawStations[j].center;
            p.x=mouth->jawStations[j].width*.65f;
            TEST_ASSERT(jaw.evaluateDistance(jaw.context,p)<0,"Tejido mandibular en rama, ángulo, cuerpo y sínfisis");
            float distance=jaw.evaluateDistance(jaw.context,p);
            sdf.mouths[0].faceCapWeight=0;
            TEST_ASSERT(FLOAT_NEAR(distance,jaw.evaluateDistance(jaw.context,p)),"El casquete superior no recorta la mandíbula");
            sdf.mouths[0].faceCapWeight=1;
        }
        SDFMesherConfig jc=SDFMesher_DefaultConfig();jc.voxelSize=.025f*m->phenotype.axial.totalScale;
        jc.maxCells=600000;jc.adaptiveDetail=false;
        SDFMesher jm=SDFMesher_Create(jc);Mesh mesh=Mesh_Create();
        TEST_ASSERT(SDFMesher_GenerateMesh(&jm,&jaw,&mesh)&&mesh.vertexCount>0,"Extraer mandíbula real separada");
        float lo=INFINITY,hi=-INFINITY;
        unsigned bins[5]={0};float length=mouth->jawStations[0].center.z-mouth->hingeCenterLocal.z;
        size_t* parents=malloc(mesh.vertexCount*sizeof(*parents));TEST_ASSERT(parents,"Conectividad mandibular");
        for(size_t i=0;i<mesh.vertexCount;++i) {
            parents[i]=i;float z=mesh.vertices[i].position.z;lo=fminf(lo,z);hi=fmaxf(hi,z);
            int bin=(int)((z-mouth->hingeCenterLocal.z)/length*5);
            if(bin<0)bin=0;
            if(bin>4)bin=4;
            ++bins[bin];
        }
        for(size_t i=0;i<mesh.indexCount;i+=3)for(int k=1;k<3;++k) {
            size_t a=mesh.indices[i],b=mesh.indices[i+k];
            while(parents[a]!=a){parents[a]=parents[parents[a]];a=parents[a];}
            while(parents[b]!=b){parents[b]=parents[parents[b]];b=parents[b];}
            parents[b]=a;
        }
        size_t components=0;
        for(size_t i=0;i<mesh.vertexCount;++i)if(parents[i]==i)++components;
        TEST_ASSERT(components==1,"Sin islas mandibulares ni lengua desprendida");
        TEST_ASSERT(lo<mouth->hingeCenterLocal.z+length*.08f&&hi>mouth->hingeCenterLocal.z+length*.94f,"Malla desde cóndilo hasta sínfisis");
        for(int j=0;j<5;++j)TEST_ASSERT(bins[j]>0,"Geometría distribuida por todo el trayecto mandibular");
        free(parents);Mesh_Free(&mesh);SDFMesher_Free(&jm);
        MonsterVisualMouth vm;
        TEST_ASSERT(MonsterVisual_BuildMouthMeshesFromSDF(&vm,&m->mouths[0],m,&sdf,0),"Construir malla articulada");
        const float factors[]={0,.35f,1};Vector3 chin=vm.jaw.vertices[0].position;
        for(int pose=0;pose<3;++pose) {
            Monster_SetHeadOpenFactor(m,factors[pose]);
            MonsterVisual_UpdateMouthArticulation(&vm,&m->mouths[0],m);
            TEST_ASSERT(FLOAT_NEAR(Mouth_GetJawAngle(&m->mouths[0]),factors[pose]*m->mouths[0].maxJawAngle),"Ángulo articulado alrededor de bisagra");
            TEST_ASSERT(vm.jaw.vertexCount==vm.jawBase.vertexCount,"Articulación sin remallar ni perder rama");
        }
        TEST_ASSERT(Vec3_Distance(chin,vm.jaw.vertices[0].position)>.001f,"Apertura mueve la malla real");
        Monster_SetHeadOpenFactor(m,0);MonsterVisualMouth_Free(&vm);
        for(unsigned leg=0;leg<4;++leg) {
            uint32_t module=10+leg;
            const AnatomyNode* foot=AnatomyGraph_FindModuleNode(&m->anatomyGraph,module,LIMB_NODE_AUTOPOD);
            const AnatomyNode* heel=AnatomyGraph_FindModuleNode(&m->anatomyGraph,module,Limb_DigitLocalId(0,1));
            const AnatomyNode* toe=AnatomyGraph_FindModuleNode(&m->anatomyGraph,module,Limb_DigitLocalId(0,2));
            TEST_ASSERT(foot&&heel&&toe&&!AnatomyGraph_FindModuleNode(&m->anatomyGraph,module,Limb_DigitLocalId(1,0)),"Un dedo principal, sin segundo casco escondido");
            TEST_ASSERT(fabsf(heel->center.x-foot->center.x)<1e-5f&&fabsf(toe->center.x-foot->center.x)<1e-5f,"Casco central alineado con metapodio");
            LimbRigDescriptor rig;
            TEST_ASSERT(Limb_BuildRigDescriptor(&p->limbs[leg],module,&m->anatomyGraph,&rig),"Descriptor compartido de apoyo");
            float sole=foot->center.y-rig.soleHeight;
            TEST_ASSERT(fabsf(sole)<1e-5f,"Los cuatro cascos llegan al suelo de la receta");
            TEST_ASSERT(fabsf(heel->center.y-heel->heightRadius-sole)<1e-5f,"El rig y la cápsula usan la misma suela");
            unsigned count=0;
            for(size_t i=0;i<sdf.connectorCount;++i) {
                const MonsterSDFConnector* c=&sdf.connectors[i];
                if(c->moduleInstanceId!=module||c->material!=SDF_MATERIAL_HOOF)continue;
                ++count;
                for(unsigned j=0;j<=12;++j) {
                    Vector3 point=Vec3_Lerp(c->a,c->b,j/12.f);
                    TEST_ASSERT(MonsterSDF_EvaluateDistance(&sdf,point)<=1e-5f,"Continuidad longitudinal de queratina");
                    Vector3 h={.004f,.004f,.004f};AABB3D cell={Vec3_Sub(point,h),Vec3_Add(point,h)};
                    SDFField field=MonsterSDF_GetField(&sdf);float lo,hi;
                    float value=MonsterSDF_EvaluateDistance(&sdf,point);
                    TEST_ASSERT(field.getCellRange(field.context,cell,&lo,&hi)&&value>=lo&&value<=hi,"Cotas conservadoras del casco central");
                }
            }
            TEST_ASSERT(count==1,"Exactamente una cápsula de casco por miembro");
            for(unsigned j=0;j<=16;++j) {
                Vector3 point=Vec3_Lerp(foot->center,heel->center,j/16.f);
                TEST_ASSERT(MonsterSDF_EvaluateDistance(&sdf,point)<=1e-5f,"Casco conectado al menudillo, sin isla flotante");
            }
        }
        TEST_ASSERT(MonsterAnimation_Update(m,1.f/60),"La pose usa el rig existente");
        MonsterSDF_Free(&sdf);Monster_Free(m);
    }
    CreaturePhenotype invalid=r->adult;
    invalid.limbs[0].digitCount=3;
    TEST_ASSERT(!CreatureRecipe_Validate(r,&invalid),"Rechazar tres cascos en un contrato ungulado mono/bidáctilo");
    invalid=r->adult;invalid.limbs[0].digitCount=0;
    TEST_ASSERT(!CreatureRecipe_Validate(r,&invalid),"Rechazar ungulado sin casco");
    invalid=r->adult;invalid.limbs[0].dewclawSize=.2f;
    TEST_ASSERT(!CreatureRecipe_Validate(r,&invalid),"El casco único no genera espolones de rumiante");
    Monster* juvenile=malloc(sizeof(*juvenile));Monster* adult=malloc(sizeof(*adult));
    TEST_ASSERT(juvenile&&adult,"Reservar transición équida");
    *juvenile=Monster_Create();*adult=Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(juvenile,r,&r->juvenile)&&Creature_BuildMonster(adult,r,&r->adult),"Extremos de crecimiento");
    TEST_ASSERT(CreatureMorph_CollectTopologyEvents(juvenile,adult,NULL,0)==0,"Crecimiento équido sin cambios de topología falsos");
    for(unsigned step=0;step<=10;++step) {
        *m=Monster_Create();
        TEST_ASSERT(CreatureMorph_Interpolate(juvenile,adult,step/10.f,m),"Metamorfosis juvenil-adulto real");
        MuzzlePigment(m);
        TEST_ASSERT(m->anatomyGraph.nodeCount==adult->anatomyGraph.nodeCount,"Cantidad estable de estaciones durante crecimiento");
        for(size_t i=0;i<m->anatomyGraph.nodeCount;++i) {
            const AnatomyNode* n=&m->anatomyGraph.nodes[i];
            TEST_ASSERT(isfinite(n->center.x)&&isfinite(n->center.y)&&isfinite(n->center.z),"Estaciones de crecimiento finitas");
            TEST_ASSERT(AnatomyGraph_FindNode(&adult->anatomyGraph,n->id),"IDs estables durante crecimiento");
        }
        Monster_Free(m);
    }
    Monster_Free(juvenile);Monster_Free(adult);free(juvenile);free(adult);free(m);free(doc);
}

void run_equine_anatomy_tests(void) {
    CranialComposition();
    FeetAndStages("recipes/caballo.creature.json");
    FeetAndStages("recipes/asno.creature.json");
    puts("[PASS] test_equine_anatomy: cráneo, casco único, apoyo, cuatro etapas, rig y metamorfosis");
}
