#include "Anatomy.h"
#include <math.h>
#include <string.h>

void AnatomyGraph_Init(AnatomyGraph* graph) {
    if (graph) memset(graph, 0, sizeof(*graph));
}

const AnatomyNode* AnatomyGraph_FindNode(const AnatomyGraph* graph, AnatomyId id) {
    if (!graph || id == 0) return NULL;
    for (size_t i = 0; i < graph->nodeCount; ++i)
        if (graph->nodes[i].id == id) return &graph->nodes[i];
    return NULL;
}

bool AnatomyGraph_AddNode(AnatomyGraph* graph, AnatomyNode node) {
    if (!graph || node.id == 0 || graph->nodeCount >= ANATOMY_MAX_NODES ||
        AnatomyGraph_FindNode(graph, node.id) || !isfinite(node.center.x) ||
        !isfinite(node.center.y) || !isfinite(node.center.z) ||
        !isfinite(node.widthRadius) || !isfinite(node.heightRadius) ||
        node.widthRadius <= 0.0f || node.heightRadius <= 0.0f) return false;
    if(node.moduleInstanceId&&(node.id!=Anatomy_MakeId(node.moduleInstanceId,node.localNodeId)||
        !isfinite(node.development)||node.development<0||node.development>1))return false;
    graph->nodes[graph->nodeCount++] = node;
    return true;
}

bool AnatomyGraph_Connect(AnatomyGraph* graph, BodyConnection connection) {
    if (!graph || connection.id == 0 || connection.fromId == connection.toId ||
        graph->connectionCount >= ANATOMY_MAX_CONNECTIONS ||
        !AnatomyGraph_FindNode(graph, connection.fromId) ||
        !AnatomyGraph_FindNode(graph, connection.toId)) return false;
    for (size_t i = 0; i < graph->connectionCount; ++i) {
        const BodyConnection* edge = &graph->connections[i];
        if (edge->id == connection.id ||
            (edge->fromId == connection.fromId && edge->toId == connection.toId)) return false;
    }
    graph->connections[graph->connectionCount++] = connection;
    return true;
}

bool AnatomyGraph_HasConnection(const AnatomyGraph* graph, AnatomyId fromId, AnatomyId toId) {
    if (!graph) return false;
    for (size_t i = 0; i < graph->connectionCount; ++i) {
        const BodyConnection* edge = &graph->connections[i];
        if (edge->fromId == fromId && edge->toId == toId) return true;
    }
    return false;
}

bool AnatomyGraph_Validate(const AnatomyGraph* graph) {
    if (!graph || graph->nodeCount == 0 || graph->connectionCount == 0 ||
        graph->nodeCount>ANATOMY_MAX_NODES || graph->connectionCount>ANATOMY_MAX_CONNECTIONS) return false;
    for(size_t i=0;i<graph->nodeCount;++i) {
        const AnatomyNode* n=&graph->nodes[i];
        if(!n->id||!isfinite(n->center.x)||!isfinite(n->center.y)||!isfinite(n->center.z)||
           !isfinite(n->widthRadius)||!isfinite(n->heightRadius)||n->widthRadius<=0||n->heightRadius<=0)return false;
        if(n->moduleInstanceId&&(n->id!=Anatomy_MakeId(n->moduleInstanceId,n->localNodeId)||
            !isfinite(n->development)||n->development<0||n->development>1))return false;
        for(size_t j=0;j<i;++j)if(graph->nodes[j].id==n->id)return false;
    }
    for (size_t i = 0; i < graph->connectionCount; ++i) {
        const BodyConnection* edge = &graph->connections[i];
        if (!isfinite(edge->widthBulge) || !isfinite(edge->heightBulge) ||
            !isfinite(edge->transverseAxis.x) || !isfinite(edge->transverseAxis.y) ||
            !isfinite(edge->transverseAxis.z)) return false;
        if (!AnatomyGraph_FindNode(graph, edge->fromId) ||
            !AnatomyGraph_FindNode(graph, edge->toId)) return false;
    }
    return true;
}

