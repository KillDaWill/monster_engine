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
#define CREATURE_MAX_FLEXIBLE_APPENDAGES 12
#define CREATURE_MAX_REPEATED_APPENDAGES 4
#define CREATURE_MAX_FINS 12
#define CREATURE_MAX_DIGITS 8
#define CREATURE_MAX_TAIL_STATIONS 16
#define CREATURE_MAX_ORNAMENTS 64

typedef enum AxialArchetype {
    AXIAL_ARCHETYPE_SPRAWLING_TETRAPOD = 0,
    AXIAL_ARCHETYPE_UPRIGHT_TETRAPOD,
    AXIAL_ARCHETYPE_SERPENTINE,
    AXIAL_ARCHETYPE_AVIAN,
    AXIAL_ARCHETYPE_CUSTOM,
    AXIAL_ARCHETYPE_DISC_RAY,
    AXIAL_ARCHETYPE_CEPHALOPOD,
    AXIAL_ARCHETYPE_ARTHROPOD,
    AXIAL_ARCHETYPE_MEDUSA_RADIAL,
    AXIAL_ARCHETYPE_AQUATIC_FUSIFORM, /**< Cuerpo hidrodinámico fusiforme continuo (peces, escualos, cetáceos). */
    AXIAL_ARCHETYPE_CURVED_VERTICAL   /**< Cuerpo acuático vertical curvado (signátidos, caballitos de mar, dragones de mar). */
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
    unsigned radialSymmetryOrder; /**< Opcional v1: orden de simetría radial (p. ej. 4 u 8 para medusas/cefalópodos). */
    float bellConcavity; /**< Opcional v1: profundidad relativa de la cavidad subumbrelar [0..1]. */
    float bellScallopAmplitude; /**< Opcional v1: amplitud del festoneado marginal de la campana [0..1]. */
    unsigned abdomenSegments; /**< Opcional v1: segmentos abdominales articulados de artrópodos (0..12). */
    float discSweep; /**< Opcional v1: flecha/barrido posterior del disco pectoral batoideo [0..1]. */
    float mantleElongation; /**< Opcional v1: elongación del manto cefalópodo [0..2]. */
} AxialPhenotype;

typedef enum LimbArchetype {
    LIMB_ARCHETYPE_REPTILE = 0,
    LIMB_ARCHETYPE_MAMMAL,
    LIMB_ARCHETYPE_AVIAN,
    LIMB_ARCHETYPE_WING,
    LIMB_ARCHETYPE_FLIPPER,
    LIMB_ARCHETYPE_CUSTOM,
    LIMB_ARCHETYPE_UNGULATE, /**< Ungulígrado: uno a tres dedos de apoyo según digitCount. */
    LIMB_ARCHETYPE_ARTHROPOD_LEG, /**< Pata articulada exoesquelética (coxa, trocánter, fémur, tibia, tarso, uñas). */
    LIMB_ARCHETYPE_INSECT_WING /**< Ala membranosa fina con venación estructural independiente. */
} LimbArchetype;

/** Perfil de tejido independiente del número de dedos y de la especie. */
typedef enum LimbAnatomyProfile {
    LIMB_PROFILE_LEGACY = 0, /**< Conserva exactamente las recetas v1 anteriores. */
    LIMB_PROFILE_CURSORIAL, /**< Musculatura integrada y estaciones distales ungulígradas. */
    LIMB_PROFILE_COMPACT_UNGULATE, /**< Miembro corto con mezcla muscular y transición al menudillo. */
    LIMB_PROFILE_COLUMNAR, /**< Extremidad columnar con apoyo acolchado. */
    LIMB_PROFILE_PLANTIGRADE, /**< Palma y planta continuas con dedos independientes. */
    LIMB_PROFILE_MACROPOD, /**< Extremidad posterior saltadora macrópoda con tibia larga, tarso-metatarso alargado y cuarto dedo dominante. */
    LIMB_PROFILE_MARINE_FLIPPER /**< Aleta propulsora derivada de extremidad tetrápoda (tortugas marinas, cetáceos, pinnípedos). */
} LimbAnatomyProfile;

