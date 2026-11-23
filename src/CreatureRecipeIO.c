/** @file CreatureRecipeIO.c
 * @brief Serialización estricta por campos; no depende del layout binario del archivo.
 */
#include "CreatureRecipeIO.h"
#include "Creature.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <math.h>
#include <float.h>
#include <ctype.h>

typedef enum FieldKind { F_FLOAT,F_UNSIGNED,F_SIZE,F_U32,F_U16,F_U8,F_BOOL,F_ENUM,F_STRING,F_OBJECT } FieldKind;
typedef struct Field {
    const char* name;
    size_t offset, stride, capacity;
    FieldKind kind;
    double maximum;
    const struct Field* children;
    size_t countOffset;
    const char* enumNames;
    bool optional;
} Field;
#define NO_COUNT SIZE_MAX
#include "CreatureRecipeSchema.inc"
typedef struct Envelope { uint32_t version; CreatureRecipe recipe; } Envelope;
static const Field envelopeFields[]={
    {"version",offsetof(Envelope,version),sizeof(uint32_t),1,F_U32,1,NULL,NO_COUNT,NULL,false},
    {"recipe",offsetof(Envelope,recipe),sizeof(CreatureRecipe),1,F_OBJECT,0,CreatureRecipe_fields,NO_COUNT,NULL,false},{0}
};
typedef struct Reader { const char* start; const char* p; char name[192]; } Reader;
static void Space(Reader* r) { while(isspace((unsigned char)*r->p)) ++r->p; }
static bool Take(Reader* r,char c) { Space(r); if(*r->p!=c)return false; ++r->p;return true; }
static bool Hex(Reader* r,unsigned* value) {
    *value=0;
    for(int i=0;i<4;++i){unsigned char c=(unsigned char)*r->p;if(!c)return false;++r->p;
        int x=c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:c>='A'&&c<='F'?c-'A'+10:-1;
        if(x<0)return false;
        *value=(*value<<4)|(unsigned)x;
    }return true;
}
static bool String(Reader* r,char* out,size_t cap) {
    if(!Take(r,'"'))return false;
    size_t n=0;
    while(*r->p && *r->p!='"') {
        unsigned char c=(unsigned char)*r->p++;
        if(c<32)return false;
        if(c=='\\'){
            c=(unsigned char)*r->p; if(!c)return false; ++r->p;
            if(c=='u'){
                unsigned u;if(!Hex(r,&u)||!u)return false;
                if(u>=0xd800 && u<=0xdbff){if(*r->p!='\\')return false;++r->p;
                    if(*r->p!='u')return false;
                    ++r->p;
                    unsigned lo;if(!Hex(r,&lo)||lo<0xdc00||lo>0xdfff)return false;
                    u=0x10000+((u-0xd800)<<10)+(lo-0xdc00);
                }else if(u>=0xdc00&&u<=0xdfff)return false;
                unsigned char bytes[4];size_t k=0;
                if(u<128)bytes[k++]=(unsigned char)u;
                else if(u<2048){bytes[k++]=0xc0|(u>>6);bytes[k++]=0x80|(u&63);}
                else if(u<65536){bytes[k++]=0xe0|(u>>12);bytes[k++]=0x80|((u>>6)&63);bytes[k++]=0x80|(u&63);}
                else {bytes[k++]=0xf0|(u>>18);bytes[k++]=0x80|((u>>12)&63);bytes[k++]=0x80|((u>>6)&63);bytes[k++]=0x80|(u&63);}
                if(n+k>=cap)return false;
                for(size_t j=0;j<k;++j)out[n++]=(char)bytes[j];
                continue;
            }
            if(c=='n')c='\n';else if(c=='r')c='\r';else if(c=='t')c='\t';
            else if(c=='b')c='\b';else if(c=='f')c='\f';
            else if(c!='"'&&c!='\\'&&c!='/')return false;
        }
        if(n+1>=cap)return false;
        out[n++]=(char)c;
    }
    if(*r->p!='"')return false;
    ++r->p;out[n]=0;return true;
}
static bool Number(Reader* r,double* value) {
    Space(r);const char* start=r->p;
    if(*r->p=='-')++r->p;
    if(*r->p=='0')++r->p;
    else {if(*r->p<'1'||*r->p>'9')return false;while(isdigit((unsigned char)*r->p))++r->p;}
    if(*r->p=='.'){++r->p;if(!isdigit((unsigned char)*r->p))return false;while(isdigit((unsigned char)*r->p))++r->p;}
    if(*r->p=='e'||*r->p=='E'){++r->p;if(*r->p=='+'||*r->p=='-')++r->p;
        if(!isdigit((unsigned char)*r->p))return false;
        while(isdigit((unsigned char)*r->p))++r->p;}
    char* end;*value=strtod(start,&end);return end==r->p&&isfinite(*value);
}
static bool Object(Reader*,void*,const Field*,unsigned);
static bool Value(Reader* r,void* ptr,const Field* f,unsigned depth) {
    if(f->kind==F_OBJECT)return Object(r,ptr,f->children,depth+1);
    if(f->kind==F_STRING){if(!String(r,r->name,sizeof(r->name)))return false;*(const char**)ptr=r->name;return true;}
    if(f->kind==F_BOOL){Space(r);bool v;
        if(!strncmp(r->p,"true",4)){r->p+=4;v=true;}
        else if(!strncmp(r->p,"false",5)){r->p+=5;v=false;}else return false;
        *(bool*)ptr=v;return true;
    }
    double x;if(!Number(r,&x))return false;
    if(f->kind==F_FLOAT){if(fabs(x)>FLT_MAX || fabs(x)>1000000)return false;*(float*)ptr=(float)x;return true;}
    if(x<0||x>f->maximum||floor(x)!=x)return false;
    switch(f->kind){
    case F_ENUM: *(int*)ptr=(int)x;break;
    case F_UNSIGNED: *(unsigned*)ptr=(unsigned)x;break;
    case F_SIZE: if(x>1024)return false;*(size_t*)ptr=(size_t)x;break;
    case F_U32: *(uint32_t*)ptr=(uint32_t)x;break;
    case F_U16: *(uint16_t*)ptr=(uint16_t)x;break;
    case F_U8: *(uint8_t*)ptr=(uint8_t)x;break;
    default:return false;
    }return true;
}
static bool Object(Reader* r,void* ptr,const Field* fields,unsigned depth) {
    if(depth>16||!Take(r,'{'))return false;
    bool seen[128]={0};size_t lengths[128]={0},count=0;
    while(fields[count].name)++count;
    if(count>128)return false;
    for(size_t k=0;k<count;++k){
        Space(r);if(*r->p=='}')break;
        if(k&&!Take(r,','))return false;
        char key[96];if(!String(r,key,sizeof(key))||!Take(r,':'))return false;
        size_t i=0;while(i<count&&strcmp(fields[i].name,key))++i;
        if(i==count||seen[i])return false;
        seen[i]=true;const Field* f=&fields[i];char* dst=(char*)ptr+f->offset;
        if(f->capacity>1){
            if(!Take(r,'['))return false;
            size_t n=0;Space(r);
            if(*r->p!=']')do{
                if(n>=f->capacity||!Value(r,dst+n*f->stride,f,depth))return false;
                ++n;Space(r);
            }while(Take(r,','));
            if(!Take(r,']'))return false;
            lengths[i]=n;
            if(f->countOffset==NO_COUNT&&n!=f->capacity)return false;
        }else if(!Value(r,dst,f,depth))return false;
    }
    if(!Take(r,'}'))return false;
    for(size_t i=0;i<count;++i)if(!seen[i]&&!fields[i].optional)return false;
    if(fields==HeadSurfaceRecipe_fields) {
        HeadSurfaceRecipe* surface=ptr;
        for(size_t i=0;i<count;++i) {
            if(strcmp(fields[i].name,"nasalDiscWeight")!=0)continue;
            if(!seen[i])surface->nasalDiscWeight=-1;
            else if(surface->nasalDiscWeight<0 || surface->nasalDiscWeight>1)return false;
            break;
        }
    }
    if(fields==HeadHybridProfile_fields) {
        HeadHybridProfile* hybrid=ptr;
        HeadSurfaceRecipe* surface=&hybrid->regional.surface;
        if(surface->nasalDiscWeight<0) {
            surface->nasalDiscWeight=0;
            /* v1 guardaba el plano de corte y el planum, sin su peso nasal.
             * Sólo se migra el disco cuando el perfil antiguo identifica
             * ascendencia suida y conserva un planum y un casquete. */
            bool suid=hybrid->regional.archetype==HEAD_ARCHETYPE_SUID ||
                hybrid->secondary.archetype==HEAD_ARCHETYPE_SUID;
            if(suid && surface->sweptSkull && surface->hasNasalPad) {
                float weight=fminf(1,fmaxf(0,surface->faceCapWeight*(1-surface->nostrilCurve)));
                surface->nasalDiscWeight=weight;
                surface->faceCapDepth+=(surface->cranialRadii[0].y*.40f-surface->faceCapDepth)*weight;
                surface->oralArch=surface->faceTipRadii.y*.09f*weight;
            }
        }
    }
    for(size_t i=0;i<count;++i)if(fields[i].countOffset!=NO_COUNT &&
        lengths[i]!=*(size_t*)((char*)ptr+fields[i].countOffset))return false;
    return true;
}
static void WriteString(FILE* f,const char* s){
    fputc('"',f);for(const unsigned char* p=(const unsigned char*)s;*p;++p){
        if(*p=='"'||*p=='\\'){fputc('\\',f);fputc(*p,f);}
        else if(*p<32)fprintf(f,"\\u%04x",*p);else fputc(*p,f);
    }fputc('"',f);
}
static bool WriteObject(FILE*,const void*,const Field*);

