/** @file test_suid_anatomy.c
 * @brief Suinos: disco frontal, narinas abiertas, cola helicoidal y metamorfosis. */
#include "CreatureRecipeIO.h"
#include "CreatureRecipes.h"
#include "Creature.h"
#include "CreatureMorph.h"
#include "MonsterSDF.h"
#include "MonsterVisualAsync.h"
#include "Tail.h"
#include "test_utils.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static void SuidHead(void) {
    TEST_ASSERT(HEAD_ARCHETYPE_EQUINE==4&&HEAD_ARCHETYPE_SUID==5,"Enums cefálicos compatibles");
    TEST_ASSERT(TAIL_ARCHETYPE_CUSTOM==5&&TAIL_ARCHETYPE_COILED==6,"Enums caudales compatibles");
    for(unsigned seed=1;seed<=100;++seed) {
        HeadPhenotype p=HeadPhenotype_RandomValid(HEAD_ARCHETYPE_SUID,seed);
        HeadAnatomy h;
        TEST_ASSERT(p.archetype==HEAD_ARCHETYPE_SUID,"Familia suina aleatoria preservada");
        TEST_ASSERT(HeadAnatomy_Resolve(&p,0,Vec3_Create(.7f,.6f,.8f),&h),"Resolver variación suina");
        TEST_ASSERT(HeadAnatomy_Validate(&h)==HEAD_VALID,"Cabeza suina válida");
        TEST_ASSERT(h.surface.hasNasalPad&&h.surface.hasCranialProfile&&h.surface.hasMandibularProfile,"Perfiles continuos y disco integrado");
        TEST_ASSERT(h.surface.mandibularCenters[0].z<h.surface.faceTip.z,
            "El labio inferior termina detrás del disco nasal");
        TEST_ASSERT(h.surface.noseRadii.x>h.surface.noseRadii.z*2,"Disco achatado longitudinalmente");
        TEST_ASSERT(h.surface.leftNostrilCenter.z>h.surface.noseCenter.z,"Narinas anteriores");
        TEST_ASSERT(h.surface.leftNostrilCenter.x>h.surface.nostrilRadii.x,"Narinas separadas");
    }
    Head h=Head_Create(HEAD_ARCHETYPE_SUID,0,Vec3_Create(.7f,.6f,.8f));
    TEST_ASSERT(h.phenotype.archetype==HEAD_ARCHETYPE_SUID,"Constructor usa preset suino");
    HeadPhenotype old=HeadPhenotype_CanidPreset();HeadAnatomy a;
    TEST_ASSERT(HeadAnatomy_Resolve(&old,0,Vec3_Create(.7f,.6f,.8f),&a),"Resolver cánido heredado");
    TEST_ASSERT(a.surface.nasalColor.r==30&&a.surface.nasalColor.g==27,"Color nasal heredado sin cambio");
}

