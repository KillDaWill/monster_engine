/** @file MorphModuleState.h
 * @brief Certificación y versiones de módulos durante una metamorfosis.
 */
#ifndef MORPH_MODULE_STATE_H
#define MORPH_MODULE_STATE_H
#include <stdbool.h>
#include <stdint.h>

/** @brief Identidad inmutable del trabajo producido para un módulo. */
typedef struct MorphPatchToken {
    uint64_t morphGeneration;
    uint64_t topologyVersion;
    uint64_t interfaceVersion;
    uint64_t sdfVersion;
    uint32_t moduleId;
    float age;
} MorphPatchToken;

/** @brief Estados temporales de cesión; no son estados del worker. */
typedef enum MorphHandoffState { MORPH_MESH_ACTIVE, MORPH_ENTERING_SDF,
    MORPH_SDF_ACTIVE, MORPH_ENTERING_MESH } MorphHandoffState;

/** @brief Estado de publicación independiente de un módulo topológico. */
typedef struct MorphModuleState {
    uint32_t moduleId;
    float certifiedAge;
    float targetAge;
    uint64_t morphGeneration;
    uint64_t topologyVersion;
    uint64_t interfaceVersion;
    uint64_t sdfVersion;
    bool topologyTransition;
    bool certified;
    bool fallbackActive;
    MorphHandoffState handoffState;
    float handoffTransition,stableSeconds;
    bool fallbackWanted,handoffInitialized;
} MorphModuleState;

/** @brief Inicializa un módulo conocido antes de su primera publicación. */
void MorphModuleState_Init(MorphModuleState* state,uint32_t moduleId,
    uint64_t morphGeneration,bool topologyTransition);
/** @brief Fija la edad solicitada sin invalidar una malla previamente certificada. */
void MorphModuleState_Request(MorphModuleState* state,float age);
/** @brief Crea el token de una construcción para el estado actual. */
MorphPatchToken MorphModuleState_Token(const MorphModuleState* state,float age);
/** @brief Rechaza publicaciones de otra generación o versiones obsoletas. */
bool MorphModuleState_Accept(MorphModuleState* state,MorphPatchToken token);
/** @brief Autoriza predicción solo dentro de la edad certificada para topología cambiante. */
bool MorphModuleState_CanPredict(const MorphModuleState* state,float age);
/** @brief Solicita representación implícita cuando la malla congelada queda demasiado atrás. */
bool MorphModuleState_NeedsExactFallback(const MorphModuleState* state,
    float age,float maximumFrozenAgeLag);
/** @brief Avanza cesión con umbrales de entrada/salida distintos y residencia corta.
 * La salida exige certificado e interfaz segura; duration configura la rampa.
 */
void MorphModuleState_UpdateHandoff(MorphModuleState*,float age,float enterLag,
    bool interfaceValid,float deltaTime,float duration);
#endif
