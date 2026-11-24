// Materiales anatómicos procedurales reutilizables en espacio local/reposo
// para caparazón, plastrón, aletas reptilianas y placas cefálicas.
// Sin dependencias de ID de especie ni texturas externas; determinista y anti-aliased.

float RepNoise2D(vec2 p, uint seed) {
    ivec2 i = ivec2(floor(p));
    vec2 f = fract(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = CellHash(i, seed).x;
    float b = CellHash(i + ivec2(1, 0), seed).x;
    float c = CellHash(i + ivec2(0, 1), seed).x;
    float d = CellHash(i + ivec2(1, 1), seed).x;
    return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);
}

float RepFbm2D(vec2 p, uint seed) {
    float v = 0.0;
    float amp = 0.55;
    for (int k = 0; k < 3; ++k) {
        v += amp * RepNoise2D(p, seed ^ uint(k * 7919));
        p = mat2(0.80, -0.60, 0.60, 0.80) * p * 2.07 + vec2(11.3, 7.1);
        amp *= 0.52;
    }
    return v;
}

vec3 RepHeightBump(vec3 n, vec3 viewPos, float height, float maxTilt) {
    vec3 dx = dFdx(viewPos), dy = dFdy(viewPos);
    vec3 rx = cross(dy, n), ry = cross(n, dx);
    float det = dot(dx, rx);
    if (abs(det) < 1e-10) return n;
    vec3 grad = (dFdx(height) * rx + dFdy(height) * ry) * sign(det) / max(abs(det), 1e-9);
    float gl = length(grad);
    if (gl > maxTilt) grad *= maxTilt / gl;
    return normalize(n - grad);
}

void RepVoronoi2D(vec2 p, float irregularity, uint seed,
                  out vec2 bestCell, out float edgeDist, out vec3 cellRand) {
    ivec2 base = ivec2(floor(p));
    vec2 fracP = fract(p);
    float jitter = clamp(irregularity, 0.15, 0.95) * 0.44;
    float f1 = 1e9, f2 = 1e9;
    vec2 minVec = vec2(0.0);
    vec2 secVec = vec2(0.0);
    bestCell = vec2(base);
    cellRand = vec3(0.5);

    for (int oy = -1; oy <= 1; ++oy) {
        for (int ox = -1; ox <= 1; ++ox) {
            ivec2 c = base + ivec2(ox, oy);
            vec3 h = CellHash(c, seed);
            vec2 offset = vec2(float(ox), float(oy)) + 0.5 + (h.xy - 0.5) * 2.0 * jitter - fracP;
            float d2 = dot(offset, offset);
            if (d2 < f1) {
                f2 = f1;
                secVec = minVec;
                f1 = d2;
                minVec = offset;
                bestCell = vec2(c);
                cellRand = h;
            } else if (d2 < f2) {
                f2 = d2;
                secVec = offset;
            }
        }
    }
    // Distancia exacta al borde poligonal entre la celda más cercana y la segunda.
    vec2 diff = secVec - minVec;
    float dLen = length(diff);
    edgeDist = dLen > 1e-5 ? dot(0.5 * (minVec + secVec), diff / dLen) : 0.0;
}

