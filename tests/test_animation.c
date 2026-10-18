#include "Creature.h"
#include "Limb.h"
#include "test_utils.h"
#include "Skeleton.h"
#include "IK.h"
#include "CreatureRig.h"
#include "Monster.h"
#include "MonsterAger.h"
#include "AnatomyDeformer.h"
#include "MonsterAnimation.h"
#include "GaitPresets.h"
#include "SDFPrimitives.h"
#include "SDFMesher.h"
#include "CreatureBoundary.h"
#include "LizardVariations.h"
#include <string.h>
#include <stdlib.h>
#include <math.h>

static void Near(Vector3 a,Vector3 b) { TEST_ASSERT(Vec3_Distance(a,b)<.001f,"Vectores coinciden"); }
static void TestSubtreeTransforms(void) {
    Skeleton skeleton={0};skeleton.jointCount=6;
    int parents[]={-1,0,0,1,2,3};
    for(size_t i=0;i<6;++i)skeleton.joints[i]=(SkeletonJoint){.id=(AnatomyId)i+1,.parentIndex=parents[i],
        .restPosition={(float)i*.1f,.2f,.3f},.restRotation={0,0,0,1}};
    SkeletonPose partial,full;
    TEST_ASSERT(SkeletonPose_Init(&partial,&skeleton),"Preparar ramas intercaladas");
    for(int frame=0;frame<100;++frame) {
        partial.joints[1].localRotation=Quat_FromAxisAngle(Vec3_Create(0,1,0),frame*.013f);
        full=partial;JointPose sibling=partial.joints[4];
        TEST_ASSERT(SkeletonPose_UpdateSubtree(&partial,&skeleton,1),"Actualizar sólo rama modificada");
        TEST_ASSERT(SkeletonPose_UpdateWorldTransforms(&full,&skeleton),"Referencia completa de la misma pose");
        TEST_ASSERT(memcmp(&sibling,&partial.joints[4],sizeof(sibling))==0,"Rama hermana conserva estado exacto");
        for(size_t i=0;i<6;++i) {
            TEST_ASSERT(Vec3_Distance(partial.joints[i].worldPosition,full.joints[i].worldPosition)<1e-6f,"Actualización parcial equivale a la jerarquía completa");
            Near(Quat_RotateVector(partial.joints[i].worldRotation,Vec3_Create(1,0,0)),
                 Quat_RotateVector(full.joints[i].worldRotation,Vec3_Create(1,0,0)));
        }
    }
    TEST_ASSERT(!SkeletonPose_UpdateSubtree(&partial,&skeleton,6),"Rechazar raíz fuera del esqueleto");
}
static void TestCanidWalkAndTurnContacts(void);
static void TestTailCurvature(void);
static void TestBoundaryFlexion(void);
static void TestBoundary(void);
static void TestBoundaryCandidates(void);
static void TestRigIK(void);
static void TestDeformer(void);
static void TestLocomotion(void);
static void TestProceduralTurning(void);
static void TestMorphContacts(void);
void run_animation_tests(void) {
    TestSubtreeTransforms();
    TestCanidWalkAndTurnContacts();
    TestTailCurvature();
    TestBoundaryFlexion();
    TestBoundary();
    TestBoundaryCandidates();
    TestRigIK();
    TestDeformer();
    TestLocomotion();
    TestProceduralTurning();
    TestMorphContacts();
    Vector3 x=Vec3_Create(1,0,0),y=Vec3_Create(0,1,0);
    Quaternion q=Quat_FromAxisAngle(Vec3_Create(0,0,1),1.57079632679f);
    Near(Quat_RotateVector(Quat_Identity(),x),x);
    Near(Quat_RotateVector(q,x),y);
    Near(Quat_RotateVector(Quat_Multiply(q,q),x),Vec3_Scale(x,-1));
    Near(Quat_RotateVector(Quat_FromTo(x,Vec3_Scale(x,-1)),x),Vec3_Scale(x,-1));
    Near(Quat_RotateVector(Quat_FromTo(x,y),x),y);
    Near(Quat_RotateVector(Quat_Multiply(q,Quat_Inverse(q)),x),x);
    Near(Quat_RotateVector(Quat_Slerp(Quat_Identity(),q,.5f),x),Vec3_Create(.70710678f,.70710678f,0));
    Near(Quat_RotateVector(Quat_Normalize((Quaternion){0,0,0,5}),x),x);
    Near(Quat_RotateVector(Quat_Normalize((Quaternion){NAN,0,0,0}),x),x);
    Skeleton s={0}; s.jointCount=2;
    s.joints[0]=(SkeletonJoint){.id=1,.anatomyId=1,.parentIndex=-1,.restRotation=Quat_Identity()};
    s.joints[1]=(SkeletonJoint){.id=2,.anatomyId=2,.parentIndex=0,.restPosition={1,0,0},.restRotation=Quat_Identity()};
    SkeletonPose p;
    TEST_ASSERT(SkeletonPose_Init(&p,&s),"Inicializar pose");
    Near(p.joints[1].worldPosition,x);
    p.joints[0].localPosition=x; p.joints[0].localRotation=q;
    TEST_ASSERT(SkeletonPose_UpdateWorldTransforms(&p,&s),"Jerarquía FK");
    Near(p.joints[1].worldPosition,Vec3_Add(x,y));
    SkeletonPose copy; SkeletonPose_Copy(&copy,&p);
    Near(copy.joints[1].worldPosition,p.joints[1].worldPosition);
    TEST_ASSERT(SkeletonPose_ResetToRest(&p,&s),"Restaurar reposo"); Near(p.joints[1].worldPosition,x);
    SkeletonJoint j=s.joints[0];
    j.constraint=(JointConstraint){.type=JOINT_HINGE,.hingeAxis={0,0,1},.minAngle=0,.maxAngle=.4f};
    Near(Quat_RotateVector(Skeleton_ConstrainRotation(&j,q),x),Vec3_Create(cosf(.4f),sinf(.4f),0));
    Near(Quat_RotateVector(Skeleton_ConstrainRotation(&j,Quat_Inverse(q)),x),x);
    j.constraint.type=JOINT_FIXED; Near(Quat_RotateVector(Skeleton_ConstrainRotation(&j,q),x),x);
    j.constraint=(JointConstraint){.type=JOINT_BALL,.hingeAxis={1,0,0},.maxSwingAngle=.2f,.maxTwistAngle=.1f};
    Near(Quat_RotateVector(Skeleton_ConstrainRotation(&j,q),x),Vec3_Create(cosf(.2f),sinf(.2f),0));
}

