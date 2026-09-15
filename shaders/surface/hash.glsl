// Hash entero: identidad exacta de 32 bits, sin semillas temporales ni texturas.
uint SurfaceHash(uint x) {
    x^=x>>16; x*=0x7feb352du; x^=x>>15; x*=0x846ca68bu; return x^(x>>16);
}
vec3 CellHash(ivec2 cell,uint seed) {
    uint h=SurfaceHash(uint(cell.x)*0x9e3779b9u^uint(cell.y)*0x85ebca6bu^seed);
    return vec3(h>>8,SurfaceHash(h)>>8,SurfaceHash(h+1u)>>8)*(1.0/16777216.0);
}
float PigmentNoise(vec3 p,uint seed) {
    ivec3 i=ivec3(floor(p)); vec3 f=fract(p); f=f*f*(3.0-2.0*f);
    float z[2];
    for(int k=0;k<2;++k) {
        float a=CellHash(i.xy,seed^uint(i.z+k)).x,b=CellHash(i.xy+ivec2(1,0),seed^uint(i.z+k)).x;
        float c=CellHash(i.xy+ivec2(0,1),seed^uint(i.z+k)).x,d=CellHash(i.xy+ivec2(1,1),seed^uint(i.z+k)).x;
        z[k]=mix(mix(a,b,f.x),mix(c,d,f.x),f.y);
    }
    return mix(z[0],z[1],f.z);
}
