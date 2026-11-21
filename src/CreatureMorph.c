/** @file CreatureMorph.c
 * @brief Interpolación de anatomía resuelta; no elige una especie a mitad del recorrido.
 */
#include "CreatureMorph.h"
#include "AxialBodyUprightTetrapod.h"
#include "MathUtils.h"
#include "Tail.h"
#include "FlexibleAppendage.h"
#include "HeadModule.h"
#include <math.h>
#include <string.h>

static const BodyConnection* Incoming(const AnatomyGraph* g,AnatomyId id) {
    for(size_t i=0;i<g->connectionCount;++i)
        if(g->connections[i].toId==id)return &g->connections[i];
    return NULL;
}
static const BodyConnection* Edge(const AnatomyGraph* g,const BodyConnection* e) {
    for(size_t i=0;i<g->connectionCount;++i) {
        const BodyConnection* x=&g->connections[i];
        if(x->fromId==e->fromId && x->toId==e->toId && x->kind==e->kind)return x;
    }
    return NULL;
}
static bool Morph_IsNonOrnamentSupport(const AnatomyGraph* g,const BodyConnection* e) {
    if(!g||!e||e->kind!=BODY_CONNECTION_SUPPORT)return false;
    const AnatomyNode* from=AnatomyGraph_FindNode(g,e->fromId);
    const AnatomyNode* to=AnatomyGraph_FindNode(g,e->toId);
    return (!from||from->region!=ANATOMY_REGION_ORNAMENT)&&
           (!to||to->region!=ANATOMY_REGION_ORNAMENT);
}
static const BodyConnection* EdgeSameEnds(const AnatomyGraph* g,const BodyConnection* e) {
    for(size_t i=0;i<g->connectionCount;++i) {
        const BodyConnection* x=&g->connections[i];
        if(x->fromId==e->fromId && x->toId==e->toId)return x;
    }
    return NULL;
}
static AnatomyNode Counterpart(const AnatomyGraph* own,const AnatomyGraph* other,const AnatomyNode* n) {
    const AnatomyNode* match=AnatomyGraph_FindNode(other,n->id);
    if(match)return *match;
    AnatomyNode collapsed=*n;
    AnatomyId id=n->id;
    const AnatomyNode* root=n;
    for(size_t depth=0;depth<own->nodeCount;++depth) {
        const BodyConnection* parent=Incoming(own,id);
        if(!parent)break;
        id=parent->fromId;
        match=AnatomyGraph_FindNode(other,id);
        if(match){
            collapsed.center=match->center;
            if(n->region==ANATOMY_REGION_ORNAMENT||n->region==ANATOMY_REGION_FLEXIBLE) {
                const AnatomyNode* host=AnatomyGraph_FindNode(own,id);
                if(host) {
                    /* El brote nace en su ranura superficial, no en el centro
                     * del anfitrión. Todos sus nodos colapsan en la misma raíz. */
                    Vector3 offset=Vec3_Sub(root->center,host->center);
                    Vector3 from=host->envelopeRadii,to=match->envelopeRadii;
                    offset.x*=to.x/fmaxf(from.x,1e-6f);
                    offset.y*=to.y/fmaxf(from.y,1e-6f);
                    offset.z*=to.z/fmaxf(from.z,1e-6f);
                    collapsed.center=Vec3_Add(match->center,offset);
                }
            }
            break;
        }
        const AnatomyNode* ancestor=AnatomyGraph_FindNode(own,id);
        if(ancestor&&ancestor->region==n->region&&ancestor->moduleInstanceId==n->moduleInstanceId)root=ancestor;
    }
    collapsed.widthRadius=collapsed.heightRadius=1e-6f;
    collapsed.envelopeRadii=Vec3_Zero();
    collapsed.development=0;
    return collapsed;
}

/* Los identificadores de módulo son locales a cada receta. Si dos recetas
 * reutilizan uno con significado distinto, se traslada todo el módulo del
 * segundo extremo a un espacio libre antes de formar el grafo unión. */
static bool Morph_ModuleUsed(const AnatomyGraph* a,const AnatomyGraph* b,uint32_t module) {
    for(size_t i=0;i<a->nodeCount;++i)if(a->nodes[i].moduleInstanceId==module)return true;
    for(size_t i=0;i<b->nodeCount;++i)if(b->nodes[i].moduleInstanceId==module)return true;
    for(size_t i=0;i<a->connectionCount;++i)if(a->connections[i].moduleInstanceId==module)return true;
    for(size_t i=0;i<b->connectionCount;++i)if(b->connections[i].moduleInstanceId==module)return true;
    return false;
}
static uint32_t Morph_RemapModule(uint32_t module,const uint32_t* oldModules,
    const uint32_t* newModules,size_t count) {
    for(size_t i=0;i<count;++i)if(oldModules[i]==module)return newModules[i];
    return module;
}
static AnatomyId Morph_RemapId(AnatomyId id,const uint32_t* oldModules,
    const uint32_t* newModules,size_t count) {
    uint32_t module=Morph_RemapModule(id>>16,oldModules,newModules,count);
    return Anatomy_MakeId(module,(uint16_t)id);
}
static bool Morph_LocalDigitUsed(const AnatomyGraph* g,uint32_t module,uint16_t local) {
    AnatomyId id=Anatomy_MakeId(module,local);
    if(!id||AnatomyGraph_FindNode(g,id))return true;
    for(size_t i=0;i<g->connectionCount;++i)
        if(g->connections[i].id==id)return true;
    return false;
}

