#include "test_utils.h"
#include "Head.h"
#include "Monster.h"
#include "MonsterSDF.h"
#include "Creature.h"
#include "CreatureRecipeIO.h"
#include "CreatureRecipes.h"
#include "helpers/monster_sdf_frozen_oracle.h"
#include <float.h>
#include <math.h>
#include <string.h>
#if defined(__SSE2__) && defined(__x86_64__) && \
    (defined(__GNUC__) || defined(__clang__))
#include <xmmintrin.h>
#define TEST_HAS_EYELID_FP_CONTROLS 1
#else
#define TEST_HAS_EYELID_FP_CONTROLS 0
#endif
#if TEST_HAS_EYELID_FP_CONTROLS && defined(FLT_EVAL_METHOD) && \
    FLT_EVAL_METHOD == 0 && FLT_RADIX == 2 && FLT_MANT_DIG == 24 && FLT_MAX_EXP == 128
#define TEST_EYELID_CACHE_COMPILE_SUPPORTED 1
#else
#define TEST_EYELID_CACHE_COMPILE_SUPPORTED 0
#endif

static Monster EyelidCacheFixture(HeadArchetype archetype) {
    Monster monster=Monster_Create();
    Monster_Init(&monster);
    monster.bodyParts[0].width=monster.bodyParts[0].widthRender=1.0f;
    monster.bodyParts[0].height=monster.bodyParts[0].heightRender=.9f;
    monster.bodyParts[0].length=monster.bodyParts[0].lengthRender=1.0f;
    Head head=Head_Create(archetype,0,Vec3_Create(.5f,.45f,.5f));
    TEST_ASSERT(Monster_SetHead(&monster,head),"No se pudo instalar la cabeza para prueba del cache palpebral");
    return monster;
}

static bool SameFloatBits(float a,float b) {
    return memcmp(&a,&b,sizeof(a))==0;
}

static SDFSample CachedSample(const void* context,Vector3 point) {
    return MonsterSDF_Evaluate((const MonsterSDF*)context,point);
}

static SDFSample FrozenSample(const void* context,Vector3 point) {
    return MonsterSDF_Frozen_Evaluate((const MonsterSDF*)context,point);
}

