#include "OpenGLRenderer.h"
#include "Monster.h"

#include <GL/gl.h>
#include <GL/glu.h>

#include <math.h>


/* ============================================================
 * FRAME
 * ============================================================ */

static void OpenGL_BeginFrame(MonsterRenderer* self) {
    (void)self;

    glClear(
        GL_COLOR_BUFFER_BIT |
        GL_DEPTH_BUFFER_BIT
    );

    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
}


static void OpenGL_EndFrame(MonsterRenderer* self) {
    (void)self;

    /*
     * La presentación del buffer la gestiona la ventana
     * (SDL2 / GLFW / GLUT).
     */
}


/* ============================================================
 * UTILIDADES
 * ============================================================ */

static Color ScaleColor(Color color, float factor) {
    Color result;

    float r = (float)color.r * factor;
    float g = (float)color.g * factor;
    float b = (float)color.b * factor;

    result.r = (GLubyte)fminf(255.0f, fmaxf(0.0f, r));
    result.g = (GLubyte)fminf(255.0f, fmaxf(0.0f, g));
    result.b = (GLubyte)fminf(255.0f, fmaxf(0.0f, b));
    result.a = color.a;

    return result;
}


/* ============================================================
 * ESFERA
 * ============================================================ */

static void DrawSphereGL(
    float radius,
    int slices,
    int stacks
) {
    for (int i = 0; i < stacks; ++i) {

        float lat0 =
            M_PI * (-0.5f + (float)i / (float)stacks);

        float z0 = sinf(lat0);
        float r0 = cosf(lat0);


        float lat1 =
            M_PI * (-0.5f + (float)(i + 1) / (float)stacks);

        float z1 = sinf(lat1);
        float r1 = cosf(lat1);


        glBegin(GL_QUAD_STRIP);

        for (int j = 0; j <= slices; ++j) {

            float lng =
                2.0f * M_PI *
                (float)j /
                (float)slices;

            float x = cosf(lng);
            float y = sinf(lng);


            glNormal3f(
                x * r0,
                y * r0,
                z0
            );

            glVertex3f(
                x * r0 * radius,
                y * r0 * radius,
                z0 * radius
            );


            glNormal3f(
                x * r1,
                y * r1,
                z1
            );

            glVertex3f(
                x * r1 * radius,
                y * r1 * radius,
                z1 * radius
            );
        }

        glEnd();
    }
}


/* ============================================================
 * ESFERA DEL RENDERER
 * ============================================================ */

static void OpenGL_RenderSphere(
    MonsterRenderer* self,
    Vector3 center,
    float radius,
    Color color
) {
    (void)self;

    glPushMatrix();

    glTranslatef(
        center.x,
        center.y,
        center.z
    );

    glColor4ub(
        color.r,
        color.g,
        color.b,
        color.a
    );

    DrawSphereGL(
        radius,
        16,
        16
    );

    glPopMatrix();
}


/* ============================================================
 * CÁPSULAS
 * ============================================================ */

static void OpenGL_RenderCapsule(
    MonsterRenderer* self,
    Vector3 p1,
    Vector3 p2,
    float r1,
    float r2,
    Color color
) {
    (void)self;


    /*
     * Nodo inicial.
     */
    OpenGL_RenderSphere(
        self,
        p1,
        r1,
        color
    );


    /*
     * Dirección hacia el segundo punto.
     */
    Vector3 dir =
        Vec3_Sub(p2, p1);

    float len =
        Vec3_Length(dir);


    if (len <= 0.0001f) {
        return;
    }


    glPushMatrix();

    glTranslatef(
        p1.x,
        p1.y,
        p1.z
    );


    /*
     * Rotamos el cilindro, inicialmente orientado en Z,
     * para apuntar hacia p2.
     */
    Vector3 v =
        Vec3_Normalize(dir);


    float ax = -v.y;
    float ay = v.x;

    float angle =
        acosf(v.z) *
        180.0f /
        M_PI;


    if (fabsf(v.z - 1.0f) < 0.001f) {

        /*
         * Ya apunta en +Z.
         */

    } else if (fabsf(v.z + 1.0f) < 0.001f) {

        glRotatef(
            180.0f,
            1.0f,
            0.0f,
            0.0f
        );

    } else {

        glRotatef(
            angle,
            ax,
            ay,
            0.0f
        );
    }


    glColor4ub(
        color.r,
        color.g,
        color.b,
        color.a
    );


    const int slices = 16;


    glBegin(GL_QUAD_STRIP);

    for (int i = 0; i <= slices; ++i) {

        float theta =
            (float)i *
            2.0f *
            M_PI /
            (float)slices;


        float nx =
            cosf(theta);

        float ny =
            sinf(theta);


        glNormal3f(
            nx,
            ny,
            0.0f
        );


        glVertex3f(
            nx * r1,
            ny * r1,
            0.0f
        );


        glVertex3f(
            nx * r2,
            ny * r2,
            len
        );
    }

    glEnd();

    glPopMatrix();


    /*
     * Nodo final.
     */
    OpenGL_RenderSphere(
        self,
        p2,
        r2,
        color
    );
}


