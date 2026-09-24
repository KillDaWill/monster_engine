# Monster Engine — Procedural Creature Architecture Reference

## Purpose

This document captures the architectural conclusions reached after reviewing the current Monster Engine and comparing it with the procedural creature work described by Rune Skovbo Johansen / RuneVision, especially:

- Procedural Creature Progress 2021–2024  
  https://blog.runevision.com/2025/01/procedural-creature-progress-2021-2024.html
- RuneVision Locomotion System / thesis material  
  https://runevision.com/thesis/
- SMAL: Skinned Multi-Animal Linear Model  
  https://smal.is.tue.mpg.de/

The goal is to turn the current monster generator into a **general procedural creature engine** capable of producing anatomically coherent animals and fantasy creatures, then rigging and animating them procedurally.

The immediate desired progression is:

1. Anatomically correct procedural heads.
2. Reusable animal head presets such as lizard, wolf/canid and bird.
3. A general anatomical graph for complete creatures.
4. Procedural arms, legs, wings and tails.
5. Automatic rigging / skinning.
6. Procedural locomotion and animation.

---

# 1. Current Engine Direction

The current engine has already made an important architectural improvement: it is moving away from rendering disconnected primitive parts and toward building a continuous creature surface from **Signed Distance Fields (SDFs)**.

The current high-level pipeline is approximately:

```text
Monster
  |
  +-- BodyPart[]
  +-- Eye[]
  +-- Mouth / head-related parameters
  |
  v
MonsterSDF
  |
  +-- ellipsoidal volumes
  +-- tapered connectors
  +-- facial volumes
  +-- subtractive oral cavity
  |
  v
Marching Cubes
  |
  v
Mesh
```

The mouth/head work is particularly important because it already establishes a useful principle:

```text
static head/body geometry
        +
subtractive oral cavity
        +
articulated lower jaw
        +
hinge / seam tissue
```

This is preferable to the old approach of representing the mouth as a flat ellipse or decorative object.

The engine should preserve this distinction between:

- **shape/rest geometry**, and
- **runtime pose/articulation**.

The current SDF foundation, CSG operations, Marching Cubes pipeline, material handling, asynchronous meshing, shape fingerprinting and existing validation tests should be treated as assets rather than discarded.

---

# 2. Main Architectural Limitation

The largest limitation is not the SDF representation.

The limitation is that the engine still describes anatomy too close to the final geometry.

A simplified `BodyPart` currently behaves conceptually like:

```c
typedef struct BodyPart {
    Vector3 position;
    float width;
    float height;
    float length;
    ...
} BodyPart;
```

The SDF builder then creates volumes for these parts and connects sequential items.

This works for a mostly linear chain:

```text
HEAD
 |
CHEST
 |
ABDOMEN
 |
TAIL_0
 |
TAIL_1
```

but it does not scale to branching anatomy:

```text
                    HEAD
                      |
                     NECK
                      |
                    CHEST
                  /       \
         SHOULDER_L       SHOULDER_R
             |                 |
          ELBOW_L           ELBOW_R
             |                 |
           WRIST_L           WRIST_R
                      |
                    SPINE
                  /       \
               HIP_L       HIP_R
                 |           |
               KNEE        KNEE
                 |           |
              ANKLE       ANKLE
                      |
                     TAIL
```

A linear array in which array order implies anatomical connectivity becomes invalid as soon as limbs branch away from the torso.

## Core conclusion

**Creature anatomy must no longer be represented implicitly by the order of `BodyPart[]`.**

Instead, the engine needs an explicit anatomical hierarchy / graph.

---

# 3. Most Important Lesson from RuneVision

RuneVision's project is valuable primarily because of its representation choices.

The early system used hundreds of low-level parameters describing things such as:

- bone lengths,
- positions,
- rotations,
- local thickness,
- local orientation.

This could represent and interpolate animals, but the parameters were not meaningful from a creature-design point of view.

A designer wants to control concepts such as:

- body bulk,
- animal height,
- head size,
- muzzle length,
- tail thickness,
- ear pointiness,
- eye size,
- eye laterality,
- limb length.

The key lesson is:

> Do not expose low-level geometric coordinates as the primary creature phenotype.

Instead:

```text
high-level semantic parameters
          |
          v
anatomical constraints / resolver
          |
          v
valid low-level anatomy
          |
          v
geometry
```

The Monster Engine should adopt this principle directly.

---

# 4. Separate Genome, Phenotype, Anatomy and Geometry