static void CoiledTail(void) {
    TailPhenotype p={.archetype=TAIL_ARCHETYPE_COILED,.length=1.6f,
        .baseWidth=.06f,.baseHeight=.06f,.tipWidth=.015f,.tipHeight=.015f,
        .curvature=.8f,.taperCurve=.85f,.segmentCount=16,.development=1};
    AttachmentSlot slot={.position={0,2,0},.forward={0,0,-1},.up={0,1,0},
        .side={1,0,0},.hostRadii={1,1,1},.scale=1,.hostNode=Anatomy_MakeId(1,1)};
    AnatomyGraph* g=calloc(1,sizeof(*g));TEST_ASSERT(g,"Reservar grafo de cola");
    AnatomyGraph_Init(g);
    TEST_ASSERT(AnatomyGraph_AddNode(g,(AnatomyNode){.id=slot.hostNode,
        .moduleInstanceId=1,.localNodeId=1,.center={0,2,0},.widthRadius=1,.heightRadius=1,
        .development=1}),"Anfitrión de cola");
    TEST_ASSERT(Tail_Resolve(&p,20,&slot,g),"Resolver espiral con estaciones estables");
    float length=0;bool returns=false;
    for(unsigned i=1;i<p.segmentCount;++i) {
        Vector3 d=Vec3_Sub(g->nodes[i+1].center,g->nodes[i].center);
        length+=Vec3_Length(d);if(Vec3_Dot(d,slot.forward)<0)returns=true;
        TEST_ASSERT(Vec3_Length(d)>p.baseWidth*.5f,"Estaciones distintas y ordenadas");
    }
    TEST_ASSERT(returns&&length>p.length*.70f&&length<p.length*1.1f,"Un giro real, longitud acotada");
    for(unsigned i=1;i<16;++i)for(unsigned j=i+3;j<16;++j)
        for(unsigned a=0;a<=4;++a)for(unsigned b=0;b<=4;++b) {
            Vector3 x=Vec3_Lerp(g->nodes[i].center,g->nodes[i+1].center,a/4.f);
            Vector3 y=Vec3_Lerp(g->nodes[j].center,g->nodes[j+1].center,b/4.f);
            TEST_ASSERT(Vec3_Distance(x,y)>g->nodes[i].widthRadius+g->nodes[j].widthRadius,
                "Tramos no vecinos de la espiral separados");
        }
    Vector3 tip=g->nodes[16].center,root=g->nodes[1].center;
    TEST_ASSERT(Vec3_Distance(root,tip)>p.baseWidth*2,"Extremos de la espiral no se intersecan");
    p.development=.5f;
    AnatomyGraph* half=calloc(1,sizeof(*half));TEST_ASSERT(half,"Reservar desarrollo de cola");
    AnatomyGraph_Init(half);
    TEST_ASSERT(AnatomyGraph_AddNode(half,g->nodes[0]),"Anfitrión de desarrollo");
    TEST_ASSERT(Tail_Resolve(&p,20,&slot,half),"Desarrollo continuo");
    TEST_ASSERT(Vec3_Distance(Vec3_Sub(half->nodes[16].center,half->nodes[1].center),
        Vec3_Scale(Vec3_Sub(tip,root),.5f))<1e-5f,"Toda la curva se desarrolla uniformemente");
    free(g);free(half);
}

