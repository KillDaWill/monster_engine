#include "PrimitiveMesh.h"
#include "MathUtils.h"
#include <math.h>

#define RING_VERTEX(i, j) ((MeshIndex)(1 + ((size_t)(i) - 1) * (size_t)segments + (size_t)(j)))

bool PrimitiveMesh_GenerateEllipsoidEx(Mesh* out, Transform3D transform, unsigned int segments, unsigned int rings, Color color, bool inwardFacing) {
    if (!out || transform.scale.x <= 0.0f || transform.scale.y <= 0.0f || transform.scale.z <= 0.0f || segments < 3 || rings < 2) {
        return false;
    }

    size_t interiorRings = (size_t)rings - 1;
    size_t vertexCount = 2 + interiorRings * (size_t)segments;
    size_t indexCount = 6 * (size_t)segments * interiorRings;

    Mesh_Clear(out);
    if (!Mesh_ReserveVertices(out, vertexCount) || !Mesh_ReserveIndices(out, indexCount)) {
        return false;
    }

    const float pi = 3.14159265358979323846f;
    float phiStep = pi / (float)rings;
    float thetaStep = 2.0f * pi / (float)segments;

    float normMult = inwardFacing ? -1.0f : 1.0f;

    /* Polo Norte local: (0, ry, 0) */
    Vector3 poleNLocalPos = Vec3_Create(0.0f, transform.scale.y, 0.0f);
    Vector3 poleNLocalNorm = Vec3_Scale(Vec3_Create(0.0f, 1.0f, 0.0f), normMult);
    MeshVertex poleN = {
        .position = Vec3_Add(transform.position, Transform3D_RotateVector(transform.rotation, poleNLocalPos)),
        .normal = Transform3D_RotateVector(transform.rotation, poleNLocalNorm),
        .color = color
    };
    MeshIndex poleNIndex = 0;
    if (!Mesh_AddVertex(out, poleN, &poleNIndex)) {
        return false;
    }

    /* Anillos interiores */
    for (size_t i = 1; i <= interiorRings; ++i) {
        float phi = phiStep * (float)i;
        float sinPhi = sinf(phi);
        float cosPhi = cosf(phi);
        for (size_t j = 0; j < segments; ++j) {
            float theta = thetaStep * (float)j;
            Vector3 unitDir = Vec3_Create(sinPhi * cosf(theta), cosPhi, sinPhi * sinf(theta));

            Vector3 localPos = Vec3_Create(
                unitDir.x * transform.scale.x,
                unitDir.y * transform.scale.y,
                unitDir.z * transform.scale.z
            );

            Vector3 localNormUnnorm = Vec3_Create(
                unitDir.x / Math_Max(transform.scale.x, 0.0001f),
                unitDir.y / Math_Max(transform.scale.y, 0.0001f),
                unitDir.z / Math_Max(transform.scale.z, 0.0001f)
            );
            Vector3 localNorm = Vec3_Scale(Vec3_Normalize(localNormUnnorm), normMult);

            MeshVertex v = {
                .position = Vec3_Add(transform.position, Transform3D_RotateVector(transform.rotation, localPos)),
                .normal = Transform3D_RotateVector(transform.rotation, localNorm),
                .color = color
            };
            MeshIndex index = 0;
            if (!Mesh_AddVertex(out, v, &index)) {
                return false;
            }
        }
    }

    /* Polo Sur local: (0, -ry, 0) */
    Vector3 poleSLocalPos = Vec3_Create(0.0f, -transform.scale.y, 0.0f);
    Vector3 poleSLocalNorm = Vec3_Scale(Vec3_Create(0.0f, -1.0f, 0.0f), normMult);
    MeshVertex poleS = {
        .position = Vec3_Add(transform.position, Transform3D_RotateVector(transform.rotation, poleSLocalPos)),
        .normal = Transform3D_RotateVector(transform.rotation, poleSLocalNorm),
        .color = color
    };
    MeshIndex poleSIndex = 0;
    if (!Mesh_AddVertex(out, poleS, &poleSIndex)) {
        return false;
    }

    /* Triángulos polo norte */
    for (size_t j = 0; j < segments; ++j) {
        MeshIndex j2 = (MeshIndex)((j + 1) % segments);
        if (inwardFacing) {
            if (!Mesh_AddTriangle(out, poleNIndex, RING_VERTEX(1, j), RING_VERTEX(1, j2))) return false;
        } else {
            if (!Mesh_AddTriangle(out, poleNIndex, RING_VERTEX(1, j2), RING_VERTEX(1, j))) return false;
        }
    }

    /* Triángulos anillos centrales */
    for (size_t i = 1; i + 1 <= interiorRings; ++i) {
        for (size_t j = 0; j < segments; ++j) {
            MeshIndex j2 = (MeshIndex)((j + 1) % segments);
            MeshIndex a = RING_VERTEX(i, j);
            MeshIndex b = RING_VERTEX(i + 1, j);
            MeshIndex c = RING_VERTEX(i + 1, j2);
            MeshIndex d = RING_VERTEX(i, j2);
            if (inwardFacing) {
                if (!Mesh_AddTriangle(out, a, b, c)) return false;
                if (!Mesh_AddTriangle(out, a, c, d)) return false;
            } else {
                if (!Mesh_AddTriangle(out, a, c, b)) return false;
                if (!Mesh_AddTriangle(out, a, d, c)) return false;
            }
        }
    }

    /* Triángulos polo sur */
    for (size_t j = 0; j < segments; ++j) {
        MeshIndex j2 = (MeshIndex)((j + 1) % segments);
        if (inwardFacing) {
            if (!Mesh_AddTriangle(out, poleSIndex, RING_VERTEX(interiorRings, j2), RING_VERTEX(interiorRings, j))) return false;
        } else {
            if (!Mesh_AddTriangle(out, poleSIndex, RING_VERTEX(interiorRings, j), RING_VERTEX(interiorRings, j2))) return false;
        }
    }

    return true;
}