static void TestMorphContacts(void) {
    Monster m=Monster_Create();CreaturePhenotype phenotype=CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&m,CreatureRecipes_Lizard(),&phenotype),"Crear marcha para morph");
    m.animation->animator.desiredVelocity=Vec3_Create(0,0,.4f);
    for(int i=0;i<23;++i)TEST_ASSERT(MonsterAnimation_Update(&m,1.f/60),"Preparar apoyos activos");
    Locomotion old=m.animation->animator.locomotion;
    Vector3 rootResidual=Vec3_Sub(Vec3_Sub(m.animation->pose.joints[0].localPosition,m.animation->rig.skeleton.joints[0].restPosition),old.bodyPosition);
    phenotype.axial.totalScale*=1.01f;
    TEST_ASSERT(Creature_BuildMonster(&m,CreatureRecipes_Lizard(),&phenotype),"Reconfigurar anatomía sin reiniciar marcha");
    Locomotion* current=&m.animation->animator.locomotion;
    TEST_ASSERT(FLOAT_NEAR(current->phase,old.phase),"Conservar fase global");
    Near(m.animation->pose.joints[0].worldPosition,Vec3_Add(m.animation->rig.skeleton.joints[0].restPosition,Vec3_Add(current->bodyPosition,rootResidual)));
    for(size_t i=0;i<m.animation->rig.limbCount;++i) {
        TEST_ASSERT(current->feet[i].phase==old.feet[i].phase,"Conservar apoyo o vuelo");
        Near(current->feet[i].plantedPosition,old.feet[i].plantedPosition);
        Near(current->feet[i].targetPosition,old.feet[i].targetPosition);
        TEST_ASSERT(FLOAT_NEAR(current->feet[i].swingProgress,old.feet[i].swingProgress),"Conservar vuelo parcial");
    }
    TEST_ASSERT(MonsterAnimation_Update(&m,1.f/60),"Marcha continúa con reposo nuevo");
    Monster_Free(&m);
}

static void TestRigIK(void) {
    Skeleton s={0}; s.jointCount=4;
    for(int i=0;i<4;++i) s.joints[i]=(SkeletonJoint){.id=(unsigned)i+1,.anatomyId=(unsigned)i+1,
        .parentIndex=i-1,.restPosition={i?1:0,0,0},.restRotation=Quat_Identity(),
        .constraint={.type=JOINT_BALL,.hingeAxis={1,0,0},.maxSwingAngle=3.141593f,.maxTwistAngle=3.141593f}};
    SkeletonPose p; SkeletonPose_Init(&p,&s);
    IKChain c={.jointIndices={0,1,2,3},.jointCount=4,.endEffectorJoint=3,
        .targetPosition={1.5f,1.2f,.4f},.maxIterations=80,.tolerance=.001f};
    IKResult result=IK_SolveFABRIK(&s,&p,&c);
    TEST_ASSERT(result.valid && result.converged,"FABRIK objetivo 3D alcanzable");
    for(int i=1;i<4;++i) TEST_ASSERT(FLOAT_NEAR(Vec3_Distance(p.joints[i].worldPosition,p.joints[i-1].worldPosition),1),"Longitudes constantes");
    c.targetPosition=Vec3_Create(12,5,3); result=IK_SolveFABRIK(&s,&p,&c);
    TEST_ASSERT(result.valid && !result.converged && result.iterations<=80 && isfinite(result.error),"FABRIK inalcanzable acotado");
    c.maxIterations=0; result=IK_SolveFABRIK(&s,&p,&c); TEST_ASSERT(result.iterations==0,"Presupuesto cero");
    c.targetPosition.x=NAN; TEST_ASSERT(!IK_SolveFABRIK(&s,&p,&c).valid,"Rechazar NaN");
    c.targetPosition=Vec3_Create(1,1,1); c.maxIterations=1;
    result=IK_SolveFABRIK(&s,&p,&c); TEST_ASSERT(result.iterations<=1,"Presupuesto de una iteración");
    for(int j=1;j<4;++j)s.joints[j].restPosition=Vec3_Zero();
    SkeletonPose_ResetToRest(&p,&s); c.maxIterations=8;
    result=IK_SolveFABRIK(&s,&p,&c);
    TEST_ASSERT(result.valid && isfinite(result.error) && result.iterations<=8,"Cadena colapsada finita");
    Near(p.joints[3].worldPosition,Vec3_Zero());
    Monster m=Monster_Create(); CreaturePhenotype phenotype=CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&m,CreatureRecipes_Lizard(),&phenotype),"Crear lagarto");
    uint64_t fp=AnatomyGraph_Fingerprint(&m.anatomyGraph);
    Rig rig; TEST_ASSERT(CreatureRig_Build(&m,&rig),"Rig derivado de anatomía");
    TEST_ASSERT(rig.limbCount==4 && rig.jawJoint>=0,"Cuatro miembros y mandíbula");
    SkeletonPose_Init(&p,&rig.skeleton);
    for(size_t i=0;i<m.anatomyGraph.nodeCount;++i) {
        int j=Skeleton_FindJoint(&rig.skeleton,m.anatomyGraph.nodes[i].id);
        TEST_ASSERT(j>=0,"Todos los nodos representados"); Near(p.joints[j].worldPosition,m.anatomyGraph.nodes[i].center);
    }
    for(size_t l=0;l<4;++l) {
        SkeletonPose_ResetToRest(&p,&rig.skeleton);
        IKChain chain=rig.limbs[l].chain;
        /* Objetivos producidos por una pose válida: alcanzabilidad comprobada. */
        int root=chain.jointIndices[0];
        p.joints[root].localRotation=Quat_FromAxisAngle(Vec3_Create(0,1,0),.10f);
        SkeletonPose_UpdateWorldTransforms(&p,&rig.skeleton);
        chain.targetPosition=p.joints[chain.endEffectorJoint].worldPosition;
        SkeletonPose_ResetToRest(&p,&rig.skeleton);
        result=IK_SolveFABRIK(&rig.skeleton,&p,&chain);
        TEST_ASSERT(result.valid && result.error<.012f,"Miembro del lagarto alcanza objetivo válido");
        for(size_t k=1;k<chain.jointCount;++k) {
            int j=chain.jointIndices[k],a=chain.jointIndices[k-1];
            TEST_ASSERT(fabsf(Vec3_Distance(p.joints[j].worldPosition,p.joints[a].worldPosition)-Vec3_Length(rig.skeleton.joints[j].restPosition))<.0001f,"Longitud de miembro preservada");
        }
    }
    for(size_t l=0;l<4;++l)for(int sample=0;sample<120;++sample) {
        SkeletonPose_ResetToRest(&p,&rig.skeleton);
        IKChain chain=rig.limbs[l].chain;
        for(int k=0;k<3;++k) {
            int j=chain.jointIndices[k]; const SkeletonJoint* joint=&rig.skeleton.joints[j];
            Vector3 axis=k==1?joint->constraint.hingeAxis:Vec3_Create(sinf(sample*.23f+k),cosf(sample*.17f),sinf(sample*.47f));
            p.joints[j].localRotation=Skeleton_ConstrainRotation(joint,Quat_FromAxisAngle(axis,.25f*sinf(sample*.31f+k)));
        }
        SkeletonPose_UpdateWorldTransforms(&p,&rig.skeleton);
        chain.targetPosition=p.joints[chain.endEffectorJoint].worldPosition;
        SkeletonPose_ResetToRest(&p,&rig.skeleton);
        result=IK_SolveFABRIK(&rig.skeleton,&p,&chain);
        TEST_ASSERT(result.valid && result.converged,"Barrido de 480 objetivos 3D alcanzables");
    }
    TEST_ASSERT(fp==AnatomyGraph_Fingerprint(&m.anatomyGraph),"Pose no modifica anatomía");
    Monster_Free(&m);
}