static void CompareCachedAndFallback(const MonsterSDF* cached,const MonsterSDF* fallback,
    const MonsterSDF* frozen,const AABB3D* bounds) {
    for(int x=0;x<=8;++x)for(int y=0;y<=8;++y)for(int z=0;z<=8;++z){
        Vector3 p=Vec3_Create(
            bounds->start.x+(bounds->end.x-bounds->start.x)*(float)x/8.0f,
            bounds->start.y+(bounds->end.y-bounds->start.y)*(float)y/8.0f,
            bounds->start.z+(bounds->end.z-bounds->start.z)*(float)z/8.0f);
        float a=MonsterSDF_EvaluateDistance(cached,p);
        float b=MonsterSDF_EvaluateDistance(fallback,p);
        float f=MonsterSDF_Frozen_EvaluateDistance(frozen,p);
        TEST_ASSERT(SameFloatBits(a,b),"El cache palpebral alteró el campo SDF");
        TEST_ASSERT(SameFloatBits(b,f),"El fallback difiere del MonsterSDF F congelado");
        SDFSample cachedSample=MonsterSDF_Evaluate(cached,p);
        SDFSample fallbackSample=MonsterSDF_Evaluate(fallback,p);
        SDFSample frozenSample=MonsterSDF_Frozen_Evaluate(frozen,p);
        TEST_ASSERT(SameFloatBits(cachedSample.distance,fallbackSample.distance)&&
            memcmp(&cachedSample.color,&fallbackSample.color,sizeof(Color))==0&&
            cachedSample.material==fallbackSample.material,
            "El sample (distancia/color/material) cambió con el cache");
        TEST_ASSERT(SameFloatBits(fallbackSample.distance,frozenSample.distance)&&
            memcmp(&fallbackSample.color,&frozenSample.color,sizeof(Color))==0&&
            fallbackSample.material==frozenSample.material,
            "El sample difiere del oráculo F congelado");
        Vector3 cachedNormal=SDF_EstimateNormal(CachedSample,cached,p,.003f);
        Vector3 fallbackNormal=SDF_EstimateNormal(CachedSample,fallback,p,.003f);
        Vector3 frozenNormal=SDF_EstimateNormal(FrozenSample,frozen,p,.003f);
        TEST_ASSERT(SameFloatBits(cachedNormal.x,fallbackNormal.x)&&
            SameFloatBits(cachedNormal.y,fallbackNormal.y)&&SameFloatBits(cachedNormal.z,fallbackNormal.z),
            "La normal cambió con el cache");
        TEST_ASSERT(SameFloatBits(fallbackNormal.x,frozenNormal.x)&&
            SameFloatBits(fallbackNormal.y,frozenNormal.y)&&SameFloatBits(fallbackNormal.z,frozenNormal.z),
            "La normal difiere del oráculo F congelado");
        const MonsterHeadDebugMode modes[]={MONSTER_HEAD_DEBUG_PERIORBITAL,MONSTER_HEAD_DEBUG_UPPER_HEAD};
        for(size_t mode=0;mode<sizeof(modes)/sizeof(modes[0]);++mode){
            SDFSample ca=MonsterSDF_EvaluateDebug(cached,p,modes[mode]);
            SDFSample fb=MonsterSDF_EvaluateDebug(fallback,p,modes[mode]);
            SDFSample fr=MonsterSDF_Frozen_EvaluateDebug(frozen,p,modes[mode]);
            TEST_ASSERT(SameFloatBits(ca.distance,fb.distance)&&
                memcmp(&ca.color,&fb.color,sizeof(Color))==0&&ca.material==fb.material,
                "Modo de depuración cefálico cambió con el cache");
            TEST_ASSERT(SameFloatBits(fb.distance,fr.distance)&&
                memcmp(&fb.color,&fr.color,sizeof(Color))==0&&fb.material==fr.material,
                "Modo de depuración cefálico difiere del oráculo F");
        }
    }
}

static void BuildPair(Monster* monster,MonsterSDF* cached,MonsterSDF* fallback,MonsterSDF* frozen) {
    *cached=MonsterSDF_Create();
    *fallback=MonsterSDF_Create();
    *frozen=MonsterSDF_Frozen_Create();
    TEST_ASSERT(MonsterSDF_Build(cached,monster,MonsterSDF_DefaultConfig()),"No se compiló SDF con cache palpebral");
    TEST_ASSERT(MonsterSDF_Build(fallback,monster,MonsterSDF_DefaultConfig()),"No se compiló SDF fallback");
    TEST_ASSERT(MonsterSDF_Frozen_Build(frozen,monster,MonsterSDF_Frozen_DefaultConfig()),"No se compiló SDF del oráculo F congelado");
    if(fallback->mouthCount)fallback->mouths[0].eyelidCapsuleCacheValid=false;
}

static void test_eyelid_cache_exact_field_and_mutation_fallback(void) {
    for(int archetype=HEAD_ARCHETYPE_LIZARD;archetype<=HEAD_ARCHETYPE_AVIAN;++archetype){
        Monster monster=EyelidCacheFixture((HeadArchetype)archetype);
        MonsterSDF cached,fallback,frozen;
        BuildPair(&monster,&cached,&fallback,&frozen);
        TEST_ASSERT(cached.mouthCount==1,"Se esperaba una boca en la fixture");
        if(cached.mouths[0].eyelidCoverage>.001f&&cached.mouths[0].eyelidCapsuleCacheValid){
            TEST_ASSERT(cached.mouths[0].eyelidCapsuleCacheValid,"Cache válido no publicado");
            CompareCachedAndFallback(&cached,&fallback,&frozen,&cached.bounds);
        } else {
            /* Toolchains/FP modes no reconocidos deben conservar el camino original. */
            TEST_ASSERT(!cached.mouths[0].eyelidCapsuleCacheValid,
                "Un cache se habilitó para cobertura o entorno FP no admitido");
        }
        MonsterSDF_Free(&cached);MonsterSDF_Free(&fallback);MonsterSDF_Frozen_Free(&frozen);Monster_Free(&monster);
    }
    printf("[PASS] test_eyelid_cache_exact_field_and_mutation_fallback\n");
}

