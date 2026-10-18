/**
 * @file OpenGLRenderer.h
 * @brief Implementación OpenGL del backend gráfico agnóstico MonsterRenderer.
 * @author Monster Engine Team
 * @date 2026
 */

#ifndef MONSTER_OPENGL_RENDERER_H
#define MONSTER_OPENGL_RENDERER_H

#include "RenderInterfaces.h"
#include "Mesh.h"
#include "MorphHandoff.h"
#include "MonsterSDF.h"
#include "GpuSDFSurfaceCache.h"
#include "AnatomyDeformer.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Comprueba OpenGL 4.3 y límites de cómputo de la GPU actual. */
bool OpenGLRenderer_SupportsComputeSurface(Renderer3D*);
/** @brief Publica skinning GPU de una malla vinculada con dominio de superficie; NULL desactiva la selección.
 * @param renderer Backend. @param mesh Malla propietaria. @param binding Bases e influencias.
 * @param skeleton Esqueleto. @param pose Pose actual. @param generation Generación de vinculación.
 * @return false ante incompatibilidad o fallo; no sustituye la malla por otra representación. */
bool OpenGLRenderer_SetMeshSkinning(Renderer3D* renderer,const Mesh* mesh,const AnatomyDeformer* binding,
    const Skeleton* skeleton,const SkeletonPose* pose,uint64_t generation);
/** @brief Compara todos los vértices GPU publicados con el skinning CPU de referencia.
 * @param renderer Backend. @param mesh Malla dibujada. @param binding Vinculación.
 * @param skeleton Esqueleto. @param pose Misma pose. @param positionError Error máximo de posición.
 * @param normalError Error máximo de normal. @return Resultado de la lectura de diagnóstico. */
bool OpenGLRenderer_ValidateMeshSkinning(Renderer3D* renderer,const Mesh* mesh,const AnatomyDeformer* binding,
    const Skeleton* skeleton,const SkeletonPose* pose,float* positionError,float* normalError);
/** @brief Extrae y dibuja directamente el SDF actual; sin readback normal. */
bool OpenGLRenderer_RenderGpuSDFSurface(Renderer3D*,const MonsterSDF*,const GpuSDFSurfaceGrid*,const ICamera*);
/** @brief Actualiza bricks afectados y dibuja páginas GPU persistentes.
 * @param cache Control espacial puro C de la rejilla fija.
 * @return false ante error; nunca cambia a un backend alternativo.
 */
bool OpenGLRenderer_RenderGpuSparseSDFSurface(Renderer3D*,const MonsterSDF*,GpuSDFSurfaceCache*,const ICamera*);
/** @brief Transporta la superficie GPU de reposo mediante la pose actual, sin remuestrear el SDF.
 * @param renderer Backend propietario. @param monster Anatomía y animación actuales; NULL desactiva la pose.
 * @return false si la pose no corresponde a la anatomía de reposo.
 */
bool OpenGLRenderer_SetGpuSurfacePose(Renderer3D* renderer,const struct Monster* monster);
/** @brief Comprueba el shader real de pose craneal/mandibular mediante captura GPU.
 * Sólo diagnóstico: no usar en el camino normal de cada frame.
 * @param renderer Contexto activo. @param monster Criatura con pose publicada.
 * @return true si las muestras conservan su articulación y normales válidas. */
bool OpenGLRenderer_ValidateCranialPose(Renderer3D* renderer,const struct Monster* monster);
/** @brief Consulta tiempos GPU recogidos asíncronamente, sin esperar. */
GpuSDFSurfaceStats OpenGLRenderer_GetGpuSDFSurfaceStats(const Renderer3D*);
/** @brief Guarda metadatos de publicación para una captura diagnóstica; descarga explícita.
 * @param renderer Backend. @param cache Control espacial. @param path Archivo JSON.
 * @return false ante error de lectura o escritura.
 */
bool OpenGLRenderer_SaveSparseSDFDiagnostics(Renderer3D* renderer,const GpuSDFSurfaceCache* cache,const char* path);
/** @brief Exporta firmas ordenadas de triángulos y aristas; descarga sólo de diagnóstico.
 * @param renderer Backend. @param path JSON de equivalencia completa.
 * @return false ante error.
 */