/* ============================================================
 * OJOS
 * ============================================================ */

static void OpenGL_RenderEye(
    MonsterRenderer* self,
    Vector3 pos,
    Vector3 rot,
    Vector3 scale,
    Color scleraColor,
    Color pupilColor,
    float pupilScale
) {
    (void)self;


    glPushMatrix();


    glTranslatef(
        pos.x,
        pos.y,
        pos.z
    );


    glRotatef(
        rot.x,
        1.0f,
        0.0f,
        0.0f
    );

    glRotatef(
        rot.y,
        0.0f,
        1.0f,
        0.0f
    );

    glRotatef(
        rot.z,
        0.0f,
        0.0f,
        1.0f
    );


    /*
     * --------------------------------------------------------
     * Esclerótica.
     * --------------------------------------------------------
     */

    glPushMatrix();


    glScalef(
        scale.x * 0.5f,
        scale.y * 0.5f,
        scale.z * 0.5f
    );


    glColor4ub(
        scleraColor.r,
        scleraColor.g,
        scleraColor.b,
        scleraColor.a
    );


    DrawSphereGL(
        1.0f,
        16,
        16
    );


    glPopMatrix();


    /*
     * --------------------------------------------------------
     * Pupila.
     * --------------------------------------------------------
     */

    glPushMatrix();


    float pupilZ =
        scale.z * 0.45f;


    float pScale =
        pupilScale * 0.5f;


    glTranslatef(
        0.0f,
        0.0f,
        pupilZ
    );


    glScalef(
        scale.x * pScale,
        scale.y * pScale,
        scale.z * 0.15f
    );


    glColor4ub(
        pupilColor.r,
        pupilColor.g,
        pupilColor.b,
        pupilColor.a
    );


    DrawSphereGL(
        1.0f,
        12,
        12
    );


    glPopMatrix();

    glPopMatrix();
}


/* ============================================================
 * GEOMETRÍA DE BOCA / CAVIDADES REALES
 * ============================================================ */

/*
 * Las bocas NO se dibujan como elipses superpuestas ni mediante
 * una máscara de stencil. En su lugar se modifica directamente la
 * geometría del elipsoide que representa la pieza del cuerpo.
 *
 * Cada vértice de la superficie se proyecta al sistema local de la
 * boca. Si cae dentro de la abertura elíptica, se desplaza hacia el
 * interior siguiendo el eje -Z local de la boca.
 *
 * El perfil usado es el de una semielipse:
 *
 *      depthFactor = sqrt(1 - r^2)
 *
 * donde r=0 es el centro de la boca y r=1 el borde. Esto produce
 * paredes claramente cóncavas y una transición continua con la piel.
 */

typedef struct {
    Vector3 position;
    Vector3 baseNormal;
    Color color;
    int insideMouth;
} MouthSurfaceSample;


typedef struct {
    Vector3 right;
    Vector3 up;
    Vector3 forward;
} MouthBasis;


static Vector3 GLR_Vec3(
    float x,
    float y,
    float z
) {
    Vector3 v;
    v.x = x;
    v.y = y;
    v.z = z;
    return v;
}


static Vector3 GLR_Add(
    Vector3 a,
    Vector3 b
) {
    return GLR_Vec3(
        a.x + b.x,
        a.y + b.y,
        a.z + b.z
    );
}


