#include "RigBuilder.h"
#include <string.h>
bool RigBuilder_FromAnatomy(const AnatomyGraph* g,AnatomyId rootId,Rig* out) {
    if (!out || !AnatomyGraph_Validate(g) || !g->nodeCount) return false;
    const AnatomyNode* root=AnatomyGraph_FindNode(g,rootId);
    if (!root) return false;
    Rig r={0}; r.headJoint=r.neckJoint=r.jawJoint=r.pelvisJoint=-1;
    r.skeleton.joints[0]=(SkeletonJoint){.id=rootId,.anatomyId=rootId,.parentIndex=-1,
        .restPosition=root->center,.restRotation=Quat_Identity(),.constraint={.type=JOINT_FIXED}};
    r.skeleton.jointCount=1;
    /* El grafo de superficies puede contener ciclos (membranas). El rig usa
     * su árbol de expansión determinista; cada articulación tiene un padre. */
    for(size_t index=0;index<r.skeleton.jointCount;++index) {
        AnatomyId id=r.skeleton.joints[index].id;
        for(size_t e=0;e<g->connectionCount;++e) {
            const BodyConnection* edge=&g->connections[e];
            AnatomyId other=edge->fromId==id?edge->toId:edge->toId==id?edge->fromId:0;
            if (!other || Skeleton_FindJoint(&r.skeleton,other)>=0) continue;
            const AnatomyNode* a=AnatomyGraph_FindNode(g,id),*b=AnatomyGraph_FindNode(g,other);
            size_t n=r.skeleton.jointCount++;
            r.skeleton.joints[n]=(SkeletonJoint){.id=other,.anatomyId=other,.parentIndex=(int)index,
                .restPosition=Vec3_Sub(b->center,a->center),.restRotation=Quat_Identity(),
                .constraint={.type=JOINT_FIXED}};
        }
    }
    if(r.skeleton.jointCount!=g->nodeCount || !Skeleton_Validate(&r.skeleton)) return false;
    *out=r; return true;
}
bool RigBuilder_AddLimb(Rig* r,const AnatomyId* ids,size_t n,LimbRole role,LimbSide side,bool walking) {
    if (!r || !ids || n<2 || n>IK_MAX_CHAIN_JOINTS || r->limbCount>=RIG_MAX_LIMBS) return false;
    LimbRig limb={0}; limb.role=role; limb.side=side; limb.walking=walking;
    limb.chain.jointCount=n; limb.chain.maxIterations=48; limb.chain.tolerance=.002f;
    for(size_t i=0;i<n;++i) {
        int j=Skeleton_FindJoint(&r->skeleton,ids[i]);
        if(j<0 || (i && r->skeleton.joints[j].parentIndex!=limb.chain.jointIndices[i-1]))return false;
        limb.chain.jointIndices[i]=j;
    }
    limb.rootJoint=limb.chain.jointIndices[0];
    limb.endEffectorJoint=limb.chain.endEffectorJoint=limb.chain.jointIndices[n-1];
    r->limbs[r->limbCount++]=limb; return true;
}