AnatomyId Anatomy_MakeId(uint32_t module,uint16_t local) {
    if(!module||module>65534||!local)return 0;
    return (module<<16)|local;
}
const AnatomyNode* AnatomyGraph_FindModuleNode(const AnatomyGraph* g,uint32_t module,uint16_t local) {
    return AnatomyGraph_FindNode(g,Anatomy_MakeId(module,local));
}
const AnatomyNode* AnatomyGraph_FindFirstRegion(const AnatomyGraph* g,AnatomyRegion region) {
    if(g)for(size_t i=0;i<g->nodeCount;++i)if(g->nodes[i].region==region)return &g->nodes[i];
    return NULL;
}
bool AnatomyGraph_TopologyCompatible(const AnatomyGraph* a,const AnatomyGraph* b) {
    if(!a||!b||a->nodeCount!=b->nodeCount||a->connectionCount!=b->connectionCount||
        a->nodeCount>ANATOMY_MAX_NODES||a->connectionCount>ANATOMY_MAX_CONNECTIONS)return false;
    for(size_t i=0;i<a->nodeCount;++i) {
        const AnatomyNode* x=&a->nodes[i]; const AnatomyNode* y=AnatomyGraph_FindNode(b,x->id);
        if(!y||x->role!=y->role||x->region!=y->region||x->side!=y->side||
           x->moduleInstanceId!=y->moduleInstanceId||x->localNodeId!=y->localNodeId)return false;
    }
    for(size_t i=0;i<a->connectionCount;++i) {
        const BodyConnection* x=&a->connections[i]; bool found=false;
        for(size_t j=0;j<b->connectionCount;++j) {
            const BodyConnection* y=&b->connections[j];
            if(x->id==y->id&&x->fromId==y->fromId&&x->toId==y->toId&&x->kind==y->kind&&
               x->moduleInstanceId==y->moduleInstanceId){found=true;break;}
        }
        if(!found)return false;
    }
    return true;
}

static uint64_t Anatomy_HashBytes(const void* data,size_t count,uint64_t hash) {
    const unsigned char* bytes=data;
    for(size_t i=0;i<count;++i){hash^=bytes[i];hash*=0x100000001b3ULL;}
    return hash;
}

uint64_t AnatomyGraph_Fingerprint(const AnatomyGraph* g) {
    if(!g||g->nodeCount>ANATOMY_MAX_NODES||g->connectionCount>ANATOMY_MAX_CONNECTIONS)return 0;
    uint64_t hash=0xcbf29ce484222325ULL;
    hash=Anatomy_HashBytes(&g->nodeCount,sizeof(g->nodeCount),hash);
    hash=Anatomy_HashBytes(&g->connectionCount,sizeof(g->connectionCount),hash);
    for(size_t i=0;i<g->nodeCount;++i) {
        const AnatomyNode* n=&g->nodes[i];uint64_t h=0xcbf29ce484222325ULL;
#define HASH_NODE(member) h=Anatomy_HashBytes(&n->member,sizeof(n->member),h)
        HASH_NODE(id);HASH_NODE(center.x);HASH_NODE(center.y);HASH_NODE(center.z);
        HASH_NODE(envelopeRadii.x);HASH_NODE(envelopeRadii.y);HASH_NODE(envelopeRadii.z);
        HASH_NODE(widthRadius);HASH_NODE(heightRadius);HASH_NODE(colorIndex);HASH_NODE(role);
        HASH_NODE(region);HASH_NODE(side);HASH_NODE(moduleInstanceId);HASH_NODE(localNodeId);HASH_NODE(development);
#undef HASH_NODE
        hash=Anatomy_HashBytes(&h,sizeof(h),hash);
    }
    for(size_t i=0;i<g->connectionCount;++i) {
        const BodyConnection* e=&g->connections[i];uint64_t h=0xcbf29ce484222325ULL;
#define HASH_EDGE(member) h=Anatomy_HashBytes(&e->member,sizeof(e->member),h)
        HASH_EDGE(id);HASH_EDGE(fromId);HASH_EDGE(toId);HASH_EDGE(kind);HASH_EDGE(moduleInstanceId);HASH_EDGE(development);
        HASH_EDGE(widthBulge);HASH_EDGE(heightBulge);
        HASH_EDGE(transverseAxis.x);HASH_EDGE(transverseAxis.y);HASH_EDGE(transverseAxis.z);
        h=Anatomy_HashBytes(&g->dormantConnections[i],sizeof(bool),h);
#undef HASH_EDGE
        hash=Anatomy_HashBytes(&h,sizeof(h),hash);
    }
    return hash;
}