static bool WriteValue(FILE* file,const void* ptr,const Field* f){
    switch(f->kind){
    case F_OBJECT:
        return WriteObject(file,ptr,f->children);
    case F_STRING: {
        const char* name=*(const char*const*)ptr;
        if(!name||!*name||strlen(name)>191)return false;
        if(file)WriteString(file,name);
        break;
    }
    case F_FLOAT: {
        float value=*(const float*)ptr;
        if(!isfinite(value)||fabsf(value)>1000000)return false;
        if(file)fprintf(file,"%.9g",value);
        break;
    }
    case F_ENUM: {
        int value=*(const int*)ptr;
        if(value<0||value>f->maximum)return false;
        if(file)fprintf(file,"%d",value);
        break;
    }
    case F_UNSIGNED:if(file)fprintf(file,"%u",*(const unsigned*)ptr);break;
    case F_SIZE:if(file)fprintf(file,"%zu",*(const size_t*)ptr);break;
    case F_U32:if(file)fprintf(file,"%u",*(const uint32_t*)ptr);break;
    case F_U16:if(file)fprintf(file,"%u",(unsigned)*(const uint16_t*)ptr);break;
    case F_U8:if(file)fprintf(file,"%u",(unsigned)*(const uint8_t*)ptr);break;
    case F_BOOL:if(file)fputs(*(const bool*)ptr?"true":"false",file);break;
    }
    return !file||!ferror(file);
}

