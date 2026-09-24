#include "AnimatedVisual.h"
#include <stdlib.h>
#include <string.h>
#include <math.h>
static bool CopyMesh(Mesh* dst,const Mesh* src) {
    if(!Mesh_ReserveVertices(dst,src->vertexCount) || !Mesh_ReserveIndices(dst,src->indexCount))return false;
    dst->surfaceRecipe=src->surfaceRecipe; dst->hasSurface=src->hasSurface;
    dst->vertexCount=src->vertexCount; dst->indexCount=src->indexCount;
    if(src->vertexCount)memcpy(dst->vertices,src->vertices,src->vertexCount*sizeof(MeshVertex));
    if(src->indexCount)memcpy(dst->indices,src->indices,src->indexCount*sizeof(MeshIndex));
    return true;
}
void AnimatedVisual_Free(AnimatedVisual* a) {
    if(!a)return;
    AnatomyDeformer_Free(a->body); Mesh_Free(&a->headBase);
    for(size_t i=0;i<a->eyeCount;++i) Mesh_Free(&a->eyeBases[i].globe);
    free(a->eyeBases); memset(a,0,sizeof(*a));
}
bool AnimatedVisual_Bind(AnimatedVisual* a,const MonsterVisual* v,const Monster* m) {
    if(!a || !v || !m || !m->animation || !v->rebuildGeneration || !MonsterVisual_MatchesGeometry(v,m))return false;
    if(a->bound && a->generation==v->rebuildGeneration)return a->rigGeneration==m->animation->rigGeneration;
    AnimatedVisual next={0}; next.body=AnatomyDeformer_Create();
    if(!next.body || !AnatomyDeformer_BindSkeleton(next.body,&v->mesh,&m->anatomyGraph,&m->animation->rig.skeleton) ||
        !CopyMesh(&next.headBase,&v->headMesh)) { AnimatedVisual_Free(&next); return false; }
    if(v->eyeCount) {
        next.eyeBases=calloc(v->eyeCount,sizeof(*next.eyeBases));
        if(!next.eyeBases) { AnimatedVisual_Free(&next); return false; }
        next.eyeCount=v->eyeCount;
        for(size_t i=0;i<v->eyeCount;++i) {
            next.eyeBases[i]=v->eyes[i];next.eyeBases[i].globe=Mesh_Create();
            if(!CopyMesh(&next.eyeBases[i].globe,&v->eyes[i].globe)) { AnimatedVisual_Free(&next); return false; }
        }
    }
    SkeletonPose_Init(&next.restPose,&m->animation->rig.skeleton);
    next.rigGeneration=m->animation->rigGeneration;
    next.generation=v->rebuildGeneration; next.restFingerprint=m->animation->restFingerprint; next.bound=true;
    AnimatedVisual_Free(a); *a=next; return true;
}
static void RigidMesh(Mesh* mesh,const Mesh* base,Quaternion q,Vector3 rest,Vector3 target) {
    Vector3 x=Quat_RotateVector(q,Vec3_Create(1,0,0)),y=Quat_RotateVector(q,Vec3_Create(0,1,0)),z=Quat_RotateVector(q,Vec3_Create(0,0,1));
    Vector3 offset=Vec3_Sub(target,Quat_RotateVector(q,rest));
    for(size_t i=0;i<mesh->vertexCount;++i) {
        Vector3 p=base->vertices[i].position,n=base->vertices[i].normal;
        mesh->vertices[i].position=(Vector3){x.x*p.x+y.x*p.y+z.x*p.z+offset.x,x.y*p.x+y.y*p.y+z.y*p.z+offset.y,x.z*p.x+y.z*p.y+z.z*p.z+offset.z};
        mesh->vertices[i].normal=(Vector3){x.x*n.x+y.x*n.y+z.x*n.z,x.y*n.x+y.y*n.y+z.y*n.z,x.z*n.x+y.z*n.y+z.z*n.z};
    }
}
bool AnimatedVisual_Deform(const AnimatedVisual* a,MonsterVisual* v,const Monster* m) {
    if(!a || !v || !m || !m->animation || !a->bound || a->generation!=v->rebuildGeneration ||
       a->restFingerprint!=m->animation->restFingerprint || a->rigGeneration!=m->animation->rigGeneration || a->eyeCount!=v->eyeCount)return false;
    MonsterVisual_SetSurface(v,m->hasSurface?&m->surface:NULL);
    const MonsterAnimation* animation=m->animation; const Rig* rig=&animation->rig; const SkeletonPose* p=&animation->pose;
    if(!AnatomyDeformer_DeformPose(a->body,&rig->skeleton,p,&v->mesh))return false;
    int head=rig->headJoint; if(head<0)return true;
    Quaternion q=Quat_Multiply(p->joints[head].worldRotation,Quat_Inverse(a->restPose.joints[head].worldRotation));
    Vector3 rest=a->restPose.joints[head].worldPosition,target=p->joints[head].worldPosition;
    RigidMesh(&v->headMesh,&a->headBase,q,rest,target);
    for(size_t i=0;i<v->eyeCount;++i) {
        RigidMesh(&v->eyes[i].globe,&a->eyeBases[i].globe,q,rest,target);
        Vector3 center=a->eyeBases[i].center;
        v->eyes[i].center=Vec3_Add(Quat_RotateVector(q,center),Vec3_Sub(target,Quat_RotateVector(q,rest)));
        v->eyes[i].forward=Quat_RotateVector(q,a->eyeBases[i].forward);
        v->eyes[i].right=Quat_RotateVector(q,a->eyeBases[i].right);v->eyes[i].up=Quat_RotateVector(q,a->eyeBases[i].up);
        v->eyes[i].appearance=a->eyeBases[i].appearance;v->eyes[i].appearanceFingerprint=a->eyeBases[i].appearanceFingerprint;
    }
    for(size_t i=0;i<v->mouthCount && i<m->mouthCount;++i) {
        Mouth mouth=m->mouths[i];
        if(rig->jawJoint>=0 && rig->maxJawAngle>0) {
            Quaternion jaw=p->joints[rig->jawJoint].localRotation;
            float angle=2*atan2f(jaw.x,jaw.w);
            mouth.openFactor=fmaxf(0,fminf(1,angle/rig->maxJawAngle));
        }
        MonsterVisual_UpdateMouthArticulation(&v->mouths[i],&mouth,m);
        RigidMesh(&v->mouths[i].jaw,&v->mouths[i].jaw,q,rest,target);
        RigidMesh(&v->mouths[i].hinge,&v->mouths[i].hinge,q,rest,target);
    }
    return true;
}