static void PigPipeline(void) {
    CreatureRecipeDocument* doc=calloc(1,sizeof(*doc));Monster* m=malloc(sizeof(*m));
    TEST_ASSERT(doc&&m,"Reservar receta y criatura");
    char error[256];
    TEST_ASSERT(CreatureRecipeIO_LoadFile(doc,"recipes/cerdo.creature.json",error,sizeof(error)),error);
    TEST_ASSERT(doc->recipe.id==1007&&!strcmp(doc->recipe.name,"Cerdo"),"Identidad externa");
    FILE* file=tmpfile();TEST_ASSERT(file,"Flujo temporal JSON");
    TEST_ASSERT(CreatureRecipeIO_WriteJSON(file,&doc->recipe),"Serializar capacidades nuevas");
    long size=ftell(file);rewind(file);char* json=calloc((size_t)size+1,1);
    TEST_ASSERT(json&&fread(json,1,(size_t)size,file)==(size_t)size,"Leer JSON serializado");
    CreatureRecipeDocument* copy=calloc(1,sizeof(*copy));TEST_ASSERT(copy,"Documento de roundtrip");
    TEST_ASSERT(CreatureRecipeIO_ReadJSON(copy,json,error,sizeof(error)),error);
    TEST_ASSERT(copy->recipe.adult.head.nasalColor.r==doc->recipe.adult.head.nasalColor.r&&copy->recipe.adult.tails[0].archetype==TAIL_ARCHETYPE_COILED,"Roundtrip de arquetipos y pigmento");
    TEST_ASSERT(copy->recipe.adult.limbs[0].anatomyProfile==LIMB_PROFILE_COMPACT_UNGULATE,
        "Conservar el perfil compacto al guardar y cargar");
    fclose(file);free(json);free(copy);
    *m=Monster_Create();
    for(unsigned age=0;age<=12;++age) {
        CreaturePhenotype p=CreatureRecipe_EvaluateDevelopment(&doc->recipe,age/12.f);
        TEST_ASSERT(Creature_BuildMonster(m,&doc->recipe,&p),"Construcción durante desarrollo");
        TEST_ASSERT(m->hasHead&&m->eyeCount==2&&m->mouthCount==1,"Rasgos cefálicos funcionales");
        SurfaceCoordinate skin={.position={0,1,-2},.region=SURFACE_REGION_DORSAL_TRUNK,
            .secondaryRegion=SURFACE_REGION_DORSAL_TRUNK,.integumentMask=1};
        SurfaceLocalMaterial material=Surface_EvaluateLocalMaterial(&m->surface,&skin,.5f);
        TEST_ASSERT(material.skinWeight>.99f&&material.roughness>.60f,"Piel, no sombreado de manto denso");
        TEST_ASSERT(p.development.cephalic==1&&p.limbCount==4&&p.ornamentCount==0,"Neonato completo y sin cuernos");
        for(unsigned leg=0;leg<4;++leg)TEST_ASSERT(p.limbs[leg].digitCount==2&&p.limbs[leg].archetype==LIMB_ARCHETYPE_UNGULATE,"Dos uñas por miembro");
        MonsterSDF sdf=MonsterSDF_Create();
        TEST_ASSERT(MonsterSDF_Build(&sdf,m,MonsterSDF_DefaultConfig()),"Campo compartido en todas las edades");
        TEST_ASSERT(sdf.axialStationCount>=9,"La cola enrollada conserva el barrido del tronco");
        for(int i=1;i<sdf.axialStationCount;++i) {
            TEST_ASSERT(sdf.axialStations[i-1].center.z>sdf.axialStations[i].center.z,
                "Estaciones del tronco estrictamente ordenadas");
        }
        const AnatomyNode* tail=AnatomyGraph_FindFirstRegion(&m->anatomyGraph,ANATOMY_REGION_TAIL);
        bool independentTail=false;
        unsigned blendedShafts=0;
        for(size_t i=0;i<sdf.connectorCount;++i) {
            const MonsterSDFConnector* connector=&sdf.connectors[i];
            if(tail&&connector->moduleInstanceId==tail->moduleInstanceId)
                independentTail|=!connector->axialSweepMember;
            blendedShafts+=connector->kind==BODY_CONNECTION_LIMB_SEGMENT&&connector->jointBlend==1;
        }
        TEST_ASSERT(independentTail,"El SDF conserva la cola como curva independiente");
        TEST_ASSERT(blendedShafts==12,"Los tres tramos de cada pata usan mezcla articular");
        const MonsterSDFMouth* mouth=&sdf.mouths[0];
        for(unsigned side=0;side<2;++side) {
            Vector3 local=side?mouth->rightNostrilCenterLocal:mouth->leftNostrilCenterLocal;
            Vector3 world=Vec3_Add(mouth->center,local);
            MonsterSDFHeadField context;SDFField field=MonsterSDF_GetHeadField(&sdf,0,&context);
            TEST_ASSERT(field.evaluate(field.context,world).distance>0,"Narinas geométricas abiertas, no pigmentadas");
        }
        TEST_ASSERT(mouth->nasalColor.r==p.head.nasalColor.r,"Pigmento nasal compartido");
        TEST_ASSERT(isfinite(MonsterSDF_EvaluateVisualDistance(&sdf,sdf.bounds.start)),"Campo finito");
        MonsterSDF_Free(&sdf);
    }
    CreatureRecipeDocument* other=calloc(1,sizeof(*other));
    Monster* b=malloc(sizeof(*b));Monster* mix=malloc(sizeof(*mix));
    TEST_ASSERT(other&&b&&mix,"Reservar metamorfosis");
    *b=Monster_Create();*mix=Monster_Create();
    const char* paths[]={"recipes/caballo.creature.json","recipes/brumacerno.creature.json"};
    for(unsigned species=0;species<3;++species) {
        if(species<2) {
            TEST_ASSERT(CreatureRecipeIO_LoadFile(other,paths[species],error,sizeof(error)),error);
            TEST_ASSERT(Creature_BuildMonster(b,&other->recipe,&other->recipe.adult),"Extremo heredado");
        } else {
            const CreatureRecipe* lizard=CreatureRecipes_Find(1);
            TEST_ASSERT(lizard&&Creature_BuildMonster(b,lizard,&lizard->adult),"Extremo reptiliano");
        }
        CreatureMorphTopologyEvent events[32];
        size_t count=CreatureMorph_CollectTopologyEvents(b,m,events,32);
        bool headEvent=false;
        for(size_t i=0;i<count&&i<32;++i)headEvent|=events[i].moduleInstanceId==2;
        TEST_ASSERT(headEvent,"Un cambio de perfil cefálico requiere certificación aunque tenga un solo nodo");
        if(species==0) {
            MonsterVisualAsync* async=MonsterVisualAsync_Create(MonsterVisualAsync_DefaultConfig());
            TEST_ASSERT(async&&MonsterVisualAsync_SetMorphEndpoints(async,b,m),"Extremos para cesión cefálica");
            MonsterSDF sdf=MonsterSDF_Create();
            TEST_ASSERT(MonsterSDF_Build(&sdf,m,MonsterSDF_DefaultConfig()),"Campo de destino para cesión");
            MorphFallbackRegion regions[32];
            size_t n=MonsterVisualAsync_GetFallbackRegions(async,&sdf,&m->anatomyGraph,1,.08f,regions,32);
            bool covered=false;
            for(size_t i=0;i<n;++i)if(regions[i].moduleId==2) {
                AABB3D box=regions[i].bounds,head=sdf.mouths[0].headBounds;
                covered=box.start.x<=head.start.x&&box.start.y<=head.start.y&&box.start.z<=head.start.z&&
                    box.end.x>=head.end.x&&box.end.y>=head.end.y&&box.end.z>=head.end.z;
            }
            TEST_ASSERT(covered,"Dominio cefálico incluye nariz y orejas, no sólo conector cervical");
            TEST_ASSERT(async->hasMorphHeadBounds,"Extremos cefálicos resueltos una vez por pareja");
            bool oldCovered=false;
            for(size_t i=0;i<n;++i)if(regions[i].moduleId==2) {
                AABB3D box=regions[i].bounds,old=async->morphHeadBounds;
                oldCovered=box.start.y<=old.start.y&&box.end.y>=old.end.y&&
                    box.start.z<=old.start.z&&box.end.z>=old.end.z;
                for(unsigned corner=0;corner<8;++corner) {
                    Vector3 point=Vec3_Create(corner&1?old.end.x:old.start.x,
                        corner&2?old.end.y:old.start.y,corner&4?old.end.z:old.start.z);
                    TEST_ASSERT(MorphHandoff_EvaluateDomainDistance(&regions[i].handoff,point)>0,
                        "La máscara de cesión cubre los rasgos cefálicos publicados");
                }
            }
            TEST_ASSERT(oldCovered,"Dominio retira también el cráneo anterior aunque esté por encima del actual");
            MonsterSDF_Free(&sdf);MonsterVisualAsync_Free(async);
        }
        for(unsigned age=0;age<=10;++age)for(unsigned reverse=0;reverse<2;++reverse) {
            TEST_ASSERT(CreatureMorph_Interpolate(reverse?b:m,reverse?m:b,age/10.f,mix),"Metamorfosis bidireccional");
            MonsterSDF sdf=MonsterSDF_Create();
            TEST_ASSERT(MonsterSDF_Build(&sdf,mix,MonsterSDF_DefaultConfig()),"Campo intermedio válido");
            for(size_t i=0;i<mix->anatomyGraph.nodeCount;++i)
                TEST_ASSERT(isfinite(MonsterSDF_EvaluateVisualDistance(&sdf,mix->anatomyGraph.nodes[i].center)),"Campo finito en anclas intermedias");
            MonsterSDF_Free(&sdf);
        }
    }
    Monster_Free(b);Monster_Free(mix);Monster_Free(m);free(b);free(mix);free(m);free(doc);free(other);
}
void run_suid_anatomy_tests(void) {
    SuidHead();CoiledTail();PigPipeline();
}
