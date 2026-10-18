#include "AIWorld.h"
#include <math.h>
#include <string.h>
static float segmentDistance(Vector3 a, Vector3 b, Vector3 c, float *t) {
    float x = b.x - a.x, z = b.z - a.z, d = x * x + z * z;
    *t = d > 1e-8f ? fmaxf(0, fminf(1, ((c.x - a.x) * x + (c.z - a.z) * z) / d)) : 0;
    x = a.x + *t * x - c.x;
    z = a.z + *t * z - c.z;
    return x * x + z * z;
}
void AIWorld_Init(AIWorld *w, float half) {
    memset(w, 0, sizeof(*w));
    w->nextId = 1;
    w->halfSize = half;
}
AIObjectId AIWorld_Add(AIWorld *w, AIWorldObject o) {
    if (!w->nextId)
        return 0;
    for (unsigned i = 0; i < AI_WORLD_CAPACITY; ++i)
        if (!w->objects[i].active) {
            o.id = w->nextId++;
            o.active = true;
            w->objects[i] = o;
            ++w->revision;
            return o.id;
        }
    return 0;
}
const AIWorldObject *AIWorld_Get(const AIWorld *w, AIObjectId id) {
    for (unsigned i = 0; i < AI_WORLD_CAPACITY; ++i)
        if (w->objects[i].active && w->objects[i].id == id)
            return &w->objects[i];
    return NULL;
}
bool AIWorld_Move(AIWorld *w, AIObjectId id, Vector3 p) {
    for (unsigned i = 0; i < AI_WORLD_CAPACITY; ++i)
        if (w->objects[i].active && w->objects[i].id == id) {
            if (w->objects[i].type != AI_CREATURE)
                ++w->revision;
            w->objects[i].moving=Vec3_Distance(w->objects[i].position,p)>1e-6f;
            w->objects[i].position = p;
            return true;
        }
    return false;
}
bool AIWorld_SetBounds(AIWorld *w, AIObjectId id, float radius, float height) {
    if (!isfinite(radius) || !isfinite(height) || radius < 0 || height < 0)
        return false;
    for (unsigned i = 0; i < AI_WORLD_CAPACITY; ++i) {
        AIWorldObject *o = &w->objects[i];
        if (o->active && o->id == id) {
            if (o->type != AI_CREATURE &&
                (fabsf(o->radius - radius) > 1e-5f || fabsf(o->height - height) > 1e-5f))
                ++w->revision;
            o->radius = radius;
            o->height = height;
            return true;
        }
    }
    return false;
}
void AIWorld_Remove(AIWorld *w, AIObjectId id) {
    for (unsigned i = 0; i < AI_WORLD_CAPACITY; ++i)
        if (w->objects[i].id == id && w->objects[i].active) {
            w->objects[i].active = false;
            ++w->revision;
        }
}
unsigned AIWorld_Candidates(const AIWorld *w, Vector3 p, float r, AIObjectId *out, unsigned cap) {
    unsigned n = 0;
    for (unsigned i = 0; i < AI_WORLD_CAPACITY && n < cap; ++i) {
        const AIWorldObject *o = &w->objects[i];
        if (o->active &&
            Vec3_LengthSq(Vec3_Sub(p, o->position)) <= (r + o->radius) * (r + o->radius))
            out[n++] = o->id;
    }
    return n;
}
static bool clearSegment(const AIWorld *w,Vector3 a,Vector3 b,AIObjectId ignore,float radius,bool collision) {
    if(!isfinite(a.x)||!isfinite(a.y)||!isfinite(a.z)||!isfinite(b.x)||!isfinite(b.y)||!isfinite(b.z))return false;
    for(unsigned i=0;i<AI_WORLD_CAPACITY;++i) {
        const AIWorldObject *o=&w->objects[i];if(!o->active||(!o->occludes&&!(collision&&o->type==AI_OBSTACLE))||o->id==ignore)continue;
        float dx=b.x-a.x,dz=b.z-a.z,x=a.x-o->position.x,z=a.z-o->position.z;
        float aa=dx*dx+dz*dz,bb=2*(x*dx+z*dz),cc=x*x+z*z-(o->radius+radius)*(o->radius+radius);
        float lo=0,hi=1;
        if(aa<1e-12f) {if(cc>0)continue;}
        else {
            float discriminant=bb*bb-4*aa*cc;if(discriminant<0)continue;
            float root=sqrtf(discriminant);lo=fmaxf(lo,(-bb-root)/(2*aa));hi=fminf(hi,(-bb+root)/(2*aa));
        }
        float dy=b.y-a.y;
        if(fabsf(dy)<1e-8f) {if(a.y<o->position.y-radius||a.y>o->position.y+o->height+radius)continue;}
        else {
            float t0=(o->position.y-radius-a.y)/dy,t1=(o->position.y+o->height+radius-a.y)/dy;
            lo=fmaxf(lo,fminf(t0,t1));hi=fminf(hi,fmaxf(t0,t1));
        }
        if(lo<=hi)return false;
    }
    return true;
}
bool AIWorld_LineOfSight(const AIWorld *w,Vector3 a,Vector3 b,AIObjectId ignore) {
    return clearSegment(w,a,b,ignore,0,false);
}
bool AIWorld_ClearSegment(const AIWorld *w,Vector3 a,Vector3 b,float radius,AIObjectId ignore) {
    return isfinite(radius)&&radius>=0&&clearSegment(w,a,b,ignore,radius,true);
}
bool AIWorld_Walkable(const AIWorld *w, Vector3 p, float r, bool swim) {
    if (fabsf(p.x) + r > w->halfSize || fabsf(p.z) + r > w->halfSize)
        return false;
    for (unsigned i = 0; i < AI_WORLD_CAPACITY; ++i) {
        const AIWorldObject *o = &w->objects[i];
        if (!o->active || o->walkable || o->type == AI_CREATURE || (swim && o->type == AI_WATER))
            continue;
        float x = p.x - o->position.x, z = p.z - o->position.z;
        if (x * x + z * z < (r + o->radius) * (r + o->radius))
            return false;
    }
    return true;
}
bool AIWorld_SegmentWalkable(const AIWorld *w, Vector3 a, Vector3 b, float r, bool swim) {
    if (!AIWorld_Walkable(w, a, r, swim) || !AIWorld_Walkable(w, b, r, swim))
        return false;
    for (unsigned i = 0; i < AI_WORLD_CAPACITY; ++i) {
        const AIWorldObject *o = &w->objects[i];
        float t;
        if (o->active && !o->walkable && o->type != AI_CREATURE && !(swim && o->type == AI_WATER) &&
            segmentDistance(a, b, o->position, &t) < (r + o->radius) * (r + o->radius))
            return false;
    }
    return true;
}
bool AIWorld_CanDrink(const AIWorld *w, AIObjectId id, Vector3 mouth, float reach) {
    const AIWorldObject *o = AIWorld_Get(w, id);
    if (!o || o->type != AI_WATER || !o->drinkable)
        return false;
    Vector3 d = Vec3_Sub(mouth, o->position);
    d.y = 0;
    float length = Vec3_Length(d);
    if (length < o->radius - reach)
        return false;
    Vector3 bank = Vec3_Add(o->position, Vec3_Scale(Vec3_Normalize(d), o->radius));
    return Vec3_Distance(mouth, bank) <= reach && AIWorld_LineOfSight(w, mouth, bank, id);
}