The engine should progressively move toward four clearly separated layers.

```text
CreatureGenome
      |
      v
Phenotype Resolver
      |
      v
CreaturePhenotype
      |
      v
Anatomy Resolver
      |
      v
CreatureAnatomy / AnatomyGraph
      |
      v
Geometry Compiler
      |
      v
SDF -> Marching Cubes -> Mesh
```

## 4.1 CreatureGenome

The genome should contain abstract normalized traits.

Example:

```c
typedef struct CreatureGenome {
    float bodySize;
    float bulkiness;

    float headSize;
    float muzzleLength;
    float skullWidth;

    float eyeSize;
    float eyeLaterality;

    float forelimbLength;
    float hindlimbLength;

    float tailLength;
    float tailThickness;

    ...
} CreatureGenome;
```

These values should usually be normalized or otherwise bounded.

The genome should **not** directly contain arbitrary world-space coordinates for every anatomical feature.

---

## 4.2 CreaturePhenotype

The phenotype converts the genome into meaningful biological / morphological properties.

Example:

```c
typedef struct HeadPhenotype {
    float skullWidth;
    float skullHeight;
    float skullLength;

    float muzzleLength;
    float muzzleWidth;
    float muzzleTaper;

    float eyeSize;
    float eyeLaterality;

    float jawStrength;
    float jawLength;

    float noseScale;
    float earScale;

    ...
} HeadPhenotype;
```

A phenotype may also contain traits generated from environmental, age or species modifiers.

---

## 4.3 CreatureAnatomy

The anatomy layer should contain resolved landmarks, joints and relationships.

For a head:

```c
typedef struct HeadLandmarks {
    Vector3 neckAttach;

    Vector3 skullCenter;

    Vector3 orbitLeft;
    Vector3 orbitRight;

    Vector3 jawHingeLeft;
    Vector3 jawHingeRight;

    Vector3 muzzleRoot;
    Vector3 muzzleTip;

    Vector3 noseTip;

    Vector3 earBaseLeft;
    Vector3 earBaseRight;
} HeadLandmarks;
```

For the complete creature, the anatomy should become an explicit graph.

---

## 4.4 Geometry

The SDF layer should receive resolved anatomy and generate surfaces.

It should not need to know whether the creature is "a wolf" or "a lizard".

It should instead understand concepts such as:

```text
volume attached to anatomical node X
connector between anatomical nodes A and B
subtractive cavity attached to node H
surface recipe associated with a limb segment
```

This makes the geometry compiler reusable.

---

# 5. Introduce Stable Anatomy IDs

The engine needs stable anatomical identifiers before arbitrary reordering or branching is supported.

Conceptually:

```c
typedef uint32_t AnatomyId;

typedef enum AnatomyRole {
    ANATOMY_ROOT,

    ANATOMY_PELVIS,
    ANATOMY_SPINE,
    ANATOMY_NECK,
    ANATOMY_HEAD,

    ANATOMY_SHOULDER,
    ANATOMY_ELBOW,
    ANATOMY_WRIST,

    ANATOMY_HIP,
    ANATOMY_KNEE,
    ANATOMY_ANKLE,

    ANATOMY_FOOT,

    ANATOMY_TAIL
} AnatomyRole;
```

A node could resemble:

```c
typedef struct AnatomyNode {
    AnatomyId id;
    AnatomyId parent;

    AnatomyRole role;

    Transform3D restTransform;

    float length;
    Vector3 thickness;

    JointConstraint constraint;
} AnatomyNode;
```

The important property is:

```text
connectivity is explicit
```

rather than:

```text
BodyPart[i] connects to BodyPart[i + 1]
```

---

# 6. BodyPart Should Become Derived Surface Data

`BodyPart` does not necessarily need to disappear immediately.

However, it should stop being the authoritative description of anatomy.

A better interpretation is:

> A BodyPart / SDF body volume is a geometric volume generated from anatomy.

Example:

```text
ANATOMY

shoulder
   |
humerus
   |
elbow
   |
forearm

        |
        v

SURFACE RECIPE

shoulder mass
+
upper-arm elliptical tapered capsule
+
elbow transition volume
+
forearm elliptical tapered capsule
```

This distinction is fundamental:

- bones / nodes define the **structure**,
- SDF volumes define the **surface around that structure**.

---

# 7. Refactor the Head Before Building Limbs

The head should be the next architectural milestone.

The current mouth system has already accumulated responsibilities that belong to the complete head.