static bool WriteObject(FILE* file,const void* ptr,const Field* fields){
    if(file)fputc('{',file);
    bool first=true;
    for(size_t i=0;fields[i].name;++i){
        const Field* f=&fields[i];
        if(fields==HeadHybridProfile_fields && !((const HeadHybridProfile*)ptr)->hasRegionalProfile &&
            (!strcmp(f->name,"regional")||!strcmp(f->name,"hasRegionalProfile")||!strcmp(f->name,"referenceControls")))continue;
        if(fields==CreatureRecipe_fields && !((const CreatureRecipe*)ptr)->offspringVersion &&
            (!strcmp(f->name,"offspringSeed")||!strcmp(f->name,"offspringVersion")))continue;
        if(file){
            if(!first)fputc(',',file);
            first=false;
            WriteString(file,f->name);
            fputc(':',file);
        }
        const char* src=(const char*)ptr+f->offset;
        if(f->capacity>1){
            size_t n=f->countOffset==NO_COUNT ? f->capacity :
                *(const size_t*)((const char*)ptr+f->countOffset);
            if(n>f->capacity)return false;
            if(file)fputc('[',file);
            for(size_t k=0;k<n;++k){
                if(k&&file)fputc(',',file);
                if(!WriteValue(file,src+k*f->stride,f))return false;
            }
            if(file)fputc(']',file);
        }else if(!WriteValue(file,src,f))return false;
    }
    if(file)fputc('}',file);
    return !file||!ferror(file);
}

bool CreatureRecipeIO_ValidateFields(const CreatureRecipe* recipe){
    if(!recipe)return false;
    Envelope e={.version=1,.recipe=*recipe};
    return WriteObject(NULL,&e,envelopeFields);
}

