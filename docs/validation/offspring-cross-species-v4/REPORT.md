# Monster Engine — CreatureOffspring V4: Cross-Species Anatomical Recombination Repair

**Base commit:** `b90ac1808fcf966a071edcc02d700f9a3b7a4f5d`  
**Generator version:** `CREATURE_OFFSPRING_VERSION = 4`  
**Validation seeds:** `42` (16-pair matrix, 32 candidates), plus `42, 101, 2025, 48216` for Elephant × Falcon / Falcon × Elephant both-order ear exposure sweeps.

---

## 1. Confirmed Root Causes

1. **Elephant × Falcon fused ears (`HeadBlend.c`, `CreatureOffspring.c`, `OffspringRepair.c`):**
   - Blending an elephant cranium with a tiny falcon cranium produced a small head (`craniumR ≈ 0.23–0.40`) mounted directly in front of the massive elephant neck (`r ≈ 0.43`) and pectoral/shoulder mass (`r ≈ 1.02`).
   - Elephant pinnae sweep downward and backward (`earDir.y ≈ -0.80, earDir.z ≈ -0.23`), plunging directly into the neck and shoulder SDF (`SDF(no-ears)` distances `-0.02` to `-0.22`).
   - `SupportedHead` only checked ear root contact against the head-only SDF and never checked whole-pinna exposure against the full body SDF.
   - `HeadSystem` applied `inheritEars` asymmetrically depending on whether parent A or parent B had ears.

2. **Avian beak collapse and stretching (`HeadRegionalProfile.c`, `HeadBlend.c`, `CreatureOffspring.c`):**
   - `SURFACE_POINTS` and `SURFACE_RADII` in `HeadRegionalProfile.c` omitted `beakBase`, `beakMid`, `beakTip`, `beakLowerTip`, `beakBaseRadii.x/y`, `beakTipShape.x/y`, and `beakLowerRadii.x/y/z`, leaving beak geometry untranslated, unscaled, and unstretched during profile transforms.
   - `HeadAnatomy_BlendRegional` linearly blended `beakWeight` across incompatible rostra, producing invalid intermediate values (`< 0.5`) that switched off the 3D beak SDF while leaving `isBeak = true`, turning avian heads into black cylindrical snouts or collapsed stumps.

3. **Falsely perfect `headError = 0.00` (`CreatureOffspring.c`, `OffspringMorphFitter.c`):**
   - `Proposal` overwrote `target->head.anatomy` with `recipe->adult.headHybrid.regional`, comparing the candidate head against itself.
   - Headless body plans (e.g., Medusa) reported `headError = 0.00` instead of unavailable.

4. **Severe cross-archetype axial distortions (`CreatureOffspring.c`, `OffspringMorphFitter.c`, `OffspringRepair.c`):**
   - `Axial`, `FitAxial`, and `FitStance` interpolated station-by-station `profile[0..5]` and section heights across non-homologous architectures (cephalopod mantle ↔ tetrapod thorax, arthropod segments ↔ lizard trunk), creating hanging rectangular dog chests (`09_octopus_x_dog_c1`) and giant lizard dorsal humps (`06_dragonfly_x_lizard_c0`).

5. **Seed-stage failure in `07_dragonfly_x_pangolin` (`OffspringRepair.c`, `CreatureOffspring.c`):**
   - Exact diagnosed failure: `Anatomical constraints at stage 0 (2 proportions, 0 anchors; mod=4 limb.soleHeight=0.0086 not in [-0.0065,0.0065] ground_contact)`.
   - Incompatible axial profile mixing shifted stage-0 hip height while the tight adult-style `.018 * body` ground-contact tolerance (`±0.0065`) caused constraint oscillation against limb-length clamps.

6. **Oversized avian tail fans and faceted octopus webs (`SupplementaryMesh.c`):**
   - `BuildAvianTailFan` scaled feather length/width by `2.48 × tail->length` even on large non-avian reptilian tails (`03_falcon_x_lizard_c1`).
   - `BuildArmSuckers` constructed the interbrachial web from only 3 flat triangles per side with hardcoded vertical normals (`08_octopus_x_medusa_c1`).

7. **Head/neck color seams, incompatible manes, bat patagium overlap, and clone bias (`CreatureOffspring.c`):**
   - Hard albedo jumps between `SURFACE_REGION_HEAD` and `SURFACE_REGION_DORSAL_TRUNK` created straight collar seams; mammalian manes were transferred onto non-mammalian bodies; bat wing `proximalSweep` pushed membranes into hybrid heads; and selection lacked real donor-contribution measurement.

---

## 2. Code Changes by Subsystem