Conceptually it has evolved toward:

```text
Mouth
  +-- oral cavity
  +-- jaw
  +-- throat
  +-- cranium
  +-- snout
  +-- cheeks
  +-- brows
```

This was a reasonable transitional implementation, but it should now be separated.

Target structure:

```text
Head
  |
  +-- Cranium
  +-- Face
  +-- Orbits
  +-- Nose / Nares
  +-- Ears
  |
  +-- OralSystem
        |
        +-- Mouth
        +-- Jaw
        +-- Tongue
        +-- Teeth
        +-- Gums
        +-- Throat
```

The exact C module split may differ, but ownership should follow this conceptual model.

---

# 8. Head Architecture

Recommended head pipeline:

```text
HeadArchetype
      +
HeadPhenotype
      |
      v
HeadAnatomyResolver
      |
      v
HeadLandmarks
      |
      v
HeadSurfaceRecipe
      |
      v
SDF
      |
      v
Marching Cubes
```

This allows all animal heads to share the same pipeline while using different archetypes and constraints.

---

# 9. Head Presets

Do not build presets as large sets of arbitrary coordinates.

Prefer:

```text
preset
  =
archetype
  +
semantic parameter defaults
  +
valid parameter ranges
  +
anatomical constraints
```

---

## 9.1 Lizard Head

Important structural tendencies:

- relatively low / flattened skull,
- long jaw,
- broad or tapered rostrum depending on species,
- relatively lateral eyes,
- mouth corners extending posteriorly,
- nostrils near the anterior part of the snout,
- little or no mammalian lip mass,
- strong relationship between jaw and skull length.

Useful traits:

```text
skullFlattening
rostrumLength
rostrumWidth
rostrumTaper
eyeLaterality
jawLength
jawDepth
nostrilOffset
```

Useful SDF additions may include:

- rounded wedge,
- tapered rounded box,
- elliptical tapered capsule.

---

## 9.2 Wolf / Canid Head

Important tendencies:

- broad braincase,
- substantial cheek / zygomatic region,
- narrower and long muzzle,
- visible nasal pad,
- strong jaw musculature,
- more forward-facing eyes than a lizard,
- ears placed dorsally and posteriorly.

Useful traits:

```text
braincaseWidth
muzzleLength
muzzleWidth
muzzleTaper
cheekMass
jawStrength
noseScale
eyeForwardness
earSize
earPointiness
```

---

## 9.3 Bird Head

Bird heads should not be forced through a mammalian jaw implementation.

Conceptually:

```text
cranium
+
orbital region
+
upper beak
+
lower beak
```

The lower beak should articulate through its own joint model.

Useful traits:

```text
beakLength
beakDepth
beakCurvature
beakTaper
orbitalScale
eyeScale
naresPosition
lowerBeakDepth
```

The oral system therefore needs multiple anatomical archetypes.

---

# 10. Eye Sockets Instead of Eyes Glued to the Surface

The eye currently behaves approximately like a separate ellipsoid.

Keeping the eyeball as a separate articulated object is useful because eye rotation is needed.

However, the head mesh should contain an orbital structure.

Recommended construction:

```text
HEAD SDF
   -
ORBIT CUTTER
   +
ORBIT / BROW RIM
```

Then place the eye globe inside the socket.

This should improve facial anatomy significantly because the eye will appear embedded in the skull rather than attached to its exterior.

Potential later additions:

- eyelid rim,
- upper lid,
- lower lid,
- local skin fold,
- brow mass.

## Implementación actual: ojos, párpados y pinna

El globo ocular se conserva como una malla elipsoidal articulable. El iris, la pupila y el anillo limbar forman parte de `EyeAppearance` y de una textura RGBA determinista generada por `EyeTexture`; no añaden vértices a `MeshVertex` ni geometría superpuesta. `MonsterVisualEye` publica la base anatómica y una huella de apariencia. Esa huella permite que el renderer OpenGL comparta texturas iguales y las conserve en su caché mientras cambian la pose o el resto de la superficie. Una edición exclusivamente óptica no invalida el SDF.

El renderer dispone de una ruta opcional `renderEye`. El shader calcula coordenadas esféricas desde el centro, la orientación y la escala del globo, y añade respuesta especular y de Fresnel. Los renderers que no implementen esa ruta pueden seguir dibujando el globo mediante la interfaz normal de malla.

