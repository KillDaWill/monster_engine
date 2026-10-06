#include "GpuSDFSurfaceCache.h"
#include "test_utils.h"
#include "SDFPrimitives.h"
#include "SDFInterval.h"
#include "SDFOperations.h"
#include "MonsterSDF.h"
#include <string.h>

static float IntervalTestRandom(uint32_t* seed) {
    *seed=*seed*1664525u+1013904223u;return (*seed>>8)/16777215.f;
}
static void TestPinnaIntervalContainment(void) {
    uint32_t seed=0x51df17u;
    for(unsigned box=0;box<2000;++box) {
        float r=IntervalTestRandom(&seed);
        Vector3 shape=Vec3_Create(.04f+.36f*r,.08f+1.2f*IntervalTestRandom(&seed),.003f+.06f*IntervalTestRandom(&seed));
        Vector3 up=Vec3_Normalize(Vec3_Create(.7f*IntervalTestRandom(&seed),1,.4f*IntervalTestRandom(&seed)));
        Vector3 side=Vec3_Normalize(Vec3_Cross(up,Vec3_Create(0,0,1))),normal=Vec3_Cross(side,up);
        float tip=.04f+.51f*IntervalTestRandom(&seed),concavity=IntervalTestRandom(&seed),curve=shape.x*(IntervalTestRandom(&seed)-.5f);
        float roll=IntervalTestRandom(&seed),round=IntervalTestRandom(&seed),fold=shape.x*(IntervalTestRandom(&seed)-.5f);
        float flare=IntervalTestRandom(&seed),bow=2*IntervalTestRandom(&seed)-1,asym=IntervalTestRandom(&seed);
        Vector3 center=Vec3_Add(Vec3_Scale(up,shape.y*(IntervalTestRandom(&seed)-.5f)),Vec3_Add(Vec3_Scale(side,shape.x*(2*IntervalTestRandom(&seed)-1)),Vec3_Scale(normal,shape.z*(4*IntervalTestRandom(&seed)-2))));
        // Se incluyen exactamente las puntas y cajas que cruzan su rama singular.
        if(box%5==0)center=Vec3_Scale(up,shape.y*.5f);
        float radius=box%3==0?0:.0001f+.1f*IntervalTestRandom(&seed);
        Vector3 half=Vec3_Create(radius,radius,radius);AABB3D bounds={Vec3_Sub(center,half),Vec3_Add(center,half)};
        SDFInterval range=SDF_CurvedPinnaInterval(bounds,shape,up,side,normal,tip,concavity,curve,roll,round,fold,flare,bow,asym);
        TEST_ASSERT(range.lo<=range.hi,"Intervalo de pabellón ordenado");
        for(unsigned point=0;point<17;++point) {
            Vector3 p;
            if(point<8)p=Vec3_Create(point&1?bounds.end.x:bounds.start.x,point&2?bounds.end.y:bounds.start.y,point&4?bounds.end.z:bounds.start.z);
            else if(point==8)p=center;
            else p=Vec3_Create(bounds.start.x+(bounds.end.x-bounds.start.x)*IntervalTestRandom(&seed),bounds.start.y+(bounds.end.y-bounds.start.y)*IntervalTestRandom(&seed),bounds.start.z+(bounds.end.z-bounds.start.z)*IntervalTestRandom(&seed));
            float value=SDF_CurvedPinna(p,shape,up,side,normal,tip,concavity,curve,roll,round,fold,flare,bow,asym);
            TEST_ASSERT(value>=range.lo&&value<=range.hi,"Contener el campo exacto, incluso en punta y concha");
        }
    }
}