static void test_eyelid_cache_parameter_mutation_invalidates_by_key(void) {
    for(int dependency=0;dependency<19;++dependency){
        Monster monster=EyelidCacheFixture(HEAD_ARCHETYPE_CANID);
        MonsterSDF cached,fallback,frozen;
        BuildPair(&monster,&cached,&fallback,&frozen);
        MonsterSDFMouth* a=&cached.mouths[0];
        MonsterSDFMouth* b=&fallback.mouths[0];
        MonsterSDFMouth* f=&frozen.mouths[0];
        switch(dependency){
            case 0:a->leftOrbitCenterLocal.x+=.013f;b->leftOrbitCenterLocal.x+=.013f;f->leftOrbitCenterLocal.x+=.013f;break;
            case 1:a->leftOrbitCenterLocal.y+=.013f;b->leftOrbitCenterLocal.y+=.013f;f->leftOrbitCenterLocal.y+=.013f;break;
            case 2:a->leftOrbitCenterLocal.z+=.013f;b->leftOrbitCenterLocal.z+=.013f;f->leftOrbitCenterLocal.z+=.013f;break;
            case 3:a->rightOrbitCenterLocal.x+=.013f;b->rightOrbitCenterLocal.x+=.013f;f->rightOrbitCenterLocal.x+=.013f;break;
            case 4:a->rightOrbitCenterLocal.y+=.013f;b->rightOrbitCenterLocal.y+=.013f;f->rightOrbitCenterLocal.y+=.013f;break;
            case 5:a->rightOrbitCenterLocal.z+=.013f;b->rightOrbitCenterLocal.z+=.013f;f->rightOrbitCenterLocal.z+=.013f;break;
            case 6:a->leftOrbitNormal.x+=.013f;b->leftOrbitNormal.x+=.013f;f->leftOrbitNormal.x+=.013f;break;
            case 7:a->leftOrbitNormal.y+=.013f;b->leftOrbitNormal.y+=.013f;f->leftOrbitNormal.y+=.013f;break;
            case 8:a->leftOrbitNormal.z+=.013f;b->leftOrbitNormal.z+=.013f;f->leftOrbitNormal.z+=.013f;break;
            case 9:a->rightOrbitNormal.x+=.013f;b->rightOrbitNormal.x+=.013f;f->rightOrbitNormal.x+=.013f;break;
            case 10:a->rightOrbitNormal.y+=.013f;b->rightOrbitNormal.y+=.013f;f->rightOrbitNormal.y+=.013f;break;
            case 11:a->rightOrbitNormal.z+=.013f;b->rightOrbitNormal.z+=.013f;f->rightOrbitNormal.z+=.013f;break;
            case 12:a->orbitRadii.x+=.013f;b->orbitRadii.x+=.013f;f->orbitRadii.x+=.013f;break;
            case 13:a->orbitRadii.y+=.013f;b->orbitRadii.y+=.013f;f->orbitRadii.y+=.013f;break;
            case 14:a->orbitRadii.z+=.013f;b->orbitRadii.z+=.013f;f->orbitRadii.z+=.013f;break;
            case 15:a->eyelidProfile.x+=.013f;b->eyelidProfile.x+=.013f;f->eyelidProfile.x+=.013f;break;
            case 16:a->eyelidProfile.y+=.013f;b->eyelidProfile.y+=.013f;f->eyelidProfile.y+=.013f;break;
            case 17:a->eyelidProfile.z+=.013f;b->eyelidProfile.z+=.013f;f->eyelidProfile.z+=.013f;break;
            case 18:a->eyelidCoverage=.0f;b->eyelidCoverage=.0f;f->eyelidCoverage=.0f;break;
        }
        b->eyelidCapsuleCacheValid=false;
        CompareCachedAndFallback(&cached,&fallback,&frozen,&cached.bounds);
        MonsterSDF_Free(&cached);MonsterSDF_Free(&fallback);MonsterSDF_Frozen_Free(&frozen);Monster_Free(&monster);
    }
    printf("[PASS] test_eyelid_cache_parameter_mutation_invalidates_by_key\n");
}