static bool Morph_ShiftConflictingDigits(const AnatomyGraph* a,const AnatomyGraph* b,
    AnatomyGraph* remapped,uint32_t module) {
    AnatomyId oldIds[ANATOMY_MAX_NODES],newIds[ANATOMY_MAX_NODES];
    size_t shiftCount=0;
    uint16_t nextLocal=120;
    for(size_t i=0;i<remapped->nodeCount;++i) {
        AnatomyNode* node=&remapped->nodes[i];
        if(node->moduleInstanceId!=module||node->role!=ANATOMY_ROLE_DIGIT||
           node->localNodeId<32||node->localNodeId>=200)continue;
        while(nextLocal<200&&(Morph_LocalDigitUsed(a,module,nextLocal)||
              Morph_LocalDigitUsed(b,module,nextLocal)||
              Morph_LocalDigitUsed(remapped,module,nextLocal)))nextLocal++;
        if(nextLocal>=200)return false;
        oldIds[shiftCount]=node->id;
        newIds[shiftCount]=Anatomy_MakeId(module,nextLocal);
        shiftCount++;
        node->localNodeId=nextLocal;
        node->id=Anatomy_MakeId(module,nextLocal);
        nextLocal++;
    }
    for(size_t i=0;i<remapped->connectionCount;++i) {
        BodyConnection* edge=&remapped->connections[i];
        if(edge->moduleInstanceId!=module)continue;
        AnatomyId shiftedTo=0;
        for(size_t k=0;k<shiftCount;++k) {
            if(edge->fromId==oldIds[k])edge->fromId=newIds[k];
            if(edge->toId==oldIds[k]){edge->toId=newIds[k];shiftedTo=newIds[k];}
            if(edge->id==oldIds[k])edge->id=newIds[k];
        }
        bool edgeConflict=false;
        for(size_t j=0;j<a->connectionCount;++j) {
            const BodyConnection* peer=&a->connections[j];
            if(peer->id==edge->id&&(peer->fromId!=edge->fromId||peer->toId!=edge->toId||
               (peer->kind!=edge->kind&&!(Morph_IsNonOrnamentSupport(a,peer)||Morph_IsNonOrnamentSupport(remapped,edge))))) {
                edgeConflict=true;break;
            }
        }
        if(edgeConflict) {
            if(shiftedTo && !Morph_LocalDigitUsed(a,module,(uint16_t)shiftedTo) &&
               !Morph_LocalDigitUsed(remapped,module,(uint16_t)shiftedTo)) {
                edge->id=shiftedTo;
            } else {
                while(nextLocal<200&&(Morph_LocalDigitUsed(a,module,nextLocal)||
                      Morph_LocalDigitUsed(b,module,nextLocal)||
                      Morph_LocalDigitUsed(remapped,module,nextLocal)))nextLocal++;
                if(nextLocal>=200)return false;
                edge->id=Anatomy_MakeId(module,nextLocal++);
            }
        }
    }
    return true;
}

static const BodyConnection* Morph_NasalSupport(const AnatomyGraph* graph) {
    for(size_t i=0;i<graph->connectionCount;++i) {
        const BodyConnection* edge=&graph->connections[i];
        if(edge->kind!=BODY_CONNECTION_PAIRED_OPENINGS)continue;
        const BodyConnection* root=Incoming(graph,Anatomy_MakeId(edge->moduleInstanceId,1));
        const AnatomyNode* head=root?AnatomyGraph_FindNode(graph,root->fromId):NULL;
        if(head && head->region==ANATOMY_REGION_HEAD)return root;
    }
    return NULL;
}