bool OpenGLRenderer_ExportGpuSurfaceSignature(Renderer3D* renderer,const char* path);
/** @brief Auditoría explícita de salida GPU; no se invoca en presentación normal. */
bool OpenGLRenderer_ValidateGpuSurface(Renderer3D*,const MonsterSDF*);
/** @brief Diagnóstico explícito de contención CPU/GLSL para las recetas de pabellón.
 * @param renderer Backend propietario de buffers de diagnóstico.
 * @param sdf Escena actual con parámetros anatómicos resueltos.
 * @param error Diferencia máxima observada entre extremos escalares CPU/GLSL.
 * @return true si los intervalos contienen las muestras cruzadas de ambos evaluadores.
 */
bool OpenGLRenderer_ValidatePinnaIntervals(Renderer3D* renderer,const MonsterSDF* sdf,float* error);
/** @brief Paridad de diagnóstico; readback explícito de muestras compute. */
bool OpenGLRenderer_ValidateComputeSDF(Renderer3D*,const MonsterSDF*,const Vector3*,size_t,float*);



/**
 * @brief Crea e inicializa una instancia de Renderer3D respaldada por el pipeline de OpenGL.
 * @param camera Puntero a la cámara 3D.
 * @return Estructura Renderer3D con sus callbacks apuntando a las funciones de OpenGL.
 */
Renderer3D OpenGLRenderer_Create(ICamera* camera);

/**
 * @brief Configura la matriz de perspectiva e iluminación en OpenGL.
 * @param camera Puntero a la cámara.
 * @param width Ancho del viewport.
 * @param height Alto del viewport.
 */
void OpenGLRenderer_SetupCamera(ICamera* camera, int width, int height);

/**
 * @brief Configura la matriz de proyección ortogonal (axonométrica/isométrica) e iluminación en OpenGL.
 * @param camera Puntero a la cámara con posición, objetivo y vector vertical.
 * @param orthoHeight Altura visible total del volumen ortogonal en unidades mundiales.
 * @param width Ancho del viewport.
 * @param height Alto del viewport.
 */
void OpenGLRenderer_SetupCameraOrtho(ICamera* camera, float orthoHeight, int width, int height);

/**
 * @brief Dibuja una malla 3D (Mesh) directamente mediante el pipeline de OpenGL.
 * @param mesh Puntero a la malla 3D a renderizar.
 */
void OpenGLRenderer_RenderMesh(const Mesh* mesh);

/** @brief Excluye de malla, conchas y pelos las cajas con representación SDF.
 * @param renderer Backend propietario.
 * @param bounds Cajas mundiales de propiedad SDF; NULL desactiva el recorte.
 * @param count Cantidad de cajas, máximo 32.
 */
/** @brief Carga propiedad anatómica compartida; pass=-1 para malla/SDF completo.
 * @return false ante exceso de soporte, sin truncamiento silencioso.
 */
/** @brief Instala dominio material anatómico canónico para mesh y SDF. */
bool OpenGLRenderer_SetMorphSurfaceDomain(Renderer3D*,const struct Monster*,const struct Monster*);
bool OpenGLRenderer_SetMorphHandoffs(Renderer3D*,const MorphHandoffSet*,int pass);
void OpenGLRenderer_SetMeshClipRegions(Renderer3D* renderer,const AABB3D* bounds,size_t count);

/**
 * @brief Activa/desactiva el modo wireframe (contornos de triángulos).
 *
 * El cambio se aplica solo durante el renderizado de mallas y se restaura el
 * modo de polígono previo (sin fuga de estado OpenGL).
 * @param renderer Puntero al renderizador 3D.
 * @param enabled true para wireframe, false para relleno.
 */
void OpenGLRenderer_SetWireframe(Renderer3D* renderer, bool enabled);

/** @brief Configura el trazador de referencia, preservando la ruta heredada.
 * @param renderer Backend propietario del estado.
 * @param enabled Activa escena completa y detección de cruce con raíz acotada.
 * @param maxStep Paso máximo opcional; cero mantiene el avance por el campo.
 * @param steps Límite de iteraciones del diagnóstico, entre 256 y 65536.
 */
void OpenGLRenderer_SetSDFReference(Renderer3D* renderer,bool enabled,float maxStep,unsigned steps);
/** @brief Captura profundidad lineal de diagnóstico en PFM.
 * @param path Ruta de salida.
 * @param width Anchura del framebuffer.
 * @param height Altura del framebuffer.
 * @param camera Cámara que define los planos cercano y lejano.
 * @return true si se leyó y escribió el framebuffer completo.
 */
bool OpenGLRenderer_SaveDepth(const char* path,int width,int height,const ICamera* camera);
/** @brief Guarda el framebuffer posterior en PPM, invirtiendo el eje Y.
 * @param path Ruta de salida.
 * @param width Ancho del viewport.
 * @param height Alto del viewport.
 * @return true si se leyó y escribió la imagen completa.
 */