| Subsystem | Files Modified | Summary |
| --- | --- | --- |
| **Diagnostics & API** | `include/CreatureOffspring.h`, `src/OffspringRepair.c` | Bumped `CREATURE_OFFSPRING_VERSION = 4`; added `OffspringConstraintFailure`, graft breakdown counters (`graftAttempts`, `adaptedGrafts`, `repairedProportions`), error availability flags (`headErrorAvailable`, `bodyErrorAvailable`, `limbErrorAvailable`), component qualities, real donor-contribution metrics, and ear clearance diagnostics (`earExposedFraction`, `earMaxBodyPenetration`, `earRepaired`, `earDeliberatelyAbsent`). |
| **Head Transforms** | `src/HeadRegionalProfile.c` | Added `beakBase`, `beakMid`, `beakTip`, `beakLowerTip` to `SURFACE_POINTS`; scaled dimensional `beakBaseRadii.x/y`, `beakTipShape.x/y`, `beakLowerRadii.x/y/z` while keeping dimensionless controls (`beakBaseRadii.z`, `beakShape.*`, `beakTipShape.z`) invariant; updated `HeadAnatomy_EditProfile` for beak stretching. |
| **Regional Head Blending** | `src/HeadBlend.c` | Feature-aware avian beak inheritance (`beakWeight` coherence, skull-relative scaling, compact rostral skull backing, no mammalian nasal pad on beaks); skull-relative eye scaling & clamping for cross-species eyes; local-frame ear orientation and minimum pinna thickness/width guards. |
| **Ear Clearance & Repair** | `src/OffspringRepair.c` | Added `AnalyzeEarClearance` (full-body SDF grid probe with ears disabled) and `RepairEarClearance` (outward/forward pinna rotation, root seating, conservative scaling across all 4 stages); added ventral torso clearance guard, stage-aware seed ground-contact tolerance, and short-proboscis flexed nostril clearance validation. |
| **Fitter & Metrics** | `src/OffspringMorphFitter.c` | Added `AxialArchetypesCompatible` guard in `FitAxial` and `FitStance`; added beak terms to `OffspringMorph_HeadError`; added `SemanticHeadPenalty` (beak collapse, extreme elongation, oversized eyes, ear penetration); reported N/A metrics honestly via `*ErrorAvailable` and `-1.0` component quality. |
| **Recombination Core** | `src/CreatureOffspring.c` | Removed `target->head.anatomy` overwrite in `Proposal`; compatibility-aware `Axial`; symmetric ear inheritance & cross-plan head-to-body envelope bounds in `HeadSystem`; dorsal flexible-appendage graft adapter; bat wing `proximalSweep` clearance guard; head-to-neck albedo smoothing and non-mammal mane filtering in `Surface`; real `ComputeDonorContribution` and balanced `SelectionValue` with clone penalty. |
| **Supplementary Meshes** | `src/SupplementaryMesh.c` | Normalized non-avian `BuildAvianTailFan` feather dimensions to recipient trunk/tail base; upgraded octopus interbrachial web to a smoothly tessellated 5×4 scalloped curved mesh with analytic normals and edge closure. |
| **Regression Tests** | `tests/test_creature_offspring.c` | Added `HeadProfileBeakTransformRoundTrip` and `CrossSpeciesV4Matrix` (all 16 pairs across 4 stages + intermediate ages + JSON round-trip + ear exposure + beak coherence + headless N/A checks). |

---

## 3. 16-Pair V4 Audit Results (`seed=42`, 2 candidates/pair)

All **16 pairs / 32 candidates** generated cleanly (`0` failures) in **1.337s total (0.084s/pair avg)**:

- **`07_dragonfly_x_pangolin`:** Previously 100% failed at stage 0 → Now succeeds with both candidates (`grafts=3/3`, `effectiveDonorContribution=0.44 / 0.26`).
- **`08_octopus_x_medusa`:** `c0` grafts all 8 octopus arms onto the medusa scaffold (`grafts=8/9`, `headErrorAvailable=false`); `c1` grafts medusa tentacles onto the octopus scaffold with smooth tessellated interbrachial web.
- **`13_elephant_x_falcon` & `14_falcon_x_elephant`:** Both parent orders generate both avian and elephant scaffolds with zero embedded ears (`earExposedFraction >= 0.62`, all outer pinna SDF probes positive).
- **Still explicitly unsupported (reported in `unsupportedGrafts`):** Cephalopod siphon (`AXIAL_SLOT_SIPHON`) on non-cephalopod scaffolds; radial medusa tentacles on non-radial tetrapod scaffolds; non-homologous arthropod thoracic legs/wings on vertebrate scaffolds without compatible slots.

---

## 4. Visual Comparison Evidence

- `sheets/elephant_x_falcon_BEFORE.png` — Baseline reproduction showing elephant ears completely buried inside the elephant torso/shoulders (`SDF(no-ears)` tip `-0.169`).
- `sheets/elephant_x_falcon_AFTER.png` — V4 Elephant × Falcon with fully exposed, recognizable elephant ears flaring cleanly outside the shoulders (`SDF(no-ears)` tip `+0.919`).
- `sheets/elephant_x_falcon_v4_diagnostics.png` — Surface, neutral diagnostic, and ears-hidden inspection views of `13_elephant_x_falcon_c1`.
- `sheets/v4_repaired_overview.png` — V4 renders of `03_falcon_x_lizard_c1`, `04_falcon_x_horse_c0`, `06_dragonfly_x_lizard_c0`, `07_dragonfly_x_pangolin_c0`, `08_octopus_x_medusa_c1`, and `09_octopus_x_dog_c0`.
- Full metrics log: `audit_metrics_v4.jsonl` and exported recipes in `recipes/`.
