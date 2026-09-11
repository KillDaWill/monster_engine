#!/usr/bin/env python3
"""Genera la evaluación GLSL y su empaquetado desde las recetas C del núcleo.

La traducción es deliberadamente acotada: falla ante tipos o accesos desconocidos.
No copia a mano las fórmulas anatómicas; los cambios C regeneran el shader.
"""
from pathlib import Path
import re, json
ROOT=Path(__file__).resolve().parents[1]

def function(path,name):
    s=(ROOT/path).read_text()
    m=re.search(r'(?:static\s+)?(?:inline\s+)?(?:float|void)\s+'+name+r'\s*\([^;]*?\)\s*\{',s)
    if not m: raise ValueError(name)
    start=s.index('{',m.start());depth=1;i=start+1
    while depth:
        depth+=(s[i]=='{')-(s[i]=='}');i+=1
    return s[m.start():i]

prefix='''#version 330 compatibility
uniform samplerBuffer sceneData;
uniform int partBase, partCount, connectorBase, connectorCount, mouthBase, mouthCount, axialBase, axialCount;
uniform float bodySmoothness;
#define Vector3 vec3
#define Vec3_Create vec3
#define Vec3_Sub(a,b) ((a)-(b))
#define Vec3_Add(a,b) ((a)+(b))
#define Vec3_Scale(a,b) ((a)*(b))
#define Vec3_Lerp mix
#define Vec3_Dot dot
#define Vec3_Cross cross
#define Vec3_Normalize normalize
#define Vec3_Distance distance
#define cosf cos
#define sinf sin
#define Vec3_Length length
#define Vec3_LengthSq(a) dot(a,a)
#define Math_Max max
#define Math_Min min
#define Math_Lerp mix
#define Math_Clamp clamp
#define Math_Clamp01(a) clamp(a,0.0,1.0)
#define fabsf abs
#define sqrtf sqrt
vec4 dataAt(int index) { return texelFetch(sceneData,index); }
'''
pack=['/* Generado por tools/generate_sdf_shader.py. No editar. */']
access=[]
header=re.sub(r'/\*.*?\*/','',(ROOT/'include/MonsterSDF.h').read_text(),flags=re.S)
for typ,short in [('MonsterSDFBodyPart','P'),('MonsterSDFConnector','C'),('MonsterSDFMouth','M')]:
    body=re.search(r'typedef struct '+typ+r'\s*\{(.*?)\}',header,re.S)[1]
    fields=[]
    for decl in body.split(';'):
        decl=decl.strip()
        if not decl:continue
        t,names=decl.split(None,1)
        fields.extend((t,n.strip()) for n in names.split(','))
    offset=0;code=[f'static void Pack{short}(float (*out)[4], const {typ}* in) {{']
    for t,name in fields:
        arr=re.fullmatch(r'(\w+)\[(\d+)\]',name)
        if t=='SDFSweepStation' and arr:
            name=arr[1];n=int(arr[2]);access.append(f'int {short}_{name}(int i) {{ return i+{offset}; }}')
            for j in range(n):code.append(f'    PackStation(out+{offset+j*2}, &in->{name}[{j}]);')
            offset+=n*2;continue
        if t=='Vector3':
            access.append(f'vec3 {short}_{name}(int i) {{ return dataAt(i+{offset}).xyz; }}')
            code.append(f'    PackVector(out[{offset}], in->{name});');offset+=1
        elif t=='Color':
            access.append(f'vec3 {short}_{name}(int i) {{ return dataAt(i+{offset}).xyz; }}')
            code.append(f'    PackColor(out[{offset}], in->{name});');offset+=1
        elif t=='RotationBasis3D':
            access.append(f'mat3 {short}_{name}(int i) {{ return transpose(mat3(dataAt(i+{offset}).xyz,dataAt(i+{offset+1}).xyz,dataAt(i+{offset+2}).xyz)); }}')
            for j in range(3):code.append(f'    PackVector(out[{offset+j}], in->{name}.row{j});')
            offset+=3
        elif t=='AABB3D':
            for j,edge in enumerate(['start','end']):
                access.append(f'vec3 {short}_{name}_{edge}(int i) {{ return dataAt(i+{offset+j}).xyz; }}')
                code.append(f'    PackVector(out[{offset+j}], in->{name}.{edge});')
            offset+=2
        elif t in ['float','bool','size_t','AnatomyId','BodyConnectionKind']:
            gt='float' if t=='float' else 'bool' if t=='bool' else 'int'
            access.append(f'{gt} {short}_{name}(int i) {{ return {gt}(dataAt(i+{offset}).x); }}')
            code.append(f'    out[{offset}][0]=(float)in->{name};');offset+=1
        else:raise ValueError((t,name))
    access.append(f'#define {short}_STRIDE {offset}')
    pack.extend([f'#define {short}_STRIDE {offset}',*code,'}'])

