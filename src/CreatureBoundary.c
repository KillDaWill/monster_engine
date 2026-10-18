/** @file CreatureBoundary.c
 * @brief Colisión de soportes de skinning y respuesta procedural articulada.
 */
#include "CreatureBoundary.h"
#include "Monster.h"
#include "SurfaceMapper.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

static bool ValidBox(AABB3D b) {return b.start.x<=b.end.x&&b.start.y<=b.end.y&&b.start.z<=b.end.z;}
static bool Reset(CreatureBoundary* b,const Skeleton* s) {
    if(!b||!s||!Skeleton_Validate(s))return false;
    AnatomyId ids[SKELETON_MAX_JOINTS];float bend[SKELETON_MAX_JOINTS];
    size_t old=b->jointCount;if(old>SKELETON_MAX_JOINTS)return false;
    b->fingerprint=0;memcpy(ids,b->ids,sizeof(ids));memcpy(bend,b->bend,sizeof(bend));
    b->jointCount=s->jointCount;
    for(size_t j=0;j<s->jointCount;++j) {
        b->support[j]=AABB_Empty();b->contactRadius[j]=0;b->ids[j]=s->joints[j].id;b->bend[j]=0;
        for(size_t k=0;k<old;++k)if(ids[k]==b->ids[j]){b->bend[j]=bend[k];break;}
    }
    if(!SkeletonPose_Init(&b->rest,s))return false;
    for(size_t j=0;j<s->jointCount;++j)b->inverseRest[j]=Quat_Inverse(b->rest.joints[j].worldRotation);
    return true;
}
bool CreatureBoundary_BindMesh(CreatureBoundary* b,const AnatomyDeformer* d,const Skeleton* s) {
    if(!d||!d->poseBound||!Reset(b,s))return false;
    b->rest=d->bindPose;
    for(size_t j=0;j<s->jointCount;++j) {
        const AnatomyNode* n=AnatomyGraph_FindNode(&d->refGraph,s->joints[j].anatomyId);
        if(n)b->contactRadius[j]=fmaxf(fmaxf(n->widthRadius,n->heightRadius),fmaxf(n->envelopeRadii.x,fmaxf(n->envelopeRadii.y,n->envelopeRadii.z)));
    }
    for(size_t v=0;v<d->vertexCount;++v)for(int k=0;k<2*ANATOMY_MORPH_INFLUENCES;++k) {
        if(d->skinBindings[v].weights[k]<=0)continue;
        int j=d->skinBindings[v].joints[k];if(j<0||j>=(int)b->jointCount)return false;
        AABB_ExpandPoint(&b->support[j],d->basePositions[v]);
    }
    return true;
}
bool CreatureBoundary_AddRigid(CreatureBoundary* b,const Mesh* mesh,int joint) {
    if(!b||!mesh||joint<0||joint>=(int)b->jointCount)return false;
    for(size_t v=0;v<mesh->vertexCount;++v) {
        AABB_ExpandPoint(&b->support[joint],mesh->vertices[v].position);
    }
    return true;
}
typedef struct BoundarySegment {
    Vector3 a,ab;
    float inverseLengthSq,inverseMinRadius,inverseMaxRadius,spread;
    int joints[2];
} BoundarySegment;

/* Cota de todos los segmentos del nodo sobre la esfera que contiene la celda.
 * El margen también cubre el redondeo de a+(b-a) al construir el BVH. */