static float Slope(World* world,float x,float z) { (void)world; return .1f*x+.2f*z; }
static void TestLocomotion(void) {
    Monster m=Monster_Create(); CreaturePhenotype phenotype=CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&m,CreatureRecipes_Lizard(),&phenotype) && m.animation,"Rig automático");
    MonsterAnimation* a=m.animation;
    uint64_t fp=AnatomyGraph_Fingerprint(&m.anatomyGraph);
    TEST_ASSERT(MonsterAnimation_Update(&m,0),"Pose inicial estable");
    SkeletonPose initial=a->pose;
    for(int frame=0;frame<60;++frame) TEST_ASSERT(MonsterAnimation_Update(&m,1.f/60),"Reposo válido");
    for(size_t i=0;i<initial.jointCount;++i) Near(initial.joints[i].worldPosition,a->pose.joints[i].worldPosition);
    bool swung[4]={0},landed[4]={0};
    a->animator.desiredVelocity=Vec3_Create(0,0,.38f);
    for(int frame=0;frame<360;++frame) {
        FootState before[4]; memcpy(before,a->animator.locomotion.feet,sizeof(before));
        TEST_ASSERT(MonsterAnimation_Update(&m,1.f/60),"Marcha válida");
        for(size_t i=0;i<4;++i) {
            FootState* foot=&a->animator.locomotion.feet[i];
            if(foot->phase==FOOT_SWING) {
                swung[i]=true;
                TEST_ASSERT(foot->swingProgress>=0 && foot->swingProgress<=1,"Progreso swing acotado");
                TEST_ASSERT(foot->targetPosition.y>=foot->swingTarget.y-.001f,"Pie se eleva");
            }
            if(before[i].phase==FOOT_SWING && foot->phase==FOOT_STANCE)landed[i]=true;
            if(before[i].phase==FOOT_STANCE && foot->phase==FOOT_STANCE)Near(before[i].plantedPosition,foot->plantedPosition);
            TEST_ASSERT(a->animator.limbResults[i].error<.004f,"Contacto resuelto sin deslizamiento");
            int elbow=a->rig.limbs[i].chain.jointIndices[1];
            Quaternion q=a->pose.joints[elbow].localRotation;
            Quaternion constrained=Skeleton_ConstrainRotation(&a->rig.skeleton.joints[elbow],q);
            TEST_ASSERT(fabsf(q.x*constrained.x+q.y*constrained.y+q.z*constrained.z+q.w*constrained.w)>.99999f,"Bisagras respetan límites durante marcha");
        }
    }
    for(int i=0;i<4;++i)TEST_ASSERT(swung[i] && landed[i],"Todos los miembros completan pasos");
    TEST_ASSERT(a->animator.locomotion.bodyPosition.z>1,"Traslación real del cuerpo");
    a->animator.desiredVelocity=Vec3_Zero();
    for(int i=0;i<240;++i)MonsterAnimation_Update(&m,1.f/60);
    Vector3 stopped=a->animator.locomotion.bodyPosition;
    for(int i=0;i<60;++i)MonsterAnimation_Update(&m,1.f/60);
    Near(stopped,a->animator.locomotion.bodyPosition);
    TEST_ASSERT(fp==AnatomyGraph_Fingerprint(&m.anatomyGraph),"Locomoción no invalida anatomía");
    float phase=a->animator.locomotion.phase; Vector3 position=a->animator.locomotion.bodyPosition;
    phenotype=CreatureRecipes_Lizard()->juvenile; TEST_ASSERT(Creature_BuildMonster(&m,CreatureRecipes_Lizard(),&phenotype),"Cambio morfológico recompila rig");
    TEST_ASSERT(FLOAT_NEAR(m.animation->animator.locomotion.phase,phase),"Conserva fase al cambiar morfología");
    TEST_ASSERT(FLOAT_NEAR(m.animation->animator.locomotion.bodyPosition.x,position.x) && FLOAT_NEAR(m.animation->animator.locomotion.bodyPosition.z,position.z),"Conserva traslación horizontal");
    m.animation->animator.desiredVelocity=Vec3_Create(0,0,.2f);
    for(int frame=0;frame<180;++frame) {
        TEST_ASSERT(MonsterAnimation_Update(&m,1.f/60),"Juvenil camina tras recompilar rig");
        for(int l=0;l<4;++l)TEST_ASSERT(m.animation->animator.limbResults[l].error<.005f,"Contactos juveniles estables");
    }
    Monster clone=Monster_Clone(&m); TEST_ASSERT(!clone.animation,"Snapshot no comparte pose mutable"); Monster_Free(&clone);
    World world={.getWalkingHeight=Slope}; SurfaceHit hit;
    TEST_ASSERT(World_SampleGround(&world,Vec3_Create(2,10,3),&hit),"Adaptador de altura");
    TEST_ASSERT(FLOAT_NEAR(hit.position.y,.8f),"Altura del plano");
    Near(hit.normal,Vec3_Normalize(Vec3_Create(-.1f,1,-.2f)));
    Monster_Free(&m);
}
static SDFSample SphereSample(const void* context,Vector3 p) { (void)context; SDFSample s={0}; s.distance=Vec3_Length(p)-.5f; return s; }
static void TestDeformer(void) {
    AnatomyGraph g; AnatomyGraph_Init(&g);
    TEST_ASSERT(AnatomyGraph_AddNode(&g,(AnatomyNode){.id=1,.center={0,0,0},.widthRadius=.5f,.heightRadius=.5f,.colorIndex=0,.role=ANATOMY_ROLE_AXIAL,.region=ANATOMY_REGION_TRUNK,.side=ANATOMY_SIDE_CENTER,.moduleInstanceId=0,.localNodeId=0,.development=1.0f}),"Nodo raíz");
    TEST_ASSERT(AnatomyGraph_AddNode(&g,(AnatomyNode){.id=2,.center={1,0,0},.widthRadius=.5f,.heightRadius=.5f,.colorIndex=0,.role=ANATOMY_ROLE_AXIAL,.region=ANATOMY_REGION_TRUNK,.side=ANATOMY_SIDE_CENTER,.moduleInstanceId=0,.localNodeId=0,.development=1.0f}),"Nodo hijo");
    TEST_ASSERT(AnatomyGraph_Connect(&g,(BodyConnection){.id=3,.fromId=1,.toId=2,.kind=BODY_CONNECTION_AXIAL_LOFT,.moduleInstanceId=0,.development=1.0f}),"Conexión");
    Rig rig; TEST_ASSERT(RigBuilder_FromAnatomy(&g,1,&rig),"Rig genérico");
    SDFMesherConfig config=SDFMesher_DefaultConfig(); config.voxelSize=.1f; config.useAutoBounds=false;
    config.bounds.start=Vec3_Create(-.7f,-.7f,-.7f); config.bounds.end=Vec3_Create(.7f,.7f,.7f);
    SDFMesher mesher=SDFMesher_Create(config); Mesh mesh=Mesh_Create();
    SDFField field={0}; field.evaluate=SphereSample;
    TEST_ASSERT(SDFMesher_GenerateMesh(&mesher,&field,&mesh),"Malla SDF real");
    AnatomyDeformer* d=AnatomyDeformer_Create();
    TEST_ASSERT(AnatomyDeformer_BindSkeleton(d,&mesh,&g,&rig.skeleton),"Bind genérico");
    MeshIndex* indices=mesh.indices; size_t count=mesh.indexCount;
    uint64_t topologyBefore=1469598103934665603ULL;
    for(size_t i=0;i<count;++i)topologyBefore=(topologyBefore^mesh.indices[i])*1099511628211ULL;
    SkeletonPose pose; SkeletonPose_Init(&pose,&rig.skeleton);
    TEST_ASSERT(AnatomyDeformer_DeformPose(d,&rig.skeleton,&pose,&mesh),"Deformar reposo");
    for(size_t i=0;i<mesh.vertexCount;++i)Near(mesh.vertices[i].position,d->basePositions[i]);
    pose.joints[0].localPosition=Vec3_Create(2,3,4);
    pose.joints[0].localRotation=Quat_FromAxisAngle(Vec3_Create(1,0,0),1.2f);
    SkeletonPose_UpdateWorldTransforms(&pose,&rig.skeleton);
    TEST_ASSERT(AnatomyDeformer_DeformPose(d,&rig.skeleton,&pose,&mesh),"Deformar pose");
    for(size_t i=0;i<mesh.vertexCount;++i)Near(mesh.vertices[i].position,Vec3_Add(Vec3_Create(2,3,4),Quat_RotateVector(pose.joints[0].localRotation,d->basePositions[i])));
    TEST_ASSERT(mesh.indices==indices && mesh.indexCount==count,"Topología preservada");
    uint64_t topologyAfter=1469598103934665603ULL;
    for(size_t i=0;i<count;++i)topologyAfter=(topologyAfter^mesh.indices[i])*1099511628211ULL;
    TEST_ASSERT(topologyBefore==topologyAfter,"Contenido de índices inmutable");
    TEST_ASSERT(Mesh_Validate(&mesh).valid,"Malla deformada válida");
    AnatomyGraph posed; TEST_ASSERT(SkeletonPose_ToAnatomy(&rig.skeleton,&pose,&g,&posed),"Anatomía posada temporal");
    Near(g.nodes[0].center,Vec3_Zero()); Near(posed.nodes[0].center,Vec3_Create(2,3,4));
    CreatureBoundary boundary={0};MonsterAnimation animation={0};animation.rig=rig;
    TEST_ASSERT(CreatureBoundary_BindMesh(&boundary,d,&rig.skeleton),"Soportes de malla CPU");
    for(int i=0;i<16;++i) {
        animation.pose=pose;float yaw=i*.4f;Vector3 position={12.9f,0,-12.9f};
        TEST_ASSERT(CreatureBoundary_Solve(&boundary,&animation,&position,&yaw,1,13,.1f,1.f/60),"Contener malla CPU rotada");
        TEST_ASSERT(AnatomyDeformer_DeformPose(d,&rig.skeleton,&animation.pose,&mesh),"Publicar pose de contacto CPU");
        for(size_t v=0;v<mesh.vertexCount;++v) {
            Vector3 world=Vec3_Add(position,Quat_RotateVector(Quat_FromAxisAngle(Vec3_Create(0,1,0),yaw),mesh.vertices[v].position));
            TEST_ASSERT(fabsf(world.x)<13&&fabsf(world.z)<13,"Cada vértice CPU queda en la plataforma");
        }
    }
    AnatomyDeformer_Free(d); Mesh_Free(&mesh); SDFMesher_Free(&mesher);
}