La receta de cabeza expresa la cobertura palpebral. Para el cánido, `MonsterSDF` une arcos superior e inferior segmentados mediante cápsulas con el campo periorbitario; los arcos convergen en los cantos y estrechan la abertura sin tubos flotantes. La cobertura cero mantiene intactos los reptiles que no usan párpados mamíferos.

`SDF_CurvedPinna` amplía la pinna prismática: interpola una línea central curva, anchura y grosor desde la raíz hasta el ápice, una cavidad cóncava, un borde engrosado y un enrollamiento de concha en la base. Los parámetros de receta permiten plegar y dejar caer la pinna sin introducir mallas por especie.

`cephalicIndex` correlaciona longitud y anchura craneales en el continuo dolicocefálico–mesocefálico–braquicefálico. Los controles de stop, raíz maxilar y masa cigomática añaden correlaciones faciales para los cánidos; las recetas existentes conservan sus proporciones al no especificar el nuevo valor.

---

# 11. Nostrils Should Be Real Cavities

Do not represent nostrils as black spheres or decals.

Use subtractive SDF cavities.

Examples:

### Lizard

```text
snout
  -
two small dorsal/lateral nostril cutters
```

### Wolf

```text
nasal pad volume
  -
left nostril
  -
right nostril
```

### Bird

```text
upper beak
  -
left nare
  -
right nare
```

This is fully compatible with the existing CSG design.

---

# 12. Semantic Constraints Are Essential

A universal creature system must prevent invalid parameter combinations.

Do not allow every physical dimension to vary independently.

Example:

Bad:

```c
tailRadius = random_absolute_value();
```

Better:

```c
tailThickness = random01();

tailRadius = lerp(
    minimumValidTailRadius,
    torsoRadius,
    tailThickness
);
```

Similarly:

```text
jaw hinge must remain behind the mouth corner
nose must remain in front of the orbits
orbits must remain inside the skull envelope
nostrils must remain inside the snout/beak
left/right homologous landmarks should preserve symmetry unless asymmetry is intentional
neck attachment must remain behind or beneath the skull
jaw must not intersect the cranium in its valid pose range
```

The generator should try to ensure:

> Any legal parameter combination produces a legal creature.

---

# 13. Randomize Meaning, Not Coordinates

Do not generate anatomy with code equivalent to:

```c
eye.offset.x = random();
jawPivot.y = random();
snout.position.z = random();
```

Generate semantic traits:

```c
phenotype.muzzleLength  = random01();
phenotype.eyeLaterality = random01();
phenotype.jawStrength   = random01();
phenotype.skullWidth    = random01();
```

Then derive all low-level positions through the anatomy resolver.

This greatly reduces broken creatures and makes mutations / evolutionary systems more meaningful.

---

# 14. Anatomical Fuzz Testing

The engine should eventually add large randomized phenotype sweeps.

Example:

```c
for (int i = 0; i < 10000; ++i) {
    HeadPhenotype p = HeadPhenotype_Random(seed + i);
    HeadAnatomy a = HeadResolver_Resolve(&p);

    ASSERT(HeadAnatomy_Validate(&a));
    ASSERT(HeadSDF_Build(&a));
    ASSERT(Mesh_Validate(...));
}
```

Validation should test more than NaNs.

Examples:

```text
jaw hinge behind mouth corner
nose in front of eyes
orbits inside skull
left/right symmetry
mouth cavity remains enclosed correctly
jaw does not penetrate skull through its valid pose range
nostrils remain inside nose/snout/beak volume
neck connection remains valid
generated mesh remains watertight enough for the engine's topology requirements
```

The current sweep and topology tests are a good foundation for this philosophy.

---

# 15. Reference Fitting as an Offline Tool

RuneVision's silhouette-fitting work suggests a useful future tool.

Potential pipeline:

```text
reference animal model / images
          |
          v
reference silhouettes / 2D distance fields
          |
          v
procedural creature rendered silhouettes
          |
          v
error function
          |
          v
parameter optimization
          |
          v
species preset
```

This should be an offline authoring / research tool, not a runtime dependency.

Example library:

```text
references/
  heads/
    iguana.obj
    monitor_lizard.obj
    crocodilian.obj

    wolf.obj
    fox.obj
    shepherd.obj

    raven.obj
    eagle.obj
    duck.obj
```

The purpose is not to copy meshes directly.

The purpose is to discover parameter relationships and create better constrained presets.

---

# 16. Axial Skeleton Before Limbs

After the head system is stable, implement the central body skeleton.

Recommended first graph:

