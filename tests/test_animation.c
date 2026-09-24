#include "Creature.h"
#include "Limb.h"
#include "test_utils.h"
#include "Skeleton.h"
#include "IK.h"
#include "CreatureRig.h"
#include "Monster.h"
#include "AnatomyDeformer.h"
#include "MonsterAnimation.h"
#include "GaitPresets.h"
#include "SDFPrimitives.h"
#include "SDFMesher.h"
#include <string.h>
#include <math.h>

static void Near(Vector3 a,Vector3 b) { TEST_ASSERT(Vec3_Distance(a,b)<.001f,"Vectores coinciden"); }
static void TestRigIK(void);
static void TestDeformer(void);
static void TestLocomotion(void);
void run_animation_tests(void) {
    TestRigIK();
    TestDeformer();
    TestLocomotion();
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
    AnatomyDeformer_Free(d); Mesh_Free(&mesh); SDFMesher_Free(&mesher);
}
