#version 330 compatibility
// Núcleo opaco y borde estrecho mezclado; A2C cuando hay multimuestreo.
in vec4 hairColor;
in float ribbonSide,hairParameter;
in float strandCoverage;
in vec3 hairTangent, hairViewPosition;
#include "environment/scene_lighting.glsl"
uniform int coveragePass;
void main(){
    float aa=max(fwidth(ribbonSide),.06);
    float alpha=hairColor.a*strandCoverage*(1.0-smoothstep(1.0-aa,1.0,abs(ribbonSide)));
    if(alpha<=.00001)discard;
    if(coveragePass==0&&alpha<.92)discard;
    if(coveragePass==1&&alpha>=.92)discard;
    vec3 light=sceneNatural()?sceneSunView.xyz:normalize(vec3(-.4,.8,.5));
    float longitudinal=sqrt(max(0.0,1.0-pow(dot(normalize(hairTangent+vec3(1e-8)),light),2.0)));
    float lighting=.65+.35*longitudinal+.1*(1.0-ribbonSide*ribbonSide);
    vec3 color = hairColor.rgb * lighting;
    if (sceneNatural()) {
        vec3 world = sceneWorldPosition(hairViewPosition);
        float direct = sceneVisibility(world, vec3(0,1,0));
        color = sceneObjectOutput(sceneDecode(hairColor.rgb) *
            (sceneAmbient(.25) + sceneSunRadiance.rgb * (.65 * direct * longitudinal)), world);
    }
    gl_FragColor=vec4(color,coveragePass==0?1.0:alpha);
}