static void test_eyelid_cache_falls_back_for_changed_fp_controls(void) {
#if TEST_HAS_EYELID_FP_CONTROLS
    Monster monster=EyelidCacheFixture(HEAD_ARCHETYPE_CANID);
    MonsterSDF cached,fallback,frozen;
    BuildPair(&monster,&cached,&fallback,&frozen);
    unsigned int savedMxcsr=_mm_getcsr();
    unsigned short savedX87;
    __asm__ volatile("fnstcw %0" : "=m"(savedX87));
    const bool defaultEnvironment=(savedMxcsr&0x0000ffc0u)==0x00001f80u&&
        (savedX87&0x0f3fu)==0x033fu;
    bool cacheValidityMatches=false,cacheActuallyUsed=true,allControlsFallback=true;
    Vector3 probe=Vec3_Create(0,0,0);
    float savedRadius=0.0f;
    if(cached.mouthCount){
        MonsterSDFMouth* mouth=&cached.mouths[0];
        bool expectedValid=TEST_EYELID_CACHE_COMPILE_SUPPORTED&&defaultEnvironment&&
            mouth->eyelidCoverage>.001f;
        cacheValidityMatches=(mouth->eyelidCapsuleCacheValid==expectedValid);
        if(mouth->eyelidCapsuleCacheValid){
            savedRadius=mouth->eyelidCapsuleSegments[0].radius;
            mouth->eyelidCapsuleSegments[0].radius=100.0f;
            cacheActuallyUsed=false;
            for(int x=0;x<=8&&!cacheActuallyUsed;++x)for(int y=0;y<=8&&!cacheActuallyUsed;++y)
                for(int z=0;z<=8&&!cacheActuallyUsed;++z){
                    Vector3 p=Vec3_Create(
                        cached.bounds.start.x+(cached.bounds.end.x-cached.bounds.start.x)*(float)x/8.0f,
                        cached.bounds.start.y+(cached.bounds.end.y-cached.bounds.start.y)*(float)y/8.0f,
                        cached.bounds.start.z+(cached.bounds.end.z-cached.bounds.start.z)*(float)z/8.0f);
                    if(!SameFloatBits(MonsterSDF_EvaluateDistance(&cached,p),
                        MonsterSDF_Frozen_EvaluateDistance(&frozen,p))){probe=p;cacheActuallyUsed=true;}
                }
        }
    }
    struct FPControlCase { unsigned int mxcsrMask,mxcsrValue;unsigned short x87Mask,x87Value; };
    const struct FPControlCase cases[]={
        {0x00006000u,0x00002000u,0,0},{0x00006000u,0x00004000u,0,0},
        {0x00006000u,0x00006000u,0,0},{0x00008000u,0x00008000u,0,0},
        {0x00000040u,0x00000040u,0,0},
        {0,0,0x0c00u,0x0400u},{0,0,0x0c00u,0x0800u},{0,0,0x0c00u,0x0c00u},
        {0,0,0x0300u,0x0000u},{0,0,0x0300u,0x0100u},{0,0,0x0300u,0x0200u}
    };
    if(cached.mouthCount&&cached.mouths[0].eyelidCapsuleCacheValid){
        for(size_t i=0;i<sizeof(cases)/sizeof(cases[0]);++i){
            unsigned int mxcsr=(savedMxcsr&~cases[i].mxcsrMask)|cases[i].mxcsrValue;
            unsigned short x87=(unsigned short)((savedX87&~cases[i].x87Mask)|cases[i].x87Value);
            _mm_setcsr(mxcsr);
            __asm__ volatile("fldcw %0" : : "m"(x87));
            float actual=MonsterSDF_EvaluateDistance(&cached,probe);
            float reference=MonsterSDF_Frozen_EvaluateDistance(&frozen,probe);
            if(!SameFloatBits(actual,reference))allControlsFallback=false;
        }
        cached.mouths[0].eyelidCapsuleSegments[0].radius=savedRadius;
    }
    _mm_setcsr(savedMxcsr);
    __asm__ volatile("fldcw %0" : : "m"(savedX87));
    TEST_ASSERT(cacheValidityMatches,"El predicado de compilación/FP no coincide con la validez publicada");
    TEST_ASSERT(cacheActuallyUsed,"La fixture predeterminada no ejercitó realmente el cache");
    TEST_ASSERT(allControlsFallback,"Un control MXCSR/x87 no admitido usó los descriptores alterados");
    MonsterSDF_Free(&cached);MonsterSDF_Free(&fallback);MonsterSDF_Frozen_Free(&frozen);Monster_Free(&monster);
#endif
    printf("[PASS] test_eyelid_cache_falls_back_for_changed_fp_controls\n");
}

