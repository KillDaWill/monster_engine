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

/** Identidades reservadas por el preset de lagarto. */
typedef enum LizardAnatomyId {
    ANATOMY_ID_HEAD = 1,
    ANATOMY_ID_NECK = 10,
    ANATOMY_ID_PECTORAL = 20,
    ANATOMY_ID_THORAX_ANTERIOR = 21,
    ANATOMY_ID_THORAX_POSTERIOR = 22,
    ANATOMY_ID_ABDOMEN = 23,
    ANATOMY_ID_PELVIS = 24,
    ANATOMY_ID_TAIL_BASE = 30,
    ANATOMY_ID_TAIL_MIDDLE = 31,
    ANATOMY_ID_TAIL_DISTAL = 32,
    ANATOMY_ID_TAIL_TIP = 33,
    ANATOMY_ID_FORE_LEFT_SHOULDER = 100,
    ANATOMY_ID_FORE_LEFT_ELBOW = 101,
    ANATOMY_ID_FORE_LEFT_WRIST = 102,
    ANATOMY_ID_FORE_LEFT_HAND = 103,
    ANATOMY_ID_FORE_RIGHT_SHOULDER = 120,
    ANATOMY_ID_FORE_RIGHT_ELBOW = 121,
    ANATOMY_ID_FORE_RIGHT_WRIST = 122,
    ANATOMY_ID_FORE_RIGHT_HAND = 123,
    ANATOMY_ID_HIND_LEFT_HIP = 140,
    ANATOMY_ID_HIND_LEFT_KNEE = 141,
    ANATOMY_ID_HIND_LEFT_ANKLE = 142,
    ANATOMY_ID_HIND_LEFT_FOOT = 143,
    ANATOMY_ID_HIND_RIGHT_HIP = 160,
    ANATOMY_ID_HIND_RIGHT_KNEE = 161,
    ANATOMY_ID_HIND_RIGHT_ANKLE = 162,
    ANATOMY_ID_HIND_RIGHT_FOOT = 163,
    ANATOMY_ID_DIGIT_BASE = 200
} LizardAnatomyId;

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
    BODY_CONNECTION_DIGIT_SEGMENT
} BodyConnectionKind;

/** @struct AnatomyNode
 * @brief Estación anatómica resuelta; no implica por sí sola un volumen visible.
 */
typedef struct AnatomyNode {
    AnatomyId id; /**< Identidad estable independiente del índice de almacenamiento. */
    Vector3 center; /**< Centro mundial de la estación. */
    float widthRadius; /**< Radio transversal de la sección. */
    float heightRadius; /**< Radio vertical de la sección. */
    int colorIndex; /**< Índice de color en la paleta del monstruo. */
    AnatomyNodeRole role; /**< Papel anatómico y geométrico. */
} AnatomyNode;

/** @struct BodyConnection
 * @brief Arista dirigida que declara una unión anatómica sin depender del orden del arreglo.
 */
typedef struct BodyConnection {
    AnatomyId id; /**< Identidad estable de la conexión. */
    AnatomyId fromId; /**< Identidad de la estación de origen. */
    AnatomyId toId; /**< Identidad de la estación de destino. */
    BodyConnectionKind kind; /**< Receta geométrica de la unión. */
} BodyConnection;

/** Capacidad máxima de estaciones del grafo compacto embebido. */
#define ANATOMY_MAX_NODES 96
/** Capacidad máxima de conexiones del grafo compacto embebido. */
#define ANATOMY_MAX_CONNECTIONS 96

/** @struct AnatomyGraph
 * @brief Grafo anatómico compacto con nodos identificados y aristas explícitas.
 */
typedef struct AnatomyGraph {
    AnatomyNode nodes[ANATOMY_MAX_NODES]; /**< Estaciones resueltas. */
    size_t nodeCount; /**< Número de estaciones activas. */
    BodyConnection connections[ANATOMY_MAX_CONNECTIONS]; /**< Aristas declaradas. */
    size_t connectionCount; /**< Número de aristas activas. */
} AnatomyGraph;

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
/** Valida referencias, dimensiones e identidades del grafo. */
bool AnatomyGraph_Validate(const AnatomyGraph* graph);

#ifdef __cplusplus
}
#endif

#endif
