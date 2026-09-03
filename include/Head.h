/**
 * @file Head.h
 * @brief Fenotipo semántico, anatomía resuelta y receta SDF de una cabeza.
 */

#ifndef MONSTER_HEAD_H
#define MONSTER_HEAD_H

#include "Mouth.h"
#include "Vector.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Identidad estable reservada para la futura integración con AnatomyGraph. */
typedef uint32_t AnatomyId;

/** Familias de cabeza reutilizables, independientes de la especie completa. */
typedef enum HeadArchetype {
    HEAD_ARCHETYPE_LIZARD = 0,
    HEAD_ARCHETYPE_CANID,
    HEAD_ARCHETYPE_AVIAN
} HeadArchetype;

/** Controles semánticos normalizados; no contiene coordenadas anatómicas. */
typedef struct HeadPhenotype {
    HeadArchetype archetype; /**< Familia morfológica de la cabeza. */
    float skullWidth; /**< Anchura relativa del cráneo. */
    float skullHeight; /**< Altura relativa del cráneo. */
    float skullLength; /**< Longitud relativa del cráneo. */
    float muzzleLength; /**< Longitud semántica de hocico o rostro. */
    float muzzleWidth; /**< Anchura semántica de hocico o rostro. */
    float muzzleTaper; /**< Ahusamiento del rostro hacia el extremo. */
    float eyeSize; /**< Tamaño relativo de ojos y órbitas. */
    float eyeLaterality; /**< Desplazamiento lateral de las órbitas. */
    float eyeForwardness; /**< Orientación frontal relativa de las órbitas. */
    float jawLength; /**< Longitud relativa de mandíbula. */
    float jawDepth; /**< Profundidad vertical de mandíbula. */
    float jawStrength; /**< Masa funcional de mandíbula y bisagra. */
    float noseScale; /**< Escala de la almohadilla nasal. */
    float earSize; /**< Tamaño relativo de oreja. */
    float earPointiness; /**< Grado de apuntamiento de oreja. */
    float cheekMass; /**< Volumen relativo de mejillas. */
    float beakLength; /**< Longitud del pico superior e inferior. */
    float beakDepth; /**< Profundidad vertical del pico. */
    float beakTaper; /**< Ahusamiento distal del pico. */
    float beakCurvature; /**< Curvatura descendente del pico. */
    float nostrilPosition; /**< Posición longitudinal normalizada de narinas. */
} HeadPhenotype;

/** Landmarks locales con identidad anatómica estable. */
typedef struct HeadLandmarks {
    AnatomyId headId; /**< Identidad estable de la cabeza. */
    Vector3 neckAttachment; /**< Unión posterior e inferior con cuello. */
    Vector3 skullCenter; /**< Centro del volumen craneal. */
    Vector3 leftOrbit; /**< Centro de órbita izquierda. */
    Vector3 rightOrbit; /**< Centro de órbita derecha. */
    Vector3 leftJawHinge; /**< Cóndilo mandibular izquierdo. */
    Vector3 rightJawHinge; /**< Cóndilo mandibular derecho. */
    Vector3 muzzleRoot; /**< Inicio del hocico o pico. */
    Vector3 muzzleTip; /**< Extremo distal del hocico o pico. */
    Vector3 noseTip; /**< Extremo nasal. */
    Vector3 leftNostril; /**< Centro de narina izquierda. */
    Vector3 rightNostril; /**< Centro de narina derecha. */
    Vector3 leftEarBase; /**< Inserción de oreja izquierda. */
    Vector3 rightEarBase; /**< Inserción de oreja derecha. */
    Vector3 upperBeakAnchor; /**< Raíz del pico superior. */
    Vector3 lowerBeakAnchor; /**< Raíz del pico inferior. */
    Vector3 leftMouthCorner; /**< Comisura izquierda. */
    Vector3 rightMouthCorner; /**< Comisura derecha. */
} HeadLandmarks;