SurfaceResponse EvaluateCarapaceMaterial(vec3 rc, vec3 n, vec3 viewPos) {
    vec3 cBase   = detailMaterial[0].rgb;
    float rBase  = clamp(detailMaterial[0].w, 0.20, 0.90);
    vec3 cDark   = detailMaterial[1].rgb;
    float mStr   = clamp(detailMaterial[1].w, 0.0, 1.0);
    vec3 cAmber  = detailMaterial[2].rgb;
    float radStr = clamp(detailMaterial[2].w, 0.0, 1.0);
    vec3 cBorder = detailMaterial[3].rgb;
    float sContr = clamp(detailMaterial[3].w, 0.0, 1.0);
    float costalPairs = max(detailMaterial[6].x, 4.0);
    float vertebrals  = max(detailMaterial[6].y, 5.0);
    float marginals   = max(detailMaterial[6].z, 11.0);
    uint seed = uint(detailMaterial[10].y + 0.5) | (uint(detailMaterial[10].z + 0.5) << 16u);
    if (seed == 0u) seed = pigmentSeed ^ 0x43415241u;
    float relief = clamp(detailMaterial[10].w, 0.0, 1.0);

    float xNorm = clamp(rc.x, -1.0, 1.0);
    float u = clamp(rc.y, 0.0, 1.0);

    // Deformación orgánica muy sutil de límites entre escudos (bilateralmente coherente).
    float axRaw = abs(xNorm);
    float warpSym = (RepNoise2D(vec2(axRaw * 5.2, u * 6.5), seed ^ 0x101u) - 0.5) * 0.018;
    float warpAsym = (RepNoise2D(vec2(xNorm * 4.7 + 3.1, u * 5.8), seed ^ 0x202u) - 0.5) * 0.008;
    float xw = xNorm + (warpSym + warpAsym) * smoothstep(0.04, 0.20, axRaw);
    float uw = clamp(u + (RepNoise2D(vec2(axRaw * 4.5, u * 6.0 + 2.3), seed ^ 0x303u) - 0.5) * 0.016, 0.0, 1.0);
    float ax = abs(xw);
    float side = xw >= 0.0 ? 1.0 : -1.0;

    // Geometría anatómica real de escudos (Chelonia mydas):
    // - Escudo nucal anterior separado del 1er par costal
    // - 5 escudos vertebrales hexagonales entrelazados con las suturas costales
    // - 4 pares de escudos costales curvos
    // - 11 pares de escudos marginales perimetrales
    float uStart = 0.068;
    float uEnd = 0.945;
    float uSpan = clamp((uw - uStart) / (uEnd - uStart), 0.0, 0.9999);
    float mInner = 0.835 - 0.020 * sin(uSpan * 3.14159265);
    // Ancho hexagonal vertebral: se ensancha donde se encuentran las suturas transversales costales.
    float costalPhase = uSpan * costalPairs;
    float hexBulge = 0.042 * (0.5 + 0.5 * cos(costalPhase * 6.2831853));
    float vWidth = 0.258 + hexBulge * smoothstep(0.02, 0.14, uSpan) * smoothstep(0.98, 0.86, uSpan);

    int scuteId = 1;
    vec2 localUV = vec2(0.0);
    float distEdge = 1.0;
    bool isMarginal = (ax >= mInner || uw < uStart || uw > uEnd);

    if (uw < uStart && ax < 0.27) {
        // Escudo nucal (cervical)
        scuteId = 1;
        localUV = vec2(xw / 0.27, (uw / uStart) * 2.0 - 1.0);
        distEdge = min(min(0.27 - ax, uStart - uw), uw + 0.04);
    } else if (isMarginal) {
        // Anillo de escudos marginales
        float perimAng = atan(xw * 0.95, 0.5 - uw); // -pi..pi alrededor del borde
        float mNorm = (perimAng + 3.14159265) / 6.2831853 * (marginals * 2.0);
        float mIdxF = floor(mNorm);
        float mFrac = fract(mNorm);
        scuteId = 40 + int(mIdxF);
        localUV = vec2((ax - mInner) / max(1.0 - mInner, 0.08) * 2.0 - 1.0, mFrac * 2.0 - 1.0);
        float dRad = abs(ax - mInner);
        if (uw < uStart) dRad = min(dRad, uStart - uw);
        if (uw > uEnd) dRad = min(dRad, uw - uEnd);
        float dAng = min(mFrac, 1.0 - mFrac) * 0.22;
        distEdge = min(dRad, dAng);
    } else if (ax < vWidth) {
        // 5 escudos vertebrales centrales hexagonales
        float vf = uSpan * vertebrals;
        float vIdx = floor(clamp(vf, 0.0, vertebrals - 0.001));
        float vFrac = fract(vf);
        scuteId = 10 + int(vIdx);
        localUV = vec2(xw / max(vWidth, 0.1), vFrac * 2.0 - 1.0);
        float dZ = min(vFrac, 1.0 - vFrac) * (0.877 / vertebrals);
        float dX = vWidth - ax;
        distEdge = min(dZ, dX * 0.92);
    } else {
        // 4 pares de escudos costales laterales con curvatura posterior anatómica
        float uCostal = clamp(uSpan - 0.042 * (ax - vWidth), 0.0, 0.9999);
        float cf = uCostal * costalPairs;
        float cIdx = floor(clamp(cf, 0.0, costalPairs - 0.001));
        float cFrac = fract(cf);
        scuteId = (side > 0.0 ? 20 : 30) + int(cIdx);
        float cWidth = max(mInner - vWidth, 0.1);
        localUV = vec2(((ax - vWidth) / cWidth) * 2.0 - 1.0, cFrac * 2.0 - 1.0);
        float dZ = min(cFrac, 1.0 - cFrac) * (0.877 / costalPairs);
        float dX = min(ax - vWidth, mInner - ax);
        distEdge = min(dZ, dX * 0.92);
    }

    // Identidad y semilla determinista independiente por escudo.
    vec3 sr  = CellHash(ivec2(scuteId, int(seed & 0x7fffu)), seed ^ 0x53435554u);
    vec3 sr2 = CellHash(ivec2(scuteId + 71, int((seed >> 8) & 0x7fffu)), seed ^ 0x91a2b3c4u);

    // Centro de crecimiento (aréola) ligeramente desplazado hacia posterior-medial en cada escudo.
    vec2 growthCenter = vec2(isMarginal ? 0.0 : -0.14, 0.16) + (sr.xy - 0.5) * 0.26;
    vec2 rel = localUV - growthCenter;
    float rad = length(rel);
    float ang = atan(rel.y, rel.x);

    // Ruido FBM deformado en coordenadas locales del escudo (independiente entre escudos vecinos).
    vec2 pLoc = localUV * 2.25 + sr.xy * 17.3 + vec2(float(scuteId) * 3.71, float(scuteId) * 2.19);
    float w1 = RepFbm2D(pLoc, seed ^ 0x411u);
    float w2 = RepFbm2D(pLoc * 1.9 + vec2(w1 * 1.55, -w1 * 1.25), seed ^ 0x422u);
    float w3 = RepFbm2D(pLoc * 3.3 + vec2(-w2 * 1.15, w1 * 1.35), seed ^ 0x433u);

    // 1) Tono base individual por escudo (variación real entre placas vecinas).
    vec3 scuteBase = mix(cBase, cDark, (sr.x - 0.5) * 0.48 + 0.14);
    scuteBase = mix(scuteBase, cAmber, (sr.y - 0.5) * 0.36 + 0.14);
    // Sutil matiz oliva-castaño adicional en algunos escudos
    vec3 cOliveTint = cBase * vec3(0.92, 1.08, 0.80);
    scuteBase = mix(scuteBase, cOliveTint, sr2.x * 0.30);

    // 2) Moteado irregular: manchas oscuras amplias + nubes cálidas ámbar/castaño.
    float darkPatch = smoothstep(0.38, 0.76, w2);
    float amberCloud = smoothstep(0.36, 0.74, w3) * (1.0 - darkPatch * 0.45);
    vec3 albedo = mix(scuteBase, cDark, darkPatch * 0.54 * mStr);
    albedo = mix(albedo, cAmber, amberCloud * 0.46 * mStr);

    // 3) Estrías radiales curvas, rotas e irregulares centradas en la aréola del escudo.
    float angWarp = ang + (w1 - 0.5) * 1.45 + (w2 - 0.5) * 0.72;
    float rayFreq = (isMarginal ? 4.0 : 5.5) + sr.z * 4.5;
    float rWave1 = 0.5 + 0.5 * sin(angWarp * rayFreq + sr2.y * 6.28318);
    float rWave2 = 0.5 + 0.5 * sin(angWarp * (rayFreq * 1.72) - rad * 2.4 + sr2.z * 6.28318);
    float rayPattern = smoothstep(0.32, 0.74, mix(rWave1, rWave2, 0.42) * (0.62 + 0.72 * w3));
    float rayBreak = smoothstep(0.20, 0.65, RepFbm2D(vec2(angWarp * 1.6, rad * 2.8) + sr2.xy * 9.0, seed ^ 0x444u));
    float rayMask = rayPattern * rayBreak * smoothstep(0.04, 0.24, rad) * (1.0 - smoothstep(0.78, 1.20, rad));
    // Algunas estrías son ámbar/miel cálido y otras castaño dorado, siguiendo la aréola
    vec3 cChestnut = mix(cDark, cAmber, 0.65);
    vec3 rayColor = mix(cChestnut, cAmber * 1.18, smoothstep(0.28, 0.66, w1));
    albedo = mix(albedo, rayColor, rayMask * 0.78 * radStr);

    // 4) Oscurecimiento sutil cerca del borde de algunos escudos + líneas finas de crecimiento.
    float edgeDark = smoothstep(0.065, 0.008, distEdge) * (0.14 + 0.14 * sr2.z);
    albedo = mix(albedo, cDark * 0.80, edgeDark);
    float growthRing = 0.5 + 0.5 * sin(rad * 17.0 + w1 * 2.8);
    albedo *= 0.965 + 0.055 * growthRing;

    // 5) Micrograno de queratina filtrado por huella de píxel.
    float footprint = max(length(dFdx(rc.xy)), length(dFdy(rc.xy)));
    float fineGrain = RepNoise2D(rc.xy * 65.0 + sr.xy * 13.0, seed ^ 0x455u);
    fineGrain = mix(fineGrain, 0.5, smoothstep(0.008, 0.035, footprint));
    albedo *= 0.95 + 0.10 * fineGrain;

    // 6) Suturas anatómicas delgadas, oscuras, ligeramente irregulares y anti-aliased.
    float fw = max(fwidth(distEdge), 0.0018);
    float sutureMask = 1.0 - smoothstep(0.0045, 0.0135 + fw * 1.35, distEdge);
    albedo = mix(albedo, cBorder, sutureMask * (0.50 + 0.42 * sContr));

    // Respuesta de queratina dura: relieve muy contenido, brillo suave, sin aspecto plástico.
    float domeH = (1.0 - smoothstep(0.0, 0.09, distEdge)) * (-0.0022 * relief);
    float grooveH = -sutureMask * 0.0042 * relief;
    float microH = (w2 - 0.5) * 0.0007 + (growthRing - 0.5) * 0.0006 + (fineGrain - 0.5) * 0.0003;
    vec3 detailedN = RepHeightBump(n, viewPos, domeH + grooveH + microH, 0.34);

    float rough = clamp(rBase + (w3 - 0.5) * 0.09 + sutureMask * 0.24, 0.26, 0.88);
    float spec = mix(0.26, 0.04, sutureMask) * clamp(1.18 - rough * 0.65, 0.2, 1.0);
    return SurfaceResponse(albedo, detailedN, rough, spec);
}