static bool Morph_RemapConflictingModules(const AnatomyGraph* a,const AnatomyGraph* b,
    AnatomyGraph* remapped,uint32_t* outOld,uint32_t* outNew,size_t* outCount) {
    *remapped=*b;
    uint32_t oldModules[ANATOMY_MAX_NODES+ANATOMY_MAX_CONNECTIONS];
    uint32_t newModules[ANATOMY_MAX_NODES+ANATOMY_MAX_CONNECTIONS];
    size_t count=0;
    uint32_t digitShiftModules[ANATOMY_MAX_NODES];
    size_t digitShiftCount=0;
    for(size_t i=0;i<b->nodeCount;++i) {
        const AnatomyNode* source=&b->nodes[i];
        const AnatomyNode* peer=AnatomyGraph_FindNode(a,source->id);
        if(!peer||source->moduleInstanceId==0)continue;
        if(peer->role==source->role&&peer->region==source->region&&
           peer->side==source->side&&peer->localNodeId==source->localNodeId)continue;
        bool known=false;
        for(size_t j=0;j<count;++j)known|=oldModules[j]==source->moduleInstanceId;
        if(!known)oldModules[count++]=source->moduleInstanceId;
    }
    for(size_t i=0;i<b->connectionCount;++i) {
        const BodyConnection* source=&b->connections[i];
        if(source->moduleInstanceId==0)continue;
        bool alreadyWhole=false;
        for(size_t j=0;j<count;++j)alreadyWhole|=oldModules[j]==source->moduleInstanceId;
        if(alreadyWhole)continue;
        bool conflict=false;
        bool digitOnlyConflict=false;
        for(size_t j=0;j<a->connectionCount;++j) {
            const BodyConnection* peer=&a->connections[j];
            if(peer->id!=source->id && !(peer->fromId==source->fromId && peer->toId==source->toId))continue;
            if(peer->fromId!=source->fromId||peer->toId!=source->toId) {
                conflict=true;break;
            }
            if(peer->kind!=source->kind) {
                if(Morph_IsNonOrnamentSupport(a,peer)!=Morph_IsNonOrnamentSupport(b,source))
                    continue;
                const AnatomyNode* toNode=AnatomyGraph_FindNode(b,source->toId);
                if(toNode&&toNode->role==ANATOMY_ROLE_DIGIT&&toNode->localNodeId>=32&&toNode->localNodeId<200) {
                    digitOnlyConflict=true;
                } else {
                    conflict=true;break;
                }
            }
        }
        if(conflict) {
            bool known=false;
            for(size_t j=0;j<count;++j)known|=oldModules[j]==source->moduleInstanceId;
            if(!known)oldModules[count++]=source->moduleInstanceId;
        } else if(digitOnlyConflict) {
            bool known=false;
            for(size_t j=0;j<digitShiftCount;++j)known|=digitShiftModules[j]==source->moduleInstanceId;
            if(!known)digitShiftModules[digitShiftCount++]=source->moduleInstanceId;
        }
    }
    for(size_t i=0;i<digitShiftCount;++i) {
        bool whole=false;
        for(size_t j=0;j<count;++j)whole|=oldModules[j]==digitShiftModules[i];
        if(whole)continue;
        if(!Morph_ShiftConflictingDigits(a,b,remapped,digitShiftModules[i]))
            oldModules[count++]=digitShiftModules[i];
    }
    for(size_t i=0;i<count;++i) {
        uint32_t candidate=1;
        for(;candidate<=65534;++candidate) {
            bool assigned=false;
            for(size_t j=0;j<i;++j)assigned|=newModules[j]==candidate;
            if(!assigned&&!Morph_ModuleUsed(a,b,candidate))break;
        }
        if(candidate>65534)return false;
        newModules[i]=candidate;
    }
    const BodyConnection* noseA=Morph_NasalSupport(a);
    const BodyConnection* noseB=Morph_NasalSupport(b);
    if(noseA && noseB && noseA->fromId==noseB->fromId &&
       noseA->moduleInstanceId!=noseB->moduleInstanceId &&
       !Morph_ModuleUsed(b,b,noseA->moduleInstanceId)) {
        size_t at=0;
        while(at<count && oldModules[at]!=noseB->moduleInstanceId)++at;
        oldModules[at]=noseB->moduleInstanceId;
        newModules[at]=noseA->moduleInstanceId;
        if(at==count)++count;
    }
    for(size_t i=0;i<remapped->nodeCount;++i) {
        AnatomyNode* node=&remapped->nodes[i];
        node->id=Morph_RemapId(node->id,oldModules,newModules,count);
        node->moduleInstanceId=Morph_RemapModule(node->moduleInstanceId,
            oldModules,newModules,count);
    }
    for(size_t i=0;i<remapped->connectionCount;++i) {
        BodyConnection* edge=&remapped->connections[i];
        edge->id=Morph_RemapId(edge->id,oldModules,newModules,count);
        edge->fromId=Morph_RemapId(edge->fromId,oldModules,newModules,count);
        edge->toId=Morph_RemapId(edge->toId,oldModules,newModules,count);
        edge->moduleInstanceId=Morph_RemapModule(edge->moduleInstanceId,
            oldModules,newModules,count);
    }
    if(outOld&&count)memcpy(outOld,oldModules,count*sizeof(*outOld));
    if(outNew&&count)memcpy(outNew,newModules,count*sizeof(*outNew));
    if(outCount)*outCount=count;
    return AnatomyGraph_Validate(remapped);
}
static bool Morph_ModuleTopologyDifferent(const AnatomyGraph* a,const AnatomyGraph* b,
    uint32_t module,CreatureMorphTopologyEvent* event) {
    memset(event,0,sizeof(*event));event->moduleInstanceId=module;
    bool different=false;
    for(size_t i=0;i<a->nodeCount;++i)if(a->nodes[i].moduleInstanceId==module) {
        const AnatomyNode* n=&a->nodes[i];
        const AnatomyNode* peer=AnatomyGraph_FindNode(b,n->id);
        event->nodesAtStart++;
        different|=!peer||peer->role!=n->role||peer->region!=n->region||
            peer->side!=n->side;
    }
    for(size_t i=0;i<b->nodeCount;++i)
        event->nodesAtEnd+=b->nodes[i].moduleInstanceId==module;
    different|=event->nodesAtStart!=event->nodesAtEnd;
    for(size_t i=0;i<a->connectionCount;++i)if(a->connections[i].moduleInstanceId==module) {
        const BodyConnection* edge=&a->connections[i];
        bool found=false;event->connectionsAtStart++;
        for(size_t j=0;j<b->connectionCount;++j) {
            const BodyConnection* peer=&b->connections[j];
            if(edge->id==peer->id&&edge->fromId==peer->fromId&&
               edge->toId==peer->toId&&edge->kind==peer->kind) {
                found=true;break;
            }
        }
        different|=!found;
    }
    for(size_t i=0;i<b->connectionCount;++i)
        event->connectionsAtEnd+=b->connections[i].moduleInstanceId==module;
    different|=event->connectionsAtStart!=event->connectionsAtEnd;
    return different;
}
size_t CreatureMorph_CollectTopologyEvents(const Monster* a,const Monster* b,
    CreatureMorphTopologyEvent* out,size_t capacity) {
    if(!a||!b||!a->hasAnatomyGraph||!b->hasAnatomyGraph||
       !AnatomyGraph_Validate(&a->anatomyGraph)||
       !AnatomyGraph_Validate(&b->anatomyGraph))return SIZE_MAX;
    AnatomyGraph remapped;
    if(!Morph_RemapConflictingModules(&a->anatomyGraph,&b->anatomyGraph,&remapped,NULL,NULL,NULL))
        return SIZE_MAX;
    uint32_t modules[ANATOMY_MAX_NODES*2+ANATOMY_MAX_CONNECTIONS*2];
    size_t count=0;
    const AnatomyGraph* graphs[2]={&a->anatomyGraph,&remapped};
    for(int graphIndex=0;graphIndex<2;++graphIndex) {
        const AnatomyGraph* graph=graphs[graphIndex];
        for(size_t pass=0;pass<2;++pass) {
            size_t length=pass?graph->connectionCount:graph->nodeCount;
            for(size_t i=0;i<length;++i) {
                uint32_t module=pass?graph->connections[i].moduleInstanceId:
                    graph->nodes[i].moduleInstanceId;
                if(!module)continue;
                size_t at=0;
                while(at<count&&modules[at]<module)at++;
                if(at<count&&modules[at]==module)continue;
                memmove(modules+at+1,modules+at,(count-at)*sizeof(*modules));
                modules[at]=module;count++;
            }
        }
    }
    size_t events=0;
    const AnatomyNode* headA=AnatomyGraph_FindFirstRegion(&a->anatomyGraph,ANATOMY_REGION_HEAD);
    const AnatomyNode* headB=AnatomyGraph_FindFirstRegion(&remapped,ANATOMY_REGION_HEAD);
    /* Un único nodo cefálico no describe los cambios de composición del SDF.
     * Una pinna, un pico o un planum nuevos también requieren certificación;
     * no se pueden predecir deformando exclusivamente la malla anterior. */
    bool headFieldChanged=a->hasHead!=b->hasHead;
    if(a->hasHead&&b->hasHead) {
        const HeadSurfaceRecipe* x=&a->head.anatomy.surface;
        const HeadSurfaceRecipe* y=&b->head.anatomy.surface;
        headFieldChanged=a->head.anatomy.archetype!=b->head.anatomy.archetype||
            x->hasNasalPad!=y->hasNasalPad||x->hasEars!=y->hasEars||
            x->isBeak!=y->isBeak||x->hasTympana!=y->hasTympana||
            x->hasCranialProfile!=y->hasCranialProfile||
            x->hasMandibularProfile!=y->hasMandibularProfile;
    }
    for(size_t i=0;i<count;++i) {
        CreatureMorphTopologyEvent event;
        bool changed=Morph_ModuleTopologyDifferent(&a->anatomyGraph,&remapped,modules[i],&event);
        bool headModule=(headA&&headA->moduleInstanceId==modules[i])||
            (headB&&headB->moduleInstanceId==modules[i]);
        if(!changed&&!(headModule&&headFieldChanged))continue;
        if(out&&events<capacity)out[events]=event;
        events++;
    }
    return events;
}
static bool MixGraph(const AnatomyGraph* a,const AnatomyGraph* b,float t,AnatomyGraph* out) {
    AnatomyGraph_Init(out);
    /* La unión suave del SDF depende del orden de sus operandos. En el
     * extremo se conserva el orden del progenitor activo y sus valores. */
    const AnatomyGraph* order[]={t>=1?b:a,t>=1?a:b};
    for(int pass=0;pass<2;++pass) {
        const AnatomyGraph* g=order[pass];
        for(size_t i=0;i<g->nodeCount;++i) {
            const AnatomyNode* n=&g->nodes[i];
            if(AnatomyGraph_FindNode(out,n->id))continue;
            AnatomyNode x=Counterpart(g,a,n),y=Counterpart(g,b,n),v=*n;
            if(x.role!=y.role || x.region!=y.region || x.side!=y.side)return false;
            if(t<=0) v=x;
            else if(t>=1) v=y;
            else {
                v.center=Vec3_Lerp(x.center,y.center,t);
                v.envelopeRadii=Vec3_Lerp(x.envelopeRadii,y.envelopeRadii,t);
                v.widthRadius=Math_Lerp(x.widthRadius,y.widthRadius,t);
                v.heightRadius=Math_Lerp(x.heightRadius,y.heightRadius,t);
                v.development=Math_Lerp(x.development,y.development,t);
            }
            if(!AnatomyGraph_AddNode(out,v))return false;
        }

    }
    for(int pass=0;pass<2;++pass) {
        const AnatomyGraph* g=order[pass];
        for(size_t i=0;i<g->connectionCount;++i) {
            const BodyConnection* e=&g->connections[i];
            if(EdgeSameEnds(out,e))continue;
            const BodyConnection* x=Edge(a,e),*y=Edge(b,e);
            const BodyConnection* xe=EdgeSameEnds(a,e),*ye=EdgeSameEnds(b,e);
            bool supA=xe&&Morph_IsNonOrnamentSupport(a,xe);
            bool supB=ye&&Morph_IsNonOrnamentSupport(b,ye);
            bool supOverride=(xe&&ye&&xe->kind!=ye->kind&&(supA!=supB));
            if(supOverride){x=xe;y=ye;}
            BodyConnection v=*e;
            if(supOverride) {
                const BodyConnection* active=supA?y:x;
                v.id=active->id;
                v.kind=active->kind;
                v.moduleInstanceId=active->moduleInstanceId;
            }
            v.jointBlend=Math_Lerp(x?x->jointBlend:0,y?y->jointBlend:0,t);
            v.widthBulge=Math_Lerp(x&&!supA?x->widthBulge:0,y&&!supB?y->widthBulge:0,t);
            v.heightBulge=Math_Lerp(x&&!supA?x->heightBulge:0,y&&!supB?y->heightBulge:0,t);
            v.crownInset=Math_Lerp(x?x->crownInset:0,y?y->crownInset:0,t);
            v.toeRounding=Math_Lerp(x?x->toeRounding:0,y?y->toeRounding:0,t);
            v.transverseAxis=Vec3_Lerp(x?x->transverseAxis:e->transverseAxis,y?y->transverseAxis:e->transverseAxis,t);
            v.development=Math_Lerp(x?x->development:0,y?y->development:0,t);
            if(!AnatomyGraph_Connect(out,v))return false;
            bool dormantA=!x || a->dormantConnections[x-a->connections] || (supOverride&&supA);
            bool dormantB=!y || b->dormantConnections[y-b->connections] || (supOverride&&supB);
            out->dormantConnections[out->connectionCount-1]=(dormantA && dormantB)||(t<=0 && dormantA)||(t>=1 && dormantB);
        }
    }
    if(t<=0 || t>=1)return AnatomyGraph_Validate(out);
    /* Los dedos cambian de dirección mediante rotación, sin atravesar longitud cero. */
    for(size_t i=0;i<out->nodeCount;++i) {
        AnatomyNode* n=&out->nodes[i];
        if(n->role!=ANATOMY_ROLE_DIGIT)continue;
        const BodyConnection* edge=Incoming(out,n->id);
        if(!edge)continue;
        const AnatomyNode* xa=AnatomyGraph_FindNode(a,edge->fromId),*xb=AnatomyGraph_FindNode(b,edge->fromId);
        const AnatomyNode* ya=AnatomyGraph_FindNode(a,n->id),*yb=AnatomyGraph_FindNode(b,n->id);
        const AnatomyNode* parent=AnatomyGraph_FindNode(out,edge->fromId);
        if(!xa || !xb || !ya || !yb || !parent)continue;
        Vector3 va=Vec3_Sub(ya->center,xa->center),vb=Vec3_Sub(yb->center,xb->center);
        float la=Vec3_Length(va),lb=Vec3_Length(vb);
        if(la<1e-6f || lb<1e-6f)continue;
        Quaternion q=Quat_Slerp(Quat_Identity(),Quat_FromTo(va,vb),t);
        n->center=Vec3_Add(parent->center,Vec3_Scale(Quat_RotateVector(q,va),Math_Lerp(la,lb,t)/la));
    }
    return AnatomyGraph_Validate(out);
}

