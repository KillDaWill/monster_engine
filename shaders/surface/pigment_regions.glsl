#ifndef MONSTER_PIGMENT_REGIONS
#define MONSTER_PIGMENT_REGIONS
// Cuatro máscaras por banco; cero en el documento significa todas las regiones.
vec4 pigmentCoverage[3];
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
