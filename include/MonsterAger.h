/**
 * @file MonsterAger.h
 * @brief Módulo para la interpolación continua (envejecimiento/evolución) entre dos fases de un monstruo.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_AGER_H
#define MONSTER_AGER_H

#include "Monster.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @struct MonsterAger
 * @brief Estructura que gestiona la transición y mezcla progresiva entre dos estados/fases de un monstruo.
 *
 * @note Requisito de correspondencia por índice:
 * MonsterAger empareja elementos anatómicos estrictamente por índice de arreglo.
 * Los monstruos límite (endpoint monsters) deben preservar el orden semántico:
 *   bodyParts[i] <-> bodyParts[i] correspondiente
 *   eyes[i]      <-> eyes[i] correspondiente
 *   mouths[i]    <-> mouths[i] correspondiente
 *
 * @todo Introducir identificadores estables AnatomyId antes de soportar reordenamiento anatómico
 * arbitrario entre etapas de desarrollo.
 */
typedef struct MonsterAger {
    Monster monster1; /**< Copia de la fase 1 (joven / inicial) */
    Monster monster2; /**< Copia de la fase 2 (maduro / evolucionado) */
    float perc;       /**< Factor de interpolación [0.0 - 1.0] */
    Monster result;   /**< Monstruo mezclado resultante actualizado */
} MonsterAger;

/**
 * @brief Crea e inicializa una instancia de MonsterAger dada la primera y segunda fase.
 * @param first Monstruo en fase 1.
 * @param second Monstruo en fase 2.
 * @param perc Factor de transición inicial (0.0 a 1.0).
 * @return Estructura MonsterAger configurada.
 */
MonsterAger MonsterAger_Create(const Monster* first, const Monster* second, float perc);

/**
 * @brief Actualiza el porcentaje de envejecimiento/transición y vuelve a mezclar la entidad resultante.
 * @param ager Puntero al ager.
 * @param perc Nuevo porcentaje [0.0f - 1.0f].
 */
void MonsterAger_SetPerc(MonsterAger* ager, float perc);

/**
 * @brief Obtiene un puntero mutable al monstruo mezclado resultante.
 * @param ager Puntero al ager.
 * @return Puntero a la estructura Monster mezclada.
 */
Monster* MonsterAger_GetResult(MonsterAger* ager);

/**
 * @brief Obtiene un puntero constante al monstruo mezclado resultante.
 * @param ager Puntero constante al ager.
 * @return Puntero a la estructura Monster mezclada.
 */
const Monster* MonsterAger_GetResultConst(const MonsterAger* ager);

/**
 * @brief Libera los recursos asignados internamente por MonsterAger.
 * @param ager Puntero al ager.
 */
void MonsterAger_Free(MonsterAger* ager);

/**
 * @brief Realiza la mezcla e interpolación física y cromática de monster1 y monster2 hacia dst.
 * @param monster1 Puntero a la fase 1.
 * @param monster2 Puntero a la fase 2.
 * @param perc Factor de interpolación.
 * @param dst Monstruo destino donde almacenar la mezcla.
 */
void MonsterAger_Interpolate(const Monster* monster1, const Monster* monster2, float perc, Monster* dst);

/**
 * @brief Normaliza y sincroniza el número de partes del cuerpo, tamaño de paleta de colores y rasgos entre dos monstruos.
 * @param monster1 Puntero al primer monstruo.
 * @param monster2 Puntero al segundo monstruo.
 */
void MonsterAger_NormalizeEndpoints(Monster* monster1, Monster* monster2);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_AGER_H
