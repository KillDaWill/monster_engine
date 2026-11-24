/** @file AIWorld.h
 * @brief Inteligencia modular independiente del render y de la anatomía. */
#ifndef AIWORLD_H
#define AIWORLD_H
#include "Vector.h"
#include "InteractionTarget.h"
#include "AISpatialIndex.h"
#include "MoleculeRegistry.h"
#include <stdint.h>
#define AI_WORLD_CAPACITY 128
typedef uint32_t AIObjectId;
typedef enum AIObjectType { AI_CREATURE, AI_WATER, AI_OBSTACLE, AI_INTEREST, AI_FOOD } AIObjectType;
typedef struct AIWorldObject {
    AIObjectId id;
    unsigned revision; /**< Revisión propia de contacto, separada de navegación. */
    AIObjectType type;
    unsigned group, tags;
    unsigned species, category, foodCategory;
    bool available, reproductiveReady;
    Vector3 position, surfaceNormal, approachDirection, velocity;
    bool moving;
    float radius, height, maximumSpeed;
    bool active, visible, occludes, walkable, drinkable;
    /* Descriptor observable compacto de recursos moleculares (sin datos privilegiados). */
    bool molecularResource;
    uint8_t substancePhase;
    float hardness;
    float availabilityFraction;
    SensorySignature sensorySignature;
} AIWorldObject;
typedef struct AIWorld {
    AIWorldObject objects[AI_WORLD_CAPACITY];
    AIWorldObject *storage; /**< NULL usa almacenamiento heredado sin heap. */
    AISpatialIndex *spatial; /**< Índice espacial y hash O(1) cuando se usa InitCapacity. */
    void *navCache; /**< Caché compartida de ocupación estática para A* (propiedad de AIWorld). */
    unsigned capacity;
    AIObjectId nextId;
    unsigned revision;
    float halfSize;
} AIWorld;
/** @brief Inicializa registro acotado; IDs nunca se reutilizan. */
void AIWorld_Init(AIWorld *, float halfSize);
/** @brief Reserva capacidad configurable e índice espacial. No copiar por valor un mundo propietario. */
bool AIWorld_InitCapacity(AIWorld *, float halfSize, unsigned capacity);
/** @brief Libera almacenamiento opcional e índice espacial, después de retirar usuarios. */
void AIWorld_Destroy(AIWorld *);
/** @brief Reconfigura el tamaño de celda del índice espacial (p. ej. 2, 4 u 8 unidades). */
bool AIWorld_SetSpatialCellSize(AIWorld *, float cellSize);
/** @brief Acceso por ranura para sensores, sin truncar candidatos a 128. */
const AIWorldObject *AIWorld_At(const AIWorld *, unsigned slot);
/** @brief Publica datos observables sin invalidar navegación. */
bool AIWorld_SetObservation(AIWorld *, AIObjectId, unsigned species, unsigned category,
    unsigned foodCategory, bool available, bool reproductiveReady);
/** @brief Publica rol y capacidad observable del agente, sin invalidar rutas. */
bool AIWorld_SetAgentState(AIWorld *,AIObjectId,unsigned role,float maximumSpeed);
/** @brief Publicación combinada O(1) de estado, posición, cotas y observación de una criatura. */
bool AIWorld_PublishAgent(AIWorld *, AIObjectId, unsigned role, float maximumSpeed,
    Vector3 position, Vector3 velocity, float radius, float height,
    unsigned species, unsigned category, unsigned foodCategory,
    bool available, bool reproductiveReady);
/** @brief Publicación O(1) del estado observable de un recurso sin búsquedas lineales. */
bool AIWorld_PublishResource(AIWorld *, AIObjectId, unsigned species, unsigned foodCategory, bool available);
/** @brief Publicación O(1) de propiedades observables de un recurso molecular (firma sensorial y disponibilidad). */
bool AIWorld_PublishMolecularResource(AIWorld *, AIObjectId, unsigned species, unsigned foodCategory,
    bool available, uint8_t substancePhase, float hardness, float availabilityFraction,
    const SensorySignature *signature);
/** @brief Registra objeto por valor; cero indica capacidad agotada. */
AIObjectId AIWorld_Add(AIWorld *, AIWorldObject);
/** @brief Consulta por identificador estable, NULL si fue retirado. */
const AIWorldObject *AIWorld_Get(const AIWorld *, AIObjectId);
/** @brief Publica transformaciones; cambios de obstáculos invalidan caminos. */
bool AIWorld_Move(AIWorld *, AIObjectId, Vector3);
/** @brief Actualiza tamaño manteniendo ID; invalida rutas sólo para obstáculos.
 * @param radius Radio físico no negativo. @param height Altura de oclusión.
 * @return false si no existe el objeto o las dimensiones no son válidas. */
bool AIWorld_SetBounds(AIWorld *, AIObjectId, float radius, float height);
/** @brief Retira sin dejar referencias crudas. */
void AIWorld_Remove(AIWorld *, AIObjectId);
/** @brief Broadphase por esfera, devuelve IDs hasta capacidad. */
unsigned AIWorld_Candidates(const AIWorld *, Vector3, float, AIObjectId *, unsigned);
/** @brief Consulta espacial conservadora por radio devolviendo ranuras en orden determinista por ID. */
unsigned AIWorld_QuerySlotsRadius(const AIWorld *, uint32_t layerMask, Vector3 center, float radius,
    uint32_t *outSlots, unsigned capacity, bool *overflowed);
/** @brief Consulta espacial conservadora por segmento devolviendo ranuras candidatas. */
unsigned AIWorld_QuerySlotsSegment(const AIWorld *, uint32_t layerMask, Vector3 a, Vector3 b, float radius,
    uint32_t *outSlots, unsigned capacity, bool *overflowed);
/** @brief Visibilidad por cilindros estáticos; Y vertical. */
bool AIWorld_LineOfSight(const AIWorld *, Vector3, Vector3, AIObjectId ignore);
/** @brief Barrido volumétrico contra obstáculos y oclusores cilíndricos. */
bool AIWorld_ClearSegment(const AIWorld *,Vector3,Vector3,float radius,AIObjectId ignore);
/** @brief Espacio libre para disco conservador XZ. */
bool AIWorld_Walkable(const AIWorld *, Vector3, float clearance, bool swimming);
/** @brief Barrido conservador de segmento para navegación. */
bool AIWorld_SegmentWalkable(const AIWorld *, Vector3, Vector3, float, bool);
/** @brief Selecciona contacto sobre disco superficial a la altura real del recurso. */
bool AIWorld_InteractionTarget(const AIWorld *, AIObjectId, Vector3 mouth, float tolerance, InteractionTarget *);
/** @brief Valida interacción 3D desde boca, orilla cercana y visión sin
 * obstáculos. */
bool AIWorld_CanDrink(const AIWorld *, AIObjectId, Vector3 mouth, float reach);

#endif