static bool Morph_SeparateTail(const Monster* m,uint32_t module) {
    if(!m||!m->hasCreaturePhenotype)return false;
    for(size_t i=0;i<m->recipe.bodyPlan.moduleCount;++i) {
        const CreatureModuleInstance* inst=&m->recipe.bodyPlan.modules[i];
        if(inst->instanceId==module&&inst->kind==CREATURE_MODULE_TAIL&&
           inst->phenotypeIndex<m->phenotype.tailCount)
            return Tail_UsesIndependentCurve(&m->phenotype.tails[inst->phenotypeIndex]);
    }
    return false;
}

static bool Morph_HasIndependentTail(const Monster* m) {
    if(m->anatomyGraph.separateMorphTail)return true;
    for(size_t i=0;i<m->phenotype.tailCount;++i)
        if(Tail_UsesIndependentCurve(&m->phenotype.tails[i]))return true;
    return false;
}

/* Retira del barrido los nodos de una cola longitudinal. Conserva la grupa
 * de los perfiles que ya separan su cola aunque termine detrás de la pelvis. */
static int Morph_TrunkStations(const Monster* m,const SDFSweepStation* profile,int count) {
    const AnatomyNode* tail=AnatomyGraph_FindFirstRegion(&m->anatomyGraph,ANATOMY_REGION_TAIL);
    const AnatomyNode* pelvis=AnatomyGraph_FindFirstRegion(&m->anatomyGraph,ANATOMY_REGION_PELVIS);
    if(!tail||!pelvis||Morph_SeparateTail(m,tail->moduleInstanceId)||
       m->anatomyGraph.separateMorphTail)return count;
    for(int i=2;i<count;++i)
        if(Vec3_Distance(profile[i].center,tail->center)<1e-5f)return i;
    while(count>2 && profile[count-1].center.z<pelvis->center.z-1e-5f)--count;
    return count;
}

