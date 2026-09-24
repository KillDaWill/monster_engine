/** @file CreatureModuleInternal.h
 * @brief Ayudas internas para publicar nodos y conexiones semánticas.
 */
#ifndef CREATURE_MODULE_INTERNAL_H
#define CREATURE_MODULE_INTERNAL_H
#include "Anatomy.h"
static inline bool Module_Node(AnatomyGraph* g,uint32_t module,uint16_t local,Vector3 c,
    float w,float h,int color,AnatomyNodeRole role,AnatomyRegion region,AnatomySide side,float dev) {
    return AnatomyGraph_AddNode(g,(AnatomyNode){.id=Anatomy_MakeId(module,local),.center=c,
        .widthRadius=w,.heightRadius=h,.colorIndex=color,.role=role,.region=region,.side=side,
        .moduleInstanceId=module,.localNodeId=local,.development=dev});
}
static inline bool Module_Edge(AnatomyGraph* g,uint32_t module,uint16_t local,AnatomyId from,
    AnatomyId to,BodyConnectionKind kind,float dev) {
    return AnatomyGraph_Connect(g,(BodyConnection){.id=Anatomy_MakeId(module,local),
        .fromId=from,.toId=to,.kind=kind,.moduleInstanceId=module,.development=dev});
}
#endif
