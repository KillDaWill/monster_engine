/** @file test_anatomical_reach.c
 * @brief Regresiones de contacto, límites, tiempo, recursos y recompilación. */
#include "AnatomicalReach.h"
#include "Creature.h"
#include "Monster.h"
#include "CreatureRecipes.h"
#include "MonsterAnimation.h"
#include "CreatureRig.h"
#include "GaitPresets.h"
#include "AnatomyDeformer.h"
#include "MonsterVisual.h"
#include "ai_test_helpers.h"
static Rig simpleRig(void) {
    Rig r={0};r.headJoint=3;r.neckJoint=2;r.cervicalBaseJoint=1;r.jawJoint=-1;
    r.oralPoint=Vec3_Create(0,0,.3f);r.skeleton.jointCount=4;
    for(int k=0;k<4;++k)r.skeleton.joints[k]=(SkeletonJoint){.id=(unsigned)k+1,.parentIndex=k-1,
        .restPosition={0,k==0?1:0,k>1?.4f:0},.restRotation={0,0,0,1},
        .constraint={.type=k?JOINT_BALL:JOINT_FIXED,.hingeAxis={0,0,1},.maxSwingAngle=.8f,.maxTwistAngle=.2f}};
    return r;
}
static Vector3 oral(const Rig *r,const SkeletonPose *p) {
    return Vec3_Add(p->joints[r->headJoint].worldPosition,Quat_RotateVector(p->joints[r->headJoint].worldRotation,r->oralPoint));
}
static void checkPose(const Rig *r,const SkeletonPose *p) {
    for(size_t k=0;k<r->skeleton.jointCount;++k) {
        const SkeletonJoint *j=&r->skeleton.joints[k];
        TEST_ASSERT(isfinite(p->joints[k].worldPosition.y),"pose finita");
        if(j->parentIndex>=0)TEST_ASSERT(fabsf(Vec3_Distance(p->joints[k].worldPosition,p->joints[j->parentIndex].worldPosition)-Vec3_Length(j->restPosition))<1e-5f,"huesos constantes");
        Quaternion q=p->joints[k].localRotation,c=Skeleton_ConstrainRotation(j,q);
        TEST_ASSERT(Vec3_Distance(Quat_RotateVector(q,Vec3_Create(0,0,1)),Quat_RotateVector(c,Vec3_Create(0,0,1)))<1e-5f,"límites duros");
    }
}
static void solverTests(void) {
    Rig r=simpleRig();AnatomicalReachProfile profile;
    TEST_ASSERT(AnatomicalReach_Compile(&profile,&r,AnatomicalReach_DefaultPreset()),"perfil cervical");
    Rig invalid=r;invalid.oralDirection.x=NAN;AnatomicalReachProfile rejected;
    TEST_ASSERT(!AnatomicalReach_Compile(&rejected,&invalid,AnatomicalReach_DefaultPreset()),"dirección oral inválida rechazada");
    SkeletonPose stalePose;SkeletonPose_Init(&stalePose,&r.skeleton);AnatomicalReachState staleState={0};
    Rig changed=r;changed.skeleton.joints[2].restPosition.z+=.1f;
    TEST_ASSERT(AnatomicalReach_Update(&profile,&staleState,&changed.skeleton,&stalePose,NULL,.016f).status==REACH_INVALID,"cache incompatible rechazado");
    for(int sample=0;sample<4;++sample) {
        SkeletonPose reference;SkeletonPose_Init(&reference,&r.skeleton);
        reference.joints[1].localRotation=Quat_FromAxisAngle(Vec3_Create(1,0,0),sample==0?.35f:sample==1?-.3f:.1f);
        reference.joints[2].localRotation=Quat_FromAxisAngle(Vec3_Create(0,1,0),sample==2?.3f:0);
        SkeletonPose_UpdateWorldTransforms(&reference,&r.skeleton);
        InteractionTarget t={.objectId=12,.valid=true,.normal={0,1,0},.position=oral(&r,&reference),
            .approach=Quat_RotateVector(reference.joints[3].worldRotation,Vec3_Create(0,0,1)),
            .positionTolerance=.02f,.orientationTolerance=.3f};
        Vector3 endpoints[3];
        for(int rate=0;rate<3;++rate) {
            int hz=rate==0?30:rate==1?60:120;SkeletonPose pose;SkeletonPose_Init(&pose,&r.skeleton);
            AnatomicalReachState state={0};ReachResult result={0};
            Quaternion previousRotation[REACH_MAX_JOINTS];for(unsigned k=0;k<profile.count;++k)previousRotation[k]=Quat_Identity();
            for(int frame=0;frame<hz*5;++frame) {
                Vector3 previous[REACH_MAX_JOINTS];for(unsigned k=0;k<profile.count;++k)previous[k]=state.velocity[k];
                result=AnatomicalReach_Update(&profile,&state,&r.skeleton,&pose,&t,1.f/hz);
                checkPose(&r,&pose);
                for(unsigned k=0;k<profile.count;++k) {
                    Quaternion q=state.rotation[k],old=previousRotation[k];
                    float dot=fabsf(q.x*old.x+q.y*old.y+q.z*old.z+q.w*old.w);
                    TEST_ASSERT(2*acosf(fminf(1,dot))<=profile.preset.velocity/hz+.0002f,"rotación publicada continua");
                    previousRotation[k]=q;
                    TEST_ASSERT(Vec3_Length(state.velocity[k])<=profile.preset.velocity+1e-5f,"velocidad limitada");
                    TEST_ASSERT(Vec3_Distance(previous[k],state.velocity[k])<=profile.preset.acceleration/hz+1e-4f,"aceleración limitada");
                }
                TEST_ASSERT(result.iterations<=profile.preset.iterations,"trabajo acotado a 30 Hz");
            }
            printf("[REACH] caso=%d hz=%d estado=%d error=%.6f orient=%.6f estable=%.3f\n",sample,hz,result.status,result.error,result.orientationError,result.stableTime);
            TEST_ASSERT(result.status==REACH_REACHED&&result.error<=t.positionTolerance&&result.stableTime>.25f,"contacto real sostenido");
            endpoints[rate]=result.mouth;
            t.position.y+=10;
            result=AnatomicalReach_Update(&profile,&state,&r.skeleton,&pose,&t,1.f/hz);
            TEST_ASSERT(result.status==REACH_UNREACHABLE&&result.error>9&&result.stableTime==0,"diez metros imposible");
            t.position.y-=10;t.blocked=true;
            result=AnatomicalReach_Update(&profile,&state,&r.skeleton,&pose,&t,1.f/hz);
            TEST_ASSERT(result.status==REACH_BLOCKED,"obstrucción explícita");t.blocked=false;
            t.position.x=NAN;
            result=AnatomicalReach_Update(&profile,&state,&r.skeleton,&pose,&t,1.f/hz);
            TEST_ASSERT(result.status==REACH_INVALID,"NaN rechazado");t.position=oral(&r,&reference);
        }
        TEST_ASSERT(Vec3_Distance(endpoints[0],endpoints[1])<1e-5f&&Vec3_Distance(endpoints[1],endpoints[2])<1e-5f,"mismo resultado a 30, 60 y 120 Hz");
    }
}
static void resourceTests(void) {
    AIWorld w=AI_TestWorld();AIObjectId food=AIWorld_Add(&w,(AIWorldObject){.type=AI_FOOD,.position={0,1,0},.height=.2f,.radius=.4f,.visible=true,.walkable=true});
    InteractionTarget t;TEST_ASSERT(AIWorld_InteractionTarget(&w,food,Vec3_Create(.2f,2,0),.025f,&t),"superficie alimentaria");
    TEST_ASSERT(FLOAT_NEAR(t.position.x,.2f)&&FLOAT_NEAR(t.position.y,1.2f),"superficie real por encima del centro");
    BrainProfile p=BrainProfile_Default();p.needs[NEED_HUNGER]=(NeedDefinition){true,0,1,.9f,1,.15f};
    p.weights[ACTION_EAT]=p.weights[ACTION_APPROACH_FOOD]=p.weights[ACTION_SEARCH_FOOD]=2;
    CreatureBodyState b=AI_TestBody();b.mouth=t.position;b.mouthDirection=t.approach;
    CreatureMind m;CreatureMind_Init(&m,&p,80,12);m.needs.urgency[NEED_HUNGER]=.9f;m.time=2;
    AI_TestObserve(&m,&w,b);m.target=food;m.action=ACTION_EAT;
    CreatureActions_Update(&m,&w,&b,.1f);
    TEST_ASSERT(m.needs.urgency[NEED_HUNGER]==.9f,"proximidad sin certificación no consume");
    b.mouthDirection=m.interaction.approach;b.reach=(ReachResult){.status=REACH_REACHED,.objectId=food};
    for(int k=0;k<2;++k)CreatureActions_Update(&m,&w,&b,.1f);
    TEST_ASSERT(m.needs.urgency[NEED_HUNGER]==.9f,"contacto breve no consume");
    Vector3 validDirection=b.mouthDirection;b.mouthDirection=Vec3_Scale(validDirection,-1);
    CreatureActions_Update(&m,&w,&b,.1f);
    TEST_ASSERT(m.needs.urgency[NEED_HUNGER]==.9f&&m.contactTime==0,"orientación actual prevalece sobre resultado antiguo");
    b.mouthDirection=validDirection;
    for(int k=0;k<3;++k)CreatureActions_Update(&m,&w,&b,.1f);
    TEST_ASSERT(m.needs.urgency[NEED_HUNGER]<.9f,"hambre satisfecha tras contacto estable");
    float need=m.needs.urgency[NEED_HUNGER];AIWorld_Move(&w,food,Vec3_Create(0,11,0));
    CreatureActions_Update(&m,&w,&b,.1f);
    TEST_ASSERT(m.needs.urgency[NEED_HUNGER]==need&&m.contactTime==0,"movimiento invalida contacto antiguo");
    b.reach.status=REACH_UNREACHABLE;
    for(int k=0;k<400;++k) {m.time+=.1f;CreatureMind_Update(&m,&w,&b,.1f);}
    TEST_ASSERT(m.completedMeals==0&&m.actionFailures>0&&m.actionFailures<20,"fallo finito con cooldown");
    CreatureActions_Cancel(&m);
    TEST_ASSERT(m.target==0&&!m.intent.interact&&m.interactionPhase==INTERACTION_CANCELLED,"cancelación explícita");
    AIObjectId barrier=AIWorld_Add(&w,(AIWorldObject){.type=AI_OBSTACLE,.position={0,2,0},.radius=1,.height=1,.occludes=true});
    TEST_ASSERT(!AIWorld_LineOfSight(&w,Vec3_Create(0,5,0),Vec3_Create(0,0,0),0),"rayo vertical bloqueado en 3D");
    TEST_ASSERT(AIWorld_LineOfSight(&w,Vec3_Create(0,5,0),Vec3_Create(0,4,0),0),"oclusor debajo no tapa rayo alto");
    AIWorld_Remove(&w,barrier);
}
static void rebuildTests(void) {
    for(int species=0;species<2;++species) {
        static Monster m; m=Monster_Create();const CreatureRecipe *recipe=species?CreatureRecipes_Dog():CreatureRecipes_Lizard();
        TEST_ASSERT(Creature_BuildMonster(&m,recipe,&recipe->adult),"especie procedural");
        Rig rig;CreatureRig_Build(&m,&rig);GaitProfile gait=species?GaitPreset_CanidWalk(&rig):GaitPreset_SprawlingQuadruped(1);
        TEST_ASSERT(MonsterAnimation_Configure(&m,&rig,gait),"rig corporal");
        MonsterAnimation_Update(&m,1.f/60);
        AnatomicalReachProfile old=m.animation->animator.reachProfile;
        m.animation->animator.oralReachActive=true;
        m.animation->animator.oralReachTarget=oral(&rig,&m.animation->pose);
        for(int k=0;k<240;++k)TEST_ASSERT(MonsterAnimation_Update(&m,1.f/60),"gesto genérico");
        TEST_ASSERT(m.animation->animator.reachResult.status==REACH_REACHED,"boca neutral alcanzable en perro y lagarto");
        Vector3 impossible=oral(&rig,&m.animation->pose);impossible.y+=10;
        m.animation->animator.oralReachTarget=impossible;
        Vector3 root=m.animation->pose.joints[0].worldPosition;
        for(int frame=0;frame<120;++frame) {
            TEST_ASSERT(MonsterAnimation_Update(&m,1.f/60),"rechazo corporal de altura imposible");
            TEST_ASSERT(m.animation->animator.reachResult.status==REACH_UNREACHABLE,"cuerpo no inventa alcance");
            TEST_ASSERT(m.animation->animator.reachCrouch<=m.animation->animator.reachProfile.maximumCrouch,"compensación acotada");
            TEST_ASSERT(fabsf(root.x-m.animation->pose.joints[0].worldPosition.x)<1e-5f&&fabsf(root.z-m.animation->pose.joints[0].worldPosition.z)<1e-5f,"raíz no se traslada hacia comida");
            for(size_t k=0;k<rig.limbCount;++k)if(rig.limbs[k].walking)TEST_ASSERT(m.animation->animator.limbResults[k].error<.004f,"pies plantados durante alcance fallido");
        }
        TEST_ASSERT(Creature_BuildMonster(&m,recipe,&recipe->juvenile),"morfología juvenil recompila");
        CreatureRig_Build(&m,&rig);gait=species?GaitPreset_CanidWalk(&rig):GaitPreset_SprawlingQuadruped(1);
        TEST_ASSERT(MonsterAnimation_Configure(&m,&rig,gait),"perfil recompilado");
        TEST_ASSERT(fabsf(old.maximumReach-m.animation->animator.reachProfile.maximumReach)>.001f&&m.animation->animator.reachState.stableTime==0,"longitudes nuevas invalidan contacto");
        checkPose(&rig,&m.animation->pose);Monster_Free(&m);
    }
}
static void auxiliarySkinTest(void) {
    AnatomyGraph graph;AnatomyGraph_Init(&graph);
    for(unsigned k=0;k<2;++k)TEST_ASSERT(AnatomyGraph_AddNode(&graph,(AnatomyNode){.id=k+1,
        .center={0,0,(float)k},.widthRadius=.1f,.heightRadius=.1f,.development=1}),"nodo cervical");
    TEST_ASSERT(AnatomyGraph_Connect(&graph,(BodyConnection){.id=3,.fromId=1,.toId=2,
        .kind=BODY_CONNECTION_AXIAL_LOFT,.development=1}),"conexión cervical");
    Skeleton sk={.jointCount=3};
    sk.joints[0]=(SkeletonJoint){.id=1,.anatomyId=1,.parentIndex=-1,.restRotation={0,0,0,1}};
    sk.joints[1]=(SkeletonJoint){.id=4,.parentIndex=0,.restRotation={0,0,0,1},
        .constraint={.type=JOINT_HINGE,.hingeAxis={1,0,0},.minAngle=-.8f,.maxAngle=.8f}};
    sk.joints[2]=(SkeletonJoint){.id=2,.anatomyId=2,.parentIndex=1,.restPosition={0,0,1},.restRotation={0,0,0,1}};
    Mesh mesh=Mesh_Create();MeshIndex index;
    TEST_ASSERT(Mesh_AddVertex(&mesh,(MeshVertex){.position={.1f,0,.25f},.normal={1,0,0}},&index),"vértice de cuello");
    AnatomyDeformer *d=AnatomyDeformer_Create();
    TEST_ASSERT(AnatomyDeformer_BindSkeleton(d,&mesh,&graph,&sk),"pivote auxiliar enlazado");
    SkeletonPose pose;SkeletonPose_Init(&pose,&sk);pose.joints[1].localRotation=Quat_FromAxisAngle(Vec3_Create(1,0,0),.6f);
    SkeletonPose_UpdateWorldTransforms(&pose,&sk);
    TEST_ASSERT(AnatomyDeformer_DeformPose(d,&sk,&pose,&mesh),"deformación cervical");
    Vector3 expected=Quat_RotateVector(pose.joints[1].localRotation,Vec3_Create(.1f,0,.25f));
    TEST_ASSERT(Vec3_Distance(mesh.vertices[0].position,expected)<1e-6f,"cuello sigue pivote sin anclaje al tórax");
    TEST_ASSERT(fabsf(Vec3_Length(mesh.vertices[0].position)-sqrtf(.1f*.1f+.25f*.25f))<1e-6f,"volumen local no se estira por pivote omitido");
    AnatomyDeformer_Free(d);Mesh_Free(&mesh);
}
static void cranialSkinTest(void) {
    static Monster m;
    for(int species=0;species<2;++species) {
        m=Monster_Create();const CreatureRecipe* recipe=species?CreatureRecipes_Dog():CreatureRecipes_Lizard();
        TEST_ASSERT(Creature_BuildMonster(&m,recipe,&recipe->adult),"Crear cráneo articulado");
        Rig rig;TEST_ASSERT(CreatureRig_Build(&m,&rig),"Resolver articulación cefálica");
        const HeadLandmarks* l=&m.head.anatomy.landmarks;
        Vector3 host=m.bodyParts[m.head.anatomy.attachmentBodyPartIndex].positionRender;
        Vector3 points[]={l->skullCenter,l->muzzleRoot,l->muzzleTip,l->leftMouthCorner,
            l->leftNostril,l->leftEyeCenter,l->leftEarBase,l->mandibularSymphysis};
        Mesh mesh=Mesh_Create();TEST_ASSERT(Mesh_ReserveVertices(&mesh,8),"Muestras rígidas de cara");mesh.vertexCount=8;
        for(int k=0;k<8;++k)mesh.vertices[k]=(MeshVertex){.position=Vec3_Add(host,points[k]),.normal={0,1,0}};
        AnatomyDeformer* d=AnatomyDeformer_Create();
        TEST_ASSERT(AnatomyDeformer_BindSkeleton(d,&mesh,&m.anatomyGraph,&rig.skeleton),"Vincular cara");
        SkeletonPose p;SkeletonPose_Init(&p,&rig.skeleton);
        p.joints[rig.cervicalBaseJoint].localRotation=Quat_FromAxisAngle(Vec3_Create(1,0,0),1.0f);
        p.joints[rig.neckJoint].localRotation=Quat_FromAxisAngle(Vec3_Create(1,0,0),.45f);
        p.joints[rig.headJoint].localRotation=Quat_FromAxisAngle(Vec3_Create(0,1,0),.35f);
        SkeletonPose_UpdateWorldTransforms(&p,&rig.skeleton);
        AnatomyPosePalette palette;AnatomyDeformer_BuildPosePalette(d,&rig.skeleton,&p,&palette);
        Vector3 restHead=d->bindPose.joints[rig.headJoint].worldPosition;
        Quaternion q=Quat_Multiply(p.joints[rig.headJoint].worldRotation,Quat_Inverse(d->bindPose.joints[rig.headJoint].worldRotation));
        float previousError=0;
        for(int k=0;k<8;++k) {
            Vector3 actual,n,expected=Vec3_Add(p.joints[rig.headJoint].worldPosition,
                Quat_RotateVector(q,Vec3_Sub(mesh.vertices[k].position,restHead)));
            AnatomyDeformer_EvaluatePoseVertex(d,&palette,k,&actual,&n);
            previousError=fmaxf(previousError,Vec3_Distance(actual,expected));
        }
        TEST_ASSERT(previousError>.001f,"Reproducir deformación antigua de la cara");
        TEST_ASSERT(AnatomyDeformer_BindCranium(d,&rig.skeleton,rig.headJoint,
            rig.cranialAttachment,rig.cranialAxis,rig.cranialCollar,NULL),"Fijar cráneo sin escalar");
        TEST_ASSERT(AnatomyDeformer_DeformPose(d,&rig.skeleton,&p,&mesh),"Articular cara");
        float error=0;
        for(int k=0;k<8;++k) {
            Vector3 expected=Vec3_Add(p.joints[rig.headJoint].worldPosition,
                Quat_RotateVector(q,Vec3_Sub(d->basePositions[k],restHead)));
            error=fmaxf(error,Vec3_Distance(mesh.vertices[k].position,expected));
            TEST_ASSERT(error<1e-5f,"Cráneo, morro y anclajes orales mantienen forma rígida");
            for(int j=0;j<k;++j)TEST_ASSERT(fabsf(Vec3_Distance(mesh.vertices[k].position,mesh.vertices[j].position)-
                Vec3_Distance(d->basePositions[k],d->basePositions[j]))<1e-5f,"Distancias faciales constantes");
        }
        printf("[CRANIUM] especie=%d error_anterior=%.6f error_rigido=%.8f\n",species,previousError,error);
        for(int k=0;k<8;++k)mesh.vertices[k].position=d->basePositions[k];
        TEST_ASSERT(AnatomyDeformer_BindSkeleton(d,&mesh,&m.anatomyGraph,&rig.skeleton),"Reenlazar muestras faciales");
        /* Una conexión torácica puede ganar la proximidad al cambiar proporciones;
         * la propiedad del campo craneal debe prevalecer sobre ese vecino. */
        for(size_t c=0;c<m.anatomyGraph.connectionCount;++c) {
            const BodyConnection* edge=&m.anatomyGraph.connections[c];
            AnatomyId headId=rig.skeleton.joints[rig.headJoint].anatomyId;
            if(edge->fromId!=headId&&edge->toId!=headId){d->bindings[0].connIndex[0]=(uint16_t)c;break;}
        }
        MonsterSDF sdf=MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf,&m,MonsterSDF_DefaultConfig()),"Campo craneal semántico");
        TEST_ASSERT(AnatomyDeformer_BindCranium(d,&rig.skeleton,rig.headJoint,rig.cranialAttachment,
            rig.cranialAxis,rig.cranialCollar,&sdf),"Prioridad craneal ante vecinos corporales");
        TEST_ASSERT(d->skinBindings[0].joints[0]==rig.headJoint&&d->skinBindings[0].weights[0]>.99999f,
            "Un vecino torácico no vuelve a convertir el cráneo en piel flexible");
        MonsterSDF_Free(&sdf);
        MonsterVisualMouth mouth={0};
        TEST_ASSERT(MonsterVisual_BuildMouthMeshes(&mouth,&m.mouths[0],&m),"Construir mandíbula anatómica");
        Mouth source=m.mouths[0];source.openFactor=0;
        MonsterVisual_UpdateMouthArticulation(&mouth,&source,&m);
        Vector3* closed=malloc(mouth.jaw.vertexCount*sizeof(*closed));
        TEST_ASSERT(closed&&mouth.jaw.vertexCount>0,"Base mandibular cerrada");
        for(size_t v=0;v<mouth.jaw.vertexCount;++v)closed[v]=mouth.jaw.vertices[v].position;
        for(int frame=0;frame<=120;++frame) {
            source.openFactor=(float)frame/120;
            MonsterVisual_UpdateMouthArticulation(&mouth,&source,&m);
            for(size_t v=0;v<mouth.jaw.vertexCount;v+=17) {
                TEST_ASSERT(fabsf(Vec3_Distance(closed[0],closed[v])-Vec3_Distance(
                    mouth.jaw.vertices[0].position,mouth.jaw.vertices[v].position))<1e-5f,
                    "Mandíbula conserva sus longitudes durante toda la apertura");
                Vector3 hinge=Vec3_Add(mouth.worldPosition,Transform3D_RotateVector(mouth.rotation,mouth.pivot));
                TEST_ASSERT(fabsf(Vec3_Distance(closed[v],hinge)-Vec3_Distance(mouth.jaw.vertices[v].position,hinge))<1e-5f,
                    "Mandíbula gira alrededor de su bisagra sin traslación ni estiramiento");
            }
        }
        free(closed);MonsterVisualMouth_Free(&mouth);
        m.mouths[0].rotation=Vec3_Create(0,.35f,.2f);
        Rig rotated;TEST_ASSERT(CreatureRig_Build(&m,&rotated),"Boca con orientación propia");
        SkeletonPose rotatedRest;SkeletonPose_Init(&rotatedRest,&rotated.skeleton);
        Vector3 expectedPivot=Vec3_Add(m.bodyParts[m.mouths[0].bodyPartIndex].positionRender,
            Vec3_Add(m.mouths[0].offset,Transform3D_RotateVector(m.mouths[0].rotation,m.mouths[0].jawPivot)));
        TEST_ASSERT(Vec3_Distance(rotatedRest.joints[rotated.jawJoint].worldPosition,expectedPivot)<1e-5f,
            "La bisagra usa la orientación anatómica real de la boca");
        TEST_ASSERT(Vec3_Distance(rotated.skeleton.joints[rotated.jawJoint].constraint.hingeAxis,
            Transform3D_RotateVector(m.mouths[0].rotation,Vec3_Create(1,0,0)))<1e-5f,"Eje mandibular orientado");
        AnatomyDeformer_Free(d);Mesh_Free(&mesh);Monster_Free(&m);
    }
}
void run_anatomical_reach_tests(void) { solverTests();resourceTests();rebuildTests();auxiliarySkinTest();cranialSkinTest(); }