```text
HEAD
 |
NECK
 |
SPINE_0
 |
SPINE_1
 |
SPINE_2
 |
PELVIS
 |
TAIL_0
 |
TAIL_1
 |
TAIL_2
 ...
```

This establishes:

- root motion,
- torso orientation,
- head attachment,
- pelvis,
- tail,
- limb attachment points.

---

# 17. Procedural Tail

The current segmented tail can evolve into:

```c
typedef struct TailPhenotype {
    float length;
    float baseThickness;
    float tipThickness;
    float stiffness;
    float curvature;
    int vertebraCount;
} TailPhenotype;
```

The resolver then generates a variable number of tail nodes / vertebrae.

Surface generation can use tapered elliptical segments blended together.

Later, animation can use the same graph for:

- follow-through,
- balance,
- steering,
- expressive motion,
- secondary dynamics.

---

# 18. Limb Anatomy

Use a common anatomical hierarchy.

## Forelimb

```text
shoulder
   |
humerus
   |
elbow
   |
radius / ulna segment
   |
wrist
   |
metacarpal region
   |
digits
```

## Hindlimb

```text
hip
 |
femur
 |
knee
 |
tibia segment
 |
ankle / hock
 |
metatarsal region
 |
digits
```

This is preferable to simply creating:

```text
upper tube
+
lower tube
```

because different species derive much of their appearance from distal limb proportions.

---

# 19. Limb Posture Must Be Parameterized

The same general limb hierarchy can support very different animals.

Useful categories:

```c
typedef enum LimbPostureType {
    LIMB_POSTURE_SPRAWLING,
    LIMB_POSTURE_PLANTIGRADE,
    LIMB_POSTURE_DIGITIGRADE,
    LIMB_POSTURE_UNGULIGRADE,
    LIMB_POSTURE_AVIAN
} LimbPostureType;
```

A useful continuous parameter is:

```c
float stanceSprawl;
```

Conceptually:

```text
0.0 -> limbs mostly below the body
1.0 -> strongly sprawling lateral posture
```

This makes a lizard fundamentally different from a wolf without requiring a completely different engine.

---

# 20. Wings Should Be Specialized Forelimbs

Avoid making wings unrelated appendages at the skeletal level.

A bird wing still follows a forelimb hierarchy:

```text
shoulder
 |
humerus
 |
elbow
 |
radius/ulna
 |
wrist
 |
distal wing structure
```

What differs is:

- proportions,
- joint constraints,
- surface recipe,
- feather attachment,
- animation.

Therefore:

```text
same anatomical graph concept
+
different limb archetype
+
different surface generator
```

is preferable to a completely separate `Wing` architecture.

---

# 21. Separate Anatomy, Posture and Locomotion

Do not hard-code species behavior throughout the engine.

Prefer composing a creature from reusable systems.

Conceptually:

```text
HeadArchetype
LimbArchetype
LimbPosture
AxialBodyArchetype
TailArchetype
LocomotionProfile
SurfaceProfile
```

Example wolf:

```text
head          = CANID
forelimbs     = MAMMAL_FORELIMB
hindlimbs     = MAMMAL_HINDLIMB
posture       = DIGITIGRADE
locomotion    = QUADRUPED
tail          = FLEXIBLE_MAMMAL
```

Example lizard:

```text
head          = LIZARD
forelimbs     = TETRAPOD_FORELIMB
hindlimbs     = TETRAPOD_HINDLIMB
posture       = SPRAWLING
locomotion    = QUADRUPED
tail          = LONG_TAPERED
```

Example raven:

```text
head          = AVIAN
forelimbs     = WING
hindlimbs     = AVIAN_HINDLIMB
posture       = AVIAN
locomotion    = BIPED / FLIGHT
tail          = FEATHERED
```

---

# 22. Universal Does Not Mean One Continuous Animal Formula

The target should not be a single mathematical shape space that freely interpolates all animals.

The target should be:

> A common anatomical language capable of describing different valid topologies.

A wolf and bird can share many anatomical concepts while still requiring different surface recipes and topology-level features.

The engine should therefore support:

- shared abstractions,
- different archetypes,
- constrained parameter spaces,
- modular specialization.

---

# 23. Grounding Must Move from Body Parts to Feet

A fully articulated body cannot have each body section independently sampling terrain height.

The desired model is:

```text
Creature Root
     |
     v
Body / spine pose
     |
     v
Leg targets
     |
     v
Footstep planner
     |
     v
terrain raycasts
     |
     v
IK
```