static Vector3 GLR_Sub(
    Vector3 a,
    Vector3 b
) {
    return GLR_Vec3(
        a.x - b.x,
        a.y - b.y,
        a.z - b.z
    );
}


static Vector3 GLR_Scale(
    Vector3 v,
    float s
) {
    return GLR_Vec3(
        v.x * s,
        v.y * s,
        v.z * s
    );
}


static float GLR_Dot(
    Vector3 a,
    Vector3 b
) {
    return
        a.x * b.x +
        a.y * b.y +
        a.z * b.z;
}


static Vector3 GLR_Cross(
    Vector3 a,
    Vector3 b
) {
    return GLR_Vec3(
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    );
}


static float GLR_Length(
    Vector3 v
) {
    return sqrtf(
        v.x * v.x +
        v.y * v.y +
        v.z * v.z
    );
}


static Vector3 GLR_Normalize(
    Vector3 v
) {
    float len = GLR_Length(v);

    if (len <= 0.000001f) {
        return GLR_Vec3(0.0f, 0.0f, 1.0f);
    }

    return GLR_Scale(v, 1.0f / len);
}


static float GLR_Clamp01(
    float value
) {
    return fmaxf(0.0f, fminf(1.0f, value));
}


static float GLR_DegToRad(
    float degrees
) {
    return degrees * (float)M_PI / 180.0f;
}


/*
 * Reproduce la transformación generada por:
 *
 *     glRotatef(rot.x, 1, 0, 0);
 *     glRotatef(rot.y, 0, 1, 0);
 *     glRotatef(rot.z, 0, 0, 1);
 *
 * En OpenGL clásico las matrices se postmultiplican, por lo que sobre
 * el vector se aplica primero Z, después Y y finalmente X.
 */
static Vector3 GLR_RotateXYZ(
    Vector3 v,
    Vector3 rotationDegrees
) {
    float rx = GLR_DegToRad(rotationDegrees.x);
    float ry = GLR_DegToRad(rotationDegrees.y);
    float rz = GLR_DegToRad(rotationDegrees.z);

    float cx = cosf(rx);
    float sx = sinf(rx);
    float cy = cosf(ry);
    float sy = sinf(ry);
    float cz = cosf(rz);
    float sz = sinf(rz);

    /* Z */
    Vector3 zRot = GLR_Vec3(
        cz * v.x - sz * v.y,
        sz * v.x + cz * v.y,
        v.z
    );

    /* Y */
    Vector3 yRot = GLR_Vec3(
        cy * zRot.x + sy * zRot.z,
        zRot.y,
        -sy * zRot.x + cy * zRot.z
    );

    /* X */
    return GLR_Vec3(
        yRot.x,
        cx * yRot.y - sx * yRot.z,
        sx * yRot.y + cx * yRot.z
    );
}


static MouthBasis GLR_MakeMouthBasis(
    Vector3 rotationDegrees
) {
    MouthBasis basis;

    basis.right = GLR_Normalize(
        GLR_RotateXYZ(
            GLR_Vec3(1.0f, 0.0f, 0.0f),
            rotationDegrees
        )
    );

    basis.up = GLR_Normalize(
        GLR_RotateXYZ(
            GLR_Vec3(0.0f, 1.0f, 0.0f),
            rotationDegrees
        )
    );

    basis.forward = GLR_Normalize(
        GLR_RotateXYZ(
            GLR_Vec3(0.0f, 0.0f, 1.0f),
            rotationDegrees
        )
    );

    return basis;
}


/*
 * Radio del elipsoide desde su centro en una dirección determinada.
 * Se usa para impedir que una boca extremadamente profunda atraviese
 * toda la cabeza.
 */
static float GLR_EllipsoidRadiusAlongDirection(
    Vector3 direction,
    float halfX,
    float halfY,
    float halfZ
) {
    direction = GLR_Normalize(direction);

    halfX = fmaxf(halfX, 0.0001f);
    halfY = fmaxf(halfY, 0.0001f);
    halfZ = fmaxf(halfZ, 0.0001f);

    float denominator = sqrtf(
        (direction.x * direction.x) / (halfX * halfX) +
        (direction.y * direction.y) / (halfY * halfY) +
        (direction.z * direction.z) / (halfZ * halfZ)
    );

    if (denominator <= 0.000001f) {
        return fminf(halfX, fminf(halfY, halfZ));
    }

    return 1.0f / denominator;
}


