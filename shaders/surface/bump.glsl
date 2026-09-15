// Gradiente superficial en espacio de vista: no exige UV ni tangentes precalculadas.
vec3 SurfaceBump(vec3 n,vec3 p,vec3 coord,vec3 heightGradient) {
    vec3 dx=dFdx(p),dy=dFdy(p);
    vec3 rx=cross(dy,n),ry=cross(n,dx);
    float determinant=dot(dx,rx);
    float scale=sqrt((dot(dx,dx)+dot(dy,dy))/max(dot(dFdx(coord),dFdx(coord))+dot(dFdy(coord),dFdy(coord)),1e-12));
    vec3 gradient=(dot(heightGradient,dFdx(coord))*rx+dot(heightGradient,dFdy(coord))*ry)*sign(determinant)*scale/max(abs(determinant),1e-10);
    // Acota pendientes extremas en bordes de triángulos casi tangentes a la cámara.
    gradient/=max(1.0,length(gradient)/1.5);
    return normalize(n-gradient);
}