Only contact effectors such as feet should directly solve against the ground.

The torso should be positioned from:

- root motion,
- gait state,
- support polygon / stance,
- spine controller,
- leg constraints.

This matches the general procedural locomotion direction described by RuneVision.

---

# 24. Procedural Locomotion Architecture

Initial locomotion should prioritize:

- FK,
- analytical or constrained IK,
- terrain raycasts,
- gait logic,

rather than full-body physics.

Recommended pipeline:

```text
desired velocity
      |
      v
Gait Controller
      |
      v
Footstep Planner
      |
      v
Foot Targets
      |
      v
IK Solver
      |
      v
Skeleton Pose
      |
      v
Skinning
```

Possible later layers:

```text
spine flex
head stabilization
tail balance
look-at
breathing
secondary motion
procedural idle behavior
```

---

# 25. Gait Parameters Should Be Relative

Avoid fixed world-space gait values where possible.

Prefer normalized relationships:

```text
strideLength / legLength
stepHeight / legLength
contactDistance / legLength
bodyBob / bodyHeight
```

Example:

```c
typedef struct GaitProfile {
    float phaseOffsets[MAX_LIMBS];

    float strideLengthRatio;
    float contactLengthRatio;
    float stepHeightRatio;

    float dutyFactor;

    float bodyBobRatio;
    float spineFlex;
} GaitProfile;
```

Potential profiles:

```text
wolf_walk
wolf_trot
wolf_gallop

lizard_walk

bird_walk
bird_run
```

The locomotion solver can remain common while profiles change.

---

# 26. IK Must Preserve Pose Continuity

A procedural IK solver can have multiple mathematically valid solutions.

Without temporal continuity, knees or elbows may suddenly flip between solutions.

The solver should consider:

- joint limits,
- preferred bend direction,
- previous pose,
- nearest valid solution to previous pose.

Conceptually:

```c
IKSolution_SelectClosestToPreviousPose(...);
```

This will be important for:

- elbows,
- knees,
- wrists,
- hocks,
- wing joints.

---

# 27. Do Not Re-Mesh the Whole Creature Every Animation Frame

The current pipeline is approximately:

```text
SDF
 |
v
Marching Cubes
 |
v
Mesh
```

This is excellent for generating the rest shape.

It is not appropriate to rebuild the entire creature SDF at 60 FPS for normal skeletal animation.

The long-term animation pipeline should be:

```text
REST SHAPE GENERATION

AnatomyGraph
     |
     v
SDF
     |
     v
Marching Cubes
     |
     v
Rest Mesh + Skin Weights


RUNTIME ANIMATION

Skeleton Pose
     |
     v
Skinning
     |
     v
Rendered Mesh
```

---

# 28. Automatic Skin Weights from Procedural Anatomy

Because the geometry is generated from known anatomical segments, skinning weights can also be generated procedurally.

Each SDF contribution can retain information about its source bone / anatomy node.

Example:

```text
upper arm field -> HUMERUS
forearm field   -> FOREARM
elbow blend     -> HUMERUS + FOREARM
```

During or after mesh extraction, each vertex can receive weights.

Example:

```text
vertex:
  humerus = 0.78
  forearm = 0.22
```

This creates a path toward:

> fully automatic rig + skin generation from the same creature description.

A useful target pipeline is:

```text
CreatureGenome
      |
      v
CreaturePhenotype
      |
      v
AnatomyGraph
      |
      v
SDF
      |
      v
Mesh + Rig + SkinWeights
      |
      v
Procedural Animation
```

---

# 29. Existing Systems to Preserve

The following concepts should be preserved and generalized rather than replaced:

## SDF primitives

Continue expanding the library.

Potential additions:

```text
elliptical tapered capsule
rounded wedge
tapered rounded box
oriented cavity cutters
flattened ellipsoid variants
```

## CSG

Keep and extend:

```text
union
smooth union
subtract
smooth subtract
intersection where useful
```

## Marching Cubes

Keep the current stable meshing path.

## Materials

They will become increasingly important for semantic tissues:

```text
skin
mouth interior
teeth
claws
horn
beak
feather attachment
eye
nose
```

## Async meshing

Creature generation will become more expensive, so asynchronous rebuilds remain valuable.

## Fingerprinting / change detection

Keep the distinction between:

```text
shape changes
```

and:

```text
pose-only changes
```

## Articulated jaw

Treat it as the first example of the future rig architecture.

## Tests

