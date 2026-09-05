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
    if (!graph || graph->nodeCount == 0 || graph->connectionCount == 0) return false;
    for (size_t i = 0; i < graph->connectionCount; ++i) {
        const BodyConnection* edge = &graph->connections[i];
        if (!AnatomyGraph_FindNode(graph, edge->fromId) ||
            !AnatomyGraph_FindNode(graph, edge->toId)) return false;
    }
    return true;
}