static float BoundaryNodeLower(const SurfaceMorphDomain* domain,int node,Vector3 center,float radius) {
    const float* lo=domain->nodes[node][0];const float* hi=domain->nodes[node][1];
    float pad=1e-4f*(1+fmaxf(fmaxf(fabsf(lo[0]),fabsf(hi[0])),
        fmaxf(fmaxf(fabsf(lo[1]),fabsf(hi[1])),fmaxf(fabsf(lo[2]),fabsf(hi[2])))));
    float x=fmaxf(0,fmaxf(lo[0]-center.x,center.x-hi[0])-pad);
    float y=fmaxf(0,fmaxf(lo[1]-center.y,center.y-hi[1])-pad);
    float z=fmaxf(0,fmaxf(lo[2]-center.z,center.z-hi[2])-pad);
    float bound=fmaxf(0,sqrtf(x*x+y*y+z*z)-radius)/fmaxf(lo[3],1e-6f);
    return fmaxf(0,bound-fmaxf(1e-4f,bound*1e-5f));
}
static float BoundarySegmentBounds(const BoundarySegment* seg,Vector3 center,float radius,float* lower,float* projection) {
    float dx=center.x-seg->a.x,dy=center.y-seg->a.y,dz=center.z-seg->a.z;
    float t=(dx*seg->ab.x+dy*seg->ab.y+dz*seg->ab.z)*seg->inverseLengthSq;
    *projection=t;t=fmaxf(0,fminf(1,t));dx-=t*seg->ab.x;dy-=t*seg->ab.y;dz-=t*seg->ab.z;
    float distance=sqrtf(dx*dx+dy*dy+dz*dz);
    *lower=fmaxf(0,distance-radius)*seg->inverseMaxRadius;
    return (distance+radius)*seg->inverseMinRadius;
}
static void BoundaryCellSupport(CreatureBoundary* b,const SurfaceMorphDomain* domain,
    const BoundarySegment* segments,Vector3 lo,Vector3 hi,Vector3 center,float radius,bool exhaustive) {
    float lower[ANATOMY_MAX_CONNECTIONS],projection[ANATOMY_MAX_CONNECTIONS];
    float best[4]={INFINITY,INFINITY,INFINITY,INFINITY};
    bool seen[ANATOMY_MAX_CONNECTIONS]={0};
    int stack[2*ANATOMY_MAX_CONNECTIONS];unsigned top=0;
    /* Primera pasada: el mismo cuarto mejor upper, sin evaluar ramas que no
     * pueden mejorarlo. Segunda pasada: todas las influencias admisibles. */
    for(int pass=0;pass<2;++pass) {
        float threshold=best[3]+fmaxf(1e-4f,best[3]*1e-5f);
        stack[top++]=0;
        while(top) {
            int node=stack[--top];const float* info=domain->nodes[node][2];
            float cutoff=pass?threshold:best[3];
            if(!exhaustive&&BoundaryNodeLower(domain,node,center,radius)>cutoff)continue;
            unsigned begin=(unsigned)info[2],count=(unsigned)info[3];
            if(!count) {
                int left=(int)info[0],right=(int)info[1];
                if(!exhaustive&&BoundaryNodeLower(domain,left,center,radius)>
                    BoundaryNodeLower(domain,right,center,radius)){int swap=left;left=right;right=swap;}
                stack[top++]=right;stack[top++]=left;continue;
            }
            for(unsigned i=begin;i<begin+count;++i) {
                if(!pass||!seen[i]) {
                    float upper=BoundarySegmentBounds(&segments[i],center,radius,&lower[i],&projection[i]);
                    seen[i]=true;
                    if(!pass)for(int k=0;k<4;++k)if(upper<best[k]) {
                        for(int j=3;j>k;--j)best[j]=best[j-1];
                        best[k]=upper;break;
                    }
                }
                if(!pass||lower[i]>threshold)continue;
                for(int e=0;e<2;++e) {
                    const BoundarySegment* seg=&segments[i];
                    if((e==0&&projection[i]-seg->spread>=1)||(e==1&&projection[i]+seg->spread<=0))continue;
                    AABB_ExpandPoint(&b->support[seg->joints[e]],lo);AABB_ExpandPoint(&b->support[seg->joints[e]],hi);
                }
            }
        }
    }
}