SurfaceResponse EvaluatePlastronMaterial(vec3 rc, vec3 n, vec3 viewPos) {
    vec3 cBase   = detailMaterial[4].rgb;
    float rBase  = clamp(detailMaterial[4].w, 0.22, 0.92);
    vec3 cSeam   = detailMaterial[5].rgb;
    float soft   = clamp(detailMaterial[5].w, 0.0, 1.0);
    float mStr   = clamp(detailMaterial[6].w, 0.0, 1.0);
    uint seed = uint(detailMaterial[10].y + 0.5) | (uint(detailMaterial[10].z + 0.5) << 16u);
    if (seed == 0u) seed = pigmentSeed ^ 0x504c4153u;

    float xn = clamp(rc.x, -1.0, 1.0);
    float u  = clamp(rc.y, 0.0, 1.0);
    float bridge = clamp(rc.z, 0.0, 1.0);
    float ax = abs(xn);

    // Disposición anatómica real de escudos plastrales (gular, humeral, pectoral, abdominal, femoral, anal)
    // sin rejilla periódica ni franjas equidistantes.
    float symWarp = (RepNoise2D(vec2(ax * 5.0, u * 6.0), seed ^ 0x511u) - 0.5) * 0.014;
    float midWarp = (RepNoise2D(vec2(u * 7.5, 1.7), seed ^ 0x512u) - 0.5) * 0.011;
    float dMid = abs(xn + midWarp);

    // 5 suturas transversales anatómicas con curvatura orgánica y correspondencia bilateral.
    float dTrans = 1.0;
    float t0 = 0.135 + 0.045 * ax + symWarp; // gular / humeral
    float t1 = 0.285 + 0.030 * ax + symWarp; // humeral / pectoral
    float t2 = 0.495 + 0.010 * ax + symWarp; // pectoral / abdominal
    float t3 = 0.695 - 0.022 * ax + symWarp; // abdominal / femoral
    float t4 = 0.855 - 0.032 * ax + symWarp; // femoral / anal
    dTrans = min(dTrans, abs(u - t0));
    dTrans = min(dTrans, abs(u - t1));
    dTrans = min(dTrans, abs(u - t2));
    dTrans = min(dTrans, abs(u - t3));
    dTrans = min(dTrans, abs(u - t4));

    // Sutura inframarginal sutil en el puente lateral (bridge > 0)
    float dBridge = 1.0;
    if (bridge > 0.08) {
        dBridge = abs(ax - (0.71 + symWarp * 0.8));
        if (ax > 0.71) {
            // 4 escudos inframarginales sutiles
            float ib = clamp((u - 0.24) / 0.50, 0.0, 0.999) * 4.0;
            float ibFrac = fract(ib);
            dBridge = min(dBridge, min(ibFrac, 1.0 - ibFrac) * 0.125);
        }
    }

    float innerMask = smoothstep(0.95, 0.74, ax);
    float fwMid = max(fwidth(dMid), 0.002);
    float fwTr  = max(fwidth(dTrans), 0.002);
    float fwBr  = max(fwidth(dBridge), 0.002);

    float sMid   = (1.0 - smoothstep(0.004, 0.014 + fwMid * 1.4, dMid)) * smoothstep(0.03, 0.10, u) * smoothstep(0.98, 0.90, u);
    float sTrans = (1.0 - smoothstep(0.004, 0.014 + fwTr * 1.4, dTrans)) * innerMask;
    float sBr    = (1.0 - smoothstep(0.005, 0.016 + fwBr * 1.4, dBridge)) * smoothstep(0.10, 0.35, bridge);
    float suture = max(sMid * 0.88, max(sTrans * 0.82, sBr * 0.65));

    // Base marfil-crema cálido con gradiente suave y moteado muy sutil.
    float nBroad = RepFbm2D(vec2(xn * 2.6, u * 3.1) + vec2(3.7, 1.9), seed ^ 0x521u);
    float nFine  = RepNoise2D(vec2(xn * 26.0, u * 30.0), seed ^ 0x522u);
    float fp = max(length(dFdx(rc.xy)), length(dFdy(rc.xy)));
    nFine = mix(nFine, 0.5, smoothstep(0.01, 0.04, fp));

    vec3 cCreamWarm = cBase * vec3(0.97, 0.94, 0.86);
    vec3 cOchreEdge = mix(cBase, cSeam, 0.52);
    vec3 albedo = mix(cBase, cCreamWarm, 0.35 * (1.0 - u) + 0.25 * nBroad);
    // Moteado muy débil e imperfecciones orgánicas
    albedo = mix(albedo, cOchreEdge, (nBroad - 0.45) * 0.55 * mStr);
    // Oscurecimiento natural en el puente y transición marginal
    float bridgeShade = smoothstep(0.56, 0.96, ax) * (0.28 + 0.32 * bridge);
    albedo = mix(albedo, mix(cOchreEdge, detailMaterial[0].rgb, 0.26), bridgeShade);
    albedo *= 0.97 + 0.06 * nFine;

    // Suturas de bajo contraste (nunca rejilla negra).
    float seamOpacity = clamp(mix(0.46, 0.22, soft), 0.14, 0.48);
    albedo = mix(albedo, cSeam, suture * seamOpacity);

    float h = -suture * 0.0014 + (nBroad - 0.5) * 0.0004;
    vec3 detailedN = RepHeightBump(n, viewPos, h, 0.18);
    float rough = clamp(rBase + (nBroad - 0.5) * 0.07 + suture * 0.12, 0.30, 0.88);
    float spec = mix(0.14, 0.04, suture);
    return SurfaceResponse(albedo, detailedN, rough, spec);
}

