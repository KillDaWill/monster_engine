#version 330 compatibility
// Recorte de cobertura: sin ordenación de miles de hijos ni caras desaparecidas.
in vec4 hairColor;
in float ribbonSide,hairParameter;
void main(){
    float alpha=hairColor.a*(1.0-smoothstep(.55,1.0,abs(ribbonSide)));
    if(alpha<.35)discard;
    float lighting=.8+.2*(1.0-ribbonSide*ribbonSide);
    gl_FragColor=vec4(hairColor.rgb*lighting,1);
}