bool OpenGLRenderer_SavePPM(const char* path,int width,int height);

/**
 * @brief Libera los recursos del contexto del renderizador OpenGL y limpia sus callbacks.
 * @param renderer Puntero al renderizador 3D.
 */
void OpenGLRenderer_Destroy(Renderer3D* renderer);

/** @brief Dibuja directamente el campo compilado sin reconstruir triángulos.
 * @param renderer Backend OpenGL propietario de los recursos GPU.
 * @param sdf Campo compilado correspondiente al fotograma actual.
 * @param camera Cámara actual.
 * @param width Anchura del framebuffer.
 * @param height Altura del framebuffer.
 * @return false si el backend no admite GLSL 330 o falla la preparación.
 */
bool OpenGLRenderer_RenderSDF(Renderer3D* renderer,const MonsterSDF* sdf,
    const ICamera* camera,int width,int height);

/** @brief Dibuja el campo SDF acotado a una región AABB espacial mediante scissoring GPU y filtrado de tiles.
 * @param renderer Backend OpenGL.
 * @param sdf Campo compilado.
 * @param camera Cámara de proyección.
 * @param width Anchura del framebuffer.
 * @param height Altura del framebuffer.
 * @param regionBounds Bounding box que acota la región local a renderizar.
 * @return true si se renderizó con éxito.
 */
/** @brief Traza una unión de cajas locales en un solo pase, conservando el SDF completo. */
bool OpenGLRenderer_RenderSDFRegions(Renderer3D*,const MonsterSDF*,const ICamera*,int,int,
    const AABB3D*,size_t);
bool OpenGLRenderer_RenderSDFRegion(Renderer3D* renderer,const MonsterSDF* sdf,
    const ICamera* camera,int width,int height,AABB3D regionBounds);

/** @brief Inicia un fotograma SDF con resolución espacial adaptada al presupuesto GPU.
 * @param renderer Backend propietario del framebuffer y las consultas temporales.
 * @param camera Cámara actual; se conserva su proyección y relación de aspecto.
 * @param width Anchura de presentación.
 * @param height Altura de presentación.
 * @param adaptive Ajustar gradualmente la resolución al presupuesto GPU para mantener >= 30 FPS (100% entre 30 y 60 Hz).
 * @return true si se preparó el framebuffer.
 */
bool OpenGLRenderer_BeginSDFFrame(Renderer3D* renderer,ICamera* camera,int width,int height,bool adaptive);
/** @brief Presenta el fotograma SDF y recoge tiempos GPU sin bloquear. */
void OpenGLRenderer_EndSDFFrame(Renderer3D* renderer,int width,int height);
/** @brief Última duración GPU completada, en milisegundos. */
float OpenGLRenderer_GetSDFGpuMs(const Renderer3D* renderer);
/** @brief Escala espacial actual del framebuffer SDF respecto a la ventana. */
float OpenGLRenderer_GetSDFResolutionScale(const Renderer3D* renderer);

/** @brief Compara en GPU y CPU puntos del último campo dibujado por RenderSDF.
 * @param renderer Backend que acaba de recibir el snapshot sdf.
 * @param sdf El mismo snapshot compilado, sin modificar desde RenderSDF.
 * @param points Puntos mundiales de comprobación.
 * @param count Cantidad de puntos, entre 1 y 4096.
 * @param maxError Salida: máximo error absoluto de distancia.
 * @return true si se ejecutó la lectura GPU; el llamador comprueba la tolerancia.
 */
bool OpenGLRenderer_ValidateSDF(Renderer3D* renderer,const MonsterSDF* sdf,
    const Vector3* points,size_t count,float* maxError);

/** @brief Espera a la GPU para medir el coste real en validaciones de rendimiento. */
void OpenGLRenderer_Finish(void);
/** @brief Selecciona visualización de superficie (0 normal, 1..9 diagnóstico). */
void OpenGLRenderer_SetSurfaceDebug(Renderer3D* renderer,int mode);
/** @brief Compila y verifica el programa; false permite detectar fallback heredado. */
/** @brief Selecciona representaciones y fuerza 1..32 conchas (0=automático). */
void OpenGLRenderer_SetFurQuality(Renderer3D*,bool shells,bool guards,int forcedShells);
bool OpenGLRenderer_SurfaceReady(Renderer3D* renderer);
/** @brief Consume y comunica errores GL pendientes en validaciones. */
bool OpenGLRenderer_CheckErrors(void);

