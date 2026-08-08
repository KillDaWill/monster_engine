/**
 * @file MonsterBehavior.h
 * @brief Interfaz para controladores de comportamiento de la entidad Monster.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_BEHAVIOR_H
#define MONSTER_BEHAVIOR_H

#ifdef __cplusplus
extern "C" {
#endif

struct Monster;

/**
 * @struct MonsterBehavior
 * @brief Controlador de comportamiento de un monstruo.
 */
typedef struct MonsterBehavior {
    void (*init)(struct MonsterBehavior* self, struct Monster* monster);
    void (*update)(struct MonsterBehavior* self, struct Monster* monster, double diff);
} MonsterBehavior;

#ifdef __cplusplus
}
#endif

#endif // MONSTER_BEHAVIOR_H