/*
 * Color del borde/interior. El labio se integra en la propia malla:
 * no se añade ningún anillo geométrico por delante de la cabeza.
 */
static Color GLR_MouthSurfaceColor(
    const Mouth* mouth,
    float radial,
    float depthFactor
) {
    const float rimStart = 0.78f;

    if (radial >= rimStart) {
        return mouth->lipColor;
    }

    /*
     * El fondo se oscurece ligeramente para reforzar la lectura 3D.
     * depthFactor=1 corresponde al centro de la cavidad.
     */
    float brightness =
        0.72f -
        0.25f * GLR_Clamp01(depthFactor);

    return ScaleColor(
        mouth->insideColor,
        brightness
    );
}


/*
 * Evalúa la superficie deformada para un único punto paramétrico del
 * elipsoide. La posición que devuelve está en coordenadas de mundo.
 */
static MouthSurfaceSample GLR_EvaluateBodySurface(
    const BodyPart* part,
    const Mouth* const* mouths,
    size_t mouthCount,
    Color skinColor,
    float latitude,
    float longitude
) {
    MouthSurfaceSample sample;

    float halfX = fmaxf(part->widthRender * 0.5f, 0.0001f);
    float halfY = fmaxf(part->heightRender * 0.5f, 0.0001f);
    float halfZ = fmaxf(part->lengthRender * 0.5f, 0.0001f);

    float cosLat = cosf(latitude);

    Vector3 unitSphere = GLR_Vec3(
        cosLat * cosf(longitude),
        cosLat * sinf(longitude),
        sinf(latitude)
    );

    Vector3 localPoint = GLR_Vec3(
        unitSphere.x * halfX,
        unitSphere.y * halfY,
        unitSphere.z * halfZ
    );

    Vector3 worldPoint = GLR_Add(
        part->positionRender,
        localPoint
    );

    /* Normal correcta del elipsoide antes de tallarlo. */
    Vector3 baseNormal = GLR_Normalize(
        GLR_Vec3(
            unitSphere.x / halfX,
            unitSphere.y / halfY,
            unitSphere.z / halfZ
        )
    );

    sample.position = worldPoint;
    sample.baseNormal = baseNormal;
    sample.color = skinColor;
    sample.insideMouth = 0;

    float bestSink = 0.0f;

    for (size_t m = 0; m < mouthCount; ++m) {
        const Mouth* mouth = mouths[m];

        if (mouth == NULL) {
            continue;
        }

        float openFactor = GLR_Clamp01(mouth->openFactor);

        float radiusX =
            fmaxf(mouth->scale.x * 0.5f, 0.001f);

        float radiusY =
            fmaxf(
                mouth->scale.y * 0.5f *
                (0.10f + 0.90f * openFactor),
                0.001f
            );

        MouthBasis basis =
            GLR_MakeMouthBasis(mouth->rotation);

        Vector3 mouthCenter =
            GLR_Add(
                part->positionRender,
                mouth->offset
            );

        Vector3 fromMouth =
            GLR_Sub(
                worldPoint,
                mouthCenter
            );

        float localX =
            GLR_Dot(fromMouth, basis.right);

        float localY =
            GLR_Dot(fromMouth, basis.up);

        float nx = localX / radiusX;
        float ny = localY / radiusY;

        float q = nx * nx + ny * ny;

        if (q >= 1.0f) {
            continue;
        }

        /*
         * Evita seleccionar la cara opuesta del elipsoide que pueda
         * proyectarse accidentalmente dentro de la misma elipse.
         */
        float facing =
            GLR_Dot(baseNormal, basis.forward);

        if (facing <= 0.05f) {
            continue;
        }

        float radial = sqrtf(fmaxf(q, 0.0f));

        /*
         * Perfil de semielipse: pared pronunciada cerca del borde y
         * fondo suavemente redondeado en el centro.
         */
        float depthFactor =
            sqrtf(fmaxf(0.0f, 1.0f - q));

        float requestedDepth =
            fmaxf(
                mouth->scale.z *
                (0.25f + 0.55f * openFactor),
                radiusY * 0.60f
            );

        float bodyRadius =
            GLR_EllipsoidRadiusAlongDirection(
                basis.forward,
                halfX,
                halfY,
                halfZ
            );

        /*
         * Nunca tallamos más allá de aproximadamente el centro de la
         * pieza. Esto evita invertir la malla con genomas extremos.
         */
        float maxDepth =
            fmaxf(bodyRadius * 0.82f, 0.001f);

        float cavityDepth =
            fminf(requestedDepth, maxDepth);

        float sink =
            cavityDepth * depthFactor;

        if (sink > bestSink) {
            bestSink = sink;

            sample.position = GLR_Sub(
                worldPoint,
                GLR_Scale(
                    basis.forward,
                    sink
                )
            );

            sample.color =
                GLR_MouthSurfaceColor(
                    mouth,
                    radial,
                    depthFactor
                );

            sample.insideMouth = 1;
        }
    }

    return sample;
}