bool CreatureBoundary_BindSDF(CreatureBoundary* b,const Monster* m,const MonsterSDF* sdf,float spacing) {
    if(!b||!m||!m->animation||!sdf||!ValidBox(sdf->bounds)||!isfinite(spacing)||spacing<=0)return false;
    uint64_t fingerprint=AnatomyGraph_Fingerprint(&m->anatomyGraph);
    if(b->fingerprint==fingerprint&&b->sampleSpacing==spacing&&memcmp(&b->fieldBounds,&sdf->bounds,sizeof(sdf->bounds))==0&&b->jointCount==m->animation->rig.skeleton.jointCount)return true;
    const Skeleton* s=&m->animation->rig.skeleton;SurfaceMorphDomain domain;
    if(!Reset(b,s)||!SurfaceMapper_BuildMorphDomain(&domain,&m->anatomyGraph,&m->anatomyGraph,&m->surfaceMapping)||!domain.count)return false;
    for(size_t j=0;j<s->jointCount;++j) {
        const AnatomyNode* n=AnatomyGraph_FindNode(&m->anatomyGraph,s->joints[j].anatomyId);
        if(n)b->contactRadius[j]=fmaxf(fmaxf(n->widthRadius,n->heightRadius),fmaxf(n->envelopeRadii.x,fmaxf(n->envelopeRadii.y,n->envelopeRadii.z)));
    }
    BoundarySegment segments[ANATOMY_MAX_CONNECTIONS];
    bool exhaustive=getenv("MONSTER_BOUNDARY_EXHAUSTIVE")!=NULL;
    AABB3D samplingBounds=sdf->bounds;AABB_Pad(&samplingBounds,spacing);
    const int cells=12;Vector3 step=Vec3_Scale(AABB_Size(samplingBounds),1.f/cells);float radius=.5f*Vec3_Length(step);
    for(size_t i=0;i<domain.count;++i) {
        const float* a=domain.data[i][0];const float* c=domain.data[i][1];BoundarySegment* seg=&segments[i];
        seg->a=(Vector3){a[0],a[1],a[2]};seg->ab=(Vector3){c[0]-a[0],c[1]-a[1],c[2]-a[2]};
        seg->inverseLengthSq=1/fmaxf(Vec3_LengthSq(seg->ab),1e-12f);
        seg->inverseMinRadius=1/fmaxf(fminf(a[3],c[3]),1e-6f);seg->inverseMaxRadius=1/fmaxf(fmaxf(a[3],c[3]),1e-6f);
        seg->spread=radius*sqrtf(seg->inverseLengthSq);
        for(int e=0;e<2;++e) {
            Vector3 p={domain.data[i][e][0],domain.data[i][e][1],domain.data[i][e][2]};seg->joints[e]=-1;
            for(size_t n=0;n<m->anatomyGraph.nodeCount;++n)if(Vec3_LengthSq(Vec3_Sub(p,m->anatomyGraph.nodes[n].center))<1e-12f) {
                seg->joints[e]=Skeleton_FindJoint(s,m->anatomyGraph.nodes[n].id);break;
            }
            if(seg->joints[e]<0)return false;
        }
    }
    SDFField field=MonsterSDF_GetField(sdf);
    /* Certificar grupos vacíos antes de evaluar sus ocho celdas. La prueba
     * conserva el intervalo completo, incluyendo superficies finas y bocas. */
    bool emptyGroups[6][6][6]={0};
    for(int z=0;z<6;++z)for(int y=0;y<6;++y)for(int x=0;x<6;++x) {
        Vector3 lo=Vec3_Add(samplingBounds.start,Vec3_Create(2*x*step.x,2*y*step.y,2*z*step.z));
        AABB3D box={lo,Vec3_Add(lo,Vec3_Scale(step,2))};AABB_Pad(&box,spacing);
        float minimum,maximum;
        bool empty=field.getCellRange&&field.getCellRange(field.context,box,&minimum,&maximum)&&minimum>0;
        for(size_t mouth=0;empty&&mouth<sdf->mouthCount;++mouth) {
            AABB3D oral=sdf->mouths[mouth].visualBounds;
            if(box.start.x<=oral.end.x&&box.end.x>=oral.start.x&&box.start.y<=oral.end.y&&box.end.y>=oral.start.y&&box.start.z<=oral.end.z&&box.end.z>=oral.start.z)empty=false;
        }
        emptyGroups[z][y][x]=empty;
    }
    for(int z=0;z<cells;++z)for(int y=0;y<cells;++y)for(int x=0;x<cells;++x) {
        if(emptyGroups[z/2][y/2][x/2])continue;
        Vector3 lo=Vec3_Add(samplingBounds.start,Vec3_Create(x*step.x,y*step.y,z*step.z));
        Vector3 hi=Vec3_Add(lo,step),center=Vec3_Scale(Vec3_Add(lo,hi),.5f);
        AABB3D sampleBox={lo,hi};AABB_Pad(&sampleBox,spacing);float minimum,maximum;
        bool empty=field.getCellRange&&field.getCellRange(field.context,sampleBox,&minimum,&maximum)&&minimum>0;
        for(size_t mouth=0;empty&&mouth<sdf->mouthCount;++mouth) {
            AABB3D oral=sdf->mouths[mouth].visualBounds;
            if(sampleBox.start.x<=oral.end.x&&sampleBox.end.x>=oral.start.x&&sampleBox.start.y<=oral.end.y&&sampleBox.end.y>=oral.start.y&&sampleBox.start.z<=oral.end.z&&sampleBox.end.z>=oral.start.z)empty=false;
        }
        if(empty)continue;
        BoundaryCellSupport(b,&domain,segments,lo,hi,center,radius,exhaustive);
    }
    b->fingerprint=fingerprint;b->sampleSpacing=spacing;b->fieldBounds=sdf->bounds;return true;
}
float CreatureBoundary_TurningRadius(const CreatureBoundary* b,const MonsterAnimation* a,float scale,float padding) {
    if(!b||!a||!isfinite(scale)||scale<=0)return 0;
    float radiusSquared=0;Vector3 center=a->animator.locomotion.bodyPosition;center.y=0;
    for(size_t j=0;j<b->jointCount;++j){AABB3D box=b->support[j];if(!ValidBox(box))continue;
        Quaternion q=Quat_Multiply(a->pose.joints[j].worldRotation,b->inverseRest[j]);
        Vector3 shift=Vec3_Sub(a->pose.joints[j].worldPosition,Quat_RotateVector(q,b->rest.joints[j].worldPosition));
        for(unsigned corner=0;corner<8;++corner){Vector3 point=Vec3_Create(corner&1?box.end.x:box.start.x,corner&2?box.end.y:box.start.y,corner&4?box.end.z:box.start.z);
            point=Vec3_Sub(Vec3_Add(Quat_RotateVector(q,point),shift),center);
            radiusSquared=fmaxf(radiusSquared,point.x*point.x+point.z*point.z);
        }
    }
    return sqrtf(radiusSquared)*scale+sqrtf(2)*fmaxf(0,padding);
}

