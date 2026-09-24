/** @file Fur.h
 * @brief Campo de fibras, raíces persistentes y óptica; núcleo sin dependencias gráficas.
 */
#ifndef MONSTER_FUR_H
#define MONSTER_FUR_H
#include "Mesh.h"
/** @brief Raíz vinculada a un triángulo; la pose no cambia su identidad. */
typedef struct FurRoot {
    uint32_t triangleIndex;
    Vector3 barycentric;
    uint32_t randomSeed;
} FurRoot;
/** @brief Distribución por área del dominio canónico. */
typedef struct FurRootSet { FurRoot* roots; size_t count; float area; } FurRootSet;
/** @brief Posición relativa y derivada analítica de la curva. */
typedef struct FurCurve { Vector3 offset,derivative; float radius; } FurCurve;
/** @brief Genera raíces deterministas, con densidad máxima y límite de memoria explícitos.
 * @param set Destino inicializado a cero. @param mesh Malla mapeada.
 * @param seed Identidad del fenotipo. @param perArea Candidatos por unidad canónica cuadrada.
 * @param limit Límite de representación. @return Éxito. */
bool FurRootSet_Build(FurRootSet* set,const Mesh* mesh,uint32_t seed,float perArea,size_t limit);
/** @brief Libera raíces. @param set Conjunto. */
void FurRootSet_Free(FurRootSet* set);
/** @brief Evalúa la curva compartida con GLSL. @param fur Fenotipo normalizado.
 * @param normal Normal unitaria. @param flow Flujo tangente unitario.
 * @param random Variación estable [0,1]. @param h Altura [0,1]. @return Curva. */
FurCurve Fur_EvaluateCurve(const FurPhenotype* fur,Vector3 normal,Vector3 flow,float random,float h);
/** @brief Alfa de Beer-Lambert. @param density Extinción. @param occupancy Fracción ocupada.
 * @param deltaH Espesor integrado. @param cosine Ángulo con la normal. @return Alfa. */
float Fur_OpticalAlpha(float density,float occupancy,float deltaH,float cosine);
/** @brief Resolución continua acotada de conchas. @param pixels Longitud proyectada.
 * @param grazing Fracción rasante [0,1]. @param quality Calidad positiva. @return Resolución [0,32]. */
float Fur_ShellResolution(float pixels,float grazing,float quality);
/** @brief Hash reproducible para subconjuntos anidados. @param seed Identidad. @return Valor [0,1). */
float Fur_Random(uint32_t seed);
/** @brief Alturas anidadas y pesos de integración conservativos. @return Número de muestras (0..32). */
int Fur_ShellSamples(float resolution,float samples[32][2]);
#endif