/*
 * La normal radial de la esfera ya no sirve una vez deformada.
 * Calculamos las tangentes de la nueva superficie mediante diferencias
 * finitas y obtenemos su producto vectorial.
 */
static Vector3 GLR_CalculateBodySurfaceNormal(
    const BodyPart* part,
    const Mouth* const* mouths,
    size_t mouthCount,
    Color skinColor,
    float latitude,
    float longitude
) {
    const float epsilon = 0.0015f;

    float latMinusValue =
        fmaxf(
            -0.5f * (float)M_PI + epsilon,
            latitude - epsilon
        );

    float latPlusValue =
        fminf(
            0.5f * (float)M_PI - epsilon,
            latitude + epsilon
        );

    MouthSurfaceSample latMinus =
        GLR_EvaluateBodySurface(
            part,
            mouths,
            mouthCount,
            skinColor,
            latMinusValue,
            longitude
        );

    MouthSurfaceSample latPlus =
        GLR_EvaluateBodySurface(
            part,
            mouths,
            mouthCount,
            skinColor,
            latPlusValue,
            longitude
        );

    MouthSurfaceSample lonMinus =
        GLR_EvaluateBodySurface(
            part,
            mouths,
            mouthCount,
            skinColor,
            latitude,
            longitude - epsilon
        );

    MouthSurfaceSample lonPlus =
        GLR_EvaluateBodySurface(
            part,
            mouths,
            mouthCount,
            skinColor,
            latitude,
            longitude + epsilon
        );

    Vector3 tangentLat =
        GLR_Sub(
            latPlus.position,
            latMinus.position
        );

    Vector3 tangentLon =
        GLR_Sub(
            lonPlus.position,
            lonMinus.position
        );

    /*
     * Con esta parametrización, dP/dLongitude x dP/dLatitude apunta
     * hacia el exterior de una esfera no deformada.
     */
    Vector3 normal =
        GLR_Cross(
            tangentLon,
            tangentLat
        );

    if (GLR_Length(normal) <= 0.000001f) {
        MouthSurfaceSample center =
            GLR_EvaluateBodySurface(
                part,
                mouths,
                mouthCount,
                skinColor,
                latitude,
                longitude
            );

        return center.baseNormal;
    }

    return GLR_Normalize(normal);
}


static void GLR_EmitBodySurfaceVertex(
    const BodyPart* part,
    const Mouth* const* mouths,
    size_t mouthCount,
    Color skinColor,
    float latitude,
    float longitude
) {
    MouthSurfaceSample sample =
        GLR_EvaluateBodySurface(
            part,
            mouths,
            mouthCount,
            skinColor,
            latitude,
            longitude
        );

    Vector3 normal =
        GLR_CalculateBodySurfaceNormal(
            part,
            mouths,
            mouthCount,
            skinColor,
            latitude,
            longitude
        );

    glColor4ub(
        sample.color.r,
        sample.color.g,
        sample.color.b,
        sample.color.a
    );

    glNormal3f(
        normal.x,
        normal.y,
        normal.z
    );

    glVertex3f(
        sample.position.x,
        sample.position.y,
        sample.position.z
    );
}