bool AIWorld_InteractionTarget(const AIWorld *w,AIObjectId id,Vector3 mouth,float tolerance,InteractionTarget *t) {
    if(!t)return false;
    *t=(InteractionTarget){0};const AIWorldObject *o=AIWorld_Get(w,id);
    if(!o||(o->type!=AI_WATER&&o->type!=AI_FOOD)||(o->type==AI_WATER&&!o->drinkable))return false;
    Vector3 normal=Vec3_LengthSq(o->surfaceNormal)>.5f?Vec3_Normalize(o->surfaceNormal):Vec3_Create(0,1,0);
    Vector3 origin=Vec3_Add(o->position,Vec3_Scale(normal,o->type==AI_FOOD?o->height:0));
    Vector3 d=Vec3_Sub(mouth,origin);d=Vec3_Sub(d,Vec3_Scale(normal,Vec3_Dot(d,normal)));
    float distance=Vec3_Length(d);
    Vector3 point=Vec3_Add(origin,d);
    if(distance>o->radius&&distance>1e-6f)point=Vec3_Add(origin,Vec3_Scale(d,o->radius/distance));
    Vector3 approach=Vec3_Sub(point,mouth);
    if(Vec3_LengthSq(o->approachDirection)>.5f)approach=o->approachDirection;
    if(Vec3_LengthSq(approach)<1e-8f)approach=Vec3_Scale(normal,-1);
    *t=(InteractionTarget){.objectId=id,.revision=w->revision,
        .type=o->type==AI_WATER?INTERACTION_DRINK:INTERACTION_EAT,.position=point,
        .normal=normal,.approach=Vec3_Normalize(approach),.velocity=o->velocity,.moving=o->moving,.positionTolerance=tolerance,
        .orientationTolerance=1.4f,.valid=true,
        .blocked=!AIWorld_ClearSegment(w,mouth,point,0,id)};
    return InteractionTarget_Validate(t);
}
