// Evaluador compartido por raymarch y extracción; mantiene el orden booleano.
bool rayHeadActive=true,rayMouthActive=true,rayJawActive=true;
bool spatialTrace=false;
float outsideBox(vec3 p,vec3 lo,vec3 hi){return length(max(max(lo-p,p-hi),vec3(0)));}
bool pruneBox(vec3 p,vec3 lo,vec3 hi,float scale,float cutoff) {
    vec3 d=max(max(lo-p,p-hi),vec3(0));
    if(cutoff<=0.0)return any(greaterThan(d,vec3(0)));
    float safe=cutoff/max(scale,.0001);
    return dot(d,d)>=safe*safe;
}
vec4 joinField(vec4 a,vec4 b,float k) {
    float h=k<=.0001?(a.w<b.w?1.0:0.0):clamp(.5+.5*(b.w-a.w)/k,0.0,1.0);
    return vec4(mix(b.xyz,a.xyz,h),SDF_SmoothUnion(a.w,b.w,k));
}
vec4 cutField(vec4 a,float d,vec3 color,float k) {
    float h=k<=.0001?(-d>a.w?1.0:0.0):clamp(.5-.5*(a.w+d)/k,0.0,1.0);
    return vec4(mix(a.xyz,color,h),SDF_SmoothSubtract(a.w,d,k));
}
uniform int tileDataBase, tileColumns, validationMode, validationPointBase;
uniform int disableGroupPruning;
int tileOffset, tileCount;
int sceneConnectorIndex(int i) {
    return tileOffset<0?i:int(dataAt(tileOffset+i/4)[i%4]);
}
// Los grupos son rangos contiguos certificados por el núcleo. Saltar el rango
// conserva el orden de las uniones suaves y el subconjunto ordenado de tiles.
bool pruneConnectorGroup(int c,vec3 p,float distance) {
    return disableGroupPruning==0&&C_groupCount(c)>1&&
        pruneBox(p,C_groupBounds_start(c),C_groupBounds_end(c),
            C_groupLowerBoundScale(c),distance+C_groupSmoothness(c));
}
int skipConnectorGroup(int i,int c,int globalIndex) {
    int end=globalIndex+C_groupCount(c);
    if(tileOffset<0)return min(end,tileCount)-1;
    while(i+1<tileCount&&sceneConnectorIndex(i+1)<end)++i;
    return i;
}
// Identidad y cobertura siguen las mismas operaciones booleanas que la geometría.
struct SDFResult {float distance;uint moduleId;uint regionId;uint materialId;float skinWeight;};
// Los IDs de transporte del shader son independientes del enumerado C.
uint tissueMaterial(int material){return material==8?5u:material==9?6u:1u;}
vec4 joinSemantic(inout SDFResult sem,vec4 a,vec4 b,float k,uint module,uint region,uint material,float skin){
    float h=k<=.0001?(a.w<b.w?1.0:0.0):clamp(.5+.5*(b.w-a.w)/k,0.0,1.0);
    sem.skinWeight=mix(skin,sem.skinWeight,h);
    if(h<.5){sem.moduleId=module;sem.regionId=region;sem.materialId=material;}
    return joinField(a,b,k);
}
vec4 cutSemantic(inout SDFResult sem,vec4 a,float d,vec3 color,float k,uint material){
    float h=k<=.0001?(-d>a.w?1.0:0.0):clamp(.5-.5*(a.w+d)/k,0.0,1.0);
    sem.skinWeight*=1.0-h;if(h>=.5)sem.materialId=material;
    return cutField(a,d,color,k);
}
vec4 sceneDetailed(vec3 p,out SDFResult semantic) {
    semantic=SDFResult(1e6,0u,0u,1u,1.0);
    vec4 result=vec4(1,1,1,1e6);
    if(axialCount>1){result=vec4(connectorCount>0?C_color(connectorBase):vec3(1),SDF_EllipticalSweepZ(p,axialBase,axialCount));
        semantic.distance=result.w;
        for(int i=0;i<connectorCount;++i){int c=connectorBase+i*C_STRIDE;
            if(C_kind(c)==0&&!C_isHeadNeck(c)&&p.z<=max(C_a(c).z,C_b(c).z)&&p.z>=min(C_a(c).z,C_b(c).z)){
                semantic.moduleId=uint(C_moduleInstanceId(c));semantic.regionId=uint(C_region(c));break;}}}
    for(int i=0;i<partCount;++i) {
        int b=partBase+i*P_STRIDE;
        result=joinSemantic(semantic,result,vec4(P_color(b),SDF_Ellipsoid(p-P_center(b),P_radii(b))),bodySmoothness,0u,0u,tissueMaterial(P_material(b)),P_material(b)==0?1.0:0.0);
    }
    for(int i=0;i<tileCount;++i) {
        int globalIndex=sceneConnectorIndex(i),c=connectorBase+globalIndex*C_STRIDE;
        if(pruneConnectorGroup(c,p,result.w)){i=skipConnectorGroup(i,c,globalIndex);continue;}
        if(C_isHeadNeck(c)||(axialCount>1&&C_axialSweepMember(c)))continue;
        float k=C_localSmoothness(c);
        if(pruneBox(p,C_bounds_start(c),C_bounds_end(c),C_distanceLowerBoundScale(c),result.w+k))continue;
        result=joinSemantic(semantic,result,vec4(C_color(c),MonsterSDF_EvalConnectorDistance(c,p)),k,uint(C_moduleInstanceId(c)),uint(C_region(c)),tissueMaterial(C_material(c)),C_material(c)==0?1.0:0.0);
    }
    for(int i=0;i<mouthCount;++i) {
        if(!rayHeadActive)continue;
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        if(!M_anatomicalHead(m))result=joinSemantic(semantic,result,vec4(M_skinColor(m),MonsterSDF_EvalMuzzleDistance(m,local)),M_muzzleSmoothness(m),uint(M_moduleInstanceId(m)),1u,1u,1.0);
        vec3 headColor=M_skinColor(m);float headSkin=1.0;uint headMaterial=1u;
        if(M_hasNasalPad(m) && SDF_Ellipsoid(local-M_noseCenterLocal(m),M_noseRadii(m))<M_headUnionSmoothness(m)*.5) {headColor=vec3(30,27,24)/255.0;headSkin=0.0;headMaterial=4u;}
        result=joinSemantic(semantic,result,vec4(headColor,MonsterSDF_EvalUpperHeadDistance(m,local)),M_anatomicalHead(m)?M_headBodySmoothness(m):M_muzzleSmoothness(m),uint(M_moduleInstanceId(m)),1u,headMaterial,headSkin);
    }
    for(int i=0;i<mouthCount;++i) {
        if(!(rayHeadActive||rayMouthActive))continue;
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        if(rayMouthActive) {
            vec3 ext=M_entranceHalfExtents(m),r=M_hostRadii(m);
            vec3 oralRel=local-M_entranceCenterLocal(m);oralRel.y-=M_oralSlope(m)*oralRel.z;
            float entrance=SDF_RoundedSlotExtruded(oralRel,ext.x,ext.y,ext.z);
            vec3 cavityRel=local-M_cavityCenterLocal(m);cavityRel.y-=M_oralSlope(m)*cavityRel.z;
            float cavity=SDF_Ellipsoid(cavityRel,M_cavityRadii(m));
            if(M_sweptSkull(m)) {
                float wall=max(ext.y*2.0,M_faceMidRadii(m).x*.12);
                cavity=max(cavity,SDF_EllipticalSweepZ(local,M_headStations(m),6)+wall);
            }
            float devMouth=clamp((M_cephalicDevelopment(m)-0.05)/0.25,0.0,1.0);
            float cutter=SDF_SmoothUnion(entrance,cavity,M_entranceToCavitySmoothness(m))+(1.0-devMouth)*max(r.x,max(r.y,r.z))*2.0;
            result=cutSemantic(semantic,result,cutter,M_insideColor(m),M_rimBevel(m),2u);
        }
        if(rayHeadActive&&M_anatomicalHead(m)) {
            result=cutSemantic(semantic,result,MonsterSDF_EvalOrbitCavities(m,local),vec3(24,28,19)/255.0,M_headUnionSmoothness(m)*.06,3u);
            result=cutSemantic(semantic,result,MonsterSDF_EvalNostrilCavities(m,local),vec3(18,20,14)/255.0,M_headUnionSmoothness(m)*.04,3u);
            if(M_hasTympana(m))result=cutSemantic(semantic,result,MonsterSDF_EvalTympanumCavities(m,local),vec3(25,24,16)/255.0,M_headUnionSmoothness(m)*.04,3u);
        }
    }
    for(int i=0;i<mouthCount;++i) {
        if(!rayJawActive)continue;
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        float jawWeight,tongueDistance;
        float d=MonsterSDF_EvalPosedMouthMaterial(m,local,jawWeight,tongueDistance);
        if(d<result.w) {
            vec3 col=M_skinColor(m);float jawSkin=1.0;uint jawMaterial=1u;
            if(M_taperedMandible(m)&&tongueDistance<=d+0.005){col=M_insideColor(m);jawSkin=0.0;jawMaterial=2u;}
            // Transportar el anclaje ya resuelto; no repetir la inversa de la costura.
            uint anchor=1u+uint(round(jawWeight*255.0));
            result=vec4(col,d);semantic=SDFResult(d,uint(M_moduleInstanceId(m)),1u,jawMaterial|256u|(anchor<<9),jawSkin);
        }
    }
    semantic.distance=result.w;return result;
}
vec4 scene(vec3 p){SDFResult sem;return sceneDetailed(p,sem);}
// Metadatos de pose persistentes: 8 bits de cobertura y 9 de anclaje mandibular.
// Se calculan al extraer/materializar, nunca mediante evaluación SDF por frame.
float surfacePoseMaterial(vec3 p,SDFResult semantic) {
    uint skin=uint(round(clamp(semantic.skinWeight,0.0,1.0)*255.0));
    uint jaw=(semantic.materialId>>9)&511u;
    return float(skin|(jaw<<8)|(semantic.regionId==1u?131072u:0u)|((semantic.materialId&15u)<<18));
}

