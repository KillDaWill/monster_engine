/** @file test_dog.c
 * @brief Regresiones anatómicas, auriculares y de conectividad del tetrápodo erecto.
 */
#include "test_utils.h"
#include "Creature.h"
#include "CreatureRig.h"
#include "AxialBody.h"
#include "Limb.h"
#include "Monster.h"
#include "MonsterVisual.h"
#include "SDFPrimitives.h"
#include "AxialBodyUprightTetrapod.h"
#include <string.h>

static const AnatomyNode* Node(const AnatomyGraph* g,unsigned module,unsigned local) {
    const AnatomyNode* n=AnatomyGraph_FindModuleNode(g,module,local);
    TEST_ASSERT(n,"Existe el landmark requerido");return n;
}
static void AnatomyChecks(void) {
    const CreatureRecipe* r=CreatureRecipes_Dog();
    TEST_ASSERT(CreatureRecipes_Find(r->id)==r && r->id!=CreatureRecipes_Lizard()->id,"ID persistente canino registrado");
    TEST_ASSERT(r->gait==CREATURE_GAIT_NONE,"El hito estático no activa locomoción");
    AnatomyGraph adult;
    TEST_ASSERT(Creature_ResolveAnatomy(r,&r->adult,&adult),"Resolver anatomía adulta");
    for (unsigned stage=0;stage<4;++stage) {
        const CreaturePhenotype* p=CreatureRecipe_GetStage(r,(CreatureStage)stage);
        TEST_ASSERT(CreatureRecipe_Validate(r,p),"Valida cada etapa canina");
        AnatomyGraph g;
        TEST_ASSERT(Creature_ResolveAnatomy(r,p,&g) && AnatomyGraph_Validate(&g),"Grafo mamífero válido");
        TEST_ASSERT(AnatomyGraph_TopologyCompatible(&adult,&g),"Topología persistente entre etapas");
        Monster m=Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(&m,r,p),"Construcción completa con rig estático");
        TEST_ASSERT(m.hasHead && m.head.phenotype.archetype==HEAD_ARCHETYPE_CANID && !m.animation,"Cabeza canina sin locomoción");
        Rig rig;
        TEST_ASSERT(CreatureRig_Build(&m,&rig) && rig.limbCount==4,"Cuatro cadenas de soporte válidas");
        float ground=0;
        for (unsigned i=0;i<4;++i) {
            TEST_ASSERT(p->limbs[i].archetype==LIMB_ARCHETYPE_MAMMAL && p->limbs[i].digitCount==4,"Cuatro miembros mamíferos con cuatro dedos");
            const AnatomyNode* root=Node(&g,10+i,LIMB_NODE_ROOT);
            const AnatomyNode* knee=Node(&g,10+i,LIMB_NODE_MIDDLE);
            const AnatomyNode* ankle=Node(&g,10+i,LIMB_NODE_DISTAL);
            const AnatomyNode* paw=Node(&g,10+i,LIMB_NODE_AUTOPOD);
            LimbRigDescriptor desc;
            TEST_ASSERT(Limb_BuildRigDescriptor(&p->limbs[i],10+i,&g,&desc),"Descriptor mamífero");
            float sole=paw->center.y-desc.soleHeight;
            if (i==0) ground=sole;
            TEST_ASSERT(fabsf(sole-ground)<.001f*p->axial.totalScale,"Cuatro suelas coplanares");
            TEST_ASSERT(ankle->center.y-paw->center.y>.25f*p->axial.totalScale,"Carpo/corvejón elevado");
            TEST_ASSERT(fabsf(paw->center.x-root->center.x)<.05f*p->axial.totalScale,"Soporte parasagital");
            for (unsigned d=0;d<4;++d) {
                const AnatomyNode* tip=Node(&g,10+i,Limb_DigitLocalId(d,p->limbs[i].digitPhalanges[d]+1));
                TEST_ASSERT(tip->center.z>paw->center.z && tip->center.z-paw->center.z<.45f*p->axial.totalScale,"Dedos cortos orientados hacia delante");
                TEST_ASSERT(fabsf(tip->center.y-tip->heightRadius-ground)<.001f,"Falanges apoyadas en la suela");
            }
            if (i>=2) TEST_ASSERT(knee->center.z>root->center.z && ankle->center.z<knee->center.z && paw->center.z>ankle->center.z,"Zigzag sagital de cadera a pata");
            else TEST_ASSERT(fabsf(knee->center.y-(Node(&g,1,AXIAL_NODE_THORAX_ANTERIOR)->center.y-Node(&g,1,AXIAL_NODE_THORAX_ANTERIOR)->heightRadius))<.15f*p->axial.totalScale,"Pecho aproximadamente al codo");
            TEST_ASSERT(FLOAT_NEAR(rig.limbs[i].soleHeight,desc.soleHeight),"Rig conserva altura de suela");
        }
        const AnatomyNode* chest=Node(&g,1,AXIAL_NODE_THORAX_ANTERIOR);
        const AnatomyNode* abdomen=Node(&g,1,AXIAL_NODE_ABDOMEN);
        const AnatomyNode* pelvis=Node(&g,1,AXIAL_NODE_PELVIS);
        float H=p->axial.withersHeight*p->axial.totalScale;
        TEST_ASSERT(chest->heightRadius>abdomen->heightRadius*1.4f && chest->widthRadius>pelvis->widthRadius,"Caja torácica dominante");
        TEST_ASSERT(abdomen->center.y-abdomen->heightRadius>chest->center.y-chest->heightRadius+.15f*H,"Retracción abdominal visible");
        TEST_ASSERT(fabsf(chest->center.y+chest->heightRadius-pelvis->center.y-pelvis->heightRadius)<.02f*H,"Dorso nivelado");
        float bodyLength=p->axial.trunkLength*p->axial.totalScale+chest->widthRadius;
        TEST_ASSERT(bodyLength/H>.90f && bodyLength/H<1.10f,"Proporción corporal aproximadamente cuadrada");
        const HeadAnatomy* h=&m.head.anatomy;
        float headLength=h->landmarks.muzzleTip.z-h->landmarks.neckAttachment.z;
        TEST_ASSERT(headLength/H>.33f && headLength/H<.48f,"Longitud cefálica cercana a 0.4 H");
        float muzzle=h->landmarks.muzzleTip.z-h->landmarks.muzzleRoot.z;
        TEST_ASSERT(muzzle/headLength>.40f && muzzle/headLength<.60f,"Hocico y caja craneal comparables");
        TEST_ASSERT(h->surface.hasNasalPad && h->surface.hasEars && h->surface.sweptSkull,"Rasgos cefálicos compartidos activos");
        Monster_Free(&m);
    }
    AnatomyGraph reptile;
    TEST_ASSERT(Creature_ResolveAnatomy(CreatureRecipes_Lizard(),&CreatureRecipes_Lizard()->adult,&reptile),"Lagarto sigue resolviendo");
    float mammalSpread=fabsf(Node(&adult,10,4)->center.x-Node(&adult,10,1)->center.x);
    float reptileSpread=fabsf(Node(&reptile,10,4)->center.x-Node(&reptile,10,1)->center.x);
    TEST_ASSERT(mammalSpread<reptileSpread*.15f,"Abducción mamífera mucho menor que reptiliana");
    CreaturePhenotype unsupported=r->adult;
    unsupported.axial.archetype=AXIAL_ARCHETYPE_CUSTOM;
    TEST_ASSERT(!CreatureRecipe_Validate(r,&unsupported),"Rechazar capacidades axiales no implementadas");
    unsupported=r->adult;unsupported.limbs[0].archetype=LIMB_ARCHETYPE_WING;
    TEST_ASSERT(!CreatureRecipe_Validate(r,&unsupported),"Rechazar miembros no implementados");
}
/* La apariencia no puede reconstruir la posición desde la órbita ni
 * convertir cobertura palpebral en un desplazamiento del globo. */
