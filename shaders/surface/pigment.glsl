vec3 EvaluatePigment(vec3 p,float underside,vec4 params,uint seed) {
    vec3 q=p*params.w;
    float footprint=max(length(dFdx(q)),length(dFdy(q)));
    float noise=PigmentNoise(q,seed);
    float bandWave=.5+.5*sin(q.z*5.0+noise*3.0);
    float bands=smoothstep(.48,.7,bandWave);
    float spots=smoothstep(.48,.7,noise);
    float pattern=mix(spots,bands,surfaceRecipe[6].w);
    pattern=mix(pattern,.35,smoothstep(.3,1.0,footprint));
    vec3 color=mix(surfaceRecipe[0].rgb,surfaceRecipe[1].rgb,pattern*params.z);
    color*=1.0-params.x*(1.0-underside);
    return mix(color,surfaceRecipe[2].rgb,underside*params.y);
}