/**
 * @enum FlipperSurfaceStyle
 * @brief Perfil superficial semántico de aletas marinas (independiente del ID de especie).
 */
typedef enum FlipperSurfaceStyle {
    FLIPPER_SURFACE_AUTO = 0,             /**< Retrocompatible: inferido del integumento. */
    FLIPPER_SURFACE_SMOOTH = 1,           /**< Piel hidrodinámica lisa (p. ej. cetáceos). */
    FLIPPER_SURFACE_REPTILIAN_SCALES = 2  /**< Mosaico poligonal reptiliano dorsal/ventral. */
} FlipperSurfaceStyle;

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
    unsigned featherCount; /**< Opcional v1: número de rémiges primarias/secundarias en alas aviares. */
    float wingAspect; /**< Opcional v1: alargamiento relativo envergadura/cuerda para alas/aletas [0..3]. */
    float wingPointedness; /**< Opcional v1: apuntamiento distal del ala (p. ej. halcón vs búho) [0..1]. */
    float finSweep; /**< Opcional v1: flecha posterior de aleta pectoral/pélvica [-0.6..1.0]. */
    float finCamber; /**< Opcional v1: curvatura hidrodinámica/aerodinámica del perfil [0..0.4]. */
    FlipperSurfaceStyle flipperSurface; /**< Opcional v1: estilo superficial de aleta marina (0=auto, 1=lisa, 2=escamas reptilianas). */
    float flipperScaleSize; /**< Opcional v1: factor de escala/densidad de escamas en aleta [0=defecto, 0.3..3.0]. */
    float flipperScaleIrregularity; /**< Opcional v1: irregularidad poligonal Voronoi de escamas [0..1]. */
    float flipperBorderContrast; /**< Opcional v1: contraste de bordes claros entre escamas [0..1]. */
    float flipperRoughness; /**< Opcional v1: rugosidad superficial de la aleta [0=defecto, 0.15..0.95]. */
} LimbPhenotype;

typedef enum TailArchetype {
    TAIL_ARCHETYPE_TAPERED = 0,
    TAIL_ARCHETYPE_WHIP,
    TAIL_ARCHETYPE_HEAVY,
    TAIL_ARCHETYPE_PREHENSILE,
    TAIL_ARCHETYPE_FINNED,
    TAIL_ARCHETYPE_CUSTOM,
    TAIL_ARCHETYPE_COILED, /**< Espiral cartilaginosa corta con paso lateral y longitud de arco estable. */
    TAIL_ARCHETYPE_PADDLE, /**< Cola ancha y aplanada dorsoventralmente con extremo redondeado. */
    TAIL_ARCHETYPE_FEATHER_FAN /**< Base caudal aviar con abanico superpuesto de rectrices. */
} TailArchetype;

typedef enum CaudalFinType {
    CAUDAL_FIN_NONE = 0,
    CAUDAL_FIN_HOMOCERCAL_LUNATE, /**< Aleta caudal vertical simétrica lunada (atún, peces pelágicos). */
    CAUDAL_FIN_HETEROCERCAL,      /**< Aleta caudal vertical asimétrica con lóbulo superior mayor (escualos). */
    CAUDAL_FIN_HORIZONTAL_FLUKES, /**< Dos lóbulos horizontales con escotadura central (cetáceos). */
    CAUDAL_FIN_ROUNDED_VERTICAL   /**< Aleta caudal vertical pequeña y redondeada (juveniles/signátidos tempranos). */
} CaudalFinType;

