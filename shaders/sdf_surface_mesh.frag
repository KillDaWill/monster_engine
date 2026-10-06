in vec3 surfacePosition,surfaceNormal;
flat in uint extractionOverflow;
out vec4 fragmentColor;
void main() {
    if(extractionOverflow!=0u) {fragmentColor=vec4(1,0,0,1);return;}
    MonsterSDFUseCompleteScene();
    vec3 n=normalize(surfaceNormal),rd=normalize(surfacePosition-cameraPosition);
    fragmentColor=vec4(SDFSurfaceColor(surfacePosition,n,rd),1);
}
