in vec3 surfacePosition,surfaceNormal,surfacePosedPosition,surfacePosedNormal;
in vec4 surfaceMaterial;
in vec3 surfaceDomainPosition,surfaceDomainNormal,surfaceDomainFlow;
in vec4 surfaceDomainProfileA,surfaceDomainProfileB;
in float surfaceDomainVentral;
flat in uint extractionOverflow;
flat in uint surfaceTissue;
flat in uint surfaceRegionId;
in vec4 surfacePigment0,surfacePigment1,surfacePigment2;
out vec4 fragmentColor;
uniform bool exactMeshMaterial;
void main() {
    if(extractionOverflow!=0u) {fragmentColor=vec4(1,0,0,1);return;}
    MonsterSDFUseCompleteScene();
    vec3 n=normalize(surfaceNormal),rd=normalize(surfacePosition-cameraPosition);
    if(sdfSurfaceDebug==28||exactMeshMaterial)fragmentColor=vec4(SDFSurfaceColorPosed(surfacePosition,n,rd,surfacePosedPosition,normalize(surfacePosedNormal)),1);
    else {
        pigmentCoverage[0]=surfacePigment0;pigmentCoverage[1]=surfacePigment1;pigmentCoverage[2]=surfacePigment2;
        SDFResult semantic=SDFResult(0.0,0u,surfaceRegionId,surfaceTissue,clamp(surfaceMaterial.w,0.0,1.0));
        fragmentColor=vec4(SDFSurfaceColorWithDomain(surfacePosition,n,rd,surfacePosedPosition,normalize(surfacePosedNormal),vec4(surfaceMaterial.rgb,0),semantic,
            surfaceDomainPosition,normalize(surfaceDomainNormal),surfaceDomainProfileA,surfaceDomainProfileB,surfaceDomainVentral,normalize(surfaceDomainFlow)),1);
    }
}
