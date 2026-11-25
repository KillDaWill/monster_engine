#ifndef MONSTER_SCENE_LIGHTING_H
#define MONSTER_SCENE_LIGHTING_H

#include "RenderInterfaces.h"
#include <stdbool.h>

/** @brief Estado GL de una escena de presentación. Propiedad del renderizador de entorno. */
typedef struct SceneLighting {
    unsigned frameBuffer, shadowFbo, shadowDepth, staticFbo, staticDepth;
    int staticSize;
    bool staticValid;
    unsigned staticEpoch;
    float staticMatrix[16];
    int shadowSize;
    bool active, depthPass;
    int previousBlock, previousTexture, previousSampler;
    bool previousSRGB;
    int previousDrawFbo, previousReadFbo, previousMatrixMode;
    unsigned compositionFbo, compositionColor, compositionDepth;
    int compositionWidth, compositionHeight, compositionSamples, compositionDepthFormat;
    int outputDrawFbo, outputReadFbo, outputBounds[4], outputEncoding;
    bool composite;
    float viewToWorld[16], worldToShadow[16];
    float depthBias, shadowTexel;
    unsigned presentationSeed;
    bool periodicMaterials, cheapShadows;
    Vector3 sun;
} SceneLighting;

typedef enum SceneShadowQuality {
    SCENE_SHADOW_AUTO = -1,
    SCENE_SHADOW_OFF,
    SCENE_SHADOW_LOW,
    SCENE_SHADOW_MEDIUM,
    SCENE_SHADOW_HIGH
} SceneShadowQuality;

/** @brief Reserva y vincula el bloque neutro que conserva la iluminación de estudio. */
unsigned SceneLighting_CreateStudio(void);
void SceneLighting_BindStudio(unsigned buffer);
void SceneLighting_LinkProgram(unsigned program);
unsigned SceneLighting_LinkDepth(unsigned vertexShader);
void SceneLighting_Destroy(SceneLighting *scene);
bool SceneLighting_Begin(SceneLighting *scene, const ICamera *camera);
void SceneLighting_Apply(SceneLighting *scene, const ICamera *camera, bool useShadow);
void SceneLighting_End(SceneLighting *scene);

/** @brief Inicia una pasada de profundidad direccional acotada por los límites del terreno. */
bool SceneLighting_BeginShadow(SceneLighting *scene, const ICamera *camera,
                               Vector3 minimum, Vector3 maximum, SceneShadowQuality quality);
void SceneLighting_EndShadow(SceneLighting *scene);
/** @brief Selecciona el destino estático si su proyección y época requieren actualización. */
bool SceneLighting_BeginStatic(SceneLighting *scene, unsigned epoch);
void SceneLighting_EndStatic(SceneLighting *scene, unsigned epoch, bool updated);
bool SceneLighting_SetSun(SceneLighting *scene, float azimuthDegrees, float elevationDegrees);
bool SceneLighting_ParseShadows(const char *name, SceneShadowQuality *quality);

#endif