/* Resolver el mismo perfil axial que consume el SDF, antes de la interpolación. */
static int Profile(const Monster* m,SDFSweepStation* stations) {
    const AnatomyGraph* g=&m->anatomyGraph;
    if(g->axialProfileCount) {
        memcpy(stations,g->axialProfile,g->axialProfileCount*sizeof(*stations));
        return (int)g->axialProfileCount;
    }
    const AnatomyNode* start=NULL;
    for(size_t i=0;i<g->nodeCount;++i) {
        const AnatomyNode* n=&g->nodes[i];
        if(n->role!=ANATOMY_ROLE_AXIAL || n->region==ANATOMY_REGION_HEAD || n->region==ANATOMY_REGION_ORNAMENT)continue;
        bool incoming=false;
        for(size_t j=0;j<g->connectionCount;++j) {
            const BodyConnection* e=&g->connections[j];
            const AnatomyNode* from=AnatomyGraph_FindNode(g,e->fromId);
            if(e->toId==n->id && e->kind==BODY_CONNECTION_AXIAL_LOFT && from &&
               from->region!=ANATOMY_REGION_HEAD && from->region!=ANATOMY_REGION_ORNAMENT)incoming=true;
        }
        if(!incoming){if(start)return 0;start=n;}
    }
    if(!start)return 0;
    int count=0;
    const AnatomyNode* n=start;
    if(m->phenotype.axial.archetype==AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD) {
        if(!AxialBodyUprightTetrapod_BuildSweepStations(&m->phenotype.axial,g,start->moduleInstanceId,stations,ANATOMY_MAX_SWEEP_STATIONS,&count))return 0;
        n=AnatomyGraph_FindModuleNode(g,start->moduleInstanceId,AXIAL_NODE_PELVIS);
    } else stations[count++]=(SDFSweepStation){.center=n->center,.width=n->widthRadius,.height=n->heightRadius};
    for(size_t step=0;n && step<g->nodeCount;++step) {
        const AnatomyNode* next=NULL;
        for(size_t i=0;i<g->connectionCount;++i) {
            const BodyConnection* e=&g->connections[i];
            if(e->fromId!=n->id || e->kind!=BODY_CONNECTION_AXIAL_LOFT)continue;
            const AnatomyNode* candidate=AnatomyGraph_FindNode(g,e->toId);
            if(candidate && candidate->region!=ANATOMY_REGION_HEAD) {
                /* Una cola independiente abandona el loft axial en la pelvis, igual que MonsterSDF_Build. */
                if(candidate->region==ANATOMY_REGION_TAIL && Morph_SeparateTail(m,candidate->moduleInstanceId))continue;
                if(next)return 0;
                next=candidate;
            }
        }
        n=next;
        if(n){if(count==ANATOMY_MAX_SWEEP_STATIONS)return 0;stations[count++]=(SDFSweepStation){.center=n->center,.width=n->widthRadius,.height=n->heightRadius};}
    }
    return SDF_SweepResolveTangents(stations,count)?count:0;
}
static float Hermite(float a,float b,float da,float db,float u,float dz,float* slope) {
    float u2=u*u,u3=u2*u;
    *slope=((6*u2-6*u)*a+(-6*u2+6*u)*b)/dz+(3*u2-4*u+1)*da+(3*u2-2*u)*db;
    return (2*u3-3*u2+1)*a+(-2*u3+3*u2)*b+(u3-2*u2+u)*dz*da+(u3-u2)*dz*db;
}
static float ProfileCoordinate(float z,const SDFSweepStation* p,int count,float pelvis) {
    if(z>=pelvis)return (z-p[0].center.z)/(pelvis-p[0].center.z);
    return 1+(z-pelvis)/(p[count-1].center.z-pelvis);
}
static float ProfileZ(float u,const SDFSweepStation* p,int count,float pelvis) {
    return u<=1?Math_Lerp(p[0].center.z,pelvis,u):Math_Lerp(pelvis,p[count-1].center.z,u-1);
}
static SDFSweepStation SampleProfile(const SDFSweepStation* p,int count,float z) {
    int i=0;while(i<count-2 && z<p[i+1].center.z)++i;
    const SDFSweepStation* a=&p[i],*b=&p[i+1];
    float dz=b->center.z-a->center.z,t=(z-a->center.z)/dz;
    SDFSweepStation out={.center={Math_Lerp(a->center.x,b->center.x,t),0,z}};
    out.center.y=Hermite(a->center.y,b->center.y,a->centerSlope,b->centerSlope,t,dz,&out.centerSlope);
    out.width=Hermite(a->width,b->width,a->widthSlope,b->widthSlope,t,dz,&out.widthSlope);
    out.height=Hermite(a->height,b->height,a->heightSlope,b->heightSlope,t,dz,&out.heightSlope);
    return out;
}
static bool MixProfile(const Monster* a,const Monster* b,float t,AnatomyGraph* g) {
    SDFSweepStation x[ANATOMY_MAX_SWEEP_STATIONS],y[ANATOMY_MAX_SWEEP_STATIONS];
    int nx=Profile(a,x),ny=Profile(b,y);
    if(nx<2 || ny<2)return true; /* Las anatomías ramificadas conservan sus conexiones. */
    if(t<=0.0f) {
        memcpy(g->axialProfile,x,(size_t)nx*sizeof(g->axialProfile[0]));
        g->axialProfileCount=(size_t)nx;
        g->separateMorphTail=a->anatomyGraph.separateMorphTail;
        return true;
    }
    if(t>=1.0f) {
        memcpy(g->axialProfile,y,(size_t)ny*sizeof(g->axialProfile[0]));
        g->axialProfileCount=(size_t)ny;
        g->separateMorphTail=b->anatomyGraph.separateMorphTail;
        return true;
    }
    /* Si una cola usa una curva propia, ambos extremos interpolan la cola
     * por sus tramos del grafo. El diámetro de la pelvis no se propaga por
     * los cortes caudales del extremo que sí forma parte del barrido axial. */
    if(Morph_HasIndependentTail(a)||Morph_HasIndependentTail(b)) {
        int tx=Morph_TrunkStations(a,x,nx),ty=Morph_TrunkStations(b,y,ny);
        if(tx!=nx&&!SDF_SweepResolveTangents(x,tx))return false;
        if(ty!=ny&&!SDF_SweepResolveTangents(y,ty))return false;
        g->separateMorphTail=true;
        nx=tx;ny=ty;
    }
    const AnatomyNode* pa=AnatomyGraph_FindFirstRegion(&a->anatomyGraph,ANATOMY_REGION_PELVIS);
    const AnatomyNode* pb=AnatomyGraph_FindFirstRegion(&b->anatomyGraph,ANATOMY_REGION_PELVIS);
    float pelvisX=pa?pa->center.z:x[nx-1].center.z;
    float pelvisY=pb?pb->center.z:y[ny-1].center.z;
    bool hasTailX=pelvisX<x[0].center.z-1e-5f && pelvisX>x[nx-1].center.z+1e-5f;
    bool hasTailY=pelvisY<y[0].center.z-1e-5f && pelvisY>y[ny-1].center.z+1e-5f;
    if(!hasTailX)pelvisX=x[nx-1].center.z;
    if(!hasTailY)pelvisY=y[ny-1].center.z;
    if(pelvisX>=x[0].center.z-1e-5f)pelvisX=(x[0].center.z+x[nx-1].center.z)*.5f;
    if(pelvisY>=y[0].center.z-1e-5f)pelvisY=(y[0].center.z+y[ny-1].center.z)*.5f;
    float knots[ANATOMY_MAX_SWEEP_STATIONS];int count=0;
    for(int pass=0;pass<2;++pass) {
        const SDFSweepStation* p=pass?y:x;int n=pass?ny:nx;
        for(int i=0;i<n;++i) {
            float u=ProfileCoordinate(p[i].center.z,p,n,pass?pelvisY:pelvisX);
            if(u>1.00001f && !((hasTailX && t<1.0f)||(hasTailY && t>0.0f)))continue;
            int j=0;while(j<count && knots[j]<u-1e-6f)++j;
            if(j<count && fabsf(knots[j]-u)<1e-6f)continue;
            if(count==ANATOMY_MAX_SWEEP_STATIONS)return false;
            memmove(knots+j+1,knots+j,(count-j)*sizeof(*knots));knots[j]=u;++count;
        }
    }
    int outCount=0;
    for(int i=0;i<count;++i) {
        bool tail=knots[i]>1.00001f;
        float px=tail && !hasTailX?pelvisX:ProfileZ(knots[i],x,nx,pelvisX);
        float py=tail && !hasTailY?pelvisY:ProfileZ(knots[i],y,ny,pelvisY);
        SDFSweepStation p=SampleProfile(x,nx,px),q=SampleProfile(y,ny,py);
        float dx=tail?(hasTailX?x[nx-1].center.z-pelvisX:0.0f):pelvisX-x[0].center.z;
        float dy=tail?(hasTailY?y[ny-1].center.z-pelvisY:0.0f):pelvisY-y[0].center.z;
        float dz=Math_Lerp(dx,dy,t);
        if(fabsf(dz)<1e-6f)continue;
        g->axialProfile[outCount++]=(SDFSweepStation){.center=Vec3_Lerp(p.center,q.center,t),
            .width=Math_Lerp(p.width,q.width,t),.height=Math_Lerp(p.height,q.height,t),
            .widthSlope=Math_Lerp(p.widthSlope*dx,q.widthSlope*dy,t)/dz,
            .heightSlope=Math_Lerp(p.heightSlope*dx,q.heightSlope*dy,t)/dz,
            .centerSlope=Math_Lerp(p.centerSlope*dx,q.centerSlope*dy,t)/dz};
    }
    g->axialProfileCount=(size_t)outCount;
    return true;
}