Preserve and expand:

- phenotype sweeps,
- scale tests,
- topology tests,
- geometry validation.

---

# 30. Systems That Should Stop Being Authoritative

The following should be progressively deprecated as primary architectural concepts:

```text
linear BodyPart[] as anatomy
array index as anatomical identity
array adjacency as anatomical connectivity
Mouth owning the entire head
eyes only placed on top of the head surface
absolute random feature coordinates
grounding every body part independently
age / morph logic tied exclusively to body-part indices
```

They may temporarily remain for compatibility, but the new architecture should not depend on them.

---

# 31. MonsterAger / Morphing Direction

Directly interpolating low-level positions and dimensions is useful for the current prototype but should not become the final morph system.

Future direction:

```text
Genome A
   |
   | interpolate semantic traits
   v
Genome B
```

Then regenerate:

```text
phenotype
anatomy
surface
```

Example:

```text
juvenile muzzleLength = 0.30
adult    muzzleLength = 0.70
```

instead of manually interpolating every derived muzzle / jaw / orbit coordinate.

Stable `AnatomyId` values are important if creatures are allowed to change proportions or node counts.

---

# 32. Proposed Preset Structure

A preset should contain:

```text
CreaturePreset
  |
  +-- anatomy archetypes
  +-- default phenotype
  +-- valid trait ranges
  +-- anatomical constraints
  +-- locomotion defaults
  +-- surface defaults
```

Do not scatter checks such as:

```c
if (species == WOLF) ...
```

throughout unrelated geometry and animation code.

Prefer modular archetypes and data-driven presets.

---

# 33. Recommended Development Phases

## Phase 1 — Procedural Anatomical Head System

Goal:

Create a general head system capable of producing convincing:

- lizard,
- wolf/canid,
- bird

heads from semantic parameters.

Main work:

```text
HeadPhenotype
HeadArchetype
HeadLandmarks
HeadAnatomyResolver
HeadSurfaceRecipe
```

Move full-head responsibilities out of `Mouth`.

Add:

```text
eye sockets
brow/orbit structure
nostrils
nose / nasal pad
ears
beak support
more general jaw/oral archetypes
```

### Acceptance criteria

- Existing mouth cavity and jaw articulation continue to work.
- Lizard, wolf and bird presets all use the same high-level pipeline.
- Their morphology is controlled primarily through semantic traits.
- Eye placement is derived from head anatomy.
- Eyes sit inside sockets.
- Nostrils are cavities.
- Jaw hinges and mouth corners remain anatomically constrained.
- Broad valid parameter sweeps do not generate obviously broken heads.
- Existing tests remain passing.
- New anatomy validation tests are added.

---

## Phase 2 — AnatomyGraph and Axial Skeleton

Goal:

Replace implicit body connectivity with explicit anatomical connectivity.

Main work:

```text
AnatomyId
AnatomyNode
AnatomyGraph
JointConstraint
rest transforms
```

Build:

```text
root
pelvis
spine
neck
head
tail chain
```

### Acceptance criteria

- Branching anatomy is supported.
- No important system assumes node `i` connects to node `i + 1`.
- Stable IDs exist.
- Current body can be represented through the graph.
- SDF body volumes can be generated from the graph.

---

## Phase 3 — Procedural Limbs

Goal:

Generate anatomically meaningful limbs.

Add reusable forelimb / hindlimb chains.

Support at least:

```text
sprawling tetrapod
digitigrade mammal
avian hindlimb
bird wing
```

Use semantic traits and posture constraints.

### Acceptance criteria

- Lizard and wolf can be generated as complete four-limbed creatures.
- Bird can generate two hindlimbs and two wings.
- Limb joint limits are explicit.
- Distal limb proportions are represented.
- Surface remains continuous at limb-body attachment.

---

## Phase 4 — Automatic Rigging and Skinning

Goal:

Animate the generated rest mesh without rebuilding the whole SDF.

Main work:

```text
bind pose
bone transforms
skin weights
procedural weight generation
CPU or GPU skinning path
```

### Acceptance criteria

- The entire creature can be posed from `AnatomyGraph`.
- Ordinary joint animation does not require Marching Cubes rebuilds.
- Generated mesh deforms continuously at joints.
- Jaw becomes part of the same general pose architecture where practical.

---

## Phase 5 — Procedural Locomotion

Goal:

Generate locomotion without hand-authored clips.

Main work:

