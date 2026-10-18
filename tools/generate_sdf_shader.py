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
    m=re.search(r'(?:static\s+)?(?:inline\s+)?(?:float|void|SDFInterval)\s+'+name+r'\s*\([^;]*?\)\s*\{',s)
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
vec4 dataAt(int index) {return texelFetch(sceneData,index);}


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
    offset=0;scalar_row=None;scalar_component=4
    code=[f'static void Pack{short}(float (*out)[4], const {typ}* in) {{']
    for t,name in fields:
        if name in ['isDistalLimb','development'] and typ=='MonsterSDFConnector':continue
        arr=re.fullmatch(r'(\w+)\[(\d+)\]',name)
        if t=='SDFSweepStation' and arr:
            name=arr[1];n=int(arr[2]);access.append(f'int {short}_{name}(int i) {{ return i+{offset}; }}')
            for j in range(n):code.append(f'    PackStation(out+{offset+j*2}, &in->{name}[{j}]);')
            offset+=n*2;scalar_row=None;scalar_component=4;continue
        if t=='Vector3':
            access.append(f'vec3 {short}_{name}(int i) {{ return dataAt(i+{offset}).xyz; }}')
            code.append(f'    PackVector(out[{offset}], in->{name});');scalar_row=offset;scalar_component=3;offset+=1
        elif t=='Color':
            access.append(f'vec3 {short}_{name}(int i) {{ return dataAt(i+{offset}).xyz; }}')
            code.append(f'    PackColor(out[{offset}], in->{name});');scalar_row=offset;scalar_component=3;offset+=1
        elif t=='RotationBasis3D':
            access.append(f'mat3 {short}_{name}(int i) {{ return transpose(mat3(dataAt(i+{offset}).xyz,dataAt(i+{offset+1}).xyz,dataAt(i+{offset+2}).xyz)); }}')
            for j in range(3):code.append(f'    PackVector(out[{offset+j}], in->{name}.row{j});')
            scalar_row=offset+2;scalar_component=3;offset+=3
        elif t=='AABB3D':
            for j,edge in enumerate(['start','end']):
                access.append(f'vec3 {short}_{name}_{edge}(int i) {{ return dataAt(i+{offset+j}).xyz; }}')
                code.append(f'    PackVector(out[{offset+j}], in->{name}.{edge});')
            scalar_row=offset+1;scalar_component=3;offset+=2
        elif t in ['float','bool','size_t','AnatomyId','BodyConnectionKind','AnatomyRegion','uint32_t']:
            gt='float' if t=='float' else 'bool' if t=='bool' else 'int'
            if scalar_component==4:
                scalar_row=offset;offset+=1;scalar_component=0
            component='xyzw'[scalar_component]
            access.append(f'{gt} {short}_{name}(int i) {{ return {gt}(dataAt(i+{scalar_row}).{component}); }}')
            code.append(f'    out[{scalar_row}][{scalar_component}]=(float)in->{name};')
            scalar_component+=1
        else:raise ValueError((t,name))
    access.append(f'#define {short}_STRIDE {offset}')
    pack.extend([f'#define {short}_STRIDE {offset}',*code,'}'])

functions=[]
for name in ['SDF_SinHalfTurn','SDF_CurvedPinna','SDF_TaperedPinna','SDF_Sphere','SDF_Ellipsoid','SDF_Capsule','SDF_TaperedCapsuleApprox','SDF_TaperedEllipticalCapsuleApprox','SDF_ThreeSectionEllipticalLoftApprox','SDF_TaperedEllipticalSegmentApprox','SDF_RoundedSlotExtruded','SDF_Hermite']:
    functions.append(function('src/SDFPrimitives.c',name))
for name in ['SDF_SmoothUnion','SDF_SmoothSubtract']:
    functions.append(function('src/SDFOperations.c',name))