bool CreatureRecipeIO_WriteJSON(FILE* file,const CreatureRecipe* recipe){
    if(!file||!recipe)return false;
    Envelope e={.version=1,.recipe=*recipe};
    return WriteObject(file,&e,envelopeFields)&&fputc('\n',file)!=EOF;
}
static void WriteSchema(FILE* file,const Field* fields){
    fputs("{\"type\":\"object\",\"properties\":{",file);
    for(size_t i=0;fields[i].name;++i){const Field* f=&fields[i];if(i)fputc(',',file);WriteString(file,f->name);fputc(':',file);
        if(f->capacity>1)fprintf(file,"{\"type\":\"array\",\"maxItems\":%zu,\"items\":",f->capacity);
        if(f->kind==F_OBJECT)WriteSchema(file,f->children);
        else if(f->kind==F_STRING)fputs("{\"type\":\"string\",\"maxLengthBytes\":191}",file);
        else if(f->kind==F_BOOL)fputs("{\"type\":\"boolean\"}",file);
        else if(f->kind==F_FLOAT)fputs("{\"type\":\"number\"}",file);
        else {fprintf(file,"{\"type\":\"integer\",\"minimum\":0,\"maximum\":%.0f",f->maximum);
            if(f->enumNames){fputs(",\"enumNames\":[",file);const char* begin=f->enumNames;unsigned n=0;
                while(*begin){const char* end=strchr(begin,'|');size_t length=end?(size_t)(end-begin):strlen(begin);
                    if(n++)fputc(',',file);
                    fputc('"',file);fwrite(begin,1,length,file);fputc('"',file);if(!end)break;begin=end+1;}
                fputc(']',file);}
            fputc('}',file);}
        if(f->capacity>1)fputc('}',file);
    }fputs("},\"additionalProperties\":false}",file);
}
bool CreatureRecipeIO_WriteSchema(FILE* file){if(!file)return false;WriteSchema(file,envelopeFields);return !ferror(file);}
bool CreatureRecipeIO_ReadJSON(CreatureRecipeDocument* out,const char* json,char* error,size_t errorSize){
    if(error&&errorSize)error[0]=0;
    if(!out||!json)return false;
    Envelope* e=calloc(1,sizeof(*e));if(!e)return false;
    Reader r={.start=json,.p=json};
    bool ok=Object(&r,e,envelopeFields,0);Space(&r);
    ok=ok&&!*r.p&&e->version==1&&r.name[0]&&e->recipe.gait<=CREATURE_GAIT_MEDUSA_PULSE;
    /* Resolver cada etapa detecta ranuras inexistentes, ciclos y arquetipos no implementados. */
    AnatomyGraph* graph=malloc(sizeof(*graph));
    if(!graph)ok=false;
    for(int s=0;s<4&&ok;++s){const CreaturePhenotype* p=CreatureRecipe_GetStage(&e->recipe,(CreatureStage)s);
        ok=CreatureRecipe_Validate(&e->recipe,p)&&Creature_ResolveAnatomy(&e->recipe,p,graph);
        if(ok)ok=AnatomyGraph_FindNode(graph,Anatomy_MakeId(e->recipe.bodyPlan.root.moduleInstanceId,
            e->recipe.bodyPlan.root.localNodeId))!=NULL;
    }
    if (ok && (e->recipe.seed.headHybrid.active || e->recipe.larva.headHybrid.active ||
                e->recipe.juvenile.headHybrid.active || e->recipe.adult.headHybrid.active)) {
        for (int i=1; i<4 && ok; ++i)
            ok=CreaturePhenotype_MorphCompatible(&e->recipe.seed,
                CreatureRecipe_GetStage(&e->recipe,(CreatureStage)i));
    }
    free(graph);
    if(ok){out->recipe=e->recipe;memcpy(out->name,r.name,sizeof(out->name));out->recipe.name=out->name;}
    else if(error&&errorSize)snprintf(error,errorSize,"Receta inválida (byte %zu): JSON completo v1, conteos, identificadores o anclajes incompatibles.",(size_t)(r.p-r.start));
    free(e);return ok;
}
bool CreatureRecipeIO_LoadFile(CreatureRecipeDocument* out,const char* path,char* error,size_t errorSize){
    if(error&&errorSize)snprintf(error,errorSize,"No se pudo leer la receta.");
    FILE* file=path?fopen(path,"rb"):NULL;
    if(!file){if(error&&errorSize)snprintf(error,errorSize,"No se puede abrir la receta.");return false;}
    char* data=malloc(2097153);if(!data){fclose(file);return false;}
    size_t n=fread(data,1,2097153,file);bool ok=!ferror(file)&&n<=2097152&&!memchr(data,0,n);
    fclose(file);
    if(ok){data[n]=0;ok=CreatureRecipeIO_ReadJSON(out,data,error,errorSize);}
    else if(error&&errorSize)snprintf(error,errorSize,"Archivo demasiado grande o con bytes nulos.");
    free(data);return ok;
}

void CreatureRecipeDocument_Copy(CreatureRecipeDocument* out, const CreatureRecipe* recipe) {
    char name[sizeof(out->name)];
    snprintf(name,sizeof(name),"%s",recipe->name ? recipe->name : "");
    out->recipe = *recipe;
    strcpy(out->name,name);
    out->recipe.name = out->name;
}