/** Receta genérica de volúmenes y cutters consumida por MonsterSDF. */
typedef struct HeadSurfaceRecipe {
    Vector3 craniumCenter; /**< Centro del volumen craneal. */
    Vector3 craniumRadii; /**< Radios del volumen craneal. */
    Vector3 faceRoot; /**< Centro del extremo proximal del rostro. */
    Vector3 faceTip; /**< Centro del extremo distal del rostro. */
    Vector3 faceRootRadii; /**< Radios elípticos proximales. */
    Vector3 faceTipRadii; /**< Radios elípticos distales. */
    Vector3 leftCheekCenter; /**< Centro de mejilla izquierda. */
    Vector3 rightCheekCenter; /**< Centro de mejilla derecha. */
    Vector3 cheekRadii; /**< Radios comunes de mejilla. */
    Vector3 browCenter; /**< Centro del volumen supraorbital. */
    Vector3 browRadii; /**< Radios del volumen supraorbital. */
    Vector3 leftOrbitCenter; /**< Centro del cutter orbital izquierdo. */
    Vector3 rightOrbitCenter; /**< Centro del cutter orbital derecho. */
    Vector3 orbitRadii; /**< Radios de cutters orbitales. */
    Vector3 leftOrbitRimCenter; /**< Centro del reborde orbital izquierdo. */
    Vector3 rightOrbitRimCenter; /**< Centro del reborde orbital derecho. */
    Vector3 orbitRimRadii; /**< Radios exteriores de reborde orbital. */
    Vector3 noseCenter; /**< Centro de almohadilla nasal. */
    Vector3 noseRadii; /**< Radios de almohadilla nasal. */
    Vector3 leftNostrilCenter; /**< Centro del cutter nasal izquierdo. */
    Vector3 rightNostrilCenter; /**< Centro del cutter nasal derecho. */
    Vector3 nostrilRadii; /**< Radios de cutters nasales. */
    Vector3 leftEarCenter; /**< Centro de oreja izquierda. */
    Vector3 rightEarCenter; /**< Centro de oreja derecha. */
    Vector3 earRadii; /**< Radios de volúmenes auriculares. */
    float unionSmoothness; /**< Suavidad común de uniones SDF. */
    bool hasNasalPad; /**< Activa volumen nasal diferenciado. */
    bool hasEars; /**< Activa volúmenes auriculares. */
    bool isBeak; /**< Indica una receta facial aviar. */
} HeadSurfaceRecipe;

/** Anatomía resuelta y validada, incluyendo el subsistema oral. */
typedef struct HeadAnatomy {
    HeadArchetype archetype; /**< Familia resuelta. */
    size_t attachmentBodyPartIndex; /**< Anfitrión del puente heredado. */
    HeadLandmarks landmarks; /**< Landmarks anatómicos locales. */
    HeadSurfaceRecipe surface; /**< Receta compilable a SDF. */
    Mouth oralSystem; /**< Subsistema oral propietario. */
    Vector3 eyeScale; /**< Escala derivada de globos oculares separados. */
} HeadAnatomy;

/** Cabeza poseíble: fenotipo fuente más anatomía de reposo resuelta. */
typedef struct Head {
    HeadPhenotype phenotype; /**< Autoridad semántica editable. */
    HeadAnatomy anatomy; /**< Anatomía de reposo resuelta. */
} Head;

/** Máscara de errores producida por HeadAnatomy_Validate. */
typedef enum HeadValidationError {
    HEAD_VALID = 0,
    HEAD_INVALID_FINITE = 1u << 0,
    HEAD_INVALID_SYMMETRY = 1u << 1,
    HEAD_INVALID_ORDER = 1u << 2,
    HEAD_INVALID_ORBITS = 1u << 3,
    HEAD_INVALID_NOSTRILS = 1u << 4,
    HEAD_INVALID_NECK = 1u << 5
} HeadValidationError;

/** @return Fenotipo predefinido de lagarto. */
HeadPhenotype HeadPhenotype_LizardPreset(void);
/** @return Fenotipo predefinido de cánido. */
HeadPhenotype HeadPhenotype_CanidPreset(void);
/** @return Fenotipo predefinido de ave. */
HeadPhenotype HeadPhenotype_AvianPreset(void);
/** Normaliza todos los controles semánticos a rangos válidos. */
void HeadPhenotype_Normalize(HeadPhenotype* phenotype);
/** Genera un fenotipo legal determinista a partir de una semilla. */
HeadPhenotype HeadPhenotype_RandomValid(HeadArchetype archetype, uint32_t seed);
/** Resuelve landmarks y receta SDF a partir del fenotipo y del volumen anfitrión. */
bool HeadAnatomy_Resolve(const HeadPhenotype* phenotype, size_t attachmentBodyPartIndex,
                         Vector3 hostRadii, HeadAnatomy* anatomy);
/** Comprueba finitud, simetría, orden y contención anatómica. */
uint32_t HeadAnatomy_Validate(const HeadAnatomy* anatomy);
/** Crea y resuelve una cabeza predefinida. */
Head Head_Create(HeadArchetype archetype, size_t attachmentBodyPartIndex, Vector3 hostRadii);
/** Ajusta la pose oral sin alterar la forma de reposo. */
void Head_SetOpenFactor(Head* head, float factor);

#ifdef __cplusplus
}
#endif

#endif