AABB3D CreatureBoundary_Bounds(const CreatureBoundary* b,const MonsterAnimation* a,Vector3 position,float yaw,float scale,float padding) {
    AABB3D bounds=AABB_Empty();
    Quaternion rotation=Quat_FromAxisAngle(Vec3_Create(0,1,0),yaw);
    Vector3 center=a->animator.locomotion.bodyPosition;center.y=0;
    for(size_t j=0;j<b->jointCount;++j) {
        AABB3D box=b->support[j];if(!ValidBox(box))continue;
        Quaternion q=Quat_Multiply(a->pose.joints[j].worldRotation,b->inverseRest[j]);
        Vector3 shift=Vec3_Sub(a->pose.joints[j].worldPosition,Quat_RotateVector(q,b->rest.joints[j].worldPosition));
        Quaternion worldRotation=Quat_Normalize(Quat_Multiply(rotation,q));
        Vector3 boxCenter=Vec3_Scale(Vec3_Add(box.start,box.end),.5f);
        Vector3 extent=Vec3_Scale(Vec3_Sub(box.end,box.start),.5f*scale);
        Vector3 worldCenter=Vec3_Add(position,Quat_RotateVector(rotation,
            Vec3_Scale(Vec3_Sub(Vec3_Add(Quat_RotateVector(q,boxCenter),shift),center),scale)));
        float qx=worldRotation.x,qy=worldRotation.y,qz=worldRotation.z,qw=worldRotation.w;
        Vector3 worldExtent={fabsf(1-2*(qy*qy+qz*qz))*extent.x+fabsf(2*(qx*qy-qw*qz))*extent.y+fabsf(2*(qx*qz+qw*qy))*extent.z,
            fabsf(2*(qx*qy+qw*qz))*extent.x+fabsf(1-2*(qx*qx+qz*qz))*extent.y+fabsf(2*(qy*qz-qw*qx))*extent.z,
            fabsf(2*(qx*qz-qw*qy))*extent.x+fabsf(2*(qy*qz+qw*qx))*extent.y+fabsf(1-2*(qx*qx+qy*qy))*extent.z};
        AABB_ExpandPoint(&bounds,Vec3_Sub(worldCenter,worldExtent));
        AABB_ExpandPoint(&bounds,Vec3_Add(worldCenter,worldExtent));
    }
    if(ValidBox(bounds))AABB_Pad(&bounds,padding);
    return bounds;
}
static float Penetration(AABB3D b,float h) {
    float p[]={fmaxf(0,b.end.x-h),fmaxf(0,-h-b.start.x),fmaxf(0,b.end.z-h),fmaxf(0,-h-b.start.z)};
    return p[0]*p[0]+p[1]*p[1]+p[2]*p[2]+p[3]*p[3];
}
/** @brief Presión local anatómica; la envolvente completa certifica la salida final. */
static float ContactCost(const CreatureBoundary* b,const MonsterAnimation* a,Vector3 position,
    float yaw,float scale,float half,float padding) {
    float c=cosf(yaw),s=sinf(yaw),cost=0;
    Vector3 center=a->animator.locomotion.bodyPosition;
    for(size_t j=0;j<b->jointCount;++j) {
        if(b->contactRadius[j]<=0)continue;
        Vector3 p=Vec3_Sub(a->pose.joints[j].worldPosition,center);
        float x=position.x+scale*(c*p.x+s*p.z),z=position.z+scale*(-s*p.x+c*p.z);
        float radius=b->contactRadius[j]*scale+padding;
        float dx=fmaxf(0,fabsf(x)+radius-half),dz=fmaxf(0,fabsf(z)+radius-half);
        cost+=dx*dx+dz*dz;
    }
    return cost;
}
bool CreatureBoundary_Solve(CreatureBoundary* b,MonsterAnimation* a,Vector3* position,float* orientation,float scale,float half,float padding,float dt) {
    if(!b||!a||!position||!orientation||b->jointCount!=a->pose.jointCount||!isfinite(*orientation)||!isfinite(scale)||scale<=0||
       !isfinite(half)||half<=0||!isfinite(padding)||padding<0||!isfinite(dt)||dt<0||
       !isfinite(position->x)||!isfinite(position->z))return false;
    b->contactTrials=0;
    float yaw=*orientation;
    bool bending=false;for(size_t j=0;j<b->jointCount;++j)bending|=fabsf(b->bend[j])>1e-4f;
    AABB3D initial=CreatureBoundary_Bounds(b,a,*position,yaw,scale,padding);
    if(!bending&&ValidBox(initial)&&Penetration(initial,half-.35f)==0)return true;
    SkeletonPose proceduralPose=a->pose;
    const Rig* r=&a->rig;const Skeleton* s=&r->skeleton;
    Quaternion base[SKELETON_MAX_JOINTS];bool axial[SKELETON_MAX_JOINTS]={0};
    for(size_t i=0;i<r->spineCount;++i)axial[r->spine[i]]=true;
    for(size_t t=0;t<r->tailCount;++t)for(size_t i=0;i<r->tails[t].jointCount;++i)axial[r->tails[t].joints[i]]=true;
    if(r->neckJoint>=0)axial[r->neckJoint]=true;
    if(r->headJoint>=0)axial[r->headJoint]=true;
    for(size_t j=0;j<b->jointCount;++j) {
        base[j]=a->pose.joints[j].localRotation;
        b->bend[j]*=expf(-3*dt);
        if(axial[j]&&s->joints[j].parentIndex>=0)a->pose.joints[j].localRotation=Skeleton_ConstrainRotation(&s->joints[j],
            Quat_Multiply(base[j],Quat_FromAxisAngle(Vec3_Create(0,1,0),b->bend[j])));
    }
    if(!SkeletonPose_UpdateWorldTransforms(&a->pose,s))return false;
    float step=fminf(.08f,2.4f*dt);
    /* Presupuesto acotado y contacto local: O(J) por prueba, independiente
     * del número total de vértebras. El resto sigue su animación procedural. */
    float cost=ContactCost(b,a,*position,yaw,scale,half-.35f,padding);
    bool changed=bending;
    bool touched[SKELETON_MAX_JOINTS]={0};float c=cosf(yaw),sn=sinf(yaw);
    for(size_t j=0;j<b->jointCount;++j) {
        if(b->contactRadius[j]<=0)continue;
        Vector3 p=Vec3_Sub(a->pose.joints[j].worldPosition,a->animator.locomotion.bodyPosition);
        float x=position->x+scale*(c*p.x+sn*p.z),z=position->z+scale*(-sn*p.x+c*p.z);
        float radius=b->contactRadius[j]*scale+padding+.35f;
        if(fabsf(x)+radius>half||fabsf(z)+radius>half)
            touched[j]=true;
    }
    /* La jerarquía está ordenada: propagar presión hacia la raíz en O(J). */
    for(size_t j=b->jointCount;j>0;--j) {
        int parent=s->joints[j-1].parentIndex;
        if(touched[j-1]&&parent>=0)touched[parent]=true;
    }
    size_t trials=0;
    for(size_t offset=0;offset<b->jointCount&&trials<8&&step>0&&cost>1e-8f;++offset) {
        size_t j=b->jointCount-1-offset;
        if(!axial[j]||s->joints[j].parentIndex<0||s->joints[j].constraint.type==JOINT_FIXED)continue;
        float original=b->bend[j],chosen=original;
        if(!touched[j])continue;
        ++trials;
        for(int direction=-1;direction<=1;direction+=2) {
            float limit=fminf(.7f,s->joints[j].constraint.maxSwingAngle+s->joints[j].constraint.maxTwistAngle);
            float candidate=fmaxf(-limit,fminf(limit,original+direction*step));
            a->pose.joints[j].localRotation=Skeleton_ConstrainRotation(&s->joints[j],Quat_Multiply(base[j],Quat_FromAxisAngle(Vec3_Create(0,1,0),candidate)));
            if(!SkeletonPose_UpdateSubtree(&a->pose,s,(int)j))return false;
            float next=ContactCost(b,a,*position,yaw,scale,half-.35f,padding);++b->contactTrials;
            if(next<cost-1e-7f){cost=next;chosen=candidate;}
        }
        changed|=chosen!=original;b->bend[j]=chosen;
        a->pose.joints[j].localRotation=Skeleton_ConstrainRotation(&s->joints[j],Quat_Multiply(base[j],Quat_FromAxisAngle(Vec3_Create(0,1,0),chosen)));
        if(!SkeletonPose_UpdateSubtree(&a->pose,s,(int)j))return false;
    }
    AABB3D box=CreatureBoundary_Bounds(b,a,*position,yaw,scale,padding);
    if(box.end.x-box.start.x>2*half||box.end.z-box.start.z>2*half) {
        changed=true;
        SkeletonPose bestPose=a->pose;float bestBend[SKELETON_MAX_JOINTS];memcpy(bestBend,b->bend,sizeof(bestBend));
        Vector3 size=AABB_Size(box);float best=fmaxf(size.x-2*half,0)+fmaxf(size.z-2*half,0);
        for(int direction=-1;direction<=1;direction+=2)for(int level=1;level<=4;++level) {
            a->pose=bestPose;
            for(size_t t=0;t<r->tailCount;++t)for(size_t i=0;i<r->tails[t].jointCount;++i) {
                int j=r->tails[t].joints[i];float candidate=direction*.08f*level;
                a->pose.joints[j].localRotation=Skeleton_ConstrainRotation(&s->joints[j],Quat_Multiply(base[j],Quat_FromAxisAngle(Vec3_Create(0,1,0),candidate)));
            }
            if(!SkeletonPose_UpdateWorldTransforms(&a->pose,s))return false;
            Vector3 trial=AABB_Size(CreatureBoundary_Bounds(b,a,*position,yaw,scale,padding));
            float cost=fmaxf(trial.x-2*half,0)+fmaxf(trial.z-2*half,0);
            if(cost<best) {
                best=cost;bestPose=a->pose;
                for(size_t t=0;t<r->tailCount;++t)for(size_t i=0;i<r->tails[t].jointCount;++i)bestBend[r->tails[t].joints[i]]=direction*.08f*level;
            }
        }
        a->pose=bestPose;memcpy(b->bend,bestBend,sizeof(bestBend));
        box=CreatureBoundary_Bounds(b,a,*position,yaw,scale,padding);
    }
    /* Una aparición o crecimiento puede exigir flexión inmediata para caber.
     * Este caso excepcional minimiza el tamaño, independientemente de la raíz. */
    for(int sweep=0;sweep<1&&(box.end.x-box.start.x>2*half||box.end.z-box.start.z>2*half);++sweep) {
        changed=true;
        Vector3 previousSize=AABB_Size(box);
        float previousCost=fmaxf(0,previousSize.x-2*half)+fmaxf(0,previousSize.z-2*half);
        for(size_t j=0;j<b->jointCount;++j) {
            if(!axial[j]||s->joints[j].parentIndex<0||s->joints[j].constraint.type==JOINT_FIXED)continue;
            float original=b->bend[j],chosen=original;
            Vector3 size=AABB_Size(box);
            float cost=powf(fmaxf(0,size.x-2*half),2)+powf(fmaxf(0,size.z-2*half),2);
            for(int direction=-1;direction<=1;direction+=2) {
                float candidate=fmaxf(-.7f,fminf(.7f,original+direction*.12f));
                a->pose.joints[j].localRotation=Skeleton_ConstrainRotation(&s->joints[j],Quat_Multiply(base[j],Quat_FromAxisAngle(Vec3_Create(0,1,0),candidate)));
                if(!SkeletonPose_UpdateSubtree(&a->pose,s,(int)j))return false;
                AABB3D trial=CreatureBoundary_Bounds(b,a,*position,yaw,scale,padding);size=AABB_Size(trial);
                float next=powf(fmaxf(0,size.x-2*half),2)+powf(fmaxf(0,size.z-2*half),2);
                if(next<cost){cost=next;chosen=candidate;}
            }
            b->bend[j]=chosen;
            a->pose.joints[j].localRotation=Skeleton_ConstrainRotation(&s->joints[j],Quat_Multiply(base[j],Quat_FromAxisAngle(Vec3_Create(0,1,0),chosen)));
            if(!SkeletonPose_UpdateSubtree(&a->pose,s,(int)j))return false;
            box=CreatureBoundary_Bounds(b,a,*position,yaw,scale,padding);
        }
        Vector3 size=AABB_Size(box);
        float cost=fmaxf(0,size.x-2*half)+fmaxf(0,size.z-2*half);
        /* No repetir barridos cuando la envolvente o los límites del rig
         * impiden progresar; la raíz resuelve entonces el contacto. */
        if(previousCost-cost<1e-4f)break;
    }
    /* La flexión axial mueve las caderas y hombros: resolver otra vez los
     * apoyos evita levantar las patas al responder a la pared. */
    for(size_t i=0;changed&&i<r->limbCount;++i)if(r->limbs[i].walking) {
        IKChain chain=r->limbs[i].chain;chain.targetPosition=a->animator.locomotion.feet[i].targetPosition;
        a->animator.limbResults[i]=IK_SolveFABRIK(s,&a->pose,&chain);
        int joint=chain.endEffectorJoint,parent=s->joints[joint].parentIndex;
        Quaternion orientation=Quat_FromTo(Vec3_Create(0,1,0),a->animator.locomotion.feet[i].normal);
        a->pose.joints[joint].localRotation=Skeleton_ConstrainRotation(&s->joints[joint],Quat_Multiply(Quat_Inverse(a->pose.joints[parent].worldRotation),orientation));
    }
    if(!SkeletonPose_UpdateWorldTransforms(&a->pose,s))return false;
    box=CreatureBoundary_Bounds(b,a,*position,yaw,scale,padding);
    /* El contacto también puede ejercer par sobre la raíz. Una criatura muy
     * larga cabe en diagonal aunque su proyección axial exceda el recinto. */
    if(box.end.x-box.start.x>2*half||box.end.z-box.start.z>2*half) {
        bool fits=false;
        for(int i=1;i<=80&&!fits;++i)for(int direction=-1;direction<=1;direction+=2) {
            float candidate=yaw+direction*.02f*i;
            AABB3D trial=CreatureBoundary_Bounds(b,a,*position,candidate,scale,padding);
            if(trial.end.x-trial.start.x<=2*half&&trial.end.z-trial.start.z<=2*half) {
                yaw=candidate;box=trial;fits=true;break;
            }
        }
        if(!fits) {
            /* Si la flexión local empeora el encaje global, conservar la pose
             * procedural ya resuelta y buscar un rumbo admisible. */
            a->pose=proceduralPose;
            memset(b->bend,0,sizeof(b->bend));
            for(int i=0;i<=80&&!fits;++i)for(int direction=-1;direction<=1;direction+=2) {
                float candidate=yaw+direction*.02f*i;
                AABB3D trial=CreatureBoundary_Bounds(b,a,*position,candidate,scale,padding);
                if(trial.end.x-trial.start.x<=2*half&&trial.end.z-trial.start.z<=2*half) {
                    yaw=candidate;box=trial;fits=true;break;
                }
            }
        }
        if(!fits)return false;
    }
    if(!ValidBox(box))return false;
    *orientation=yaw;
    float dx=fmaxf(-half-box.start.x,fminf(0,half-box.end.x));
    float dz=fmaxf(-half-box.start.z,fminf(0,half-box.end.z));
    position->x+=dx;position->z+=dz;
    return true;
}
