/**
 * @file ArmorScales.h
 * @brief Generador de coraza queratínica geométrica con escamas imbricadas reales.
 */
#ifndef MONSTER_ARMOR_SCALES_H
#define MONSTER_ARMOR_SCALES_H

#include "AnatomyDeformer.h"
#include "Mesh.h"
#include "Monster.h"
#include "MonsterSDF.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Construye la malla cerrada de escamas queratínicas imbricadas sobre el SDF de reposo.
 * @param monster Criatura con fenotipo, superficie y grafo anatómico resueltos.
 * @param restSdf Campo SDF en pose de reposo para proyección superficial exacta.
 * @param outMesh Malla destino (vacía cuando plates.armorScale <= 0.01).
 * @return true si se construyó correctamente o no aplican placas geométricas.
 */
bool ArmorScales_BuildMesh(const Monster* monster, const MonsterSDF* restSdf, Mesh* outMesh);

/**
 * @brief Vincula una malla de coraza ya generada al esqueleto sin sobreescribir colores.
 * @param monster Criatura resuelta en reposo.
 * @param restSdf Campo SDF en reposo (mismo empleado en BuildMesh).
 * @param skeleton Esqueleto del rig.
 * @param mesh Malla existente de placas.
 * @param deformer Deformador esquelético destino.
 * @return true si se vinculó correctamente (o malla vacía).
 */
bool ArmorScales_BindDeformer(const Monster* monster, const MonsterSDF* restSdf,
                              const Skeleton* skeleton, const Mesh* mesh,
                              AnatomyDeformer* deformer);

#ifdef __cplusplus
}
#endif

#endif /* MONSTER_ARMOR_SCALES_H */
