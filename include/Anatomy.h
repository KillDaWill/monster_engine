/**
 * @file Anatomy.h
 * @brief Grafo anatómico explícito con identidades estables y conexiones ramificadas.
 */

#ifndef MONSTER_ANATOMY_H
#define MONSTER_ANATOMY_H

#include "Vector.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Identidad estable de un nodo o una conexión anatómica. */
typedef uint32_t AnatomyId;

/** Identidades compuestas: módulo 1..65534, nodo local 1..65535. */
AnatomyId Anatomy_MakeId(uint32_t moduleInstanceId,uint16_t localNodeId);
typedef enum AnatomyRegion {
    ANATOMY_REGION_UNKNOWN, ANATOMY_REGION_HEAD, ANATOMY_REGION_NECK,
    ANATOMY_REGION_TRUNK, ANATOMY_REGION_PELVIS, ANATOMY_REGION_FORELIMB,
    ANATOMY_REGION_HINDLIMB, ANATOMY_REGION_LIMB, ANATOMY_REGION_WING,
    ANATOMY_REGION_TAIL, ANATOMY_REGION_DIGIT, ANATOMY_REGION_ORNAMENT
} AnatomyRegion;
typedef enum AnatomySide { ANATOMY_SIDE_CENTER, ANATOMY_SIDE_LEFT, ANATOMY_SIDE_RIGHT } AnatomySide;

/** Papel geométrico de un nodo resuelto. */
typedef enum AnatomyNodeRole {
    ANATOMY_ROLE_AXIAL,
    ANATOMY_ROLE_JOINT,
    ANATOMY_ROLE_DIGIT
} AnatomyNodeRole;

/** Tipo de superficie que compila una conexión explícita. */
typedef enum BodyConnectionKind {
    BODY_CONNECTION_AXIAL_LOFT,
    BODY_CONNECTION_LIMB_SEGMENT,
    BODY_CONNECTION_DIGIT_SEGMENT,
    BODY_CONNECTION_SUPPORT, /**< Unión estructural sin volumen adicional (láminas). */
    BODY_CONNECTION_ORNAMENT_SEGMENT /**< Tramo de cuerno o espina, separado del detalle locomotor. */
} BodyConnectionKind;

/** @struct AnatomyNode
 * @brief Estación anatómica resuelta; no implica por sí sola un volumen visible.
 */
typedef struct AnatomyNode {
    AnatomyId id; /**< Identidad estable independiente del índice de almacenamiento. */
    Vector3 center; /**< Centro mundial de la estación. */
    Vector3 envelopeRadii; /**< Envolvente física resuelta de módulos volumétricos. */
    float widthRadius; /**< Radio transversal de la sección. */
    float heightRadius; /**< Radio vertical de la sección. */
    int colorIndex; /**< Índice de color en la paleta del monstruo. */
    AnatomyNodeRole role; /**< Papel anatómico y geométrico. */
    AnatomyRegion region;
    AnatomySide side;
    uint32_t moduleInstanceId;
    uint16_t localNodeId;
    float development;
} AnatomyNode;

/** @struct BodyConnection
 * @brief Arista dirigida que declara una unión anatómica sin depender del orden del arreglo.
 */
typedef struct BodyConnection {
    AnatomyId id; /**< Identidad estable de la conexión. */
    AnatomyId fromId; /**< Identidad de la estación de origen. */
    AnatomyId toId; /**< Identidad de la estación de destino. */
    BodyConnectionKind kind; /**< Receta geométrica de la unión. */
    uint32_t moduleInstanceId;
    float development;
    float widthBulge, heightBulge; /**< Plenitud central de sección; cero conserva el cono lineal. */
    Vector3 transverseAxis; /**< Eje transversal anatómico opcional; cero usa el marco automático. */
} BodyConnection;

/** Capacidad máxima de estaciones del grafo compacto embebido. */
#define ANATOMY_MAX_NODES 1024
/** Capacidad máxima de conexiones del grafo compacto embebido. */
#define ANATOMY_MAX_CONNECTIONS 1024

/** @struct AnatomyGraph
 * @brief Grafo anatómico compacto con nodos identificados y aristas explícitas.
 */
typedef struct AnatomyGraph {
    AnatomyNode nodes[ANATOMY_MAX_NODES]; /**< Estaciones resueltas. */
    size_t nodeCount; /**< Número de estaciones activas. */
    BodyConnection connections[ANATOMY_MAX_CONNECTIONS]; /**< Aristas declaradas. */
    size_t connectionCount; /**< Número de aristas declaradas. */
    bool dormantConnections[ANATOMY_MAX_CONNECTIONS]; /**< Blueprint sin superficie ni influencia de vinculación. */
} AnatomyGraph;

/** @brief Consulta un nodo local de un módulo. @return Nodo, o NULL. */
const AnatomyNode* AnatomyGraph_FindModuleNode(const AnatomyGraph*,uint32_t,uint16_t);
/** @brief Consulta la primera región semántica. @return Nodo, o NULL. */
const AnatomyNode* AnatomyGraph_FindFirstRegion(const AnatomyGraph*,AnatomyRegion);
/** @brief Compara blueprint sin geometría ni dormancia. @return Compatibilidad. */
bool AnatomyGraph_TopologyCompatible(const AnatomyGraph*,const AnatomyGraph*);

/** Inicializa un grafo vacío. */
void AnatomyGraph_Init(AnatomyGraph* graph);
/** Añade una estación si su identidad y dimensiones son válidas y únicas. */
bool AnatomyGraph_AddNode(AnatomyGraph* graph, AnatomyNode node);
/** Añade una arista entre dos identidades existentes. */
bool AnatomyGraph_Connect(AnatomyGraph* graph, BodyConnection connection);
/** Busca una estación por identidad estable. */
const AnatomyNode* AnatomyGraph_FindNode(const AnatomyGraph* graph, AnatomyId id);
/** Comprueba si existe la arista dirigida indicada. */
bool AnatomyGraph_HasConnection(const AnatomyGraph* graph, AnatomyId fromId, AnatomyId toId);
/** @brief Huella de estaciones y aristas activas; sin leer reservas ni bytes de relleno. */
uint64_t AnatomyGraph_Fingerprint(const AnatomyGraph* graph);
/** Valida referencias, dimensiones e identidades del grafo. */
bool AnatomyGraph_Validate(const AnatomyGraph* graph);

#ifdef __cplusplus
}
#endif

#endif