static void TestProceduralTurning(void) {
    /* 1. Giro del lagarto: flexión lateral en C de columna y látigo de cola */
    Monster m = Monster_Create();
    CreaturePhenotype lizardPheno = CreatureRecipes_Lizard()->adult;
    TEST_ASSERT(Creature_BuildMonster(&m, CreatureRecipes_Lizard(), &lizardPheno) && m.animation, "Construir lagarto para test de giro");
    MonsterAnimation* a = m.animation;

    TEST_ASSERT(MonsterAnimation_Update(&m, 0), "Actualizar animación en t=0");
    a->animator.desiredVelocity = Vec3_Create(0, 0, 0.38f);
    a->animator.angularVelocity = 1.4f; /* Giro a la derecha (rad/s) */

    for (int frame = 0; frame < 60; ++frame) {
        TEST_ASSERT(MonsterAnimation_Update(&m, 1.0f / 60.0f), "Paso de animación en giro");
    }

    TEST_ASSERT(a->animator.currentAngularVelocity > 0.5f, "Velocidad angular suavizada activa");

    /* Comprobar flexión hacia la derecha en la columna (rotación yaw con componente y) */
    size_t spineCount = a->rig.spineCount;
    TEST_ASSERT(spineCount > 0, "Columna de lagarto existente");
    int lastSpine = a->rig.spine[spineCount - 1];
    Quaternion spineRot = a->pose.joints[lastSpine].localRotation;
    TEST_ASSERT(spineRot.y > 0.005f, "Columna de lagarto flexiona en yaw hacia el giro");

    /* Comprobar que la cola contrarresta (látigo) con rotación opuesta en yaw */
    if (a->rig.tailCount > 0 && a->rig.tails[0].jointCount > 0) {
        int midTail = a->rig.tails[0].joints[a->rig.tails[0].jointCount / 2];
        Quaternion tailRot = a->pose.joints[midTail].localRotation;
        TEST_ASSERT(tailRot.y < 0.0f || fabsf(tailRot.y) > 0.001f, "Cola de lagarto contrarresta el giro");
    }

    /* Comprobar estabilidad de contactos de extremidades */
    for (size_t l = 0; l < 4; ++l) {
        TEST_ASSERT(a->animator.limbResults[l].error < 0.015f, "Contactos resueltos durante giro de lagarto");
    }
    Monster_Free(&m);

    /* 2. Giro del perro: peralte mamífero (roll en Z) y flexión controlada */
    Monster dog = Monster_Create();
    CreaturePhenotype dogPheno = CreatureRecipes_Dog()->adult;
    TEST_ASSERT(Creature_BuildMonster(&dog, CreatureRecipes_Dog(), &dogPheno), "Construir perro para test de peralte");
    Rig dogRig;
    TEST_ASSERT(CreatureRig_Build(&dog, &dogRig), "Rig de perro");
    GaitProfile dogGait = (GaitProfile){
        .cycleDuration = 0.95f, .strideLength = 1.15f, .stepHeight = 0.28f, .stanceRatio = 0.65f,
        .limbPhase = {0, 0.5f, 0.5f, 0}, .spineWave = 0.02f, .tailCounterSwing = 0.1f,
        .spineTurn = 0.12f, .tailTurn = 0.20f, .bodyBank = 0.14f
    };
    TEST_ASSERT(MonsterAnimation_Configure(&dog, &dogRig, dogGait), "Configurar marcha de perro");
    dog.animation->animator.desiredVelocity = Vec3_Create(0, 0, 1.2f);
    dog.animation->animator.angularVelocity = 1.5f; /* Giro a la derecha */

    for (int frame = 0; frame < 60; ++frame) {
        TEST_ASSERT(MonsterAnimation_Update(&dog, 1.0f / 60.0f), "Marcha con giro de perro");
    }

    /* Comprobar peralte (roll en Z: bankAngle < 0 para giro a derecha) */
    bool sawBanking = false;
    for (size_t i = 0; i < dog.animation->rig.spineCount; ++i) {
        int sj = dog.animation->rig.spine[i];
        Quaternion q = dog.animation->pose.joints[sj].localRotation;
        if (fabsf(q.z) > 0.001f) {
            sawBanking = true;
            break;
        }
    }
    TEST_ASSERT(sawBanking, "Perro exhibe peralte (roll en Z) al girar");
    Monster_Free(&dog);
}

