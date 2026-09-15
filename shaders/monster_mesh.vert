#version 330 compatibility
layout(location=8) in vec3 materialCoord;
layout(location=9) in vec3 materialNormal;
layout(location=10) in vec4 surfaceRegion;
layout(location=11) in float anatomicalMaterial;
uniform vec4 surfaceRecipe[28];
out vec3 restCoord, restNormal, viewPosition, viewNormal;
out vec4 vertexColor, profileA, profileB;
out float ventral, skinMask;
void main() {
    gl_Position=gl_ModelViewProjectionMatrix*gl_Vertex;
    viewPosition=(gl_ModelViewMatrix*gl_Vertex).xyz;
    viewNormal=normalize(gl_NormalMatrix*gl_Normal);
    restCoord=materialCoord; restNormal=materialNormal; vertexColor=gl_Color;
    int a=clamp(int(surfaceRegion.x+.5),0,9),b=clamp(int(surfaceRegion.y+.5),0,9);
    float blend=clamp(surfaceRegion.z,0.0,1.0);
    profileA=mix(surfaceRecipe[8+2*a],surfaceRecipe[8+2*b],blend);
    profileB=mix(surfaceRecipe[9+2*a],surfaceRecipe[9+2*b],blend);
    // La mezcla ventral es continua y se calcula en reposo, no en el mundo animado.
    float trunkA=float(a==3 || a==4),trunkB=float(b==3 || b==4);
    float belly=surfaceRegion.w*mix(trunkA,trunkB,blend);
    profileA=mix(profileA,surfaceRecipe[16],belly);
    profileB=mix(profileB,surfaceRecipe[17],belly);
    ventral=surfaceRegion.w;
    skinMask=1.0-step(.5,anatomicalMaterial);
}