typedef struct TailPhenotype {
    float droop; /**< Caída gravitatoria de la cola; cero conserva el eje anfitrión. */
    TailArchetype archetype;
    float length, baseWidth, baseHeight, tipWidth, tipHeight;
    float taperCurve, curvature, development;
    float rootMatchStrength;
    unsigned segmentCount;
    float tipLift; /**< Curvatura vertical distal en reposo, en unidades del cuerpo. */
    CaudalFinType caudalFinType; /**< Opcional v1: propulsor caudal terminal. */
    float caudalSpan; /**< Envergadura total entre puntas de los lóbulos caudales. */
    float caudalChord; /**< Cuerda longitudinal raíz-borde posterior del propulsor caudal. */
    float caudalUpperLobeRatio; /**< Proporción relativa del lóbulo superior/dorsal [0.5..2.0]. */
    float caudalNotchDepth; /**< Profundidad de la horquilla o escotadura central [0..1]. */
    float caudalSweep; /**< Flecha posterior de los lóbulos caudales [0..1.2]. */
    float caudalThickness; /**< Espesor basal del propulsor caudal. */
    float caudalKeelStrength; /**< Quillas laterales del pedúnculo caudal [0..1] (túnidos/escómbridos). */
    Color caudalColor; /**< Color opcional del propulsor caudal (alfa 0 hereda del cuerpo). */
} TailPhenotype;

typedef enum FinFamily {
    FIN_FAMILY_RAYED_PAIRED = 0, /**< Aleta pareada con soporte de radios óseos (teleósteos). */
    FIN_FAMILY_MEDIAN_DORSAL,    /**< Aleta mediana dorsal (radiada o falcada). */
    FIN_FAMILY_MEDIAN_ANAL,      /**< Aleta mediana ventral/anal. */
    FIN_FAMILY_CARTILAGINOUS,    /**< Aleta cartilaginosa rígida y gruesa (escualos). */
    FIN_FAMILY_FLEXIBLE_TISSUE,  /**< Aleta de tejido fibroso sin radios (aleta dorsal cetácea). */
    FIN_FAMILY_CAUDAL_BILOBED    /**< Aleta caudal bilobulada independiente. */
} FinFamily;

typedef enum FinRole {
    FIN_ROLE_PECTORAL = 0,
    FIN_ROLE_PELVIC,
    FIN_ROLE_DORSAL_FIRST,
    FIN_ROLE_DORSAL_SECOND,
    FIN_ROLE_ANAL,
    FIN_ROLE_CAUDAL,
    FIN_ROLE_FINLET_SERIES /**< Serie compacta repetida de pínulas dorsales o anales. */
} FinRole;

typedef enum FinSide {
    FIN_SIDE_CENTER = 0,
    FIN_SIDE_LEFT,
    FIN_SIDE_RIGHT
} FinSide;

/** Descriptor semántico reutilizable para aletas y series de pínulas. */
typedef struct FinPhenotype {
    FinFamily family;
    FinRole role;
    FinSide side;
    float baseChord;     /**< Longitud de inserción a lo largo del eje anfitrión. */
    float span;          /**< Altura o envergadura libre desde la raíz hasta la punta. */
    float thickness;     /**< Espesor máximo en la raíz hidrodinámica. */
    float rootWidth;     /**< Ensanchamiento basal de transición con el tronco. */
    float sweep;         /**< Barrido posterior de la punta respecto a la raíz [-0.4..1.4]. */
    float leadingCurve;  /**< Curvatura del borde de ataque [-0.4..1.0]. */
    float trailingCurve; /**< Concavidad/falcación del borde de salida [-0.5..1.0]. */
    float freeRearTip;   /**< Punta posterior libre extendida tras la base [0..1.5] (2.ª dorsal/anal de escualos). */
    float tipShape;      /**< Forma de la punta [0=aguda, 1=redondeada]. */
    float camber;        /**< Curvatura transversal del perfil [-0.3..0.3]. */
    float dihedral;      /**< Inclinación vertical en radianes para aletas pareadas. */
    float cant;          /**< Inclinación lateral/balanceo en radianes. */
    unsigned rayCount;   /**< Número de radios visibles (0=lisa/cartilaginosa, 4..28=radiada). */
    unsigned finletCount;/**< Cantidad de pínulas cuando role == FIN_ROLE_FINLET_SERIES (0..16). */
    float seriesLength;  /**< Longitud axial ocupada por la serie de pínulas. */
    float development;   /**< Desarrollo ontogenético [0..1]. */
    Color baseColor;     /**< Color proximal de la aleta (alfa 0 hereda del cuerpo). */
    Color tipColor;      /**< Color distal de la aleta (alfa 0 hereda de baseColor). */
} FinPhenotype;

