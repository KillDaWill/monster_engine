/**
 * @file CreaturePhenotype.h
 * @brief Morfología modular independiente de composición y renderizado.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef CREATURE_PHENOTYPE_H
#define CREATURE_PHENOTYPE_H

#include "Head.h"
#include "Eye.h"
#include "Surface.h"
#include "RigBuilder.h"
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CREATURE_MAX_LIMBS 16
#define CREATURE_MAX_TAILS 4
#define CREATURE_MAX_DIGITS 8
#define CREATURE_MAX_TAIL_STATIONS 16
#define CREATURE_MAX_ORNAMENTS 64

typedef enum AxialArchetype {
    AXIAL_ARCHETYPE_SPRAWLING_TETRAPOD = 0,
    AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD,
    AXIAL_ARCHETYPE_SERPENTINE,
    AXIAL_ARCHETYPE_AVIAN,
    AXIAL_ARCHETYPE_CUSTOM
} AxialArchetype;

/** Deformación de estación semántica, antes de publicar anclajes. */
typedef struct AxialStationMorph {
    float longitudinalOffset, verticalOffset, widthScale, heightScale;
} AxialStationMorph;
#define AXIAL_PROFILE_STATIONS 6

typedef struct AxialPhenotype {
    AxialArchetype archetype;
    float totalScale, neckLength, neckWidth, shoulderWidth;
    float thoraxWidth, thoraxHeight, abdomenWidth, abdomenHeight;
    float pelvicWidth, pelvicHeight, trunkLength, bodyFlattening;
    float limbAttachmentLateral;
    float withersHeight; /**< Altura dorsal de referencia para troncos erectos. */
    float cervicalCurvature;    /**< Arqueamiento sagital de la columna cervical (0..1). */
    float cervicalDorsalMass;   /**< Concentración de masa muscular nucal / sesgo dorsal (0..2). */
    float cervicalMidNarrowing; /**< Retraso del ensanchamiento cervical hacia los hombros (0.5..1.0). */
    float withersElevation;     /**< Elevación sutil de la cruz sobre la línea dorsal (0..0.3). */
    AxialStationMorph profile[AXIAL_PROFILE_STATIONS];
} AxialPhenotype;

typedef enum LimbArchetype {
    LIMB_ARCHETYPE_REPTILE = 0,
    LIMB_ARCHETYPE_MAMMAL,
    LIMB_ARCHETYPE_AVIAN,
    LIMB_ARCHETYPE_WING,
    LIMB_ARCHETYPE_FLIPPER,
    LIMB_ARCHETYPE_CUSTOM
} LimbArchetype;

typedef struct LimbPhenotype {
    LimbArchetype archetype;
    LimbRole role;
    LimbSide side;
    float length, thickness, sprawl, development;
    unsigned digitCount;
    float digitLengths[CREATURE_MAX_DIGITS];
    unsigned digitPhalanges[CREATURE_MAX_DIGITS];
    float digitAngles[CREATURE_MAX_DIGITS];
    float digitSpread, footScale;
    float proximalScale, middleScale, distalScale;
    float rootThicknessScale, proximalThicknessScale, middleThicknessScale, distalThicknessScale;
    float footWidthScale, footHeightScale, digitThicknessScale;
    float attachmentInset;
    float proximalSweep, distalSweep; /**< Desplazamientos longitudinales relativos a la longitud del miembro. */
    float footYaw; /**< Giro del autopodio en radianes, simétrico entre ambos lados. */
} LimbPhenotype;

typedef enum TailArchetype {
    TAIL_ARCHETYPE_TAPERED = 0,
    TAIL_ARCHETYPE_WHIP,
    TAIL_ARCHETYPE_HEAVY,
    TAIL_ARCHETYPE_PREHENSILE,
    TAIL_ARCHETYPE_FINNED,
    TAIL_ARCHETYPE_CUSTOM
} TailArchetype;

typedef struct TailPhenotype {
    TailArchetype archetype;
    float length, baseWidth, baseHeight, tipWidth, tipHeight;
    float taperCurve, curvature, development;
    float rootMatchStrength;
    unsigned segmentCount;
} TailPhenotype;

typedef enum OrnamentArchetype {
    ORNAMENT_ARCHETYPE_HORN = 0,
    ORNAMENT_ARCHETYPE_SPINE,
    ORNAMENT_ARCHETYPE_MEMBRANE,
    ORNAMENT_ARCHETYPE_PLATE,
    ORNAMENT_ARCHETYPE_CREST,
    ORNAMENT_ARCHETYPE_CUSTOM
} OrnamentArchetype;

typedef struct OrnamentPhenotype {
    OrnamentArchetype archetype;
    float length;
    float baseRadius;
    float tipRadius;
    float curvature;
    float development;
} OrnamentPhenotype;

typedef struct EyePhenotype {
    float size, protrusion, irisScale, pupilScale, pupilAspect;
    PupilShape pupilShape;
    Color scleraColor, irisColor, pupilColor;
} EyePhenotype;

typedef struct CreatureDevelopment {
    float appendages, cephalic, pigmentation, integument, colorMaturity;
} CreatureDevelopment;

/** Envolvente física independiente de los coeficientes normalizados de forma. */
typedef struct HeadEnvelopePhenotype {
    float scale, widthScale, heightScale, lengthScale;
} HeadEnvelopePhenotype;

typedef struct CreaturePhenotype {
    AxialPhenotype axial;
    HeadPhenotype head;
    HeadEnvelopePhenotype headEnvelope;
    EyePhenotype eyes;
    LimbPhenotype limbs[CREATURE_MAX_LIMBS];
    size_t limbCount;
    TailPhenotype tails[CREATURE_MAX_TAILS];
    size_t tailCount;
    OrnamentPhenotype ornaments[CREATURE_MAX_ORNAMENTS];
    size_t ornamentCount;
    SurfacePhenotype surface;
    CreatureDevelopment development;
    Color dorsalColor, ventralColor, unpigmentedVentralColor;
} CreaturePhenotype;

/** @brief Normaliza controles sin alterar la composición. @param phenotype Fenotipo editable. */
void CreaturePhenotype_Normalize(CreaturePhenotype* phenotype);

/** @brief Interpola fenotipos compatibles garantizando extremos exactos. @return Fenotipo resuelto. */
CreaturePhenotype CreaturePhenotype_Interpolate(const CreaturePhenotype* a, const CreaturePhenotype* b, float age);

/** @brief Comprueba coincidencia de blueprint estructural (conteos, módulos, articulaciones). */
bool CreaturePhenotype_TopologyCompatible(const CreaturePhenotype* a, const CreaturePhenotype* b);

/** @brief Comprueba compatibilidad para interpolación continua (topología y arquetipos compatibles). */
bool CreaturePhenotype_MorphCompatible(const CreaturePhenotype* a, const CreaturePhenotype* b);

/** @brief Compatibilidad general (alias de MorphCompatible). */
bool CreaturePhenotype_Compatible(const CreaturePhenotype* a, const CreaturePhenotype* b);

#ifdef __cplusplus
}
#endif

#endif
