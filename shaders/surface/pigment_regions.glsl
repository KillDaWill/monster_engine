#ifndef MONSTER_PIGMENT_REGIONS
#define MONSTER_PIGMENT_REGIONS
// Cuatro máscaras por banco; cero en el documento significa todas las regiones.
vec4 pigmentCoverage[3];
// Tres filas por capa y banco; los índices anteriores de receta no cambian.
bool PigmentDomainActive(int bank,int layer) {
    return all(greaterThan(surfaceRecipe[103+12*bank+3*layer+1].xyz,vec3(0)));
}
float PigmentDomainWeight(vec3 p,int bank,int layer) {
    int row=103+12*bank+3*layer;
    vec4 center=surfaceRecipe[row],radii=surfaceRecipe[row+1];
    if(any(lessThanEqual(radii.xyz,vec3(0))))return 1.0;
    vec3 q=(p-center.xyz)/radii.xyz;
    float phase=surfaceRecipe[row+2].w*.00613592315;
    float warp=sin(q.x*3.1+phase)*sin(q.y*4.3-phase)*sin(q.z*3.7+phase);
    float t=clamp((1.0-length(q)-radii.w*.15*warp)/max(center.w,.001),0.0,1.0);
    return t*t*(3.0-2.0*t);
}
float PigmentRegionBit(float encodedMask,int region) {
    uint mask=uint(encodedMask+.5);
    return mask==0u?1.0:float((mask&(1u<<uint(clamp(region,0,10))))!=0u);
}
void PigmentClearRegions() {for(int bank=0;bank<3;++bank)pigmentCoverage[bank]=vec4(0);}
void PigmentAccumulateRegion(int region,float weight) {
    for(int bank=0;bank<3;++bank)for(int layer=0;layer<4;++layer) {
        int row=bank==0?36:bank==1?55:72;
        pigmentCoverage[bank][layer]+=weight*PigmentRegionBit(surfaceRecipe[row+3*layer].w,region);
    }
}
void PigmentNormalizeRegions(float sum) {for(int bank=0;bank<3;++bank)pigmentCoverage[bank]/=max(sum,1e-12);}
void PigmentSetRegions(vec4 regions) {
    PigmentClearRegions();
    int a=int(regions.x+.5),b=int(regions.y+.5);float t=clamp(regions.z,0.0,1.0);
    float belly=clamp(regions.w,0.0,1.0);
    PigmentAccumulateRegion(a,(1-t)*(a==3||a==4?1-belly:1));
    PigmentAccumulateRegion(b,t*(b==3||b==4?1-belly:1));
    PigmentAccumulateRegion(4,belly*((a==3||a==4?1-t:0)+(b==3||b==4?t:0)));
}
void PigmentSetAnatomyRegion(int anatomy,float belly) {
    int region=anatomy==1?1:anatomy==2?2:anatomy==3||anatomy==4?3:
        anatomy==5?5:anatomy==6?6:anatomy==9?7:anatomy==10?8:anatomy==11?10:0;
    PigmentSetRegions(vec4(float(region),float(region),0,belly));
}
#endif