static void TestPrimitiveIntervalContainment(void) {
    uint32_t seed=0xc01d51dfu;
    for(unsigned i=0;i<4000;++i) {
        Vector3 c=Vec3_Create(4*IntervalTestRandom(&seed)-2,4*IntervalTestRandom(&seed)-2,4*IntervalTestRandom(&seed)-2);
        if(i%7==0)c=Vec3_Create(0,0,0);
        float radius=i%3==0?0:.001f+.3f*IntervalTestRandom(&seed);
        Vector3 half=Vec3_Create(radius,radius,radius);AABB3D box={Vec3_Sub(c,half),Vec3_Add(c,half)};
        Vector3 r=Vec3_Create(.001f+IntervalTestRandom(&seed),.001f+IntervalTestRandom(&seed),.001f+IntervalTestRandom(&seed));
        Vector3 a=Vec3_Create(.2f,-.3f,.1f),b=Vec3_Create(-.4f,.8f,-.5f);
        if(i%9==0)b=a;
        SDFInterval ellipsoid=SDF_EllipsoidInterval(box,r),capsule=SDF_CapsuleInterval(box,a,b,r.x);
        SDFInterval slot=SDF_RoundedSlotInterval(box,r.x,r.y,r.z);
        float k=i%2==0?0:.01f+.5f*IntervalTestRandom(&seed);
        SDFInterval joined=SDF_SmoothUnionInterval(ellipsoid,capsule,k),cut=SDF_SmoothSubtractInterval(ellipsoid,slot,k);
        for(unsigned point=0;point<17;++point) {
            Vector3 p=point<8?Vec3_Create(point&1?box.end.x:box.start.x,point&2?box.end.y:box.start.y,point&4?box.end.z:box.start.z):
                Vec3_Create(box.start.x+2*radius*IntervalTestRandom(&seed),box.start.y+2*radius*IntervalTestRandom(&seed),box.start.z+2*radius*IntervalTestRandom(&seed));
            float e=SDF_Ellipsoid(p,r),ca=SDF_Capsule(p,a,b,r.x),sl=SDF_RoundedSlotExtruded(p,r.x,r.y,r.z);
            TEST_ASSERT(e>=ellipsoid.lo&&e<=ellipsoid.hi,"Intervalo elipsoidal contiene la rama singular y radios planos");
            TEST_ASSERT(ca>=capsule.lo&&ca<=capsule.hi,"Intervalo de cápsula contiene las tapas y tramos nulos");
            TEST_ASSERT(sl>=slot.lo&&sl<=slot.hi,"Intervalo de slot contiene esquinas y transición lateral");
            float u=SDF_SmoothUnion(e,ca,k),d=SDF_SmoothSubtract(e,sl,k);
            TEST_ASSERT(u>=joined.lo&&u<=joined.hi,"Unión conserva contención incluso al activar ramas tempranas");
            TEST_ASSERT(d>=cut.lo&&d<=cut.hi,"Resta conserva contención incluso en cavidades internas");
        }
    }
}

static void TestSweepIntervalContainment(void) {
    uint32_t seed=0x5ee051dfu;
    for(unsigned i=0;i<2000;++i) {
        SDFSweepStation stations[2]={0};
        for(unsigned j=0;j<2;++j) {
            SDFSweepStation* st=&stations[j];
            st->center=Vec3_Create(IntervalTestRandom(&seed)-.5f,IntervalTestRandom(&seed)-.5f,j?-.2f-2*IntervalTestRandom(&seed):.2f+2*IntervalTestRandom(&seed));
            st->width=.002f+IntervalTestRandom(&seed);st->height=.002f+IntervalTestRandom(&seed);
            st->widthSlope=4*IntervalTestRandom(&seed)-2;st->heightSlope=4*IntervalTestRandom(&seed)-2;st->centerSlope=4*IntervalTestRandom(&seed)-2;
        }
        Vector3 c=Vec3_Create(2*IntervalTestRandom(&seed)-1,2*IntervalTestRandom(&seed)-1,6*IntervalTestRandom(&seed)-3);
        float radius=i%3==0?0:.2f*IntervalTestRandom(&seed);Vector3 half=Vec3_Create(radius,radius,radius);
        AABB3D box={Vec3_Sub(c,half),Vec3_Add(c,half)};SDFSweepStation a=stations[0],b=stations[1];
        SDFInterval interval=SDF_SweepSegmentInterval(box,a.center,b.center,a.width,b.width,a.height,b.height,
            a.widthSlope,b.widthSlope,a.heightSlope,b.heightSlope,a.centerSlope,b.centerSlope,a.center.z,b.center.z);
        for(unsigned j=0;j<17;++j) {
            Vector3 p=j<8?Vec3_Create(j&1?box.end.x:box.start.x,j&2?box.end.y:box.start.y,j&4?box.end.z:box.start.z):
                Vec3_Create(box.start.x+2*radius*IntervalTestRandom(&seed),box.start.y+2*radius*IntervalTestRandom(&seed),box.start.z+2*radius*IntervalTestRandom(&seed));
            float value=SDF_EllipticalSweepZ(p,stations,2);
            TEST_ASSERT(value>=interval.lo&&value<=interval.hi,"Barrido incluye perfiles no monótonos, tapas y radio mínimo");
        }
    }
}

