/**
 * @file Ornament.c
 * @brief Implementación del módulo de ornamentos anatómicos (cuernos y espinas).
 * @author Monster Engine Team
 * @date 2026
 */

#include "Ornament.h"
#include "CreatureModuleInternal.h"
#include "MathUtils.h"
#include <math.h>

bool Ornament_Resolve(const OrnamentPhenotype* phenotype,
                      uint32_t module,
                      const AttachmentSlot* slot,
                      AnatomyGraph* graph) {
    if (!phenotype || !slot || !graph || !Anatomy_MakeId(module, 1)) return false;
    if (phenotype->archetype != ORNAMENT_ARCHETYPE_HORN &&
        phenotype->archetype != ORNAMENT_ARCHETYPE_SPINE &&
        phenotype->archetype != ORNAMENT_ARCHETYPE_MEMBRANE &&
        phenotype->archetype != ORNAMENT_ARCHETYPE_PLATE &&
        phenotype->archetype != ORNAMENT_ARCHETYPE_CREST) {
        return false;
    }

    float s = slot->scale;
    float dev = fmaxf(phenotype->development, 0.0005f);
    float length = phenotype->length * s * dev;
    float baseR = fmaxf(phenotype->baseRadius * s * dev, 1e-5f);
    /* Una punta infinitesimal puede quedar como isla al extraer el SDF con el
     * voxel de producción. El mínimo relativo conserva el cono y garantiza
     * que la última estación comparta superficie con su tramo anterior. */
    float tipR = fmaxf(phenotype->tipRadius * s * dev, baseR * 0.30f);
    float midR = (baseR + tipR) * 0.5f;
    if(phenotype->archetype==ORNAMENT_ARCHETYPE_HORN){
        /* Barrido curvo con estaciones persistentes: la base crece hacia
         * growthDirection y curvature dobla gradualmente la punta hacia up. */
        Vector3 v=phenotype->growthDirection;
        Vector3 axis=Vec3_LengthSq(v)>1e-6f?Vec3_Normalize(Vec3_Add(Vec3_Scale(slot->side,v.x),
            Vec3_Add(Vec3_Scale(slot->up,v.y),Vec3_Scale(slot->forward,v.z)))):slot->up;
        const uint16_t ids[7]={ORNAMENT_NODE_BASE,4,5,ORNAMENT_NODE_MID,6,7,ORNAMENT_NODE_TIP};
        float tip=fmaxf(phenotype->tipRadius*s*dev,baseR*.045f);
        for(unsigned i=0;i<7;++i){
            float t=(float)i/6,curve=t*t;
            Vector3 center=Vec3_Add(slot->position,Vec3_Add(Vec3_Scale(axis,length*t-baseR*.70f*(1-t)),
                Vec3_Add(Vec3_Scale(slot->side,length*phenotype->lateralSweep*curve),
                         Vec3_Scale(slot->up,length*phenotype->curvature*curve))));
            float radius=baseR*powf(1-t,.8f)+tip*t;
            if(!Module_Node(graph,module,ids[i],center,radius,radius,2,ANATOMY_ROLE_AXIAL,
                ANATOMY_REGION_ORNAMENT,ANATOMY_SIDE_CENTER,phenotype->development))return false;
            if(!Module_Edge(graph,module,(uint16_t)(i+1),i?Anatomy_MakeId(module,ids[i-1]):slot->hostNode,
                Anatomy_MakeId(module,ids[i]),i?BODY_CONNECTION_ORNAMENT_SEGMENT:BODY_CONNECTION_SUPPORT,phenotype->development))return false;
            graph->dormantConnections[graph->connectionCount-1]=phenotype->development<=0;
        }
        return true;
    }
    if (phenotype->archetype == ORNAMENT_ARCHETYPE_CREST) {
        Vector3 a=Vec3_Sub(slot->position,Vec3_Scale(slot->forward,baseR));
        Vector3 b=Vec3_Add(slot->position,Vec3_Scale(slot->up,length*.25f));
        Vector3 c=Vec3_Add(slot->position,Vec3_Scale(slot->forward,baseR));
        AnatomyId ia=Anatomy_MakeId(module,1),ib=Anatomy_MakeId(module,2),ic=Anatomy_MakeId(module,3);
        return Module_Node(graph,module,1,a,tipR,length*.10f,2,ANATOMY_ROLE_AXIAL,ANATOMY_REGION_ORNAMENT,ANATOMY_SIDE_CENTER,1) &&
            Module_Node(graph,module,2,b,tipR,length*.60f,2,ANATOMY_ROLE_AXIAL,ANATOMY_REGION_ORNAMENT,ANATOMY_SIDE_CENTER,1) &&
            Module_Node(graph,module,3,c,tipR,length*.10f,2,ANATOMY_ROLE_AXIAL,ANATOMY_REGION_ORNAMENT,ANATOMY_SIDE_CENTER,1) &&
            Module_Edge(graph,module,1,slot->hostNode,ia,BODY_CONNECTION_SUPPORT,1) &&
            Module_Edge(graph,module,2,ia,ib,BODY_CONNECTION_AXIAL_LOFT,1) &&
            Module_Edge(graph,module,3,ib,ic,BODY_CONNECTION_AXIAL_LOFT,1);
    }
    if (phenotype->archetype == ORNAMENT_ARCHETYPE_PLATE) {
        Vector3 center = Vec3_Add(slot->position, Vec3_Scale(slot->up, length*.25f));
        AnatomyId a = Anatomy_MakeId(module, ORNAMENT_NODE_BASE), b = Anatomy_MakeId(module, ORNAMENT_NODE_MID);
        return Module_Node(graph,module,ORNAMENT_NODE_BASE,center,baseR,length,2,
                   ANATOMY_ROLE_AXIAL,ANATOMY_REGION_ORNAMENT,ANATOMY_SIDE_CENTER,phenotype->development) &&
               Module_Node(graph,module,ORNAMENT_NODE_MID,center,baseR,length,2,
                   ANATOMY_ROLE_AXIAL,ANATOMY_REGION_ORNAMENT,ANATOMY_SIDE_CENTER,phenotype->development) &&
               Module_Edge(graph,module,1,slot->hostNode,a,BODY_CONNECTION_LIMB_SEGMENT,phenotype->development) &&
               Module_Edge(graph,module,2,a,b,BODY_CONNECTION_AXIAL_LOFT,phenotype->development);
    }


    /* El anclaje penetra en el volumen anfitrión para que los cuernos y las
     * espinas conserven una componente conectada al variar radios o edad. */
    Vector3 basePos = Vec3_Sub(slot->position, Vec3_Scale(slot->up, baseR * .95f));
    Vector3 midPos = Vec3_Add(slot->position,
        Vec3_Add(Vec3_Scale(slot->up, length * 0.5f),
                 Vec3_Scale(slot->forward, phenotype->curvature * 0.25f * s * dev)));
    Vector3 tipPos = Vec3_Add(slot->position,
        Vec3_Add(Vec3_Scale(slot->up, length),
                 Vec3_Scale(slot->forward, phenotype->curvature * s * dev)));
    if(Vec3_LengthSq(phenotype->growthDirection)>1e-6f) {
        Vector3 v=phenotype->growthDirection;
        Vector3 axis=Vec3_Normalize(Vec3_Add(Vec3_Scale(slot->side,v.x),
            Vec3_Add(Vec3_Scale(slot->up,v.y),Vec3_Scale(slot->forward,v.z))));
        basePos=Vec3_Sub(slot->position,Vec3_Scale(axis,baseR*.95f));
        midPos=Vec3_Add(slot->position,Vec3_Add(Vec3_Scale(axis,length*.5f),
            Vec3_Add(Vec3_Scale(slot->side,length*phenotype->lateralSweep*.6f),Vec3_Scale(slot->forward,phenotype->curvature*s*dev*.25f))));
        tipPos=Vec3_Add(slot->position,Vec3_Add(Vec3_Scale(axis,length),
            Vec3_Add(Vec3_Scale(slot->side,length*phenotype->lateralSweep*.35f),Vec3_Scale(slot->forward,phenotype->curvature*s*dev))));
    }

    AnatomyId baseId = Anatomy_MakeId(module, ORNAMENT_NODE_BASE);
    AnatomyId midId = Anatomy_MakeId(module, ORNAMENT_NODE_MID);
    AnatomyId tipId = Anatomy_MakeId(module, ORNAMENT_NODE_TIP);

    if (!Module_Node(graph, module, ORNAMENT_NODE_BASE, basePos, baseR, baseR, 2,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_ORNAMENT, ANATOMY_SIDE_CENTER, phenotype->development) ||
        !Module_Node(graph, module, ORNAMENT_NODE_MID, midPos, phenotype->archetype == ORNAMENT_ARCHETYPE_MEMBRANE ? baseR : midR,
                     phenotype->archetype == ORNAMENT_ARCHETYPE_MEMBRANE ? length*.50f : midR, 2,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_ORNAMENT, ANATOMY_SIDE_CENTER, phenotype->development) ||
        !Module_Node(graph, module, ORNAMENT_NODE_TIP, tipPos, tipR, tipR, 1,
                     ANATOMY_ROLE_AXIAL, ANATOMY_REGION_ORNAMENT, ANATOMY_SIDE_CENTER, phenotype->development)) {
        return false;
    }

    /* La lámina se obtiene por la cadena longitudinal de estaciones: el nodo
     * medio conserva la mitad de la altura local para que el tramo entre dos
     * estaciones sea una banda continua. Las costillas de subida se estrechan
     * al compilar el SDF para no convertir esa altura en un radio lateral. */
    if (phenotype->archetype == ORNAMENT_ARCHETYPE_MEMBRANE)
        return Module_Edge(graph, module, 1, slot->hostNode, baseId,
                           BODY_CONNECTION_SUPPORT, phenotype->development) &&
               Module_Edge(graph, module, 2, baseId, midId,
                           BODY_CONNECTION_SUPPORT, phenotype->development) &&
               Module_Edge(graph, module, 3, midId, tipId,
                           BODY_CONNECTION_SUPPORT, phenotype->development);

    if (!Module_Edge(graph, module, 1, slot->hostNode, baseId, BODY_CONNECTION_SUPPORT, phenotype->development) ||
        !Module_Edge(graph, module, 2, baseId, midId, BODY_CONNECTION_ORNAMENT_SEGMENT, phenotype->development) ||
        !Module_Edge(graph, module, 3, midId, tipId, BODY_CONNECTION_ORNAMENT_SEGMENT, phenotype->development)) {
        return false;
    }

    graph->dormantConnections[graph->connectionCount - 3] = (phenotype->development <= 0.0f);
    graph->dormantConnections[graph->connectionCount - 2] = (phenotype->development <= 0.0f);
    graph->dormantConnections[graph->connectionCount - 1] = (phenotype->development <= 0.0f);

    return true;
}
