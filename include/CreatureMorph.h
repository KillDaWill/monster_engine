/** @file CreatureMorph.h
 * @brief Metamorfosis entre anatomías resueltas con identidades semánticas estables.
 */
#ifndef CREATURE_MORPH_H
#define CREATURE_MORPH_H
#include "Monster.h"
/** @brief Cambio de blueprint que puede exigir una transición topológica local. */
typedef struct CreatureMorphTopologyEvent {
    uint32_t moduleInstanceId;
    size_t nodesAtStart,nodesAtEnd;
    size_t connectionsAtStart,connectionsAtEnd;
} CreatureMorphTopologyEvent;
/** @brief Enumera módulos cuya conectividad o firma nodal cambia entre extremos.
 * Los IDs corresponden al grafo unión, después de resolver colisiones de receta.
 * @param a Origen. @param b Destino. @param out Eventos opcionales.
 * @param capacity Capacidad de out.
 * @return Número total de eventos; SIZE_MAX si los grafos son inválidos.
 */
size_t CreatureMorph_CollectTopologyEvents(const Monster* a,const Monster* b,
    CreatureMorphTopologyEvent* out,size_t capacity);
/** @brief Interpola dos criaturas; las ramas ausentes se repliegan hasta su ancestro común.
 * @param a Origen. @param b Destino. @param age Progreso normalizado.
 * @param out Resultado independiente. @return false si los blueprints tienen identidades incompatibles o exceden capacidad.
 */
bool CreatureMorph_Interpolate(const Monster* a,const Monster* b,float age,Monster* out);
#endif