static void TestConnectorIntervalContainment(void) {
    uint32_t seed=0xc0ffee17u;
    for(unsigned i=0;i<2000;++i) {
        MonsterSDFConnector conn={0};
        conn.a=Vec3_Create(4*IntervalTestRandom(&seed)-2,4*IntervalTestRandom(&seed)-2,4*IntervalTestRandom(&seed)-2);
        conn.forward=Vec3_Normalize(Vec3_Create(IntervalTestRandom(&seed)-.5f,IntervalTestRandom(&seed)-.5f,1));
        conn.side=Vec3_Normalize(Vec3_Cross(Vec3_Create(0,1,0),conn.forward));conn.up=Vec3_Cross(conn.forward,conn.side);
        conn.length=i%11==0?0:.002f+2*IntervalTestRandom(&seed);
        conn.widthA=.001f+IntervalTestRandom(&seed);conn.widthB=.001f+IntervalTestRandom(&seed);
        conn.heightA=.001f+IntervalTestRandom(&seed);conn.heightB=.001f+IntervalTestRandom(&seed);
        conn.widthBulge=IntervalTestRandom(&seed)-.5f;conn.heightBulge=IntervalTestRandom(&seed)-.5f;
        MonsterSDF field={0};field.connectors=&conn;field.connectorCount=1;
        Vector3 c=Vec3_Add(conn.a,Vec3_Create(2*IntervalTestRandom(&seed)-1,2*IntervalTestRandom(&seed)-1,3*IntervalTestRandom(&seed)-1));
        float radius=i%3==0?0:.2f*IntervalTestRandom(&seed);Vector3 half=Vec3_Create(radius,radius,radius);
        AABB3D box={Vec3_Sub(c,half),Vec3_Add(c,half)};
        SDFInterval interval=SDF_ConnectorInterval(box,conn.a,conn.forward,conn.side,conn.up,conn.length,
            conn.widthA,conn.widthB,conn.heightA,conn.heightB,conn.widthBulge,conn.heightBulge);
        for(unsigned j=0;j<17;++j) {
            Vector3 p=j<8?Vec3_Create(j&1?box.end.x:box.start.x,j&2?box.end.y:box.start.y,j&4?box.end.z:box.start.z):
                Vec3_Create(box.start.x+2*radius*IntervalTestRandom(&seed),box.start.y+2*radius*IntervalTestRandom(&seed),box.start.z+2*radius*IntervalTestRandom(&seed));
            float value=MonsterSDF_EvaluateDistance(&field,p);
            TEST_ASSERT(value>=interval.lo&&value<=interval.hi,"Conector contiene la receta real con abombamiento y extremos nulos");
        }
    }
}