static bool Morph_GraphUsesModule(const AnatomyGraph* g,uint32_t module) {
    if(!module)return false;
    for(size_t i=0;i<g->nodeCount;++i)if(g->nodes[i].moduleInstanceId==module)return true;
    for(size_t i=0;i<g->connectionCount;++i)if(g->connections[i].moduleInstanceId==module)return true;
    return false;
}
static const CreatureModuleInstance* Morph_FindBodyModule(const BodyPlan* plan,uint32_t id) {
    for(size_t i=0;i<plan->moduleCount;++i)if(plan->modules[i].instanceId==id)return &plan->modules[i];
    return NULL;
}
static void Morph_AppendModule(CreatureRecipe* recipe,CreaturePhenotype* phen,
    const CreatureModuleInstance* srcMod,const CreaturePhenotype* srcPhen,uint32_t instanceId) {
    if(recipe->bodyPlan.moduleCount>=CREATURE_MAX_MODULES)return;
    CreatureModuleInstance dstMod=*srcMod;
    dstMod.instanceId=instanceId;
    if(srcMod->kind==CREATURE_MODULE_LIMB) {
        if(srcMod->phenotypeIndex>=srcPhen->limbCount||phen->limbCount>=CREATURE_MAX_LIMBS)return;
        dstMod.phenotypeIndex=(unsigned)phen->limbCount;
        phen->limbs[phen->limbCount++]=srcPhen->limbs[srcMod->phenotypeIndex];
    } else if(srcMod->kind==CREATURE_MODULE_TAIL) {
        if(srcMod->phenotypeIndex>=srcPhen->tailCount||phen->tailCount>=CREATURE_MAX_TAILS)return;
        dstMod.phenotypeIndex=(unsigned)phen->tailCount;
        phen->tails[phen->tailCount++]=srcPhen->tails[srcMod->phenotypeIndex];
    } else if(srcMod->kind==CREATURE_MODULE_ORNAMENT) {
        if(srcMod->phenotypeIndex>=srcPhen->ornamentCount||phen->ornamentCount>=CREATURE_MAX_ORNAMENTS)return;
        dstMod.phenotypeIndex=(unsigned)phen->ornamentCount;
        phen->ornaments[phen->ornamentCount++]=srcPhen->ornaments[srcMod->phenotypeIndex];
    } else if(srcMod->kind==CREATURE_MODULE_FLEXIBLE_APPENDAGE) {
        if(srcMod->phenotypeIndex>=srcPhen->flexibleAppendageCount||
           phen->flexibleAppendageCount>=CREATURE_MAX_FLEXIBLE_APPENDAGES)return;
        dstMod.phenotypeIndex=(unsigned)phen->flexibleAppendageCount;
        phen->flexibleAppendages[phen->flexibleAppendageCount++]=srcPhen->flexibleAppendages[srcMod->phenotypeIndex];
    }
    recipe->bodyPlan.modules[recipe->bodyPlan.moduleCount++]=dstMod;
}
static void Morph_SyncModules(const Monster* a,const Monster* b,float t,float u,
    const uint32_t* oldModules,const uint32_t* newModules,size_t remapCount,Monster* out) {
    if(!a->hasCreaturePhenotype||!b->hasCreaturePhenotype)return;
    out->recipe.bodyPlan.moduleCount=0;
    out->phenotype.limbCount=0;
    out->phenotype.tailCount=0;
    out->phenotype.ornamentCount=0;
    out->phenotype.flexibleAppendageCount=0;
    for(size_t i=0;i<a->recipe.bodyPlan.moduleCount;++i) {
        const CreatureModuleInstance* ma=&a->recipe.bodyPlan.modules[i];
        if(ma->kind>CREATURE_MODULE_HEAD && !Morph_GraphUsesModule(&out->anatomyGraph,ma->instanceId))continue;
        const CreatureModuleInstance* mb=Morph_FindBodyModule(&b->recipe.bodyPlan,ma->instanceId);
        for(size_t id=0;id<remapCount;++id) {
            if(newModules[id]!=ma->instanceId)continue;
            mb=Morph_FindBodyModule(&b->recipe.bodyPlan,oldModules[id]);
            break;
        }
        bool shared=mb && mb->kind==ma->kind &&
            Morph_RemapModule(mb->instanceId,oldModules,newModules,remapCount)==ma->instanceId;
        if(shared && ma->kind==CREATURE_MODULE_FLEXIBLE_APPENDAGE &&
           ma->phenotypeIndex<a->phenotype.flexibleAppendageCount &&
           mb->phenotypeIndex<b->phenotype.flexibleAppendageCount) {
            size_t at=out->phenotype.flexibleAppendageCount;
            Morph_AppendModule(&out->recipe,&out->phenotype,ma,&a->phenotype,ma->instanceId);
            if(at<out->phenotype.flexibleAppendageCount)
                out->phenotype.flexibleAppendages[at]=FlexibleAppendage_Interpolate(
                    &a->phenotype.flexibleAppendages[ma->phenotypeIndex],
                    &b->phenotype.flexibleAppendages[mb->phenotypeIndex],u,u);
        } else if(shared && ma->kind==CREATURE_MODULE_TAIL &&
           ma->phenotypeIndex<a->phenotype.tailCount && mb->phenotypeIndex<b->phenotype.tailCount) {
            Morph_AppendModule(&out->recipe,&out->phenotype,t>=0.5f?mb:ma,
                t>=0.5f?&b->phenotype:&a->phenotype,ma->instanceId);
            if(out->phenotype.tailCount>0) {
                const TailPhenotype* ta=&a->phenotype.tails[ma->phenotypeIndex];
                const TailPhenotype* tb=&b->phenotype.tails[mb->phenotypeIndex];
                TailPhenotype* dt=&out->phenotype.tails[out->phenotype.tailCount-1];
                dt->droop=Math_Lerp(ta->droop,tb->droop,u);
                dt->rootMatchStrength=Math_Lerp(ta->rootMatchStrength,tb->rootMatchStrength,u);
            }
        } else if(shared && t>=0.5f) {
            Morph_AppendModule(&out->recipe,&out->phenotype,mb,&b->phenotype,ma->instanceId);
        } else {
            Morph_AppendModule(&out->recipe,&out->phenotype,ma,&a->phenotype,ma->instanceId);
        }
    }
    for(size_t i=0;i<b->recipe.bodyPlan.moduleCount;++i) {
        const CreatureModuleInstance* mb=&b->recipe.bodyPlan.modules[i];
        uint32_t effId=Morph_RemapModule(mb->instanceId,oldModules,newModules,remapCount);
        if(Morph_FindBodyModule(&out->recipe.bodyPlan,effId))continue;
        if(mb->kind>CREATURE_MODULE_HEAD && !Morph_GraphUsesModule(&out->anatomyGraph,effId))continue;
        Morph_AppendModule(&out->recipe,&out->phenotype,mb,&b->phenotype,effId);
    }
}

