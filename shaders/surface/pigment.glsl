#include "pigment_regions.glsl"
float SamplePigmentPattern(int pat, vec3 p, float scale, float sharpness, vec3 dir, uint seed) {
    vec3 q = p * max(scale, 0.01);
    float sh = clamp(sharpness, 0.0, 1.0);
    float noise = PigmentNoise(q, seed);
    if (pat == 0) {
        return 1.0;
    } else if (pat == 1) {
        return noise;
    } else if (pat == 2) {
        float w = 0.16 * (1.0 - sh * 0.75);
        return smoothstep(0.56 - w, 0.56 + w, noise);
    } else if (pat == 3) {
        float phase = float(seed & 1023u) * (6.2831853 / 1024.0);
        float wave = 0.5 + 0.5 * sin(q.z * 4.5 + phase + noise * 2.0);
        float w = 0.18 * (1.0 - sh * 0.8);
        return smoothstep(0.50 - w, 0.50 + w, wave);
    } else if (pat == 4) {
        float phase = float(seed & 1023u) * (6.2831853 / 1024.0);
        float wave = 0.5 + 0.5 * sin(q.x * 6.5 + phase + noise * 1.8);
        float w = 0.16 * (1.0 - sh * 0.8);
        return smoothstep(0.50 - w, 0.50 + w, wave);
    } else if (pat == 5) {
        float n1 = PigmentNoise(q * 0.40, seed);
        float n2 = PigmentNoise(q * 0.85, seed + 77u);
        float combined = n1 * 0.70 + n2 * 0.30;
        float w = 0.15 * (1.0 - sh * 0.8);
        return smoothstep(0.48 - w, 0.48 + w, combined);
    } else if (pat == 6) {
        float ringDist = abs(noise - 0.54);
        float wRing = 0.10 * (1.0 - sh * 0.6);
        float ring = 1.0 - smoothstep(0.0, wRing, ringDist);
        float center = 1.0 - smoothstep(0.0, 0.06, abs(noise - 0.72));
        return max(ring, center);
    } else if (pat == 7) {
        vec3 d = length(dir) > 0.001 ? normalize(dir) : vec3(0.0, -1.0, 0.0);
        float grad = dot(p, d) * scale;
        return clamp(grad * 0.5 + 0.5, 0.0, 1.0);
    }
    return 0.0;
}

vec3 EvaluateLocalizedPigmentBank(vec3 p,vec3 baseColor,int bank);
vec3 EvaluatePigmentLegacy(vec3 p, float underside, vec4 params, uint seed) {
    int layerCount = int(surfaceRecipe[7].z + 0.5);
    vec3 color;
    if (layerCount <= 0) {
        vec3 q = p * params.w;
        float footprint = max(length(dFdx(q)), length(dFdy(q)));
        float noise = PigmentNoise(q, seed);
        float bandWave = .5 + .5 * sin(q.z * 5.0 + noise * 3.0);
        float bands = smoothstep(.48, .7, bandWave);
        float spots = smoothstep(.48, .7, noise);
        float pattern = mix(spots, bands, surfaceRecipe[6].w);
        pattern = mix(pattern, .35, smoothstep(.3, 1.0, footprint));
        color = mix(surfaceRecipe[0].rgb, surfaceRecipe[1].rgb, pattern * params.z);
        color *= 1.0 - params.x * (1.0 - underside);
        return mix(color, surfaceRecipe[2].rgb, underside * params.y);
    }

    color = surfaceRecipe[0].rgb;
    for (int i = 0; i < 4; ++i) {
        if (i >= layerCount) break;
        if(PigmentDomainActive(0,i))continue;
        vec4 layerCol = surfaceRecipe[34 + 3 * i];
        vec4 layerParam = surfaceRecipe[35 + 3 * i];
        vec4 layerDir = surfaceRecipe[36 + 3 * i];
        uint layerSeed = seed ^ uint(layerParam.w) ^ uint(i * 101);
        float sampleVal = SamplePigmentPattern(int(layerParam.x + 0.5), p, layerParam.y, layerParam.z, layerDir.xyz, layerSeed);
        float blend = clamp(sampleVal * layerCol.a * pigmentCoverage[0][i] * PigmentDomainWeight(p,0,i), 0.0, 1.0);
        color = mix(color, layerCol.rgb, blend);
    }
    color *= 1.0 - params.x * (1.0 - underside);
    return EvaluateLocalizedPigmentBank(p,mix(color, surfaceRecipe[2].rgb, underside * params.y),0);
}