SurfaceResponse EvaluateReptilianFlipperMaterial(vec3 rc, float isVentral, vec3 n, vec3 viewPos) {
    vec3 cDorsal  = detailMaterial[7].rgb;
    float rBase   = clamp(detailMaterial[7].w, 0.24, 0.92);
    vec3 cVentral = detailMaterial[8].rgb;
    float sSize   = clamp(detailMaterial[8].w, 0.35, 2.8);
    vec3 cBorder  = detailMaterial[9].rgb;
    float irreg   = clamp(detailMaterial[9].w, 0.15, 0.95);
    float bContr  = clamp(detailMaterial[10].x, 0.0, 1.0);
    uint seed = uint(detailMaterial[10].y + 0.5) | (uint(detailMaterial[10].z + 0.5) << 16u);
    if (seed == 0u) seed = scaleSeed ^ 0x464c4950u;

    float u = clamp(rc.x, 0.0, 1.0); // 0=raíz proximal, 1=punta distal
    float v = clamp(rc.y, 0.0, 1.0); // 0=borde de ataque, 1=borde de fuga
    bool isFore = rc.z > 0.5;

    // Progresión anatómica continua del tamaño de escamas:
    // más grandes en zona proximal y borde de ataque, intermedias en lámina, más finas hacia la punta y borde posterior.
    float densU = (isFore ? 13.5 : 8.2) * sSize;
    float densV = (isFore ? 6.2  : 4.8) * sSize;
    float su = pow(max(u, 0.0005), 0.84) * densU + u * u * densU * 0.28;
    float sv = pow(max(v, 0.0005), 0.82) * densV + v * v * densV * 0.22;

    // Deformación orgánica antes de Voronoi para evitar patrones regulares o hexágonos idénticos.
    vec2 pWarp = vec2(
        (RepNoise2D(vec2(u * 6.5, v * 5.2), seed ^ 0x611u) - 0.5) * 0.55,
        (RepNoise2D(vec2(u * 5.8 + 4.1, v * 6.4), seed ^ 0x612u) - 0.5) * 0.45
    );
    vec2 q = vec2(su, sv) + pWarp;

    vec2 cellId;
    float edgeDist;
    vec3 cellRand;
    RepVoronoi2D(q, irreg, seed ^ (isVentral > 0.5 ? 0x777u : 0x666u), cellId, edgeDist, cellRand);

    float fw = max(fwidth(edgeDist), 0.010);
    float borderWidth = isVentral > 0.5 ? 0.085 : 0.048;
    float scaleInterior = smoothstep(borderWidth - fw * 0.7, borderWidth + 0.045 + fw * 1.0, edgeDist);

    float mottle = RepFbm2D(vec2(u * 9.0, v * 6.0) + cellRand.xy * 5.0, seed ^ 0x621u);
    float rootFade = smoothstep(0.0, 0.16, u);

    vec3 albedo;
    float bumpH;
    float rough;
    float spec;

    if (isVentral < 0.5) {
        // Superficie dorsal: escamas poligonales oscuras oliva/pardo/carbón con variación entre placas vecinas.
        vec3 cDarkOlive = cDorsal * vec3(0.72, 0.75, 0.66);
        vec3 cBrownGray = mix(cDorsal, detailMaterial[1].rgb, 0.36);
        vec3 cMutedVar  = mix(cDorsal, cBorder, 0.24);

        vec3 plateCol = mix(cDarkOlive, cBrownGray, cellRand.x);
        plateCol = mix(plateCol, cMutedVar, smoothstep(0.42, 0.92, cellRand.y) * 0.44);
        // Sutil moteado interno dentro de cada escama + oscurecimiento leve hacia la punta
        plateCol *= 0.92 + 0.16 * mottle;
        plateCol *= mix(1.04, 0.88, smoothstep(0.55, 0.98, u));

        // Borde delgado más claro (ocre/oliva pálido, nunca blanco brillante).
        vec3 seamCol = mix(plateCol, cBorder, 0.28 + 0.42 * bContr);
        // Transición suave en la raíz proximal hacia la piel del hombro/cadera.
        float seamMask = (1.0 - scaleInterior) * mix(0.22, 1.0, rootFade);
        albedo = mix(plateCol, seamCol, seamMask);
        // Suavizado natural en los bordes anterior/posterior hacia la cara ventral.
        float edgeWrap = smoothstep(0.88, 1.0, v);
        albedo = mix(albedo, mix(albedo, cVentral, 0.35), edgeWrap * 0.45);

        bumpH = (scaleInterior * 0.0012 + (mottle - 0.5) * 0.0004) * rootFade;
        rough = clamp(rBase + (cellRand.z - 0.5) * 0.10 + seamMask * 0.14, 0.32, 0.88);
        spec = mix(0.14, 0.05, seamMask);
    } else {
        // Superficie ventral: más clara y desaturada que la dorsal, pero menos luminosa y más orgánica que el plastrón.
        vec3 vBase   = cVentral * 0.88;
        vec3 vShadow = mix(vBase, cDorsal, 0.28);
        vec3 vPlate  = mix(vBase, vShadow, 0.14 + 0.28 * cellRand.x);
        // Escamas oscuras dispersas cerca del borde de ataque y la punta distal (rasgo real de C. mydas).
        float edgePigment = (smoothstep(0.32, 0.0, v) * 0.55 + smoothstep(0.58, 0.98, u) * 0.50) * smoothstep(0.28, 0.76, cellRand.y);
        vPlate = mix(vPlate, mix(cDorsal, vBase, 0.32), edgePigment * 0.72);
        vPlate *= 0.94 + 0.12 * mottle;

        vec3 vSeam = mix(vBase * 0.92, cBorder, 0.28);
        float seamMask = (1.0 - scaleInterior) * (0.24 + 0.22 * bContr) * mix(0.25, 1.0, rootFade);
        albedo = mix(vPlate, vSeam, seamMask);

        bumpH = (scaleInterior * 0.0007 + (mottle - 0.5) * 0.0003) * rootFade;
        rough = clamp(rBase + 0.06 + (cellRand.z - 0.5) * 0.07, 0.36, 0.90);
        spec = 0.09;
    }

    vec3 detailedN = RepHeightBump(n, viewPos, bumpH, 0.22);
    return SurfaceResponse(albedo, detailedN, rough, spec);
}

