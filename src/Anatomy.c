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
        for(size_t j=0;j<i;++j)if(graph->nodes[j].id==n->id)return false;
    }
    for (size_t i = 0; i < graph->connectionCount; ++i) {
        const BodyConnection* edge = &graph->connections[i];
        if (!AnatomyGraph_FindNode(graph, edge->fromId) ||
            !AnatomyGraph_FindNode(graph, edge->toId)) return false;
    }
    return true;
}

AnatomyId Anatomy_DigitId(unsigned limb, unsigned digit, unsigned station) {
    if (limb >= 4 || digit >= 5 || station >= 7) return 0;
    return ANATOMY_ID_DIGIT_BASE + limb * 128u + digit * 16u + station;
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
        HASH_NODE(widthRadius);HASH_NODE(heightRadius);HASH_NODE(colorIndex);HASH_NODE(role);
#undef HASH_NODE
        hash=Anatomy_HashBytes(&h,sizeof(h),hash);
    }
    for(size_t i=0;i<g->connectionCount;++i) {
        const BodyConnection* e=&g->connections[i];uint64_t h=0xcbf29ce484222325ULL;
#define HASH_EDGE(member) h=Anatomy_HashBytes(&e->member,sizeof(e->member),h)
        HASH_EDGE(id);HASH_EDGE(fromId);HASH_EDGE(toId);HASH_EDGE(kind);
#undef HASH_EDGE
        hash=Anatomy_HashBytes(&h,sizeof(h),hash);
    }
    return hash;
}