static void EyeAnchorChecks(void) {
    const CreatureRecipe* recipes[]={CreatureRecipes_Dog(),CreatureRecipes_Lizard()};
    for (unsigned r=0;r<2;++r) for (unsigned stage=0;stage<4;++stage) {
        for (unsigned exposure=0;exposure<3;++exposure) {
            CreaturePhenotype p=*CreatureRecipe_GetStage(recipes[r],(CreatureStage)stage);
            p.eyes.protrusion=exposure*.5f;
            Monster m=Monster_Create();
            TEST_ASSERT(Creature_ResolveAppearance(&m,recipes[r],&p),"Resolver ojos por etapa y exposición");
            const HeadLandmarks* l=&m.head.anatomy.landmarks;
            for (unsigned eye=0;eye<2;++eye) {
                Vector3 center=eye?l->rightEyeCenter:l->leftEyeCenter;
                Vector3 delta=Vec3_Sub(m.eyes[eye].offset,center);
                TEST_ASSERT(Vec3_Length(delta)<=m.eyes[eye].scale.z*.051f+1e-6f,
                    "Apariencia conserva el centro anatómico con ajuste menor al 5.1 por ciento");
                TEST_ASSERT(Vec3_Length(Vec3_Cross(delta,m.eyes[eye].forward))<1e-6f,
                    "Ajuste de exposición sigue la normal orbital");
                if (exposure==1) TEST_ASSERT(Vec3_Length(delta)<1e-6f,"Exposición neutra conserva exactamente el anclaje");
            }
            Monster_Free(&m);
        }
    }
}