functions=[]
for name in ['SDF_Sphere','SDF_Ellipsoid','SDF_Capsule','SDF_TaperedCapsuleApprox','SDF_TaperedEllipticalCapsuleApprox','SDF_ThreeSectionEllipticalLoftApprox','SDF_TaperedEllipticalSegmentApprox','SDF_RoundedSlotExtruded','SDF_Hermite']:
    functions.append(function('src/SDFPrimitives.c',name))
for name in ['SDF_SmoothUnion','SDF_SmoothSubtract']:
    functions.append(function('src/SDFOperations.c',name))
# Declaraciones para llamadas hacia delante.
prototypes='float SDF_SmoothUnion(float a,float b,float k);\n'
sweep='''float SDF_EllipticalSweepZ(vec3 p,int base,int count) {
    int i=0;
    while(i<count-2 && p.z<dataAt(base+2*(i+1)).z) ++i;
    vec4 a=dataAt(base+2*i), b=dataAt(base+2*(i+1));
    vec4 ar=dataAt(base+2*i+1), br=dataAt(base+2*(i+1)+1);
    float h=b.z-a.z,t=clamp((p.z-a.z)/h,0.0,1.0);
    float w=max(SDF_Hermite(a.w,b.w,ar.y,br.y,h,t),.0001);
    float v=max(SDF_Hermite(ar.x,br.x,ar.z,br.z,h,t),.0001);
    float y=SDF_Hermite(a.y,b.y,ar.w,br.w,h,t),x=mix(a.x,b.x,t);
    float first=dataAt(base).z,last=dataAt(base+2*(count-1)).z;
    float z=p.z>first?p.z-first:p.z<last?p.z-last:0.0;
    float r=min(w,v);
    return (length(vec3((p.x-x)/w,(p.y-y)/v,z/r))-1.0)*r;
}
'''
heads=[]
for name in ['MonsterSDF_GetBasinParams','MonsterSDF_EvalJawBase','MonsterSDF_EvalSeamDistance','MonsterSDF_EvalJawCarvedDistance','MonsterSDF_EvalPosedMouthDistance','MonsterSDF_EvalConnectorDistance','MonsterSDF_EvalMuzzleDistance','MonsterSDF_EvalShallowCutter','MonsterSDF_EvalOrbitCavities','MonsterSDF_EvalNostrilCavities','MonsterSDF_EvalTympanumCavities','MonsterSDF_EvalRostrumDistance','MonsterSDF_EvalPeriorbitalDistance','MonsterSDF_EvalNeckCollarDistance','MonsterSDF_EvalUpperHeadDistance']:
    heads.append(function('src/MonsterSDF.c',name))

def translate(s):
    s=re.sub(r'/\*.*?\*/','',s,flags=re.S)
    s=re.sub(r'\b(static|inline)\s+','',s)
    s=re.sub(r'(?<=\d)f\b','',s)
    s=s.replace('const MonsterSDFMouth* mouth','int mouth').replace('const MonsterSDFConnector* conn','int conn')
    for name,typ in [('outCenter','Vector3'),('outRadii','Vector3'),('outK','float')]:
        s=s.replace(f'{typ}* {name}',f'out {typ} {name}')
        s=s.replace(f'if ({name}) *{name}',name)
    s=re.sub(r'&(basinCenter|basinRadii|k)\b',r'\1',s)
    s=re.sub(r'mouth->(\w+)',r'M_\1(mouth)',s)
    s=re.sub(r'conn->(\w+)',r'C_\1(conn)',s)
    if '->' in s:raise ValueError(s)
    return s
shader=prefix+'\n'.join(access)+'\n'+prototypes+'\n'.join(map(translate,functions))+sweep+'\n'.join(map(translate,heads))+'\n'+(ROOT/'shaders/monster_sdf_trace.glsl').read_text()
# Fragmentos pequeños evitan exceder el mínimo C99 de longitud de literal.
version,shader=shader.split("\n",1)
chunks=[version+"\n"]+[shader[i:i+2000] for i in range(0,len(shader),2000)]
pack+=['static const char* const sdfFragmentSource[] = {',*(json.dumps(c)+',' for c in chunks),'};']
(ROOT/'src/MonsterSDFShader.generated.h').write_text('\n'.join(pack)+'\n')