static void TestSparseDiscoveryAndPublication(void) {
    GpuSDFSurfaceGrid grid={0};AABB3D bounds={Vec3_Create(-3,-2,-3),Vec3_Create(3,2,3)};
    TEST_ASSERT(GpuSDFSurfaceGrid_Configure(&grid,bounds,.1f,2),"Rejilla de prueba dispersa");
    GpuSDFSurfaceCache* cache=GpuSDFSurfaceCache_Create(&grid,8);
    TEST_ASSERT(cache!=NULL,"Crear control persistente sin GL");
    MonsterSDFBodyPart parts[2]={0};parts[0].center=Vec3_Create(-2,0,0);parts[0].radii=Vec3_Create(.2f,.3f,.2f);
    parts[1]=parts[0];parts[1].center.x=2;
    MonsterSDF sdf={0};sdf.bounds=bounds;sdf.bodyParts=parts;sdf.bodyPartCount=1;
    sdf.config.bodySmoothness=.05f;
    TEST_ASSERT(GpuSDFSurfaceCache_Update(cache,&sdf)&&cache->dirtyCount>0,"Una feature nueva siembra bricks sin superficie anterior");
    uint64_t first=cache->generation;size_t initial=cache->residentCount;
    GpuSDFSurfaceCache_Commit(cache,first);
    TEST_ASSERT(cache->spatial->dirtyCount==0,"Publicar versión completa limpia únicamente la generación enviada");
    TEST_ASSERT(GpuSDFSurfaceCache_Update(cache,&sdf)&&cache->dirtyCount==0,"Edad estática reutiliza muestras y malla");
    sdf.bodyPartCount=2;
    TEST_ASSERT(GpuSDFSurfaceCache_Update(cache,&sdf)&&cache->newCount>0&&cache->residentCount>initial,"Feature distante naciente descubre bricks independientes");
    bool found=false;
    for(size_t i=0;i<cache->dirtyCount;++i){float x=grid.origin.x+cache->dirty[i].x*grid.voxelSize;if(x>1)found=true;}
    TEST_ASSERT(found,"El trabajo incluye la región nueva fuera de la superficie previa");
    GpuSDFSurfaceCache_Commit(cache,first);
    TEST_ASSERT(cache->spatial->dirtyCount>0,"Una publicación obsoleta no limpia solicitudes nuevas");
    GpuSDFSurfaceCache_Commit(cache,cache->generation);
    sdf.bodyPartCount=0;
    for(unsigned i=0;i<5;++i){TEST_ASSERT(GpuSDFSurfaceCache_Update(cache,&sdf),"Retirar features conserva control válido");GpuSDFSurfaceCache_Commit(cache,cache->generation);}
    TEST_ASSERT(cache->residentCount==0&&cache->freeCount>0,"La histéresis retira memoria de regiones ausentes");
    uint32_t highWater=cache->nextSlot;sdf.bodyPartCount=2;
    TEST_ASSERT(GpuSDFSurfaceCache_Update(cache,&sdf)&&cache->newCount>0,"Resurrección después de vaciar la caché");
    TEST_ASSERT(cache->nextSlot==highWater,"Resurrección reutiliza slots retirados sin crecimiento ilimitado");
    TEST_ASSERT(!memcmp(&grid,&cache->grid,sizeof(grid)),"Historia y reversión no desplazan la retícula");
    MonsterSDFSurfaceFeature features[2];
    TEST_ASSERT(MonsterSDF_CollectSurfaceFeatures(&sdf,features,1,0)==SIZE_MAX,"Capacidad insuficiente nunca trunca features");
    TEST_ASSERT(MonsterSDF_CollectSurfaceFeatures(&sdf,features,2,0)==2&&features[0].id!=features[1].id,"Cada feature conserva identidad global distinta");
    GpuSDFSurfaceCache_Destroy(cache);
}