static void test_eyelid_cache_five_species_four_stages(void) {
    CreatureRecipeDocument horse={0},donkey={0};
    char error[256]={0};
    TEST_ASSERT(CreatureRecipeIO_LoadFile(&horse,"recipes/caballo.creature.json",error,sizeof(error)),
        "No se cargó la receta équida para paridad por etapas");
    TEST_ASSERT(CreatureRecipeIO_LoadFile(&donkey,"recipes/asno.creature.json",error,sizeof(error)),
        "No se cargó la receta asnal para paridad por etapas");
    const CreatureRecipe* recipes[]={CreatureRecipes_Lizard(),CreatureRecipes_Dog(),
        CreatureRecipes_Brumacerno(),&horse.recipe,&donkey.recipe};
    for(size_t species=0;species<sizeof(recipes)/sizeof(recipes[0]);++species){
        TEST_ASSERT(recipes[species],"Receta nula en corpus de especies");
        for(unsigned stage=0;stage<4;++stage){
            Monster monster=Monster_Create();
            TEST_ASSERT(Creature_BuildMonster(&monster,recipes[species],
                CreatureRecipe_GetStage(recipes[species],(CreatureStage)stage)),
                "No se construyó criatura del corpus por etapas");
            MonsterSDF cached,fallback,frozen;
            BuildPair(&monster,&cached,&fallback,&frozen);
            TEST_ASSERT(cached.mouthCount>0,"Especie sin snapshot de cabeza en corpus");
            CompareCachedAndFallback(&cached,&fallback,&frozen,&cached.bounds);
            MonsterSDF_Free(&cached);MonsterSDF_Free(&fallback);MonsterSDF_Frozen_Free(&frozen);
            Monster_Free(&monster);
        }
    }
    printf("[PASS] test_eyelid_cache_five_species_four_stages\n");
}

void run_eyelid_cache_tests(void) {
    printf("\n--- Cache derivado de segmentos palpebrales ---\n");
    test_eyelid_cache_exact_field_and_mutation_fallback();
    test_eyelid_cache_parameter_mutation_invalidates_by_key();
    test_eyelid_cache_falls_back_for_changed_fp_controls();
    test_eyelid_cache_five_species_four_stages();
}
