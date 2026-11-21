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
#define CREATURE_MAX_FLEXIBLE_APPENDAGES 4
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

/** Distribución visual del tejido; no cambia los seis anclajes semánticos. */
typedef enum AxialContourProfile {
    AXIAL_CONTOUR_LEGACY = 0,
    AXIAL_CONTOUR_CURSORIAL
} AxialContourProfile;

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
    float dewlapDepth; /**< Descenso ventral cervical relativo a la anchura del cuello. */
    float brisketDepth; /**< Descenso del pecho relativo a la altura torácica. */
    float withersElevation;     /**< Elevación sutil de la cruz sobre la línea dorsal (0..0.3). */
    AxialStationMorph profile[AXIAL_PROFILE_STATIONS];
    AxialContourProfile contourProfile; /**< Opcional v1: cresta, flanco y grupa continuos. */
    float withersRoundness; /**< Redondeo de la cresta escapular a altura constante (0..1). */
    unsigned cervicalSegments; /**< Opcional v1: tramos cervicales articulados (0/1 legado, 2..8 cadena larga). */
    float cervicalPitch; /**< Opcional v1: elevación cervical en radianes cuando hay cadena articulada. */
    float spineFlexion; /**< Opcional v1: límite angular de flexión espinal en radianes; cero usa el predeterminado. */
} AxialPhenotype;

typedef enum LimbArchetype {
    LIMB_ARCHETYPE_REPTILE = 0,
    LIMB_ARCHETYPE_MAMMAL,
    LIMB_ARCHETYPE_AVIAN,
    LIMB_ARCHETYPE_WING,
    LIMB_ARCHETYPE_FLIPPER,
    LIMB_ARCHETYPE_CUSTOM,
    LIMB_ARCHETYPE_UNGULATE /**< Ungulígrado: uno a tres dedos de apoyo según digitCount. */
} LimbArchetype;

/** Perfil de tejido independiente del número de dedos y de la especie. */
typedef enum LimbAnatomyProfile {
    LIMB_PROFILE_LEGACY = 0, /**< Conserva exactamente las recetas v1 anteriores. */
    LIMB_PROFILE_CURSORIAL, /**< Musculatura integrada y estaciones distales ungulígradas. */
    LIMB_PROFILE_COMPACT_UNGULATE, /**< Miembro corto con mezcla muscular y transición al menudillo. */
    LIMB_PROFILE_COLUMNAR, /**< Extremidad columnar con apoyo acolchado. */
    LIMB_PROFILE_PLANTIGRADE, /**< Palma y planta continuas con dedos independientes. */
    LIMB_PROFILE_MACROPOD /**< Extremidad posterior saltadora macrópoda con tibia larga, tarso-metatarso alargado y cuarto dedo dominante. */
} LimbAnatomyProfile;

typedef struct LimbPhenotype {
    LimbArchetype archetype;
    LimbRole role;
    LimbSide side;
    float length, thickness, sprawl, development;
    unsigned digitCount; /**< Ungulados: 1 casco entero, 2 pezuña hendida o 3 dedos portantes. */
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
    float flexionScale; /**< Amplitud articular mamífera; cero conserva el rango base. */
    float hoofWallSlope; /**< Retracción de corona respecto a suela, relativa a la longitud (0..0.45). */
    float hoofToeRoundness; /**< Radio anterior relativo a la semianchura de cada uña (0.3..1). */
    float hoofHeelHeight; /**< Altura del talón relativa a la pared anterior (0.4..1). */
    float dewclawSize; /**< Tamaño de espolones no sustentantes relativo al casco; cero los omite. */
    unsigned nailCount; /**< Placas del borde anterior independientes de los dedos. */
    LimbAnatomyProfile anatomyProfile; /**< Opcional en JSON v1; ausencia equivale a LEGACY. */
    float clawScale; /**< Longitud del ungual mamífero en radios digitales. Cero lo omite. */
    float webbingCoverage; /**< Extensión interdigital de membrana [0..1]; cero la omite. */
    float webbingThickness; /**< Espesor de membrana relativo al radio digital [0.06..0.5]. */
    float webbingEdgeCurve; /**< Concavidad festoneada del borde libre [-0.3..0.55]. */
    float webbingFold; /**< Plegamiento/retracción terrestre de la membrana [0..1]. */
} LimbPhenotype;

typedef enum TailArchetype {
    TAIL_ARCHETYPE_TAPERED = 0,
    TAIL_ARCHETYPE_WHIP,
    TAIL_ARCHETYPE_HEAVY,
    TAIL_ARCHETYPE_PREHENSILE,
    TAIL_ARCHETYPE_FINNED,
    TAIL_ARCHETYPE_CUSTOM,
    TAIL_ARCHETYPE_COILED, /**< Espiral cartilaginosa corta con paso lateral y longitud de arco estable. */
    TAIL_ARCHETYPE_PADDLE /**< Cola ancha y aplanada dorsoventralmente con extremo redondeado. */
} TailArchetype;