SurfaceResponse EvaluateCephalicPlateMaterial(vec3 rc, vec3 n, vec3 viewPos) {
    vec3 cHeadBase = detailMaterial[11].rgb;
    float mStr     = clamp(detailMaterial[11].w, 0.0, 1.0);
    vec3 cDark     = detailMaterial[1].rgb;
    vec3 cFlipper  = detailMaterial[7].rgb;
    vec3 cBorder   = detailMaterial[9].rgb;
    uint seed = uint(detailMaterial[10].y + 0.5) | (uint(detailMaterial[10].z + 0.5) << 16u);
    if (seed == 0u) seed = pigmentSeed ^ 0x48454144u;

    vec2 uv = rc.xy; // [-1, 1]^2 en coordenadas locales de la placa
    int plateId = int(rc.z + 0.5);
    vec3 pr = CellHash(ivec2(plateId, int(seed & 0x7fffu)), seed ^ 0x48454144u);

    // Borde orgánico redondeado y fino (nunca rectángulo negro grueso).
    float edgeWarp = (RepNoise2D(uv * 3.8 + pr.xy * 7.0, seed ^ 0x711u) - 0.5) * 0.09;
    float rBox = length(max(abs(uv) - vec2(0.58), vec2(0.0))) / 0.42 + edgeWarp;
    float rRad = length(uv) + edgeWarp;
    float edgeMetric = mix(rBox, rRad, 0.35);

    // Moteado interno natural coherente con la piel de cabeza, cuello y aletas.
    vec2 pLoc = uv * 2.6 + pr.xy * 13.7 + vec2(float(plateId) * 2.9, float(plateId) * 1.7);
    float m1 = RepFbm2D(pLoc, seed ^ 0x721u);
    float m2 = RepFbm2D(pLoc * 2.1 + vec2(m1 * 1.3, -m1 * 1.1), seed ^ 0x722u);

    vec3 plateTone = mix(cHeadBase, cFlipper, 0.30 + 0.35 * pr.x);
    plateTone = mix(plateTone, cDark, smoothstep(0.35, 0.78, m2) * (0.42 * mStr + 0.14));
    plateTone = mix(plateTone, mix(cBorder, cHeadBase, 0.55), smoothstep(0.50, 0.85, m1) * 0.26 * mStr);

    // Sutura fina y natural en el margen de la placa que se integra con la piel circundante.
    float fw = max(fwidth(edgeMetric), 0.015);
    float rimMask = smoothstep(0.84 - fw, 0.97 + fw, edgeMetric);
    vec3 rimCol = mix(cBorder * 0.78, cDark * 0.85, 0.52);
    vec3 albedo = mix(plateTone, rimCol, rimMask * 0.65);

    float h = (1.0 - rimMask) * 0.0012 + (m2 - 0.5) * 0.0005;
    vec3 detailedN = RepHeightBump(n, viewPos, h, 0.22);
    float rough = clamp(0.48 + (m1 - 0.5) * 0.08 + rimMask * 0.14, 0.32, 0.86);
    float spec = mix(0.16, 0.05, rimMask);
    return SurfaceResponse(albedo, detailedN, rough, spec);
}