float sceneDistance(vec3 p) {
    float result=1e6;
    if(axialCount>1)result=SDF_EllipticalSweepZ(p,axialBase,axialCount);
    for(int i=0;i<partCount;++i) {
        int b=partBase+i*P_STRIDE;
        result=SDF_SmoothUnion(result,SDF_Ellipsoid(p-P_center(b),P_radii(b)),bodySmoothness);
    }
    for(int i=0;i<tileCount;++i) {
        int globalIndex=sceneConnectorIndex(i),c=connectorBase+globalIndex*C_STRIDE;
        if(pruneConnectorGroup(c,p,result)){i=skipConnectorGroup(i,c,globalIndex);continue;}
        if(C_isHeadNeck(c)||(axialCount>1&&C_axialSweepMember(c)))continue;
        float k=C_localSmoothness(c);
        if(pruneBox(p,C_bounds_start(c),C_bounds_end(c),C_distanceLowerBoundScale(c),result+k))continue;
        result=SDF_SmoothUnion(result,MonsterSDF_EvalConnectorDistance(c,p),k);
    }
    for(int i=0;i<mouthCount;++i) {
        if(!rayHeadActive)continue;
        int m=mouthBase+i*M_STRIDE;
        if(spatialTrace&&M_anatomicalHead(m)) {
            float outside=outsideBox(p,M_headBounds_start(m),M_headBounds_end(m));
            if(outside>.02){result=min(result,outside);continue;}
        }
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        if(!M_anatomicalHead(m))result=SDF_SmoothUnion(result,MonsterSDF_EvalMuzzleDistance(m,local),M_muzzleSmoothness(m));
        result=SDF_SmoothUnion(result,MonsterSDF_EvalUpperHeadDistance(m,local),M_anatomicalHead(m)?M_headBodySmoothness(m):M_muzzleSmoothness(m));
    }
    for(int i=0;i<mouthCount;++i) {
        if(!(rayHeadActive||rayMouthActive))continue;
        int m=mouthBase+i*M_STRIDE;
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        if(rayMouthActive) {
            vec3 ext=M_entranceHalfExtents(m),r=M_hostRadii(m);
            vec3 oralRel=local-M_entranceCenterLocal(m);oralRel.y-=M_oralSlope(m)*oralRel.z;
            float entrance=SDF_RoundedSlotExtruded(oralRel,ext.x,ext.y,ext.z);
            vec3 cavityRel=local-M_cavityCenterLocal(m);cavityRel.y-=M_oralSlope(m)*cavityRel.z;
            float cavity=SDF_Ellipsoid(cavityRel,M_cavityRadii(m));
            if(M_sweptSkull(m)) {
                float wall=max(ext.y*2.0,M_faceMidRadii(m).x*.12);
                cavity=max(cavity,SDF_EllipticalSweepZ(local,M_headStations(m),6)+wall);
            }
            float devMouth=clamp((M_cephalicDevelopment(m)-0.05)/0.25,0.0,1.0);
            float cutter=SDF_SmoothUnion(entrance,cavity,M_entranceToCavitySmoothness(m))+(1.0-devMouth)*max(r.x,max(r.y,r.z))*2.0;
            result=SDF_SmoothSubtract(result,cutter,M_rimBevel(m));
        }
        if(rayHeadActive&&M_anatomicalHead(m)) {
            result=SDF_SmoothSubtract(result,MonsterSDF_EvalOrbitCavities(m,local),M_headUnionSmoothness(m)*.06);
            result=SDF_SmoothSubtract(result,MonsterSDF_EvalNostrilCavities(m,local),M_headUnionSmoothness(m)*.04);
            if(M_hasTympana(m))result=SDF_SmoothSubtract(result,MonsterSDF_EvalTympanumCavities(m,local),M_headUnionSmoothness(m)*.04);
        }
    }
    for(int i=0;i<mouthCount;++i) {
        if(!rayJawActive)continue;
        int m=mouthBase+i*M_STRIDE;
        if(spatialTrace&&M_sweptSkull(m)) {
            float outside=outsideBox(p,M_visualBounds_start(m),M_visualBounds_end(m));
            if(outside>.02){result=min(result,outside);continue;}
        }
        vec3 local=M_inverseRotation(m)*(p-M_center(m));
        result=min(result,MonsterSDF_EvalPosedMouthDistance(m,local));
    }
    return result;
}

SDFResult monsterSDFScene(vec3 p){SDFResult sem;sceneDetailed(p,sem);return sem;}
void MonsterSDFUseCompleteScene() {
    tileOffset=-1;tileCount=connectorCount;
    rayHeadActive=true;rayMouthActive=true;rayJawActive=true;
}
vec3 MonsterSDFGradient(vec3 p,float e) {
    const vec3 a=vec3(1,-1,-1),b=vec3(-1,-1,1),c=vec3(-1,1,-1),d=vec3(1,1,1);
    e*=.57735026919;
    vec3 g=a*sceneDistance(p+a*e)+b*sceneDistance(p+b*e)
        +c*sceneDistance(p+c*e)+d*sceneDistance(p+d*e);
    return dot(g,g)>1e-20?normalize(g):vec3(0,1,0);
}