/** @brief La misma longitud conserva el arco con otra segmentación; marcha atrás invierte la curva. */
static float TailArcFor(int count,float velocity) {
    Rig rig={0};rig.skeleton.jointCount=(size_t)count+1;rig.headJoint=rig.neckJoint=rig.jawJoint=-1;
    rig.tailCount=1;rig.tails[0].jointCount=(size_t)count;
    for(int i=0;i<=count;++i) {
        rig.skeleton.joints[i]=(SkeletonJoint){.id=(AnatomyId)i+1,.parentIndex=i-1,.restPosition={0,0,i?1.f/count:0},
            .restRotation={0,0,0,1},.constraint={.type=JOINT_BALL,.hingeAxis={0,0,1},.maxSwingAngle=.6f,.maxTwistAngle=.1f}};
        if(i)rig.tails[0].joints[i-1]=i;
    }
    GaitProfile gait={.cycleDuration=1,.strideLength=1,.stanceRatio=.65f,.tailTurn=.2f};
    ProceduralAnimator animator;SkeletonPose pose;
    TEST_ASSERT(ProceduralAnimator_Init(&animator,&rig,gait,NULL,Vec3_Zero())&&SkeletonPose_Init(&pose,&rig.skeleton),"Preparar arco anatómico");
    animator.angularVelocity=1.4f;animator.desiredVelocity=Vec3_Create(0,0,velocity);animator.locomotion.velocity=animator.desiredVelocity;
    TEST_ASSERT(ProceduralAnimator_Update(&animator,&rig,&pose,NULL,0),"Evaluar curvatura de trayectoria");
    Quaternion q=pose.joints[count].worldRotation;return 2*atan2f(q.y,q.w);
}
static void TestTailCurvature(void) {
    float arc=TailArcFor(4,.38f);
    TEST_ASSERT(arc<-.1f&&FLOAT_NEAR(arc,TailArcFor(8,.38f)),"Arco independiente del número de estaciones");
    TEST_ASSERT(FLOAT_NEAR(-arc,TailArcFor(8,-.38f)),"Retroceder invierte la flexión");
    TEST_ASSERT(FLOAT_NEAR(TailArcFor(8,0),0),"Sin traslación no hay curvatura singular");
}