static void EarAndMeshChecks(void) {
    Monster m=Monster_Create();
    const CreatureRecipe* recipe=CreatureRecipes_Dog();
    TEST_ASSERT(Creature_BuildMonster(&m,recipe,&recipe->adult),"Construcción para prueba visual");
    const HeadSurfaceRecipe* h=&m.head.anatomy.surface;
    HeadResolvedMeasurements measure=HeadAnatomy_Measure(&m.head.anatomy);
    TEST_ASSERT(isfinite(measure.actualHeadLength)&&isfinite(measure.actualMaxZygomaticWidth)&&
                isfinite(measure.actualWidthLengthRatio)&&isfinite(measure.interEyeDistance)&&
                isfinite(measure.mouthCornerSpan),"Medidas cefálicas resueltas finitas");
    TEST_ASSERT(h->cranialTemporalRadii.x>h->cranialPostorbitalRadii.x &&
                h->cranialTemporalRadii.x>h->cranialOccipitalRadii.x,
                "Máximo transversal craneal en la región temporal posterior");
    TEST_ASSERT(measure.muzzleRootWidth<measure.actualMaxZygomaticWidth*.75f &&
                measure.muzzleTipWidth<measure.muzzleRootWidth*.58f,
                "Anchura facial desciende de región cigomática a nariz");
    TEST_ASSERT(measure.interEyeDistance<measure.actualMaxZygomaticWidth*.65f &&
                measure.mouthCornerSpan<measure.actualMaxZygomaticWidth*.55f,
                "Órbitas y comisuras permanecen dentro del ancho facial");
    TEST_ASSERT(m.head.anatomy.landmarks.leftMouthCorner.x<
                h->leftMaxillaryCenter.x+h->maxillaryRadii.x,
                "Comisura termina sobre el maxilar y no sobre la mejilla");
    TEST_ASSERT(m.head.anatomy.landmarks.leftJawHinge.z<m.head.anatomy.landmarks.leftMouthCorner.z &&
                m.head.anatomy.landmarks.leftJawHinge.x>m.head.anatomy.landmarks.leftMouthCorner.x,
                "Bisagra posterior y más lateral que la comisura");
    TEST_ASSERT(h->earShape.x<h->craniumRadii.x*.45f && h->earShape.y>h->earShape.x*3.0f,
                "Pinna erecta proporcionada al cráneo");
    TEST_ASSERT(h->orbitRadii.y/h->orbitRadii.x>.30f&&h->orbitRadii.y/h->orbitRadii.x<.8f,
        "Abertura orbital más ancha que alta");
    TEST_ASSERT(fabsf(m.head.anatomy.landmarks.leftEyeCenter.x+m.head.anatomy.landmarks.rightEyeCenter.x)<1e-5f,
        "Anclajes oculares simétricos");
    Vector3 side=h->leftEarSide,earNormal=h->leftEarNormal;
    TEST_ASSERT(fabsf(Vec3_Length(h->earDirection)-1)<1e-5f&&fabsf(Vec3_Length(side)-1)<1e-5f&&
                fabsf(Vec3_Length(earNormal)-1)<1e-5f&&fabsf(Vec3_Dot(side,h->earDirection))<1e-5f&&
                fabsf(Vec3_Dot(earNormal,h->earDirection))<1e-5f&&fabsf(Vec3_Dot(side,earNormal))<1e-5f,
                "Marco auricular ortonormal");
    Vector3 rotatedUp,rotatedSide,rotatedNormal;
    TEST_ASSERT(HeadPinna_BuildFrame(Vec3_Create(-.25f,.62f,-.73f),Vec3_Create(0,0,1),
                &rotatedUp,&rotatedSide,&rotatedNormal)&&
                fabsf(Vec3_Dot(rotatedUp,rotatedSide))<1e-5f&&
                fabsf(Vec3_Dot(rotatedUp,rotatedNormal))<1e-5f,
                "Marco derecho independiente con orientación Z significativa");
    HeadSurfaceRecipe turned=*h;
    TEST_ASSERT(HeadSurfaceRecipe_SetEarPose(&turned,true,m.head.anatomy.landmarks.rightEarBase,
                Vec3_Create(-.25f,.62f,-.73f),Vec3_Create(0,0,1)) &&
                Vec3_Distance(turned.leftEarCenter,h->leftEarCenter)<1e-6f &&
                Vec3_Distance(turned.earDirection,h->earDirection)<1e-6f &&
                Vec3_Distance(turned.rightEarDirection,h->rightEarDirection)>.3f &&
                turned.earRadii.z>=h->earRadii.z,
                "Giro unilateral actualiza su marco y cotas sin mover la oreja opuesta");
    TEST_ASSERT(SDF_Ellipsoid(Vec3_Sub(m.head.anatomy.landmarks.leftEarBase,h->craniumCenter),h->craniumRadii)<0,
                "Concha implantada dentro del cráneo");
    Vector3 earWorld=Vec3_Add(m.bodyParts[0].positionRender,h->leftEarCenter);
    SurfaceCoordinate innerCoat=SurfaceMapper_MapPoint(earWorld,h->leftEarNormal,SDF_MATERIAL_SKIN,
        &m.anatomyGraph,&m.surfaceMapping);
    SurfaceCoordinate outerCoat=SurfaceMapper_MapPoint(earWorld,Vec3_Scale(h->leftEarNormal,-1),SDF_MATERIAL_SKIN,
        &m.anatomyGraph,&m.surfaceMapping);
    TEST_ASSERT(innerCoat.integumentMask<.5f&&outerCoat.integumentMask>.99f,
                "La concha interna reduce cobertura sin alterar el dorso auricular");
    Vector3 back=Vec3_Scale(earNormal,-h->earShape.z*.7f);
    Vector3 low=Vec3_Add(back,Vec3_Add(Vec3_Scale(h->earDirection,-h->earShape.y*.42f),Vec3_Scale(side,h->earShape.x*.68f)));
    Vector3 mid=Vec3_Add(back,Vec3_Scale(side,h->earShape.x*.82f));
    Vector3 high=Vec3_Add(back,Vec3_Add(Vec3_Scale(h->earDirection,h->earShape.y*.40f),Vec3_Scale(side,h->earShape.x*1.07f)));
#define EAR_D(p,conc) SDF_CurvedPinna((p),h->earShape,h->earDirection,side,earNormal,h->earTipFraction,(conc),h->earLongitudinalCurve,h->earRootRoll,h->earTipRoundness,h->earFold,h->earRootFlare,h->earMarginBow,h->earMarginAsymmetry)
    TEST_ASSERT(EAR_D(low,h->earConcavity)<0&&EAR_D(mid,h->earConcavity)<0&&EAR_D(high,h->earConcavity)>0,
                "Concha ancha, escafa retenida y tercio distal ahusado no lineal");
    Vector3 inner=Vec3_Scale(earNormal,h->earShape.z*.62f);
    TEST_ASSERT(EAR_D(inner,h->earConcavity)>EAR_D(inner,0)+h->earShape.z*.2f,
                "Abertura interior realmente cóncava");
    Vector3 outer=Vec3_Scale(earNormal,-h->earShape.z*1.5f);
    TEST_ASSERT(EAR_D(outer,h->earConcavity)<EAR_D(Vec3_Add(outer,Vec3_Scale(side,h->earShape.x*.9f)),h->earConcavity),
                "Dorso exterior abombado respecto al margen");
    Vector3 rootBack=Vec3_Add(Vec3_Scale(h->earDirection,-h->earShape.y*.35f),
                              Vec3_Scale(earNormal,-h->earShape.z*1.2f));
    Vector3 distalBack=Vec3_Add(Vec3_Scale(h->earDirection,h->earShape.y*.30f),
                                Vec3_Scale(earNormal,-h->earShape.z*1.2f));
    TEST_ASSERT(EAR_D(rootBack,h->earConcavity)<EAR_D(distalBack,h->earConcavity),
                "Concha basal más gruesa que la escafa distal");
    for(unsigned sample=0;sample<5;++sample) {
        Vector3 relative=Vec3_Add(Vec3_Scale(h->earDirection,((float)sample*.2f-.4f)*h->earShape.y),
                                  Vec3_Scale(side,h->earShape.x*.2f));
        float leftDistance=EAR_D(relative,h->earConcavity);
        Vector3 reflected=relative;reflected.x*=-1;
        float rightDistance=SDF_CurvedPinna(reflected,h->earShape,h->rightEarDirection,
            h->rightEarSide,h->rightEarNormal,h->earTipFraction,h->earConcavity,
            h->earLongitudinalCurve,h->earRootRoll,h->earTipRoundness,h->earFold,
            h->earRootFlare,-h->earMarginBow,1-h->earMarginAsymmetry);
        TEST_ASSERT(fabsf(leftDistance-rightDistance)<1e-5f,
                    "Pabellones neutros reflejan el mismo campo");
    }
    Vector3 tip=Vec3_Add(h->leftEarCenter,Vec3_Add(Vec3_Scale(h->earDirection,h->earShape.y*.35f),
                Vec3_Scale(earNormal,h->earLongitudinalCurve*sinf(3.14159265f*.85f)-h->earShape.z*.55f)));
    TEST_ASSERT(EAR_D(Vec3_Sub(tip,h->leftEarCenter),h->earConcavity)<0,
                "Ápice de radio finito conserva volumen");
#undef EAR_D
    float nx=(tip.x-h->craniumCenter.x)/h->craniumRadii.x;
    float nz=(tip.z-h->craniumCenter.z)/h->craniumRadii.z;
    float roofY=h->craniumCenter.y+h->craniumRadii.y*sqrtf(fmaxf(0.0f,1.0f-nx*nx-nz*nz));
    TEST_ASSERT(tip.y>roofY,"Punta auricular despejada sobre el techo craneal local");
    TEST_ASSERT(fabsf(tip.x-h->leftEarCenter.x)<h->earRadii.x &&
                fabsf(tip.y-h->leftEarCenter.y)<h->earRadii.y &&
                fabsf(tip.z-h->leftEarCenter.z)<h->earRadii.z,
                "Extensión auricular contenida por cotas resueltas");
    SDFMesherConfig cfg=SDFMesher_DefaultConfig();cfg.voxelSize=.045f;
    MonsterVisual visual=MonsterVisual_Create(cfg);
    TEST_ASSERT(MonsterVisual_RebuildNow(&visual,&m,MonsterSDF_DefaultConfig()),"Malla de producción canina");
    TEST_ASSERT(Mesh_Validate(&visual.mesh).valid,"Malla canina con índices, normales y materiales válidos");
    size_t count,largest,second;
    TEST_ASSERT(Mesh_ComponentStatistics(&visual.mesh,&count,&largest,&second) && count==1,"Cuerpo, cabeza, orejas y dedos conectados sin islotes");
    const MonsterSDFMouth* mouth=&visual.sdf.mouths[0];
    SDFDetailRegion regions[MONSTER_SDF_DETAIL_REGION_CAPACITY];
    size_t regionCount=MonsterSDF_GetDetailRegions(&visual.sdf,6,regions,MONSTER_SDF_DETAIL_REGION_CAPACITY);
    bool leftDetail=false,rightDetail=false;
    for(size_t i=0;i<regionCount;++i) if(regions[i].targetVoxelSize<=h->earShape.z*.40f) {
        if(AABB_ContainsPoint(regions[i].bounds,Vec3_Add(m.bodyParts[0].positionRender,h->leftEarCenter)))leftDetail=true;
        if(AABB_ContainsPoint(regions[i].bounds,Vec3_Add(m.bodyParts[0].positionRender,h->rightEarCenter)))rightDetail=true;
    }
    TEST_ASSERT(leftDetail&&rightDetail,"Regiones de detalle incluyen ambas orejas curvas");
    Vector3 worldTip=Vec3_Add(m.bodyParts[0].positionRender,tip);
    TEST_ASSERT(AABB_ContainsPoint(mouth->headBounds,worldTip) && AABB_ContainsPoint(visual.sdf.bounds,worldTip),"Límites incluyen punta auricular");
    TEST_ASSERT(MonsterSDF_EvaluateDistance(&visual.sdf,worldTip)<0,"Punta presente en campo de producción");
    SDFField field=MonsterSDF_GetField(&visual.sdf);
    for (int x=-3;x<=3;++x) for (int y=-3;y<=3;++y) {
        Vector3 center=Vec3_Add(worldTip,Vec3_Create(x*.065f,y*.06f,0));
        Vector3 extent={.025f,.025f,.025f};
        AABB3D box={Vec3_Sub(center,extent),Vec3_Add(center,extent)};
        float lo,hi;
        TEST_ASSERT(field.getCellRange && field.getCellRange(field.context,box,&lo,&hi),"Intervalo auricular disponible");
        for (int corner=0;corner<9;++corner) {
            Vector3 p=corner==8?center:Vec3_Add(center,Vec3_Create(corner&1?extent.x:-extent.x,corner&2?extent.y:-extent.y,corner&4?extent.z:-extent.z));
            float d=field.evaluateDistance(field.context,p);
            TEST_ASSERT(d>=lo-1e-5f && d<=hi+1e-5f,"Intervalo contiene muestras de la pinna");
        }
    }
    /* Los perfiles musculares y metapodiales deben respetar la misma poda y
     * los mismos intervalos conservadores que los conectores lineales. */
    MonsterSDF unpruned=visual.sdf;
    unpruned.config.enableConnectorPruning=false;
    unsigned profiles=0;
    for (size_t i=0;i<visual.sdf.connectorCount;++i) {
        const MonsterSDFConnector* c=&visual.sdf.connectors[i];
        if (!c->widthBulge && !c->heightBulge) continue;
        ++profiles;
        TEST_ASSERT(fabsf(Vec3_Dot(c->side,c->forward))<1e-5f,"Marco muscular ortogonal");
        for (unsigned t=0;t<=8;++t) for (int x=-2;x<=2;++x) for (int y=-2;y<=2;++y) {
            Vector3 center=Vec3_Add(Vec3_Lerp(c->a,c->b,t*.125f),Vec3_Add(
                Vec3_Scale(c->side,x*c->maxRadius*.6f),Vec3_Scale(c->up,y*c->maxRadius*.6f)));
            float expected=MonsterSDF_EvaluateDistance(&unpruned,center);
            TEST_ASSERT(fabsf(MonsterSDF_EvaluateDistance(&visual.sdf,center)-expected)<1e-5f,"Poda conserva perfil muscular");
            Vector3 extent={.025f,.03f,.02f};
            AABB3D box={Vec3_Sub(center,extent),Vec3_Add(center,extent)};
            float lo,hi;
            TEST_ASSERT(field.getCellRange(field.context,box,&lo,&hi),"Intervalo muscular disponible");
            for (unsigned corner=0;corner<8;++corner) {
                Vector3 sample=Vec3_Add(center,Vec3_Create(corner&1?extent.x:-extent.x,
                    corner&2?extent.y:-extent.y,corner&4?extent.z:-extent.z));
                float d=MonsterSDF_EvaluateDistance(&unpruned,sample);
                TEST_ASSERT(d>=lo-1e-5f && d<=hi+1e-5f,"Intervalo muscular contiene campo sin poda");
                if (d<0) TEST_ASSERT(AABB_ContainsPoint(visual.sdf.bounds,sample),"Bounds globales contienen volumen muscular");
            }
        }
    }
    TEST_ASSERT(profiles==12,"Tres perfiles por cada miembro mamífero");
    AnatomyGraph changed=m.anatomyGraph;
    changed.connections[0].widthBulge=.1f;
    TEST_ASSERT(AnatomyGraph_Fingerprint(&changed)!=AnatomyGraph_Fingerprint(&m.anatomyGraph),"Perfil invalida snapshot geométrico");
    changed.connections[0].widthBulge=NAN;
    TEST_ASSERT(!AnatomyGraph_Validate(&changed),"Rechazar perfil muscular no finito");
    MonsterVisual_Free(&visual);Monster_Free(&m);
}
static void CephalocervicalTransitionChecks(void) {
    const CreatureRecipe* recipe=CreatureRecipes_Dog();
    for (unsigned stage=0;stage<4;++stage) {
        const CreaturePhenotype* p=CreatureRecipe_GetStage(recipe,(CreatureStage)stage);
        Monster m=Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(&m,recipe,p),"Construcción para prueba cefalocervical");
        MonsterSDF sdf;
        memset(&sdf,0,sizeof(sdf));
        TEST_ASSERT(MonsterSDF_Build(&sdf,&m,MonsterSDF_DefaultConfig()),"Construcción SDF cefalocervical");
        TEST_ASSERT(sdf.mouthCount>0 && sdf.mouths[0].anatomicalHead,"Cabeza anatómica activa en SDF");

        const MonsterSDFMouth* mouth=&sdf.mouths[0];
        const HeadSurfaceRecipe* headSurf=&m.head.anatomy.surface;
        const AnatomyNode* neckNode=AnatomyGraph_FindFirstRegion(&m.anatomyGraph,ANATOMY_REGION_NECK);
        TEST_ASSERT(neckNode!=NULL,"Nodo cervical presente");

        Vector3 mouthPos=mouth->center;
        Vector3 worldRoot=Vec3_Add(mouthPos,mouth->neckCollarRootLocal);
        Vector3 worldMid=Vec3_Add(mouthPos,mouth->neckCollarMidLocal);
        Vector3 worldTip=Vec3_Add(mouthPos,mouth->neckCollarTipLocal);

        float collarLen=Vec3_Distance(worldRoot,worldTip);
        float skullLen=headSurf->craniumRadii.z*2.0f;
        TEST_ASSERT(collarLen>0.20f*skullLen,"Collar cefalocervical no es degenerado (>20%% longitud craneal)");
        TEST_ASSERT(Vec3_Distance(worldRoot,worldMid)>0.05f*skullLen,"Primer tramo collar con longitud positiva");
        TEST_ASSERT(Vec3_Distance(worldMid,worldTip)>0.05f*skullLen,"Segundo tramo collar con longitud positiva");

        /* Radios estrictamente positivos en las tres estaciones */
        TEST_ASSERT(mouth->neckCollarRootRadii.x>0 && mouth->neckCollarRootRadii.y>0,"Radios positivos en raíz");
        TEST_ASSERT(mouth->neckCollarMidRadii.x>0 && mouth->neckCollarMidRadii.y>0,"Radios positivos en cuello medio");
        TEST_ASSERT(mouth->neckCollarTipRadii.x>0 && mouth->neckCollarTipRadii.y>0,"Radios positivos en extremo cervical");

        /* Cintura lateral en cuello medio */
        TEST_ASSERT(mouth->neckCollarMidRadii.x<=mouth->neckCollarRootRadii.x&&
                    mouth->neckCollarMidRadii.x<=mouth->neckCollarTipRadii.x,
                    "Sección intermedia estrecha transversalmente (cintura cervical)");

        /* Inserción y pendiente caudoventral: hacia atrás y hacia abajo */
        TEST_ASSERT(worldRoot.z>worldMid.z && worldMid.z>worldTip.z,"Orden monótono sagital estricto (Z decreciente)");
        TEST_ASSERT(worldRoot.y>=worldMid.y && worldMid.y>worldTip.y,"Pendiente caudoventral estricta (Y decreciente)");

        /* La raíz debe solapar el interior del cráneo posterior */
        Vector3 rootRelSkull=Vec3_Sub(mouth->neckCollarRootLocal,headSurf->craniumCenter);
        float skullDistSq=(rootRelSkull.x*rootRelSkull.x)/(headSurf->craniumRadii.x*headSurf->craniumRadii.x)+
                          (rootRelSkull.y*rootRelSkull.y)/(headSurf->craniumRadii.y*headSurf->craniumRadii.y)+
                          (rootRelSkull.z*rootRelSkull.z)/(headSurf->craniumRadii.z*headSurf->craniumRadii.z);
        TEST_ASSERT(skullDistSq<1.0f,"Raíz del collar está dentro del elipsoide craneal");

        /* La punta debe estar embebida dentro de la región cervical del cuello */
        float tipDistNeck=Vec3_Distance(worldTip,neckNode->center);
        TEST_ASSERT(tipDistNeck<neckNode->heightRadius*1.35f,"Punta del collar dentro del radio cervical");

        /* Continuidad del campo: SDF sólido o casi sólido en las estaciones */
        TEST_ASSERT(MonsterSDF_EvaluateDistance(&sdf,worldRoot)<0.02f,"Superficie sólida en raíz de collar");
        TEST_ASSERT(MonsterSDF_EvaluateDistance(&sdf,worldMid)<0.02f,"Superficie sólida en cuello medio");
        TEST_ASSERT(MonsterSDF_EvaluateDistance(&sdf,worldTip)<0.02f,"Superficie sólida en punta cervical");

        MonsterSDF_Free(&sdf);
        Monster_Free(&m);
    }
}
static void CervicalArchitectureChecks(void) {
    const CreatureRecipe* recipe = CreatureRecipes_Dog();
    for (unsigned stage = 0; stage < 4; ++stage) {
        const CreaturePhenotype* p = CreatureRecipe_GetStage(recipe, (CreatureStage)stage);
        Monster m = Monster_Create();
        TEST_ASSERT(Creature_BuildMonster(&m, recipe, p), "Construcción para prueba cervical");
        MonsterSDF sdf;
        memset(&sdf, 0, sizeof(sdf));
        TEST_ASSERT(MonsterSDF_Build(&sdf, &m, MonsterSDF_DefaultConfig()), "Construcción SDF arquitectura cervical");

        /* 1. Mínimo 9 estaciones de tronco resueltas para upright tetrapod */
        TEST_ASSERT(sdf.axialStationCount >= 9, "Al menos 9 estaciones de barrido axial generadas");

        /* 2. Orden monótono sagital estricto (Z estrictamente decreciente) */
        for (int i = 1; i < sdf.axialStationCount; ++i) {
            TEST_ASSERT(sdf.axialStations[i].center.z < sdf.axialStations[i - 1].center.z,
                        "Orden Z estrictamente decreciente en estaciones axiales");
        }

        /* 3. Comportamiento dorsal monótono y sin protuberancia en el cuello */
        /* Estaciones: 0=C0, 1=C1, 2=C2, 3=W, 4=Post-W, 5=ThoraxAnt */
        float dorsal0 = sdf.axialStations[0].center.y + sdf.axialStations[0].height;
        float dorsal1 = sdf.axialStations[1].center.y + sdf.axialStations[1].height;
        float dorsal2 = sdf.axialStations[2].center.y + sdf.axialStations[2].height;
        float dorsal3 = sdf.axialStations[3].center.y + sdf.axialStations[3].height;
        float dorsal4 = sdf.axialStations[4].center.y + sdf.axialStations[4].height;
        float dorsal5 = sdf.axialStations[5].center.y + sdf.axialStations[5].height;

        TEST_ASSERT(dorsal0 >= dorsal1 - 0.001f, "Dorso no asciende de C0 a C1 (sin joroba)");
        TEST_ASSERT(dorsal1 >= dorsal2 - 0.001f, "Dorso no asciende de C1 a C2");
        TEST_ASSERT(dorsal2 >= dorsal3 - 0.001f, "Dorso no asciende de C2 a cruz");
        TEST_ASSERT(dorsal3 >= dorsal4 - 0.001f, "Dorso desciende suavemente de cruz a post-cruz");
        TEST_ASSERT(dorsal4 >= dorsal5 - 0.001f, "Dorso desciende suavemente hacia tórax anterior");

        /* 4. Expansión continua de nuca a cruz, sin bola seguida de cintura. */
        float w0 = sdf.axialStations[0].width;
        float w1 = sdf.axialStations[1].width;
        float w2 = sdf.axialStations[2].width;
        float w3 = sdf.axialStations[3].width;
        TEST_ASSERT(w0 < w1, "La nuca se ensancha hacia el cuello medio sin cintura artificial");
        TEST_ASSERT(w1 < w2, "C1 más estrecho que C2");
        TEST_ASSERT(w2 < w3, "C2 se ensancha hacia la cintura escapular / cruz");

        const AnatomyNode* neck=AnatomyGraph_FindFirstRegion(&m.anatomyGraph,ANATOMY_REGION_NECK);
        TEST_ASSERT(Vec3_Distance(neck->center,sdf.axialStations[0].center)<1e-6f &&
                    FLOAT_NEAR(neck->widthRadius,w0) && FLOAT_NEAR(neck->heightRadius,sdf.axialStations[0].height),
                    "El barrido conserva la sección cervical resuelta en el grafo");
        TEST_ASSERT(sdf.axialStations[0].height<m.head.anatomy.surface.craniumRadii.y,
                    "Inserción nucal contenida bajo el perfil craneal");

        /* Muestrear la superficie interpolada, no solo sus estaciones:
         * Hermite puede introducir una joroba aunque sus extremos sean válidos. */
        float previousTop=dorsal0,previousBottom=sdf.axialStations[0].center.y-sdf.axialStations[0].height,previousWidth=w0;
        float scale=p->axial.totalScale;
        for (unsigned sample=1;sample<=60;++sample) {
            float t=(float)sample/60.0f;
            float z=sdf.axialStations[0].center.z*(1-t)+sdf.axialStations[3].center.z*t;
            int section=sample==60?2:(int)(t*3);
            const SDFSweepStation* a=&sdf.axialStations[section];
            const SDFSweepStation* b=&sdf.axialStations[section+1];
            float u=(z-a->center.z)/(b->center.z-a->center.z);
            float insideY=a->center.y*(1-u)+b->center.y*u;
            TEST_ASSERT(SDF_EllipticalSweepZ((Vector3){0,insideY,z},sdf.axialStations,sdf.axialStationCount)<0,
                        "Eje cervical dentro de la sección interpolada");
            float limits[2];
            for (unsigned side=0;side<2;++side) {
                float lo=0,hi=3*scale;
                for (unsigned iteration=0;iteration<22;++iteration) {
                    float distance=(lo+hi)*.5f;
                    Vector3 point={0,insideY+(side?1:-1)*distance,z};
                    if (SDF_EllipticalSweepZ(point,sdf.axialStations,sdf.axialStationCount)<0) lo=distance;
                    else hi=distance;
                }
                limits[side]=insideY+(side?1:-1)*(lo+hi)*.5f;
            }
            float centerY=(limits[0]+limits[1])*.5f;
            float lo=0,hi=2*scale;
            for (unsigned iteration=0;iteration<22;++iteration) {
                float x=(lo+hi)*.5f;
                if (SDF_EllipticalSweepZ((Vector3){x,centerY,z},sdf.axialStations,sdf.axialStationCount)<0) lo=x;
                else hi=x;
            }
            float width=(lo+hi)*.5f,tolerance=scale*1e-4f;
            TEST_ASSERT(limits[1]<=previousTop+tolerance,"Superficie dorsal continua sin joroba interpolada");
            TEST_ASSERT(limits[0]<=previousBottom+tolerance,"Garganta continua hacia el pecho");
            TEST_ASSERT(width>=previousWidth-tolerance,"Superficie cervical sin cintura tras una bola");
            previousTop=limits[1];previousBottom=limits[0];previousWidth=width;
        }

        /* 5. Elevación de la cruz (withers) sobre el plano torácico */
        TEST_ASSERT(dorsal3 > dorsal5, "Cruz escapular elevada sobre el nivel dorsal torácico");
        float withersElevation = dorsal3 - dorsal5;
        float expectedMinElev = 0.03f * p->axial.withersHeight * p->axial.totalScale;
        TEST_ASSERT(withersElevation >= expectedMinElev, "Elevación de la cruz apreciable sobre el dorso horizontal");

        /* 6. Descenso ventral gradual (línea de garganta -> pecho sin caída abrupta) */
        float ventral0 = sdf.axialStations[0].center.y - sdf.axialStations[0].height;
        float ventral1 = sdf.axialStations[1].center.y - sdf.axialStations[1].height;
        float ventral2 = sdf.axialStations[2].center.y - sdf.axialStations[2].height;
        float ventral3 = sdf.axialStations[3].center.y - sdf.axialStations[3].height;
        float ventral5 = sdf.axialStations[5].center.y - sdf.axialStations[5].height;

        TEST_ASSERT(ventral0 > ventral1 && ventral1 > ventral2 && ventral2 > ventral3 && ventral3 >= ventral5 - 0.001f,
                    "Línea ventral desciende monótonamente hacia la quilla torácica");

        /* Pendiente máxima entre C0 y C2 no excede caída vertical */
        float dZ_cervical = sdf.axialStations[0].center.z - sdf.axialStations[2].center.z;
        float dY_ventral = ventral0 - ventral2;
        TEST_ASSERT(dZ_cervical > 0.15f * p->axial.neckLength * p->axial.totalScale, "Tramo cervical con longitud sagital");
        TEST_ASSERT(dY_ventral / dZ_cervical < 2.5f, "Pendiente ventral moderada (no caída abrupta)");

        MonsterSDF_Free(&sdf);
        Monster_Free(&m);
    }
}
static void CervicalProfileOwnershipChecks(void) {
    CreaturePhenotype p=CreatureRecipes_Dog()->adult;
    p.axial.profile[0].widthScale=1.12f;
    p.axial.profile[0].heightScale=.90f;
    p.axial.profile[0].verticalOffset=.07f;
    p.axial.profile[0].longitudinalOffset=.10f;
    AnatomyGraph graph;
    TEST_ASSERT(Creature_ResolveAnatomy(CreatureRecipes_Dog(),&p,&graph),"Anatomía con modificadores cervicales");
    const AnatomyNode* neck=AnatomyGraph_FindFirstRegion(&graph,ANATOMY_REGION_NECK);
    SDFSweepStation stations[9];int count=0;
    TEST_ASSERT(AxialBodyUprightTetrapod_BuildSweepStations(&p.axial,&graph,neck->moduleInstanceId,
                stations,9,&count),"Barrido con modificadores cervicales");
    TEST_ASSERT(Vec3_Distance(stations[0].center,neck->center)<1e-6f &&
                FLOAT_NEAR(stations[0].width,neck->widthRadius) && FLOAT_NEAR(stations[0].height,neck->heightRadius),
                "El barrido no reconstruye una segunda anatomía que ignore el fenotipo resuelto");
}

void run_dog_tests(void) {
    AnatomyChecks();EyeAnchorChecks();EarAndMeshChecks();CephalocervicalTransitionChecks();CervicalArchitectureChecks();CervicalProfileOwnershipChecks();
    printf("[PASS] test_dog: receta, etapas, rig, suelas, proporciones, pinnae, collar cefalocervical, arquitectura cervical y malla conexa\n");
}