/* El grafo unión conserva los IDs para rig y seguimiento de superficie.
 * Las estaciones nasales se reconstruyen desde la sección del rostro mezclado. */
static bool Morph_ResolveNasalExtension(Monster* out, float weight) {
    if(weight<=0 || weight>=1 || !out->hasHead)return true;
    AnatomyGraph* graph=&out->anatomyGraph;
    const HeadSurfaceRecipe* surface=&out->head.anatomy.surface;
    for(size_t i=0;i<out->recipe.bodyPlan.moduleCount;++i) {
        const CreatureModuleInstance* m=&out->recipe.bodyPlan.modules[i];
        if(m->kind!=CREATURE_MODULE_FLEXIBLE_APPENDAGE || m->pathAttached ||
           !HeadModule_IsNasalExtension(m->attachment.slotId))continue;
        const AnatomyNode* root=AnatomyGraph_FindModuleNode(graph,m->instanceId,1);
        const AnatomyNode* second=AnatomyGraph_FindModuleNode(graph,m->instanceId,2);
        const BodyConnection* support=root?Incoming(graph,root->id):NULL;
        const AnatomyNode* head=support?AnatomyGraph_FindNode(graph,support->fromId):NULL;
        if(!head || !second || m->phenotypeIndex>=out->phenotype.flexibleAppendageCount)continue;
        float length=(FLEXIBLE_APPENDAGE_STATIONS-1)*Vec3_Distance(root->center,second->center);
        if(length<=1e-6f)continue;
        float scale=out->phenotype.axial.totalScale;
        FlexibleAppendagePhenotype* phenotype=&out->phenotype.flexibleAppendages[m->phenotypeIndex];
        phenotype->length=length/scale;
        AttachmentSlot slot={.id=m->attachment.slotId,.hostNode=head->id,.scale=scale,
            .hostRadii=HeadModule_FacialSupport(surface,HEAD_SLOT_FITTED_PROBOSCIS),
            .position=Vec3_Add(head->center,HeadModule_NasalRoot(surface)),
            .side={1,0,0},.up={0,1,0},.forward={0,0,1}};
        AnatomyGraph nasal;
        AnatomyGraph_Init(&nasal);
        if(!AnatomyGraph_AddNode(&nasal,*head) ||
           !FlexibleAppendage_Resolve(phenotype,m->instanceId,&slot,&nasal))return false;
        for(size_t j=0;j<graph->nodeCount;++j) {
            AnatomyNode* node=&graph->nodes[j];
            if(node->moduleInstanceId!=m->instanceId)continue;
            const AnatomyNode* station=AnatomyGraph_FindNode(&nasal,node->id);
            if(!station)continue;
            node->center=station->center;
            node->widthRadius=station->widthRadius;
            node->heightRadius=station->heightRadius;
        }
    }
    return true;
}

float CreatureMorph_GeometricWeight(float age) {
    float t=isfinite(age)?Math_Clamp01(age):0;
    return t*t*(3-2*t);
}