```text
GaitController
FootstepPlanner
TerrainQuery
IKSolver
SpineController
TailController
HeadStabilizer
```

Start with:

```text
wolf walk/trot
lizard walk
bird walk
```

### Acceptance criteria

- Feet adapt to uneven terrain.
- Root and torso do not independently snap every body segment to terrain.
- Leg IK respects anatomical constraints.
- IK solution remains temporally stable.
- Gaits scale with leg length and creature size.

---

# 34. Immediate Recommended Milestone

Do **not** start by adding standalone `Arm.c`, `Leg.c` and `Wing.c` modules to the existing linear `BodyPart[]` model.

The immediate milestone should be:

```text
Procedural Anatomical Head System
```

Why:

1. It solves the same representation problem on a smaller scale.
2. It forces separation between semantic phenotype, anatomy and SDF geometry.
3. It allows validation using clearly recognizable animal anatomy.
4. Once successful, the same architecture can be generalized to the body.
5. It avoids building limbs on top of a connectivity model that will need to be replaced.

Target usage:

```c
HeadPhenotype wolf   = HeadPreset_Wolf();
HeadPhenotype lizard = HeadPreset_Lizard();
HeadPhenotype bird   = HeadPreset_Bird();
```

Then:

```text
HeadPhenotype
     |
     v
HeadAnatomy
     |
     v
HeadSDF
     |
     v
Mesh
```

The presets must support meaningful variation:

```text
muzzleLength
skullWidth
skullHeight
eyeLaterality
eyeScale
jawStrength
jawLength
noseScale
earScale
beakLength
```

without collapsing into invalid geometry.

---

# 35. Architectural Principles for All Future Agent Work

Agents modifying Monster Engine should follow these principles.

## 1. Anatomy before geometry

First define what a structure means biologically / mechanically, then generate its surface.

## 2. Semantic parameters before coordinates

Expose meaningful traits, derive coordinates.

## 3. Explicit hierarchy before array order

Connectivity must be stored, never inferred from ordering.

## 4. Constraints before unrestricted randomization

Randomization must occur within anatomically valid spaces.

## 5. Rest shape before pose

SDF / Marching Cubes generate the creature shape; skeletal pose should normally animate the generated mesh.

## 6. Generic systems before species conditionals

Species presets compose reusable archetypes.

## 7. Data-driven presets before duplicated implementations

Wolf, lizard and bird should configure common systems where possible.

## 8. Preserve stable systems

Do not replace proven SDF, CSG or meshing code unless the architecture genuinely requires it.

## 9. Tests are part of the generator

Every new anatomical resolver should include validation and parameter sweeps.

## 10. Build incrementally

Do not attempt a full-engine rewrite in one patch.

Create compatibility bridges and migrate subsystems one at a time.

---

# 36. Important Non-Goals

At this stage, do not prioritize:

- physically simulated muscles,
- full soft-body simulation,
- real-time SDF remeshing for every animation frame,
- a neural generative model,
- PCA / latent-space animal representation,
- replacing SDF with a conventional modeling pipeline,
- manually rigging creatures in external DCC software,
- species-specific hard-coded animation clips.

These may become optional research directions later, but they are not required for the core universal creature architecture.

---

# 37. Target End State

The long-term engine should support a pipeline similar to:

```text
CreatureGenome
      |
      v
Phenotype Resolver
      |
      v
CreaturePhenotype
      |
      v
Anatomy Resolver
      |
      v
AnatomyGraph
      |
      +-----------------------+
      |                       |
      v                       v
Surface Recipe               Rig
      |                       |
      v                       |
SDF Geometry                 |
      |                       |
      v                       |
Marching Cubes               |
      |                       |
      v                       |
Rest Mesh + Skin Weights <---+
      |
      v
Pose / IK / Gait
      |
      v
Skinning
      |
      v
Renderer
```

This architecture should allow the same engine to generate:

- lizards,
- wolves,
- birds,
- other real animals,
- hybrid creatures,
- fantasy monsters,

while keeping their anatomy coherent enough to support procedural animation.

---

# 38. Summary

The current engine's SDF approach is not the main thing that should be replaced.

The main transformation is:

```text
FROM

low-level body pieces
+
coordinates
+
implicit sequential connectivity
+
SDF

TO

semantic genome / phenotype
+
explicit anatomy graph
+
anatomical constraints
+
surface recipes
+
SDF
+
automatic rig
+
procedural animation
```

The new head system should be used as the proving ground for this architecture before the engine expands to full limbs and locomotion.