typedef struct TailPhenotype {
    float droop; /**< Caída gravitatoria de la cola; cero conserva el eje anfitrión. */
    TailArchetype archetype;
    float length, baseWidth, baseHeight, tipWidth, tipHeight;
    float taperCurve, curvature, development;
    float rootMatchStrength;
    unsigned segmentCount;
    float tipLift; /**< Curvatura vertical distal en reposo, en unidades del cuerpo. */
} TailPhenotype;

typedef enum OrnamentArchetype {
    ORNAMENT_ARCHETYPE_HORN = 0,
    ORNAMENT_ARCHETYPE_SPINE,
    ORNAMENT_ARCHETYPE_MEMBRANE,
    ORNAMENT_ARCHETYPE_PLATE,
    ORNAMENT_ARCHETYPE_CREST,
    ORNAMENT_ARCHETYPE_CUSTOM,
    ORNAMENT_ARCHETYPE_TUSK,
    ORNAMENT_ARCHETYPE_OSSICONE /**< Osicono cubierto de piel y pelo con extremo redondeado. */
} OrnamentArchetype;

typedef struct OrnamentPhenotype {
    OrnamentArchetype archetype;
    float length;
    float baseRadius;
    float tipRadius;
    float curvature;
    float development;
    Vector3 growthDirection; /**< Dirección opcional en el marco side/up/forward del anclaje. */
    float lateralSweep; /**< Arco lateral relativo a la longitud. */
} OrnamentPhenotype;

typedef enum FlexibleTerminal {
    FLEXIBLE_TERMINAL_ROUND = 0,
    FLEXIBLE_TERMINAL_PAIRED_OPENINGS
} FlexibleTerminal;

/** Radios y longitud en unidades del anfitrión, curvatura en radianes. */
typedef struct FlexibleAppendagePhenotype {
    float length, baseRadius, tipRadius, aspect, taper;
    float initialPitch, restBend;
    FlexibleTerminal terminal;
} FlexibleAppendagePhenotype;

#define CREATURE_MAX_MEMBRANES 12

/**
 * @struct AnatomicalMembraneProfile
 * @brief Membrana/patagio genérico acotado por dos cadenas de nodos anatómicos.
 */
typedef struct AnatomicalMembraneProfile {
    uint32_t moduleInstanceId;    /**< Módulo anfitrión principal o 0 si es intermodular. */
    uint32_t moduleAInstanceId;   /**< Módulo de inicio de la cadena A. */
    uint32_t chainAStartNodeId;   /**< Nodo inicial local de la cadena A. */
    uint32_t moduleAEndInstanceId;/**< Módulo final de la cadena A (0 usa moduleAInstanceId). */
    uint32_t chainAEndNodeId;     /**< Nodo final local de la cadena A. */
    uint32_t moduleBInstanceId;   /**< Módulo de inicio de la cadena B. */
    uint32_t chainBStartNodeId;   /**< Nodo inicial local de la cadena B. */
    uint32_t moduleBEndInstanceId;/**< Módulo final de la cadena B (0 usa moduleBInstanceId). */
    uint32_t chainBEndNodeId;     /**< Nodo final local de la cadena B. */
    float coverage;               /**< Cobertura a lo largo del borde distal/libre [0..1]. */
    float thickness;              /**< Espesor relativo a escala total [0.002..0.06]. */
    float edgeConcavity;          /**< Concavidad festoneada del borde libre [-0.25..0.55]. */
    float camber;                 /**< Arqueamiento dorsal sutil de la lámina [-0.2..0.3]. */
    float tension;                /**< Tensión de la membrana en reposo [0..1]. */
    float development;            /**< Desarrollo ontogenético [0..1]. */
    Color color;                  /**< Color base coriáceo de la membrana. */
} AnatomicalMembraneProfile;

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
    HeadHybridProfile headHybrid;
    EyePhenotype eyes;
    LimbPhenotype limbs[CREATURE_MAX_LIMBS];
    size_t limbCount;
    TailPhenotype tails[CREATURE_MAX_TAILS];
    size_t tailCount;
    OrnamentPhenotype ornaments[CREATURE_MAX_ORNAMENTS];
    size_t ornamentCount;
    FlexibleAppendagePhenotype flexibleAppendages[CREATURE_MAX_FLEXIBLE_APPENDAGES];
    size_t flexibleAppendageCount;
    AnatomicalMembraneProfile membranes[CREATURE_MAX_MEMBRANES];
    size_t membraneCount;
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