/** Caparazón anatómico reutilizable (espaldar dorsal, plastrón ventral, puente lateral y escudos). */
typedef struct ShellPhenotype {
    float development;      /**< Desarrollo activo [0=ausente, 1=completo]. */
    float length;           /**< Longitud total del espaldar en unidades del cuerpo. */
    float width;            /**< Anchura máxima del espaldar en unidades del cuerpo. */
    float height;           /**< Altura/abombamiento dorsal del espaldar. */
    float dorsalConvexity;  /**< Convexidad de la cúpula dorsal [0.1..1.0]. */
    float heartTaper;       /**< Estrechamiento posterior cordiforme [0..1]. */
    float plastronWidth;    /**< Anchura relativa del plastrón ventral. */
    float plastronLength;   /**< Longitud relativa del plastrón ventral. */
    float plastronDepth;    /**< Descenso ventral del plastrón bajo el eje troncal. */
    float bridgeWidth;      /**< Extensión longitudinal del puente lateral entre aberturas. */
    float shellThickness;   /**< Espesor estructural del borde y pared del caparazón. */
    unsigned costalScutePairs; /**< Pares de escudos costales laterales (p. ej. 4 en Chelonia mydas). */
    unsigned vertebralScutes;  /**< Escudos vertebrales centrales (p. ej. 5). */
    unsigned marginalScutes;   /**< Escudos marginales por lado (p. ej. 11). */
    float scuteRelief;      /**< Relieve de suturas y abombamiento por escudo [0..1]. */
    float cervicalNotch;    /**< Escotadura anterior para cuello y cabeza [0..0.4]. */
    float limbClearance;    /**< Holgura lateral en aberturas de aletas/extremidades [0..0.5]. */
    Color carapaceColor;    /**< Color principal del espaldar dorsal. */
    Color scuteBorderColor; /**< Color de suturas y moteado de escudos. */
    Color plastronColor;    /**< Color pálido del plastrón ventral. */
    Color carapaceDarkColor; /**< Opcional v1: tono pardo/oliva oscuro para moteado por escudo. */
    Color carapaceAmberColor; /**< Opcional v1: tono ámbar/miel para estrías radiales por escudo. */
    Color plastronSeamColor; /**< Opcional v1: tono suave de suturas del plastrón. */
    float carapaceMottling; /**< Opcional v1: intensidad de moteado irregular por escudo [0..1]. */
    float carapaceRadialStrength; /**< Opcional v1: intensidad de estrías radiales curvas [0..1]. */
    float carapaceRoughness; /**< Opcional v1: rugosidad queratinosa del espaldar [0.15..0.95]. */
    float sutureContrast; /**< Opcional v1: contraste/oscurecimiento de suturas dorsales [0..1]. */
    float plastronSeamSoftness; /**< Opcional v1: suavidad de suturas ventrales del plastrón [0..1]. */
    float plastronMottling; /**< Opcional v1: variación orgánica sutil del plastrón [0..1]. */
    float plastronRoughness; /**< Opcional v1: rugosidad queratinosa del plastrón [0.15..0.95]. */
    uint32_t patternSeed; /**< Opcional v1: semilla determinista por escudo. */
} ShellPhenotype;

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

typedef enum FlexibleAppendageProfile {
    FLEXIBLE_PROFILE_PROBOSCIS = 0,
    FLEXIBLE_PROFILE_MUSCULAR_ARM, /**< Brazo hidrostático muscular (cefalópodo) con orientación radial y ventosas opcionales. */
    FLEXIBLE_PROFILE_CEPHALIC_LOBE, /**< Lóbulo cefálico anterior flexible enrollable (batoideo). */
    FLEXIBLE_PROFILE_SIPHON /**< Sifón/embudo tubular direccional. */
} FlexibleAppendageProfile;