bool CreatureMorph_Interpolate(const Monster* a,const Monster* b,float age,Monster* out) {
    if(!a || !b || !out || !a->hasAnatomyGraph || !b->hasAnatomyGraph || out==a || out==b)return false;
    float t=isfinite(age)?Math_Clamp01(age):0;
    float u=CreatureMorph_GeometricWeight(t);
    if(!Monster_CopyInto(out,t<.5f?a:b))return false;
    AnatomyGraph remapped;
    uint32_t oldModules[ANATOMY_MAX_NODES+ANATOMY_MAX_CONNECTIONS];
    uint32_t newModules[ANATOMY_MAX_NODES+ANATOMY_MAX_CONNECTIONS];
    size_t remapCount=0;
    if(!Morph_RemapConflictingModules(&a->anatomyGraph,&b->anatomyGraph,&remapped,
            oldModules,newModules,&remapCount)||
       !MixGraph(&a->anatomyGraph,&remapped,u,&out->anatomyGraph) ||
       !MixProfile(a,b,u,&out->anatomyGraph))return false;
    if(!AnatomyGraph_Validate(&out->anatomyGraph))return false;
    Morph_SyncModules(a,b,t,u,oldModules,newModules,remapCount,out);
    out->hasResolvedMorph=true;
    out->hasGrowthAge=true;out->growthAge=t;
    out->phenotype.axial.totalScale=Math_Lerp(a->phenotype.axial.totalScale,b->phenotype.axial.totalScale,u);
    out->surface=SurfacePhenotype_Interpolate(&a->surface,&b->surface,u);
    /* Tipos discretos de cobertura cambian cuando su contribución es nula. */
    if(a->surface.integument.type!=b->surface.integument.type) {
        if(t<.45f) {
            out->surface.integument=a->surface.integument;
            float k=Math_Clamp01(t/.45f);float blend=1.0f-k*k*(3.0f-2.0f*k);
            out->surface.integument.coverage*=blend;
            out->surface.integument.scales.relief*=blend;
            out->surface.integument.plates.relief*=blend;
            out->surface.integument.fur.length=Math_Lerp(0.002f,a->surface.integument.fur.length,blend);
            out->surface.integument.fur.density=Math_Lerp(0.05f,a->surface.integument.fur.density,blend);
            out->surface.integument.fur.seed=a->surface.integument.fur.seed;
            out->surface.integument.scales.seed=a->surface.integument.scales.seed;
        } else if(t>.55f) {
            out->surface.integument=b->surface.integument;
            float k=Math_Clamp01((t-.55f)/.45f);float blend=k*k*(3.0f-2.0f*k);
            out->surface.integument.coverage*=blend;
            out->surface.integument.scales.relief*=blend;
            out->surface.integument.plates.relief*=blend;
            out->surface.integument.fur.length=Math_Lerp(0.002f,b->surface.integument.fur.length,blend);
            out->surface.integument.fur.density=Math_Lerp(0.05f,b->surface.integument.fur.density,blend);
            out->surface.integument.fur.seed=b->surface.integument.fur.seed;
            out->surface.integument.scales.seed=b->surface.integument.scales.seed;
        } else {
            out->surface.integument.type=INTEGUMENT_SMOOTH_SKIN;
            out->surface.integument.coverage=0;
            out->surface.integument.scales.relief=0;
            out->surface.integument.plates.relief=0;
        }
    }
    out->phenotype.surface=out->surface;
    /* Los descriptores locales permanecen en fenotipo; el campo de render se
     * resuelve contra la anatomía interpolada, no contra un extremo obsoleto. */
    SurfacePhenotype local=SurfacePhenotype_Interpolate(&a->phenotype.surface,&b->phenotype.surface,u);
    SurfacePhenotype_CopyAnchoredDomains(&out->phenotype.surface,&local);
    SurfacePhenotype_CopyAnchoredDomains(&out->surface,&local);
    size_t colors=a->colorPalette.count<b->colorPalette.count?a->colorPalette.count:b->colorPalette.count;
    for(size_t i=0;i<colors;++i)out->colorPalette.colors[i]=Color_Lerp(a->colorPalette.colors[i],b->colorPalette.colors[i],u);
    if(a->hasHead && b->hasHead) {
        HeadAnatomy_Blend(&a->head.anatomy,&b->head.anatomy,u,&out->head.anatomy);
        const AnatomyNode* n=AnatomyGraph_FindFirstRegion(&out->anatomyGraph,ANATOMY_REGION_HEAD);
        if(n) {
            BodyPart* host=&out->bodyParts[out->head.anatomy.attachmentBodyPartIndex];
            host->position=host->positionRender=host->oldPosition=n->center;
            host->width=host->widthRender=2*n->envelopeRadii.x;
            host->height=host->heightRender=2*n->envelopeRadii.y;
            host->length=host->lengthRender=2*n->envelopeRadii.z;
        }
        if(out->mouthCount)out->mouths[0]=out->head.anatomy.oralSystem;
    }
    if(!Morph_ResolveNasalExtension(out,u))return false;
    out->surfaceMapping=SurfaceMapping_FromAnatomy(&out->anatomyGraph,out->phenotype.axial.totalScale);
    SurfaceMapper_ResolvePigmentDomains(&out->surface,&out->anatomyGraph,&out->surfaceMapping);
    size_t eyes=a->eyeCount<b->eyeCount?a->eyeCount:b->eyeCount;
    for(size_t i=0;i<eyes;++i) {
        Eye* e=&out->eyes[i];const Eye* x=&a->eyes[i],*y=&b->eyes[i];
        e->offset=Vec3_Lerp(x->offset,y->offset,u);e->scale=Vec3_Lerp(x->scale,y->scale,u);
        e->forward=Vec3_Normalize(Vec3_Lerp(x->forward,y->forward,u));
        e->rotation=Vec3_Create(-asinf(Math_Clamp(e->forward.y,-1,1))*57.2957795f,atan2f(e->forward.x,e->forward.z)*57.2957795f,0);
        e->scleraColor=Color_Lerp(x->scleraColor,y->scleraColor,u);e->irisColor=Color_Lerp(x->irisColor,y->irisColor,u);e->pupilColor=Color_Lerp(x->pupilColor,y->pupilColor,u);
        e->irisScale=Math_Lerp(x->irisScale,y->irisScale,u);e->pupilScale=Math_Lerp(x->pupilScale,y->pupilScale,u);e->pupilAspect=Math_Lerp(x->pupilAspect,y->pupilAspect,u);
    }
    out->angle=Math_Lerp(a->angle,b->angle,u);out->updateSpeed=Math_Lerp(a->updateSpeed,b->updateSpeed,u);
    return true;
}