/** @brief El contacto dobla la cadena conservando la raíz, y la pose libre no cambia. */
static void TestBoundaryFlexion(void) {
    MonsterAnimation a={0};CreatureBoundary b={0};
    a.rig.skeleton.jointCount=3;a.rig.spineCount=2;a.rig.spine[0]=1;a.rig.spine[1]=2;
    a.rig.neckJoint=a.rig.headJoint=-1;
    for(int i=0;i<3;++i)a.rig.skeleton.joints[i]=(SkeletonJoint){.id=(AnatomyId)i+1,.parentIndex=i-1,
        .restPosition={0,0,i?1:0},.restRotation={0,0,0,1},
        .constraint={.type=JOINT_BALL,.hingeAxis={0,0,1},.maxSwingAngle=.6f,.maxTwistAngle=.1f}};
    TEST_ASSERT(SkeletonPose_Init(&a.pose,&a.rig.skeleton),"Cadena para contacto local");
    b.rest=a.pose;b.jointCount=3;
    for(int i=0;i<3;++i) {
        b.inverseRest[i]=Quat_Identity();b.contactRadius[i]=.15f;
        Vector3 p=b.rest.joints[i].worldPosition;b.support[i]=(AABB3D){Vec3_Sub(p,Vec3_Create(.1f,.1f,.1f)),Vec3_Add(p,Vec3_Create(.1f,.1f,.1f))};
    }
    SkeletonPose initial=a.pose;Vector3 position=Vec3_Zero();float yaw=0;
    TEST_ASSERT(CreatureBoundary_Solve(&b,&a,&position,&yaw,1,3,0,1.f/60),"Pose libre válida");
    TEST_ASSERT(memcmp(&initial,&a.pose,sizeof(initial))==0&&b.contactTrials==0,"Sin contacto no repetir pose ni IK");
    position.x=2.85f;
    TEST_ASSERT(CreatureBoundary_Solve(&b,&a,&position,&yaw,1,3,0,1.f/60),"Responder a pared lateral");
    TEST_ASSERT(b.bend[1]<-.001f,"La cadena se dobla hacia el interior");
    TEST_ASSERT(fabsf(position.x-2.85f)<.001f,"La flexión no se sustituye por desplazar la raíz");
    TEST_ASSERT(b.contactTrials<=16,"Presupuesto de contacto acotado");
}

/** @brief Referencia escalar del skinning GPU; independiente de la envolvente. */
static Vector3 BoundaryGpuPoint(const SurfaceMorphDomain* domain,const CreatureBoundary* b,
    const MonsterAnimation* a,Vector3 p) {
    float scores[5]={INFINITY,INFINITY,INFINITY,INFINITY,INFINITY},params[5]={0};int ids[5]={0};
    for(size_t i=0;i<domain->count;++i) {
        const float* lo=domain->data[i][0];const float* hi=domain->data[i][1];
        Vector3 pa={lo[0],lo[1],lo[2]},ab={hi[0]-lo[0],hi[1]-lo[1],hi[2]-lo[2]};
        float t=fmaxf(0,fminf(1,Vec3_Dot(Vec3_Sub(p,pa),ab)/fmaxf(Vec3_LengthSq(ab),1e-12f)));
        float radius=fmaxf(lo[3]+t*(hi[3]-lo[3]),1e-6f);
        float score=Vec3_LengthSq(Vec3_Sub(p,Vec3_Add(pa,Vec3_Scale(ab,t))))/(radius*radius);
        for(int k=0;k<5;++k)if(score<scores[k]) {
            for(int j=4;j>k;--j){scores[j]=scores[j-1];params[j]=params[j-1];ids[j]=ids[j-1];}
            scores[k]=score;params[k]=t;ids[k]=(int)i;break;
        }
    }
    Vector3 result=Vec3_Zero();float sum=0;
    for(int k=0;k<4;++k) {
        float support=fmaxf(0,1-scores[k]/fmaxf(scores[4],1e-6f));
        float weight=support*support/(scores[k]*scores[k]+1e-4f);
        if(k==0&&weight<1e-12f)weight=1;
        if(weight<=0)continue;
        for(int e=0;e<2;++e) {
            const float* endpoint=domain->data[ids[k]][e];Vector3 rest={endpoint[0],endpoint[1],endpoint[2]};
            int joint=-1;
            for(size_t j=0;j<b->jointCount;++j)if(Vec3_Distance(b->rest.joints[j].worldPosition,rest)<1e-6f){joint=(int)j;break;}
            TEST_ASSERT(joint>=0,"Extremo GPU corresponde al rig");
            Quaternion q=Quat_Multiply(a->pose.joints[joint].worldRotation,Quat_Inverse(b->rest.joints[joint].worldRotation));
            Vector3 posed=Vec3_Add(a->pose.joints[joint].worldPosition,Quat_RotateVector(q,Vec3_Sub(p,rest)));
            result=Vec3_Add(result,Vec3_Scale(posed,weight*(e?params[k]:1-params[k])));
        }
        sum+=weight;
    }
    return Vec3_Scale(result,1/fmaxf(sum,1e-12f));
}