/** Radios y longitud en unidades del anfitrión, curvatura en radianes. */
typedef struct FlexibleAppendagePhenotype {
    float length, baseRadius, tipRadius, aspect, taper;
    float initialPitch, restBend;
    FlexibleTerminal terminal;
    FlexibleAppendageProfile profile; /**< Opcional v1; cero conserva probóscide heredada. */
    float yawAngle; /**< Giro lateral/acimutal respecto al eje forward del anclaje (radianes). */
    float coilBias; /**< Tendencia al enrollamiento distal [-2..2]. */
    unsigned suckerRows; /**< Filas longitudinales de ventosas ventrales (0=ninguna, 1..2). */
    unsigned suckerCount; /**< Pares o unidades de ventosas por brazo (0..40). */
    float suckerScale; /**< Radio de ventosa relativo al radio local del brazo [0..1.2]. */
} FlexibleAppendagePhenotype;

typedef enum RepeatedAppendageKind {
    REPEATED_APPENDAGE_NONE = 0,
    REPEATED_APPENDAGE_FILAMENT, /**< Filamentos/tentáculos delgados repetidos en anillo radial. */
    REPEATED_APPENDAGE_FRILLED_RIBBON /**< Brazos orales anchos con bordes rizados/ondulados. */
} RepeatedAppendageKind;

/** Descriptor compacto para geometría repetida con identidades estables por instancia. */
typedef struct RepeatedAppendageGroup {
    uint32_t groupId; /**< Identidad estable del grupo repetido (>0). */
    uint32_t hostModuleInstanceId; /**< Módulo anfitrión (normalmente axial radial). */
    uint16_t hostNodeId; /**< Nodo local anfitrión en el módulo. */
    RepeatedAppendageKind kind;
    unsigned count; /**< Cantidad de instancias (p. ej. 24 tentáculos o 4 brazos orales). */
    float ringRadius; /**< Radio de inserción respecto al eje anfitrión (unidades de receta). */
    float ringOffsetY; /**< Desplazamiento vertical respecto al nodo anfitrión (unidades de receta). */
    float arcStart; /**< Ángulo inicial en radianes. */
    float arcSpan; /**< Arco total en radianes (0 o 2*PI = anillo completo). */
    float length; /**< Longitud nominal en unidades de receta. */
    float lengthVariation; /**< Variación determinista de longitud [0..0.6]. */
    float baseRadius; /**< Semiancho/radio proximal en unidades de receta. */
    float tipRadius; /**< Semiancho/radio distal en unidades de receta. */
    float taper; /**< Exponente de ahusamiento [0.4..3.0]. */
    float ribbonWidth; /**< Anchura de cinta en brazos orales rizados. */
    float ruffleAmplitude; /**< Amplitud de pliegue marginal en cintas rizadas. */
    float ruffleFrequency; /**< Frecuencia espacial de pliegues a lo largo de la cinta. */
    float curlAmplitude; /**< Curvatura orgánica natural a lo largo del apéndice. */
    float waveFrequency; /**< Frecuencia de oscilación/ondulación. */
    float phaseSpread; /**< Desfase angular entre instancias consecutivas. */
    float development; /**< Desarrollo ontogenético [0..1]. */
    Color baseColor; /**< Color proximal. */
    Color tipColor; /**< Color distal. */
    float opacity; /**< Opacidad relativa [0.1..1.0]. */
    uint32_t seed; /**< Semilla determinista para variación orgánica. */
} RepeatedAppendageGroup;

/** @brief Identidad determinista estable de la instancia i dentro de un grupo repetido. */
static inline uint64_t RepeatedAppendage_InstanceId(uint32_t groupId, unsigned instanceIndex) {
    uint64_t h = ((uint64_t)groupId << 32) | (uint64_t)(instanceIndex + 1u);
    h ^= h >> 30;
    h *= UINT64_C(0xbf58476d1ce4e5b9);
    h ^= h >> 27;
    h *= UINT64_C(0x94d049bb133111eb);
    return (h ^ (h >> 31)) | UINT64_C(1);
}

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
    RepeatedAppendageGroup repeatedAppendages[CREATURE_MAX_REPEATED_APPENDAGES];
    size_t repeatedAppendageCount;
    FinPhenotype fins[CREATURE_MAX_FINS];
    size_t finCount;
    ShellPhenotype shell;
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