# Declaraciones para llamadas hacia delante.
prototypes='float SDF_SmoothUnion(float a,float b,float k);\n'
sweep='''uniform int sweepBinarySearch;
float SDF_EllipticalSweepZ(vec3 p,int base,int count) {
    int i=0;
    if(sweepBinarySearch!=0) {
        int last=count-2;
        while(i<last) {
            int middle=(i+last)/2;
            if(p.z<dataAt(base+2*(middle+1)).z)i=middle+1;else last=middle;
        }
    } else while(i<count-2 && p.z<dataAt(base+2*(i+1)).z) ++i;
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
for name in ['MonsterSDF_GetBasinParams','MonsterSDF_EvalJawBase','MonsterSDF_EvalTongueDistance','MonsterSDF_EvalSeamDistance','MonsterSDF_EvalJawCarvedDistance','MonsterSDF_EvalPosedMouthDistance','MonsterSDF_EvalConnectorDistance','MonsterSDF_EvalMuzzleDistance','MonsterSDF_EvalShallowCutter','MonsterSDF_EvalOrbitCavities','MonsterSDF_EvalNostrilCavities','MonsterSDF_EvalTympanumCavities','MonsterSDF_EvalRostrumDistance','MonsterSDF_EvalPeriorbitalDistance','MonsterSDF_EvalNeckCollarDistance','MonsterSDF_EvalUpperHeadDistance']:
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

def posed_material_source():
    """Extrae anclajes de la misma inversa C sin repetir sus seis iteraciones.

    La especialización sólo afecta a GLSL: el evaluador CPU y el shader de
    distancia conservan exactamente sus operaciones y su orden aritmético.
    Los reemplazos son deliberadamente estrictos ante cambios de la receta.
    """
    source=translate(function('src/MonsterSDF.c','MonsterSDF_EvalPosedMouthDistance'))
    def replace_once(old,new):
        nonlocal source
        if source.count(old)!=1:raise ValueError('Especialización mandibular: '+old)
        source=source.replace(old,new)
    replace_once('MonsterSDF_EvalPosedMouthDistance(int mouth,Vector3 localP) {',
        'MonsterSDF_EvalPosedMouthMaterial(int mouth,Vector3 localP,out float jawWeight,out float tongueDistance) {\n'
        '    jawWeight=1.0;tongueDistance=1e6;')
    jaw='float jaw=MonsterSDF_EvalJawCarvedDistance(mouth,rest)*scale;'
    replace_once(jaw,jaw+'\n    if(M_taperedMandible(mouth))tongueDistance=MonsterSDF_EvalTongueDistance(mouth,rest)*scale;')
    replace_once('float h=Math_Max(M_seamScale(mouth),.0001);',
        'float h=Math_Max(M_seamScale(mouth),.0001),weight=1.0;')
    angle='float a=angle*w*w*(3.0-2.0*w);'
    # Con ángulo cero, una sola evaluación da el mismo anclaje que seis idénticas.
    weights=source[source.index('        float skull='):source.index('        '+angle)]
    replace_once(angle,'weight=w*w*(3.0-2.0*w);'+angle)
    replace_once('return Math_Min(jaw,MonsterSDF_EvalSeamDistance(mouth,rest)*scale);',
        'float posedDistance=Math_Min(jaw,MonsterSDF_EvalSeamDistance(mouth,rest)*scale);\n'
        '    if(jaw>posedDistance+.00001) {\n        if(angle==0.0) {\n'+weights+
        '            weight=w*w*(3.0-2.0*w);\n        }\n        jawWeight=weight;\n    }\n    return posedDistance;')
    return source

surface_modules=['hash','scales','pigment','fur_curve','fur','bump','lighting','evaluate']
surface='uniform vec4 surfaceRecipe[82];\nuniform uint pigmentSeed,scaleSeed,furSeed;\nuniform int sdfHasSurface,sdfFurEnabled;\n'
surface+='\n'.join(re.sub(r'^#include .*\n','',
    (ROOT/f'shaders/surface/{name}.glsl').read_text(),flags=re.M) for name in surface_modules)
domain=(ROOT/'shaders/surface/morph_domain.glsl').read_text()
handoff=(ROOT/'shaders/morph_handoff.glsl').read_text()+'\n'+domain
core=prefix.split("\n",1)[1]+'\n'.join(access)+'\n'+prototypes+'\n'.join(map(translate,functions))+sweep+'\n'.join(map(translate,heads))+'\n'+posed_material_source()+'\n'+(ROOT/'shaders/sdf_scene.glsl').read_text()
material=(ROOT/'shaders/sdf_surface_material.glsl').read_text()
shader=prefix.split("\n",1)[0]+'\n'+core+'\n'+surface+'\n'+handoff+'\nuniform vec3 cameraPosition,cameraForward,cameraRight,cameraUp;\n'+material+'\n'+(ROOT/'shaders/monster_sdf_trace.glsl').read_text()
def embed(name,source):
    version,body=source.split("\n",1)
    chunks=[version+"\n"]+[body[i:i+2000] for i in range(0,len(body),2000)]
    return ['static const char* const '+name+'[] = {',*(json.dumps(c)+',' for c in chunks),'};']
pack+=embed('sdfFragmentSource',shader)
# Variante estructural de una boca: los accesos M usan índices uniformes fijos.
# No especializa semillas, edades ni dimensiones de la criatura.
cached=shader
for entry in access:
    if ' M_' not in entry or 'dataAt(i+' not in entry:continue
    replacement=re.sub(r'dataAt\(i\+(\d+)\)',r'mouthValues[\1]',entry)
    cached=cached.replace(entry,replacement)
# El barrido cefálico tiene seis estaciones por contrato; índices fijos evitan
# lecturas TBO dependientes en cada evaluación de cráneo, mandíbula y cavidad.
def cached_sweep(field,label):
    entry=next(entry for entry in access if entry.startswith('int M_'+field+'('))
    offset=int(re.search(r'i\+(\d+)',entry)[1])
    result=f'float SDF_{label}(vec3 p) {{vec4 a,b,ar,br;\n'
    for i in range(5):
        condition=f'if(p.z>=mouthValues[{offset+2*(i+1)}].z)' if i==0 else f'else if(p.z>=mouthValues[{offset+2*(i+1)}].z)' if i<4 else 'else'
        result+=condition+'{'+f'a=mouthValues[{offset+2*i}];b=mouthValues[{offset+2*i+2}];ar=mouthValues[{offset+2*i+1}];br=mouthValues[{offset+2*i+3}];'+'}\n'
    result+=sweep[sweep.index('    float h=b.z-a.z'):sweep.index('    float first=')]
    result+=f'float first=mouthValues[{offset}].z,last=mouthValues[{offset+10}].z;\n'
    result+=sweep[sweep.index('    float z=p.z>first'):]
    return result
head_sweep=cached_sweep('headStations','HeadSweep')
jaw_sweep=cached_sweep('jawStations','JawSweep')
cached=re.sub(r'SDF_EllipticalSweepZ\(([^,]+),M_headStations\([^)]*\),6\)',r'SDF_HeadSweep(\1)',cached)
cached=re.sub(r'SDF_EllipticalSweepZ\(([^,]+),M_jawStations\([^)]*\),6\)',r'SDF_JawSweep(\1)',cached)
cached=cached.replace('void MonsterSDF_GetBasinParams',head_sweep+'\n'+jaw_sweep+'\nvoid MonsterSDF_GetBasinParams',1)
version,body=cached.split('\n',1)
pack+=embed('sdfCachedFragmentSource',version+'\nlayout(std140) uniform SDFMouthCache {vec4 mouthValues[1024];};\n'+body)
(ROOT/'src/MonsterSDFShader.generated.h').write_text('\n'.join(pack)+'\n')
compute=['/* Generado; editar shaders/ o el generador. */']
# La misma especialización cefálica se reutiliza en muestreo y raíces GPU.
cached_core=core
for entry in access:
    if ' M_' in entry and 'dataAt(i+' in entry:
        cached_core=cached_core.replace(entry,re.sub(r'dataAt\(i\+(\d+)\)',r'mouthValues[\1]',entry))
cached_core=re.sub(r'SDF_EllipticalSweepZ\(([^,]+),M_headStations\([^)]*\),6\)',r'SDF_HeadSweep(\1)',cached_core)
cached_core=re.sub(r'SDF_EllipticalSweepZ\(([^,]+),M_jawStations\([^)]*\),6\)',r'SDF_JawSweep(\1)',cached_core)
cached_core=cached_core.replace('void MonsterSDF_GetBasinParams',head_sweep+'\n'+jaw_sweep+'\nvoid MonsterSDF_GetBasinParams',1)
cached_core='layout(std140,binding=0) uniform SDFMouthCache {vec4 mouthValues[1024];};\n'+cached_core
for name,file,scene in [('sdfSampleSource','sdf_surface_sample.comp',True),('sdfClassifySource','sdf_surface_classify.comp',False),('sdfEmitSource','sdf_surface_emit.comp',True),('sdfFinalizeSource','sdf_surface_finalize.comp',False),('sdfPrepareSource','sdf_surface_prepare.comp',False), ('sdfResetSource','sdf_surface_reset.comp',False),('sdfValidateSource','sdf_surface_validate.comp',True)]:
    compute+=embed(name,'#version 430 compatibility\n'+(core if scene else '')+(ROOT/'shaders/sdf_surface_common.glsl').read_text()+(ROOT/'shaders'/file).read_text())
for stage, scene in [('Release',False),('SampleClaim',False),('Sample',True),('Classify',False),('Allocate',False),('Persist',False),('Emit',False),('Prepare',False),('Root',True),('Statistics',False),('MaterialClaim',False),('Material',True)]:
    source=(ROOT/('shaders/sdf_sparse_'+re.sub(r'(?<!^)(?=[A-Z])','_',stage).lower()+'.comp')).read_text()
    layout=re.search(r'layout\(local_size_x=\d+\) in;',source)[0]
    compute+=embed('sdfSparse'+stage+'Source','#version 430 compatibility\n'+layout+'\n'+(core if scene else '')+(ROOT/'shaders/sdf_sparse_common.glsl').read_text()+source.replace(layout,''))
    if stage in ('Sample','Root','Material'):
        compute+=embed('sdfSparseCached'+stage+'Source','#version 430 compatibility\n'+layout+'\n'+cached_core+(ROOT/'shaders/sdf_sparse_common.glsl').read_text()+source.replace(layout,''))
# Intervalos nuevos: sólo los shaders compute 4.30 necesitan double y next-float.
def interval_translate(source):
    source=translate(source)
    source=re.sub(r'\(SDFInterval\)\{([^{}]*)\}',r'SDFInterval(\1)',source)
    # Conversiones C explícitas: preservar la agrupación del operando, no usar regex codiciosa.
    while True:
        match=re.search(r'\((float|double)\)\s*',source)
        if not match:break
        begin=match.end();end=begin
        if source[begin]=='(':
            depth=1;end=begin+1
            while depth:
                depth+=(source[end]=='(')-(source[end]==')');end+=1
        else:
            atom=re.match(r'\w+(?:\.\w+)*',source[begin:])
            if not atom:raise ValueError(source[begin:begin+80])
            end=begin+atom.end()
            if end<len(source) and source[end]=='(':
                depth=1;end+=1
                while depth:
                    depth+=(source[end]=='(')-(source[end]==')');end+=1
        source=source[:match.start()]+match[1]+'('+source[begin:end]+')'+source[end:]
    return source
interval_prefix='''struct SDFInterval {float lo;float hi;};
struct AABB3D {vec3 start;vec3 end;};
#define INFINITY uintBitsToFloat(0x7f800000u)
#define fmin min
#define fmax max
#define fminf min
#define fmaxf max
float IntervalNextToward(float value,float toward) {
    if(isnan(value)||value==toward)return value;
    if(value==0)return uintBitsToFloat(toward<0?0x80000001u:1u);
    uint bits=floatBitsToUint(value);
    return uintBitsToFloat((value<toward)==(value>0)?bits+1u:bits-1u);
}
#define nextafterf IntervalNextToward
'''
interval_names=['Interval_Pad','Interval_Constant','Interval_Add','Interval_Subtract','Interval_Multiply',
    'Interval_Scale','Interval_Min','Interval_Max','Interval_Clamp','Interval_Divide','Interval_Absolute',
    'Interval_Square','Interval_Sqrt','Interval_SinHalfTurn','Interval_Project','SDF_CurvedPinnaInterval',
    'Interval_Lerp','Interval_Length','Interval_Component','Interval_Hull','SDF_EllipsoidInterval',
    'SDF_CapsuleInterval','SDF_RoundedSlotInterval','SDF_SmoothUnionInterval','SDF_SmoothSubtractInterval',
    'Interval_Hermite','SDF_SweepSegmentInterval','Interval_RelativeProject','SDF_ConnectorInterval']
interval_code=interval_prefix+'\n'.join(interval_translate(function('src/SDFInterval.c',name)) for name in interval_names)
compute+=embed('sdfIntervalValidateSource','#version 430 compatibility\n'+core+interval_code+
    (ROOT/'shaders/sdf_interval_validate.comp').read_text())
compute+=embed('sdfMeshVertexSource','#version 430 compatibility\nuniform vec4 surfaceRecipe[82];\n'+domain+(ROOT/'shaders/sdf_surface_mesh.vert').read_text())
compute+=embed('sdfMeshFragmentSource','#version 430 compatibility\n'+core+surface+domain+'\nuniform vec3 cameraPosition,cameraForward,cameraRight,cameraUp;\n'+material+(ROOT/'shaders/sdf_surface_mesh.frag').read_text())
(ROOT/'src/MonsterSDFCompute.generated.h').write_text('\n'.join(compute)+'\n')