/*
 * Dibuja un BodyPart completo como un elipsoide cuya propia superficie
 * contiene las cavidades. No hay stencil, discos ni elipses planas.
 */
static void DrawBodyPartWithMouthCavitiesGL(
    const BodyPart* part,
    const Mouth* const* mouths,
    size_t mouthCount,
    Color skinColor,
    int slices,
    int stacks
) {
    slices = (slices < 12) ? 12 : slices;
    stacks = (stacks < 8) ? 8 : stacks;

    for (int i = 0; i < stacks; ++i) {
        float lat0 =
            -0.5f * (float)M_PI +
            (float)M_PI *
            ((float)i / (float)stacks);

        float lat1 =
            -0.5f * (float)M_PI +
            (float)M_PI *
            ((float)(i + 1) / (float)stacks);

        glBegin(GL_QUAD_STRIP);

        for (int j = 0; j <= slices; ++j) {
            float longitude =
                2.0f * (float)M_PI *
                ((float)j / (float)slices);

            GLR_EmitBodySurfaceVertex(
                part,
                mouths,
                mouthCount,
                skinColor,
                lat0,
                longitude
            );

            GLR_EmitBodySurfaceVertex(
                part,
                mouths,
                mouthCount,
                skinColor,
                lat1,
                longitude
            );
        }

        glEnd();
    }
}


/* ============================================================
 * BOCA
 * ============================================================ */

static void OpenGL_RenderMouth(
    MonsterRenderer* self,
    Vector3 pos,
    Vector3 rot,
    Vector3 scale,
    Color insideColor,
    Color lipColor,
    float openFactor
) {
    (void)self;
    (void)pos;
    (void)rot;
    (void)scale;
    (void)insideColor;
    (void)lipColor;
    (void)openFactor;

    /*
     * Las bocas forman parte topológica de su BodyPart, por lo que se
     * generan en OpenGL_RenderBodyParts(). Esta función se conserva
     * para no romper la interfaz MonsterRenderer existente.
     */
}


/* ============================================================
 * BODY PARTS
 * ============================================================ */

static void OpenGL_RenderBodyParts(
    MonsterRenderer* self,
    Monster* monster,
    const BodyPart* parts,
    size_t count
) {
    if (
        monster == NULL ||
        parts == NULL ||
        count == 0
    ) {
        return;
    }

    for (size_t i = 0; i < count; ++i) {
        const BodyPart* cur =
            &parts[i];

        Color color =
            Monster_GetColorFromIndexStruct(
                monster,
                cur->color
            );

        /*
         * Recoger todas las bocas que pertenecen a esta pieza.
         */
        size_t partMouthCount = 0;
        const Mouth* partMouths[16];

        if (
            monster->mouths != NULL &&
            monster->mouthCount > 0
        ) {
            for (size_t m = 0; m < monster->mouthCount; ++m) {
                if (
                    monster->mouths[m].bodyPartIndex == i &&
                    partMouthCount < 16
                ) {
                    partMouths[partMouthCount++] =
                        &monster->mouths[m];
                }
            }
        }

        /*
         * ====================================================
         * Pieza anatómica.
         * ====================================================
         *
         * Con boca: construimos el elipsoide ya deformado.
         * Sin boca: mantenemos el camino simple y barato original.
         */
        if (partMouthCount > 0) {
            /*
             * Una resolución mayor que el antiguo 16x16 es importante:
             * ahora la silueta de la cavidad pertenece a la malla.
             */
            DrawBodyPartWithMouthCavitiesGL(
                cur,
                partMouths,
                partMouthCount,
                color,
                48,
                32
            );

        } else {
            glPushMatrix();

            glTranslatef(
                cur->positionRender.x,
                cur->positionRender.y,
                cur->positionRender.z
            );

            glScalef(
                cur->widthRender * 0.5f,
                cur->heightRender * 0.5f,
                cur->lengthRender * 0.5f
            );

            glColor4ub(
                color.r,
                color.g,
                color.b,
                color.a
            );

            DrawSphereGL(
                1.0f,
                16,
                16
            );

            glPopMatrix();
        }

        /*
         * ====================================================
         * Conexión con el siguiente segmento.
         * ====================================================
         */
        if (i < count - 1) {
            const BodyPart* next =
                &parts[i + 1];

            float r1 =
                fmaxf(
                    cur->widthRender,
                    fmaxf(
                        cur->heightRender,
                        cur->lengthRender
                    )
                ) * 0.25f;

            float r2 =
                fmaxf(
                    next->widthRender,
                    fmaxf(
                        next->heightRender,
                        next->lengthRender
                    )
                ) * 0.25f;

            OpenGL_RenderCapsule(
                self,
                cur->positionRender,
                next->positionRender,
                r1,
                r2,
                color
            );
        }
    }
}