// Ambos campos conservan sus semillas, índices y composición de capas. El
// transporte anatómico desplaza sus coordenadas; la madurez mezcla el color.
vec3 EvaluatePigmentBank(vec3 p,float underside,int bank) {
    vec4 params=surfaceRecipe[bank+3],meta=surfaceRecipe[bank+4];
    uint seed=uint(meta.z)|(uint(meta.w)<<16u);int count=int(meta.y+.5);
    vec3 color=surfaceRecipe[bank].rgb;
    if(count<=0) {
        vec3 q=p*params.w;float footprint=max(length(dFdx(q)),length(dFdy(q)));
        float noise=PigmentNoise(q,seed);
        float bands=smoothstep(.48,.7,.5+.5*sin(q.z*5.0+noise*3.0));
        float pattern=mix(smoothstep(.48,.7,noise),bands,meta.x);
        pattern=mix(pattern,.35,smoothstep(.3,1.0,footprint));
        color=mix(color,surfaceRecipe[bank+1].rgb,pattern*params.z);
    } else for(int i=0;i<4;++i) {
        if(i>=count)break;
        if(PigmentDomainActive(bank==48?1:2,i))continue;
        vec4 col=surfaceRecipe[bank+5+3*i],layer=surfaceRecipe[bank+6+3*i];
        uint layerSeed=seed^uint(layer.w)^uint(i*101);
        float value=SamplePigmentPattern(int(layer.x+.5),p,layer.y,layer.z,surfaceRecipe[bank+7+3*i].xyz,layerSeed);
        int domainBank=bank==48?1:2;
        color=mix(color,col.rgb,clamp(value*col.a*pigmentCoverage[domainBank][i]*PigmentDomainWeight(p,domainBank,i),0.0,1.0));
    }
    color*=1.0-params.x*(1.0-underside);
    return EvaluateLocalizedPigmentBank(p,mix(color,surfaceRecipe[bank+2].rgb,underside*params.y),bank==48?1:2);
}
vec3 EvaluatePigment(vec3 p,float underside,vec4 params,uint seed) {
    if(surfaceRecipe[46].x<.5)return EvaluatePigmentLegacy(p,underside,params,seed);
    return mix(EvaluatePigmentBank(p,underside,48),EvaluatePigmentBank(p,underside,65),surfaceRecipe[46].y);
}
// x: cobertura de pelo, y: rugosidad, z: peso de piel localizada.
vec3 EvaluatePigmentMaterialBank(vec3 p,int bank,float roughness) {
    int base=bank==0?34:bank==1?53:70;
    int count=int((bank==0?surfaceRecipe[7].z:surfaceRecipe[bank==1?52:69].y)+.5);
    vec3 material=vec3(1,roughness,0);
    for(int i=0;i<4;++i) {
        if(i>=count)break;
        if(!PigmentDomainActive(bank,i))continue;
        vec4 col=surfaceRecipe[base+3*i],param=surfaceRecipe[base+1+3*i],dir=surfaceRecipe[base+2+3*i];
        uint seed=bank==0?pigmentSeed:uint(surfaceRecipe[bank==1?52:69].z)|(uint(surfaceRecipe[bank==1?52:69].w)<<16u);
        float value=SamplePigmentPattern(int(param.x+.5),p,param.y,param.z,dir.xyz,seed^uint(param.w)^uint(i*101));
        vec4 local=surfaceRecipe[105+12*bank+3*i];
        float weight=clamp(value*col.a*pigmentCoverage[bank][i]*PigmentDomainWeight(p,bank,i),0.0,1.0)*local.x;
        material.xy=mix(material.xy,local.yz,weight);
        material.z=1.0-(1.0-material.z)*(1.0-weight);
    }
    return material;
}
vec3 EvaluatePigmentMaterial(vec3 p,float roughness) {
    if(surfaceRecipe[46].x<.5)return EvaluatePigmentMaterialBank(p,0,roughness);
    return mix(EvaluatePigmentMaterialBank(p,1,roughness),EvaluatePigmentMaterialBank(p,2,roughness),surfaceRecipe[46].y);
}
float PigmentDomainDebug(vec3 p) {
    float banks[3];
    for(int b=0;b<3;++b) {
        banks[b]=0;
        int count=int((b==0?surfaceRecipe[7].z:surfaceRecipe[b==1?52:69].y)+.5);
        for(int i=0;i<4;++i) {
            if(i>=count)break;
            if(PigmentDomainActive(b,i))banks[b]=max(banks[b],PigmentDomainWeight(p,b,i)*pigmentCoverage[b][i]);
        }
    }
    return surfaceRecipe[46].x<.5?banks[0]:mix(banks[1],banks[2],surfaceRecipe[46].y);
}
vec3 EvaluateLocalizedPigmentBank(vec3 p,vec3 baseColor,int bank) {
    int base=bank==0?34:bank==1?53:70;
    int count=int((bank==0?surfaceRecipe[7].z:surfaceRecipe[bank==1?52:69].y)+.5);
    for(int i=0;i<4;++i) {
        if(i>=count)break;
        if(!PigmentDomainActive(bank,i))continue;
        vec4 col=surfaceRecipe[base+3*i],param=surfaceRecipe[base+1+3*i],dir=surfaceRecipe[base+2+3*i];
        uint seed=bank==0?pigmentSeed:uint(surfaceRecipe[bank==1?52:69].z)|(uint(surfaceRecipe[bank==1?52:69].w)<<16u);
        float value=SamplePigmentPattern(int(param.x+.5),p,param.y,param.z,dir.xyz,seed^uint(param.w)^uint(i*101));
        baseColor=mix(baseColor,col.rgb,clamp(value*col.a*pigmentCoverage[bank][i]*PigmentDomainWeight(p,bank,i),0.0,1.0));
    }
    return baseColor;
}
vec3 EvaluateLocalizedPigment(vec3 p,vec3 baseColor) {
    if(surfaceRecipe[46].x<.5)return EvaluateLocalizedPigmentBank(p,baseColor,0);
    return mix(EvaluateLocalizedPigmentBank(p,baseColor,1),EvaluateLocalizedPigmentBank(p,baseColor,2),surfaceRecipe[46].y);
}
