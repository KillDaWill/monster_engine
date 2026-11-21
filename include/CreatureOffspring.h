/** @file CreatureOffspring.h
 * @brief Recombinación determinista de recetas, independiente de la interfaz.
 */
#ifndef CREATURE_OFFSPRING_H
#define CREATURE_OFFSPRING_H

#include "Creature.h"
#include "CreatureRecipeIO.h"

enum { CREATURE_OFFSPRING_MAX = 16, CREATURE_OFFSPRING_VERSION = 3 };

typedef struct {
    size_t count;
    uint32_t seed;
    unsigned attempts, repairs;
    float minimumContribution, maximumContribution;
    unsigned proposalsPerOffspring; /**< Cero usa el presupuesto estándar. */
    float regionalVariance, mutationIntensity; /**< Multiplicadores de variación [0,2]. */
} CreatureOffspringConfig;

typedef struct {
    uint32_t seed, mutationSeed;
    unsigned generatorVersion, proposalsEvaluated, proposalsValid;
    HeadRegionalInheritance headGenes;
    float neck, shoulder, thorax, abdomen, pelvis, upperLimb, lowerLimb, foot, limbMass;
    float dorsal, ventral, facial, extremities, tailColor, eyeColor, pattern, fur, relief;
    float shapeDiversity, appearanceDiversity, quality;
    float anatomicalCoherence; /**< Soporte y continuidad geométrica, de cero a uno. */
    float global, body, head, limbs, tail, surface, size;
    unsigned scaffoldParent, headParent, limbParent, tailParent, surfaceParent;
    unsigned grafts, omitted, repairs, attempts;
    unsigned unsupportedGrafts, constrainedGrafts, unselectedGrafts, removedAnchors;
    float targetWeight, resolvedHeadWeight, headError, bodyError, limbError;
    unsigned fitEvaluations;
    bool hasMorphTarget, simplifiedHeadBasis;
    MorphologicalSignature signature;
} OffspringReport;

typedef struct CreatureOffspringBatch CreatureOffspringBatch;
typedef struct OffspringCandidate OffspringCandidate;
typedef bool (*CreatureOffspringCancel)(void *context);
typedef void (*CreatureOffspringProgress)(void *context, size_t completed);

/** @brief Configuración inicial con aportes globales entre 35 y 65 por ciento. */
CreatureOffspringConfig CreatureOffspring_Default(size_t count, uint32_t seed);
/** @brief Genera un lote completo o NULL. El lote posee copias de los padres. */
CreatureOffspringBatch *CreatureOffspring_Generate(const CreatureRecipe *, const CreatureRecipe *,
    CreatureOffspringConfig, CreatureOffspringCancel, void *, char *error, size_t capacity);
/** @brief Variante con notificación de candidatos validados. */
CreatureOffspringBatch *CreatureOffspring_GenerateProgress(const CreatureRecipe *, const CreatureRecipe *,
    CreatureOffspringConfig, CreatureOffspringCancel, CreatureOffspringProgress, void *, char *, size_t);
/** @brief Prepara una sustitución sin modificar el lote; admite lecturas concurrentes.
 * El llamador conserva el lote vivo y libera el resultado si lo descarta. */
OffspringCandidate *CreatureOffspring_PrepareReroll(const CreatureOffspringBatch *, size_t index,
    CreatureOffspringCancel, void *, char *error, size_t capacity);
/** @brief Publica el resultado preparado para este mismo lote e índice y adquiere su propiedad. */
bool CreatureOffspring_AcceptReroll(CreatureOffspringBatch *, size_t index, OffspringCandidate *);
/** @brief Libera el reemplazo preparado que el lote todavía no posee. */
void CreatureOffspring_DiscardReroll(OffspringCandidate *);
/** @brief Reemplaza un candidato (índice desde cero) al validar el resultado. */
bool CreatureOffspring_Reroll(CreatureOffspringBatch *, size_t index,
    CreatureOffspringCancel, void *, char *error, size_t capacity);
/** @brief Destruye el lote y todos sus documentos. */
void CreatureOffspring_Destroy(CreatureOffspringBatch *);
/** @return Cantidad publicada, cero si no hay lote. */
size_t CreatureOffspring_Count(const CreatureOffspringBatch *);
/** @return Semilla del lote, cero si no hay lote. */
uint32_t CreatureOffspring_Seed(const CreatureOffspringBatch *);
/** @return Instantánea prestada del padre 0 o 1, válida durante la vida del lote. */
const CreatureRecipe *CreatureOffspring_Parent(const CreatureOffspringBatch *, unsigned);
/** @return Receta e informe prestados. El índice comienza en cero.
 * Ambos punteros son válidos hasta reemplazar el candidato o destruir el lote. */
const CreatureRecipe *CreatureOffspring_Get(const CreatureOffspringBatch *, size_t,
    const OffspringReport **report);

#endif