/** @brief Esquinas, cuatro paredes, giros, escalas y todas las variantes GPU. */
static void TestBoundary(void) {
    for(int variant=0;variant<LIZARD_VARIATIONS_COUNT;++variant) {
        Monster m=Monster_Create();MonsterSDF sdf={0};CreatureBoundary boundary={0};
        CreatureVariation variation=LizardVariations_Get((size_t)variant,137);
        CreatureVariant resolved;
        TEST_ASSERT(CreatureVariation_Apply(CreatureRecipes_Lizard(),&CreatureRecipes_Lizard()->adult,&variation,&resolved)&&
            Creature_BuildMonster(&m,&resolved.recipe,&resolved.phenotype),"Crear variante para colisión");
        TEST_ASSERT(MonsterSDF_Build(&sdf,&m,MonsterSDF_DefaultConfig())&&CreatureBoundary_BindSDF(&boundary,&m,&sdf,.075f),"Compilar soporte GPU conservador");
        SurfaceMorphDomain domain;
        TEST_ASSERT(SurfaceMapper_BuildMorphDomain(&domain,&m.anatomyGraph,&m.anatomyGraph,&m.surfaceMapping),"Dominio GPU de referencia");
        for(int frame=0;frame<32;++frame) {
            float yaw=frame*6.2831853f/32,scale=(frame%2)?1.f:.8f;
            Vector3 position={frame%2?27.9f:-27.9f,0,frame%3?27.9f:-27.9f};
            m.animation->animator.desiredVelocity=Vec3_Create(0,0,.8f);
            m.animation->animator.angularVelocity=(frame%2)?1.2f:-1.2f;
            TEST_ASSERT(MonsterAnimation_Update(&m,1.f/60),"Animar giro antes del contacto");
            bool contained=CreatureBoundary_Solve(&boundary,m.animation,&position,&yaw,scale,28,.15f,1.f/60);
            TEST_ASSERT(contained,"Resolver criatura en esquina");
            TEST_ASSERT(boundary.contactTrials<=16,"Coste de flexión acotado para todas las variantes");
            AABB3D box=CreatureBoundary_Bounds(&boundary,m.animation,position,yaw,scale,.15f);
            TEST_ASSERT(box.start.x>=-28.0001f&&box.end.x<=28.0001f&&box.start.z>=-28.0001f&&box.end.z<=28.0001f,"Todo el volumen dentro de las cuatro paredes");
            for(int sample=0;sample<96;++sample) {
                Vector3 size=AABB_Size(sdf.bounds);
                Vector3 p=Vec3_Add(sdf.bounds.start,Vec3_Create(size.x*((sample*97+frame*31)%509)/508.f,
                    size.y*((sample*193+frame*23)%503)/502.f,size.z*((sample*251+frame*47)%499)/498.f));
                if(MonsterSDF_EvaluateVisualDistance(&sdf,p)>0)continue;
                Vector3 posed=BoundaryGpuPoint(&domain,&boundary,m.animation,p);
                Vector3 center=m.animation->animator.locomotion.bodyPosition;center.y=0;
                Vector3 world=Vec3_Add(position,Quat_RotateVector(Quat_FromAxisAngle(Vec3_Create(0,1,0),yaw),Vec3_Scale(Vec3_Sub(posed,center),scale)));
                TEST_ASSERT(fabsf(world.x)<=28.0001f&&fabsf(world.z)<=28.0001f,"Skinning GPU independiente permanece dentro");
            }
            for(size_t j=0;j<boundary.jointCount;++j) {
                int parent=m.animation->rig.skeleton.joints[j].parentIndex;
                if(parent>=0)TEST_ASSERT(fabsf(Vec3_Distance(m.animation->pose.joints[j].worldPosition,m.animation->pose.joints[parent].worldPosition)-
                    Vec3_Length(m.animation->pose.joints[j].localPosition))<1e-4f,"Contacto conserva longitudes óseas");
            }
        }
        Vector3 position=Vec3_Zero();float yaw=0;
        TEST_ASSERT(!CreatureBoundary_Solve(&boundary,m.animation,&position,&yaw,1,.01f,.15f,0),"Rechazar recinto donde no cabe el cuerpo");
        yaw=NAN;
        TEST_ASSERT(!CreatureBoundary_Solve(&boundary,m.animation,&position,&yaw,1,28,.15f,0),"Rechazar orientación inválida");
        MonsterSDF_Free(&sdf);Monster_Free(&m);
    }
}

/** @brief La poda conserva exactamente los soportes de la búsqueda exhaustiva. */
static void TestBoundaryCandidates(void) {
    Monster source=Monster_Create();
    TEST_ASSERT(Creature_BuildMonster(&source,CreatureRecipes_Lizard(),&CreatureRecipes_Lizard()->adult),"Origen de soportes");
    for(size_t variant=0;variant<LIZARD_VARIATIONS_COUNT;++variant) {
        CreatureVariation variation=LizardVariations_Get(variant,42);CreatureVariant resolved;
        Monster target=Monster_Create();MonsterSDF sdf={0};
        TEST_ASSERT(CreatureVariation_Apply(CreatureRecipes_Lizard(),&CreatureRecipes_Lizard()->adult,&variation,&resolved)&&
            Creature_BuildMonster(&target,&resolved.recipe,&resolved.phenotype),"Destino de soportes");
        MonsterAger ager=MonsterAger_Create(&source,&target,0);MonsterAger_SetGeometrySnapshotEnabled(&ager,false);
        for(int step=0;step<9;++step) {
            float age=step<=4?step/4.f:(8-step)/4.f;MonsterAger_SetPerc(&ager,age);
            Monster current=Monster_Clone(MonsterAger_GetResultConst(&ager));Rig rig;
            TEST_ASSERT(CreatureRig_Build(&current,&rig)&&MonsterAnimation_Configure(&current,&rig,GaitPreset_SprawlingQuadruped(current.phenotype.axial.totalScale)),"Rig de soportes actuales");
            TEST_ASSERT(MonsterSDF_Build(&sdf,&current,MonsterSDF_DefaultConfig()),"SDF de soportes actuales");
            CreatureBoundary fast={0},reference={0};
            TEST_ASSERT(CreatureBoundary_BindSDF(&fast,&current,&sdf,.075f),"Soportes con BVH");
            setenv("MONSTER_BOUNDARY_EXHAUSTIVE","1",1);
            bool ok=CreatureBoundary_BindSDF(&reference,&current,&sdf,.075f);
            unsetenv("MONSTER_BOUNDARY_EXHAUSTIVE");
            TEST_ASSERT(ok,"Soportes exhaustivos");
            TEST_ASSERT(memcmp(fast.support,reference.support,sizeof(fast.support))==0,
                "La poda conserva todas las influencias en edades de ida y vuelta");
            Monster_Free(&current);
        }
        MonsterAger_Free(&ager);MonsterSDF_Free(&sdf);Monster_Free(&target);
    }
    Monster_Free(&source);
}