bool PrimitiveMesh_GenerateEllipsoid(Mesh* out, Transform3D transform, unsigned int segments, unsigned int rings, Color color) {
    return PrimitiveMesh_GenerateEllipsoidEx(out, transform, segments, rings, color, false);
}

bool PrimitiveMesh_GenerateQuadraticBezierTube(
    Mesh* out,
    Vector3 p0,
    Vector3 p1,
    Vector3 p2,
    float radius,
    int curveSegments,
    int radialSegments,
    Color color
) {
    if (!out || radius <= 0.0f || curveSegments < 2 || radialSegments < 3) {
        return false;
    }

    size_t cSegs = (size_t)curveSegments;
    size_t rSegs = (size_t)radialSegments;

    size_t tubeVerts = (cSegs + 1) * rSegs;
    size_t totalVerts = tubeVerts + 2;
    size_t tubeIndices = cSegs * rSegs * 6;
    size_t capIndices = rSegs * 3 * 2;
    size_t totalIndices = tubeIndices + capIndices;

    Mesh_Clear(out);
    if (!Mesh_ReserveVertices(out, totalVerts) || !Mesh_ReserveIndices(out, totalIndices)) {
        return false;
    }

    const float pi = 3.14159265358979323846f;
    float dTheta = (2.0f * pi) / (float)rSegs;

    for (size_t i = 0; i <= cSegs; ++i) {
        float t = (float)i / (float)cSegs;
        float omt = 1.0f - t;

        Vector3 curvePos = Vec3_Create(
            omt * omt * p0.x + 2.0f * omt * t * p1.x + t * t * p2.x,
            omt * omt * p0.y + 2.0f * omt * t * p1.y + t * t * p2.y,
            omt * omt * p0.z + 2.0f * omt * t * p1.z + t * t * p2.z
        );

        Vector3 deriv = Vec3_Create(
            2.0f * omt * (p1.x - p0.x) + 2.0f * t * (p2.x - p1.x),
            2.0f * omt * (p1.y - p0.y) + 2.0f * t * (p2.y - p1.y),
            2.0f * omt * (p1.z - p0.z) + 2.0f * t * (p2.z - p1.z)
        );

        Vector3 tangent = Vec3_Normalize(deriv);
        if (Vec3_LengthSq(tangent) < 1e-6f) {
            tangent = Vec3_Create(1.0f, 0.0f, 0.0f);
        }

        Vector3 normalInPlane = Vec3_Create(-tangent.y, tangent.x, 0.0f);
        float normLen = Vec3_Length(normalInPlane);
        if (normLen < 1e-6f) {
            normalInPlane = Vec3_Create(1.0f, 0.0f, 0.0f);
        } else {
            normalInPlane = Vec3_Scale(normalInPlane, 1.0f / normLen);
        }

        Vector3 localZ = Vec3_Create(0.0f, 0.0f, 1.0f);

        for (size_t j = 0; j < rSegs; ++j) {
            float theta = (float)j * dTheta;
            float cosT = cosf(theta);
            float sinT = sinf(theta);

            Vector3 radialDir = Vec3_Add(
                Vec3_Scale(normalInPlane, cosT),
                Vec3_Scale(localZ, sinT)
            );
            Vector3 norm = Vec3_Normalize(radialDir);
            Vector3 pos = Vec3_Add(curvePos, Vec3_Scale(norm, radius));

            MeshVertex v = {
                .position = pos,
                .normal = norm,
                .color = color
            };
            MeshIndex unused;
            if (!Mesh_AddVertex(out, v, &unused)) return false;
        }
    }

    Vector3 deriv0 = Vec3_Create(2.0f * (p1.x - p0.x), 2.0f * (p1.y - p0.y), 2.0f * (p1.z - p0.z));
    Vector3 tan0 = Vec3_Normalize(deriv0);
    MeshVertex capStartVert = {
        .position = p0,
        .normal = Vec3_Scale(tan0, -1.0f),
        .color = color
    };
    MeshIndex capStartIndex = 0;
    if (!Mesh_AddVertex(out, capStartVert, &capStartIndex)) return false;

    Vector3 deriv1 = Vec3_Create(2.0f * (p2.x - p1.x), 2.0f * (p2.y - p1.y), 2.0f * (p2.z - p1.z));
    Vector3 tan1 = Vec3_Normalize(deriv1);
    MeshVertex capEndVert = {
        .position = p2,
        .normal = tan1,
        .color = color
    };
    MeshIndex capEndIndex = 0;
    if (!Mesh_AddVertex(out, capEndVert, &capEndIndex)) return false;

    for (size_t i = 0; i < cSegs; ++i) {
        for (size_t j = 0; j < rSegs; ++j) {
            size_t jNext = (j + 1) % rSegs;
            MeshIndex v0 = (MeshIndex)(i * rSegs + j);
            MeshIndex v1 = (MeshIndex)((i + 1) * rSegs + j);
            MeshIndex v2 = (MeshIndex)((i + 1) * rSegs + jNext);
            MeshIndex v3 = (MeshIndex)(i * rSegs + jNext);

            if (!Mesh_AddTriangle(out, v0, v1, v2)) return false;
            if (!Mesh_AddTriangle(out, v0, v2, v3)) return false;
        }
    }

    for (size_t j = 0; j < rSegs; ++j) {
        size_t jNext = (j + 1) % rSegs;
        MeshIndex vCurr = (MeshIndex)j;
        MeshIndex vNext = (MeshIndex)jNext;
        if (!Mesh_AddTriangle(out, capStartIndex, vNext, vCurr)) return false;
    }

    size_t lastRingOffset = cSegs * rSegs;
    for (size_t j = 0; j < rSegs; ++j) {
        size_t jNext = (j + 1) % rSegs;
        MeshIndex vCurr = (MeshIndex)(lastRingOffset + j);
        MeshIndex vNext = (MeshIndex)(lastRingOffset + jNext);
        if (!Mesh_AddTriangle(out, capEndIndex, vCurr, vNext)) return false;
    }

    return true;
}

bool PrimitiveMesh_GenerateUVSphere(Mesh* out, Vector3 center, float radius, unsigned int segments, unsigned int rings, Color color) {
    Transform3D t = Transform3D_Create(center, Vec3_Zero(), Vec3_Create(radius, radius, radius));
    return PrimitiveMesh_GenerateEllipsoid(out, t, segments, rings, color);
}

#undef RING_VERTEX