/** @brief Métricas acumuladas de residencia, transferencia y dibujo de mallas. */
typedef struct OpenGLMeshPerformanceStats {
    uint64_t uploadCount,cacheHitCount,totalBytesUploaded;
    double lastUploadMs;
    float gpuRenderMs;
    uint64_t gpuTimingGeneration; /**< Frame 1-based de la última consulta completada. */
    uint64_t furRootBuilds;
    size_t guardCandidates;
    int furShells;
    float furPixels;
} OpenGLMeshPerformanceStats;
/** @brief Obtiene métricas del renderer de mallas sin sincronizar la GPU. */
OpenGLMeshPerformanceStats OpenGLRenderer_GetMeshPerformanceStats(const Renderer3D* renderer);

/** @brief Ventana opaca para demos sin dependencias SDL/GL en sus fuentes. */
typedef struct OpenGLDemoWindow OpenGLDemoWindow;
typedef struct OpenGLDemoInput {
    bool quit,togglePause,toggleDebug;
    int cameraForward,cameraRight; /**< I/K y J/L mantenidas para desplazar cámara. */
    int view;
    int surfaceSelect,surfaceAdjust;
    bool surfaceSeed,surfacePigment,surfaceDebugNext,surfaceToggle;
    int digitKey; /**< 0..9 si se pulsa una tecla numérica, o -1 */
    bool keyR;     /**< true si se pulsa la tecla 'R' */
    bool keyM, keyA; /**< Comparación monocroma y anatomía. */
    bool keyG;     /**< true si se pulsa la tecla 'G' */
    bool keyF;     /**< true si se pulsa la tecla 'F' (fur toggle) */
    bool keyT;     /**< true si se pulsa la tecla 'T' (mutación de criatura) */
    int furLengthAdjust;  /**< +1 o -1 con [ / ] */
    int furDensityAdjust; /**< +1 o -1 con ; / ' */
} OpenGLDemoInput;
/** @brief Crea contexto para demos; NULL si falla. */
/** @brief Crea una ventana con MSAA solicitado y fallback sin MSAA. */
OpenGLDemoWindow* OpenGLDemoWindow_CreateMSAA(const char* title,int width,int height,int samples);
OpenGLDemoWindow* OpenGLDemoWindow_Create(const char* title,int width,int height);
/** @brief Consulta entrada: Escape, espacio, D y teclas 1..4. */
OpenGLDemoInput OpenGLDemoWindow_Poll(OpenGLDemoWindow* window);
/** @brief Presenta framebuffer. */
void OpenGLDemoWindow_Swap(OpenGLDemoWindow* window);
/** @brief Destruye contexto y ventana. */
void OpenGLDemoWindow_Free(OpenGLDemoWindow* window);
/** @brief Actualiza el título de la ventana. */
void OpenGLDemoWindow_SetTitle(OpenGLDemoWindow* window, const char* title);
/** @brief Reloj monótono en segundos. */
double OpenGLDemoWindow_Time(void);
/** @brief Duerme hasta completar el periodo del fotograma; sin espera activa ni recuperación en ráfaga.
 * @param frameStart Inicio monótono del fotograma. @param maxFPS Límite positivo de frecuencia.
 * @return Segundos de espera efectiva. */
double OpenGLDemoWindow_LimitFPS(double frameStart,double maxFPS);
/** @brief Líneas de depuración; no requiere recrear mallas. */
void OpenGLRenderer_DebugLine(Vector3 a,Vector3 b,Color color);
/** @brief Apila una traslación en la matriz de modelo para visualización desacoplada de cuadrícula. */
void OpenGLRenderer_PushModelTranslation(float x, float y, float z);
/** @brief Traslación, giro y escala uniforme de un espécimen completo. */
void OpenGLRenderer_ModelTransform(Vector3 translation, float yawDegrees, float scale, Vector3 center);
/** @brief Restaura la matriz de modelo previa. */
void OpenGLRenderer_PopModelMatrix(void);
/** @brief Dibuja un indicador superpuesto (HUD) en 2D con los FPS y tiempo de fotograma. */
void OpenGLRenderer_DrawHUD(int width, int height, float fps, float frameTimeMs);
/** @brief Panel de texto ASCII multilínea para estadísticas, sin dependencias en demos. */
void OpenGLRenderer_DrawTextPanel(int width,int height,int x,int y,const char *text);
/** @brief Historia de valores cronológicos con escala vertical automática. */
void OpenGLRenderer_DrawHistory(int width,int height,int x,int y,int plotWidth,int plotHeight,const float *values,unsigned count);

#ifdef __cplusplus
}
#endif

#endif // MONSTER_OPENGL_RENDERER_H