/** @brief Marcha, inversión, pivote y transporte mundial de contactos en ambas especies. */
static void TestCanidWalkAndTurnContacts(void) {
    for(int species=0;species<2;++species)for(int mode=0;mode<5;++mode) {
        Monster m=Monster_Create();
        const CreatureRecipe* recipe=species?CreatureRecipes_Dog():CreatureRecipes_Lizard();
        TEST_ASSERT(Creature_BuildMonster(&m,recipe,&recipe->adult),"Crear anatomía para marcha y pivote");
        Rig rig;TEST_ASSERT(CreatureRig_Build(&m,&rig),"Resolver todas las articulaciones");
        GaitProfile gait=species?GaitPreset_CanidWalk(&rig):GaitPreset_SprawlingQuadruped(1);
        TEST_ASSERT(MonsterAnimation_Configure(&m,&rig,gait),"Configurar marcha anatómica");
        MonsterAnimation* a=m.animation;
        if(species) {
            for(size_t i=0;i<4;++i) {
                TEST_ASSERT(rig.limbs[i].chain.jointCount==4,"Cánido con hombro/cadera, codo/rodilla, carpo/tarso y pata");
                for(size_t j=i+1;j<4;++j)TEST_ASSERT(gait.limbPhase[i]!=gait.limbPhase[j],"Paso cánido de cuatro tiempos");
            }
            Rig scaled=rig;
            for(size_t j=0;j<scaled.skeleton.jointCount;++j)scaled.skeleton.joints[j].restPosition=Vec3_Scale(scaled.skeleton.joints[j].restPosition,.5f);
            GaitProfile small=GaitPreset_CanidWalk(&scaled);
            TEST_ASSERT(FLOAT_NEAR(small.strideLength,gait.strideLength*.5f)&&FLOAT_NEAR(small.stepHeight,gait.stepHeight*.5f),"Zancada y elevación escalan con los huesos");
            LimbRig tmp=scaled.limbs[0];scaled.limbs[0]=scaled.limbs[3];scaled.limbs[3]=tmp;
            small=GaitPreset_CanidWalk(&scaled);
            TEST_ASSERT(FLOAT_NEAR(small.limbPhase[0],gait.limbPhase[3]),"Orden de extremidades independiente del paso");
        }
        TEST_ASSERT(MonsterAnimation_Update(&m,0),"Preparar flexión de apoyo");
        uint64_t restFingerprint=AnatomyGraph_Fingerprint(&m.anatomyGraph);
        SkeletonPose rest;SkeletonPose_Init(&rest,&rig.skeleton);
        float radius=0,maxSpeed=gait.strideLength/(gait.cycleDuration*gait.stanceRatio);
        for(size_t i=0;i<4;++i) {
            Vector3 p=Vec3_Sub(rest.joints[rig.limbs[i].endEffectorJoint].worldPosition,a->animator.locomotion.supportCenter);
            radius=fmaxf(radius,sqrtf(p.x*p.x+p.z*p.z));
        }
        float omega=mode==0?0:(mode==2?-.65f:.65f)*maxSpeed/radius;
        float speed=mode==3?0:maxSpeed-fabsf(omega)*radius;
        if(mode==4)speed=-speed;
        Vector3 position=Vec3_Zero();float yaw=0;
        bool swung[4]={0};float maximumError=0,maximumJump=0;
        for(int frame=0;frame<900;++frame) {
            Locomotion before=a->animator.locomotion;SkeletonPose oldPose=a->pose;
            float delta=omega/60.f,oldYaw=yaw;yaw+=delta;
            Quaternion qOld=Quat_FromAxisAngle(Vec3_Create(0,1,0),oldYaw),qNew=Quat_FromAxisAngle(Vec3_Create(0,1,0),yaw);
            Vector3 oldPosition=position;
            Vector3 pivot=before.supportCenter;
            position=Vec3_Add(position,Vec3_Sub(Quat_RotateVector(qOld,pivot),Quat_RotateVector(qNew,pivot)));
            Locomotion_TurnFrame(&a->animator.locomotion,delta);
            a->animator.angularVelocity=omega;a->animator.desiredVelocity=Vec3_Create(0,0,speed);
            TEST_ASSERT(MonsterAnimation_Update(&m,1.f/60),"Marcha y giro continuos");
            Locomotion* l=&a->animator.locomotion;
            Vector3 displacement=Vec3_Sub(l->bodyPosition,before.bodyPosition);displacement.y=0;
            position=Vec3_Add(position,Quat_RotateVector(qNew,displacement));
            for(size_t i=0;i<4;++i) {
                const LimbRig* limb=&rig.limbs[i];const FootState* foot=&l->feet[i];
                maximumError=fmaxf(maximumError,a->animator.limbResults[i].error);
                if(foot->phase==FOOT_SWING)swung[i]=true;
                if(before.feet[i].phase==FOOT_STANCE&&foot->phase==FOOT_STANCE) {
                    Vector3 oldCenter=before.bodyPosition,newCenter=l->bodyPosition;oldCenter.y=newCenter.y=0;
                    Vector3 oldWorld=Vec3_Add(oldPosition,Quat_RotateVector(qOld,Vec3_Sub(before.feet[i].plantedPosition,oldCenter)));
                    Vector3 newWorld=Vec3_Add(position,Quat_RotateVector(qNew,Vec3_Sub(foot->plantedPosition,newCenter)));
                    TEST_ASSERT(Vec3_Distance(oldWorld,newWorld)<.0001f,"Apoyo mundial fijo al girar, avanzar y retroceder");
                }
                for(size_t k=0;k+1<limb->chain.jointCount;++k) {
                    int j=limb->chain.jointIndices[k],child=limb->chain.jointIndices[k+1];
                    TEST_ASSERT(fabsf(Vec3_Distance(a->pose.joints[j].worldPosition,a->pose.joints[child].worldPosition)-Vec3_Length(rig.skeleton.joints[child].restPosition))<.0001f,"Todos los segmentos conservan su longitud");
                    Quaternion q=Quat_Multiply(Quat_Inverse(oldPose.joints[j].localRotation),a->pose.joints[j].localRotation);
                    if(frame>30)maximumJump=fmaxf(maximumJump,2*acosf(fminf(1,fabsf(q.w))));
                    if(species&&k>0) {
                        float initial=Vec3_Cross(rig.skeleton.joints[j].restPosition,rig.skeleton.joints[child].restPosition).x;
                        Vector3 incoming=Vec3_Sub(a->pose.joints[j].worldPosition,a->pose.joints[limb->chain.jointIndices[k-1]].worldPosition);
                        Vector3 outgoing=Vec3_Sub(a->pose.joints[child].worldPosition,a->pose.joints[j].worldPosition);
                        TEST_ASSERT(initial*Vec3_Cross(incoming,outgoing).x>=-1e-5f,"Codos, rodillas y corvejones no invierten la flexión");
                    }
                }
            }
        }
        printf("  [marcha] especie=%s modo=%d error=%.6f salto=%.5f rad\n",species?"perro":"lagarto",mode,maximumError,maximumJump);
        TEST_ASSERT(maximumError<.004f,"Patas alcanzan los apoyos en recta, curva, pivote y marcha atrás");
        TEST_ASSERT(maximumJump<.35f,"Sin saltos de rama articular entre fotogramas");
        for(size_t i=0;i<4;++i)TEST_ASSERT(swung[i],"Todas las patas pisan también durante el pivote");
        TEST_ASSERT(restFingerprint==AnatomyGraph_Fingerprint(&m.anatomyGraph),"Animación no cambia la anatomía");
        Monster_Free(&m);
    }
}