void run_gpu_sdf_surface_tests(void) {
    TestSparseDiscoveryAndPublication();
    TestPinnaIntervalContainment();
    TestPrimitiveIntervalContainment();
    TestSweepIntervalContainment();
    TestConnectorIntervalContainment();
    for(unsigned i=0;i<=100000;++i) {
        float angle=3.14159265f*(i/100000.f);
        TEST_ASSERT(fabsf(SDF_SinHalfTurn(angle)-sinf(angle))<.0000003f,
            "Seno compartido conserva precisión float en media vuelta");
    }
    AABB3D envelope={Vec3_Create(-1.03f,-2.07f,-3.01f),Vec3_Create(4.02f,3.04f,2.03f)};
    GpuSDFSurfaceGrid grid={0};
    TEST_ASSERT(GpuSDFSurfaceGrid_Configure(&grid,envelope,.1f,2),"Configurar envolvente estable");
    TEST_ASSERT(GpuSDFSurfaceGrid_Contains(&grid,envelope),"Cubrir todos los extremos de la envolvente");
    GpuSDFSurfaceGrid before=grid;
    for(unsigned i=0;i<=100;++i) {
        float t=i/100.f;
        AABB3D local={Vec3_Create(-1+t,-2+t,-3+t),Vec3_Create(4-t,3-t,2-t)};
        TEST_ASSERT(GpuSDFSurfaceGrid_Contains(&grid,local),"Las edades intermedias conservan la retícula");
    }
    TEST_ASSERT(!memcmp(&grid,&before,sizeof(grid)),"Consultar edades no modifica origen ni índices");
    AABB3D escaped=envelope;escaped.start.x=grid.origin.x-.001f;
    TEST_ASSERT(!GpuSDFSurfaceGrid_Contains(&grid,escaped),"Rechazar campo fuera del sobre sin reajuste silencioso");
    TEST_ASSERT(!GpuSDFSurfaceGrid_Configure(&grid,envelope,0,2),"Rechazar paso cero");
    TEST_ASSERT(!GpuSDFSurfaceGrid_Configure(&grid,envelope,NAN,2),"Rechazar paso no finito");
    TEST_ASSERT(!GpuSDFSurfaceGrid_Configure(&grid,envelope,.000001f,2),"Rechazar desbordamiento de índices GPU");
    TEST_ASSERT(!GpuSDFSurfaceGrid_Configure(&grid,envelope,.1f,4),"Rechazar refinamiento no soportado");
    envelope.end.x=NAN;
    TEST_ASSERT(!GpuSDFSurfaceGrid_Configure(&grid,envelope,.1f,2),"Rechazar cotas no finitas");
    TEST_ASSERT(!memcmp(&grid,&before,sizeof(grid)),"Un plan fallido conserva la rejilla anterior");
    GpuSDFSurfacePlan plan;
    size_t row=(grid.cells[0]+1)*(grid.cells[1]+1),cellRow=grid.cells[0]*grid.cells[1];
    TEST_ASSERT(GpuSDFSurfaceGrid_PlanDense(&grid,row*8,cellRow*7,6,&plan),"Plan denso acotado");
    TEST_ASSERT(plan.slabDepth==6&&plan.maxSamples==row*7&&plan.maxCells==cellRow*6,"Respetar presupuesto y profundidad solicitada");
    size_t covered=0,updated=0;
    for(unsigned z=0;z<grid.cells[2];z+=plan.slabDepth) {
        unsigned depth=grid.cells[2]-z;if(depth>plan.slabDepth)depth=plan.slabDepth;
        covered+=depth*cellRow;updated+=(depth+1)*row;
    }
    TEST_ASSERT(covered==grid.cellCount&&updated==plan.updatedSamples,"Cubrir toda celda una vez y conservar planos compartidos");
    TEST_ASSERT(!GpuSDFSurfaceGrid_PlanDense(&grid,row,cellRow*7,6,&plan),"Rechazar presupuesto sin plano compartido");
    TEST_ASSERT(GpuSDFSurfaceGrid_PlanDense(&grid,row*3,cellRow*7,6,&plan)&&plan.slabDepth==2,"Reducir franja sin reducir resolución");
    GpuSDFSurfaceGrid invalid=grid;invalid.cells[0]=UINT32_MAX;
    TEST_ASSERT(!GpuSDFSurfaceGrid_PlanDense(&invalid,SIZE_MAX,SIZE_MAX,128,&plan),"Rechazar índices de esquinas desbordados");
    printf("[PASS] gpu_sdf_surface: envolvente, retícula inmutable y límites de índices\n");
}