/* ============================================================
 * CAMERA
 * ============================================================ */

void OpenGLRenderer_SetupCamera(
    ICamera* camera,
    int width,
    int height
) {

    if (height == 0) {
        height = 1;
    }


    float aspect =
        (float)width /
        (float)height;


    glViewport(
        0,
        0,
        width,
        height
    );


    /*
     * --------------------------------------------------------
     * Projection.
     * --------------------------------------------------------
     */

    glMatrixMode(
        GL_PROJECTION
    );


    glLoadIdentity();


    gluPerspective(
        camera->fov,
        aspect,
        camera->nearPlane,
        camera->farPlane
    );


    /*
     * --------------------------------------------------------
     * View matrix.
     * --------------------------------------------------------
     */

    glMatrixMode(
        GL_MODELVIEW
    );


    glLoadIdentity();


    gluLookAt(

        camera->position.x,
        camera->position.y,
        camera->position.z,

        camera->target.x,
        camera->target.y,
        camera->target.z,

        camera->up.x,
        camera->up.y,
        camera->up.z
    );
}


/* ============================================================
 * RENDERER CREATION
 * ============================================================ */

MonsterRenderer OpenGLRenderer_Create(
    ICamera* camera
) {

    MonsterRenderer renderer;


    renderer.user_data =
        camera;


    renderer.beginFrame =
        OpenGL_BeginFrame;


    renderer.endFrame =
        OpenGL_EndFrame;


    renderer.renderSphere =
        OpenGL_RenderSphere;


    renderer.renderCapsule =
        OpenGL_RenderCapsule;


    renderer.renderEye =
        OpenGL_RenderEye;


    renderer.renderMouth =
        OpenGL_RenderMouth;


    renderer.renderBodyParts =
        OpenGL_RenderBodyParts;


    /* ========================================================
     * DEPTH
     * ======================================================== */

    glEnable(
        GL_DEPTH_TEST
    );


    glDepthFunc(
        GL_LEQUAL
    );


    /*
     * Deshabilitamos face culling de momento.
     *
     * Resulta especialmente útil para las criaturas
     * procedurales porque todavía tenemos geometría que puede
     * tener orientaciones inesperadas.
     */
    glDisable(
        GL_CULL_FACE
    );


    /*
     * Varias primitivas del renderer usan escalado no uniforme.
     * GL_NORMALIZE mantiene las normales unitarias tras glScalef().
     */
    glEnable(
        GL_NORMALIZE
    );


    /* ========================================================
     * ILUMINACIÓN
     * ======================================================== */

    glEnable(
        GL_LIGHTING
    );


    glEnable(
        GL_LIGHT0
    );


    glEnable(
        GL_COLOR_MATERIAL
    );


    glColorMaterial(
        GL_FRONT_AND_BACK,
        GL_AMBIENT_AND_DIFFUSE
    );


    GLfloat lightPos[] = {
        10.0f,
        20.0f,
        15.0f,
        1.0f
    };


    GLfloat lightAmbient[] = {
        0.4f,
        0.4f,
        0.4f,
        1.0f
    };


    GLfloat lightDiffuse[] = {
        1.0f,
        1.0f,
        1.0f,
        1.0f
    };


    glLightfv(
        GL_LIGHT0,
        GL_POSITION,
        lightPos
    );


    glLightfv(
        GL_LIGHT0,
        GL_AMBIENT,
        lightAmbient
    );


    glLightfv(
        GL_LIGHT0,
        GL_DIFFUSE,
        lightDiffuse
    );


    /* ========================================================
     * BACKGROUND
     * ======================================================== */

    glClearColor(
        0.0f,
        0.0f,
        0.0f,
        1.0f
    );


    return renderer;
}