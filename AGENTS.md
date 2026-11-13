# AGENTS.md — Monster Engine

Repository-wide instructions for coding agents. Read relevant code before editing; this file is a navigation and safety guide, not a replacement for `README.md` or `docs/`. Follow nested `AGENTS.md` files where present.

## 1. Purpose and priorities

Monster Engine is a procedural creature and ecosystem engine written in C: renderer-independent simulation/geometry core, SDL2/OpenGL demos, GLSL shaders, JSON creature recipes, and a local Python/WebGL creature editor. Main systems: anatomical construction, SDF meshes, surfaces and fur, continuous growth/morphing, procedural animation, and ecosystem AI.

- **Find the source of truth first.** Trace data from recipe/phenotype through anatomy, SDF, mesh, surface, animation, and rendering. Fix the owning layer instead of adding demo-specific patches.
- **Preserve existing behavior.** A generic engine change should work across species, body plans, all four development stages, intermediate morphs, poses, and LODs.
- **Make focused changes.** Do not silently change APIs, recipe compatibility, defaults, architecture, or unrelated subsystems.
- **Prove visual claims with evidence.** Passing unit tests does not prove an anatomy, fur, or morph rendering change looks correct.
- **Language:** code symbols and these agent instructions use English. Existing code comments and documentation are primarily **Spanish**; follow that convention when adding comments/docs.

## 2. Useful commands (from repository root)

```sh
make test                          # Compile + run C unit suite; no GL needed
make editor-test                   # Creature editor bridge/API test
make demos/demo_ecosystem_headless # Build headless ecosystem executable
make validate-ecosystem            # Headless Python ecosystem validation
make benchmark                     # CPU SDF/meshing benchmark
make benchmark-ager-realtime       # Aging and morph timing
make benchmark-ai                  # AI benchmark
make benchmark-animation           # Animation cost benchmark
make codegraph                     # Optional: sync/init CodeGraph if installed

# Only with SDL2/OpenGL development libs and a working graphics environment:
make demos                         # Build visual and console demos
make visual-morph-ownership        # Delayed mesh vs exact SDF visual check
make visual-morph-gpu-regressions  # GPU morph regressions
make visual-ager-sparse            # Sparse-field anatomy checks

make creature-editor               # Local editor: http://127.0.0.1:8765
```

- GCC/Make build with `-Wall -Wextra -Wpedantic -O2`; core/test link `-lm -pthread`. GL targets additionally use SDL2, OpenGL/GLU. Editor/validators require Python 3; some image validators require Pillow.
- `make all` builds tests, viewer, demos, and docs; do **not** use it as the fast verification loop. `make docs` may merely warn if Doxygen is missing. `make clean` deletes built artifacts.
- Headless machines can run core tests and simulations, **not** claim to have verified rendered visuals. Say what ran, failed, or was blocked.

## 3. Code map

| Change area | Primary locations |
| --- | --- |
| Public interfaces | `include/*.h` and corresponding `src/*.c` |
| Recipes and stages | `src/CreatureRecipe.c`, `src/CreatureRecipes.c`, `src/CreaturePhenotype.c`, `src/CreatureRecipeIO.c`, `src/CreatureRecipeRegistry.c`, `recipes/` |
| Anatomy and body modules | `src/Anatomy.c`, `src/AnatomyDeformer.c`, `src/Head.c`, `src/HeadModule.c`, `src/AxialBody*.c`, `src/Limb*.c`, `src/AttachmentPath.c` |
| SDF and meshing | `src/MonsterSDF.c`, `src/SDFPrimitives.c`, `src/SDFInterval.c`, `src/SDFMesher.c`, `src/SDFAdaptiveMesher.c`, `src/MeshPatch.c` |
| Morph and asynchronous mesh | `src/CreatureMorph.c`, `src/MonsterVisualAsync.c`, `src/MorphHandoff.c`, `src/MorphSpatialCache.c`, `src/MorphChunkManager.c`, `src/MorphSurfaceTracker.c` |
| Surfaces and fur | `src/Surface.c`, `src/SurfaceMapper.c`, `src/Fur.c`, `src/SurfacePresets.c`, `shaders/surface/`, `shaders/fur_guard.*` |
| Graphics/GPU | `src/OpenGLRenderer.c`, `src/GpuSDFSurfaceCache.c`, `shaders/`, `tools/generate_sdf_shader.py`, `tools/generate_surface_shader.py` |
| Animation and IK | `src/CreatureRig.c`, `src/RigBuilder.c`, `src/IK.c`, `src/ProceduralAnimator.c`, `src/AnatomicalReach.c` |
| AI/ecosystem | `src/CreatureMind.c`, `src/CreatureActions.c`, `src/CreatureNavigation.c`, `src/EcosystemWorld.c`, `src/EcosystemScenario.c` |
| End-to-end demos | `demos/demo_morph_species.c`, `demos/demo_game_scene.c`, `demos/demo_ecosystem.c`, `demos/demo_ecosystem_headless.c` |
| Editor (extra instructions) | `tools/creature_editor/AGENTS.md`, `docs/CREATURE_EDITOR.md`, `tools/creature_editor/` |
| Verification | `tests/`, `benchmarks/`, `tools/validate_*.py`, `docs/validation/` |

Read documentation **selectively**: `docs/MONSTER_ENGINE_PROCEDURAL_CREATURE_ARCHITECTURE.md`, `docs/PROCEDURAL_SURFACES.md`, `docs/PROCEDURAL_ANIMATION.md`, `docs/AGER_MORPH_SAFETY_AUDIT.md`, `docs/AGER_CHUNKED_MORPH_IMPLEMENTATION.md`, `docs/CREATURE_AI_ARCHITECTURE.md`, `docs/ECOSYSTEM_SIMULATION.md`, `docs/BRUMACERNO_BODY_AND_HOOVES.md`.

## 4. Architecture contracts and common pitfalls

- **Keep the core renderer-independent.** `RENDER_SRCS` contains `src/OpenGLRenderer.c`; do not add SDL/GL dependencies to `CORE_SRCS`. `make test` must work without a graphics stack. Respect `RenderInterfaces.h` and `WorldInterface.h`.
- **Public APIs:** prefer `include/Module.h` + `src/Module.c`, `Module_Action` naming, Doxygen descriptions on touched public APIs, and explicit memory ownership/lifetimes.
- **Makefile registration is manual:** new C core/render files must be added to `CORE_SRCS`/`RENDER_SRCS`; new test files to `TEST_SRCS`. Preserve generated dependency tracking.
- **SDF changes propagate:** check CPU evaluation, GLSL translation, analytic interval/conservative bounds, morph interpolation, mesher/LOD behavior, normals, and material mapping. Never hide missing geometry by deleting triangles or weakening bounds.
- **Async/morph changes:** preserve snapshot/fingerprint identity, rejection of stale worker results, topology/age synchronization, and lifetime of meshes. Avoid doubled silhouettes, frozen or missing parts, seams, and lagging surfaces at intermediate or reverse ages.
- **Animation/IK:** use shared rig/pose systems with real reach limits and contact constraints. Do not create unbounded neck/leg stretch, sliding planted feet, or per-frame full remeshing.
- **Surface/fur:** retain deterministic seeds and persistent surface attachment; observe material and integument masks (no fur growing through mouth, nose, or hooves). Check coverage, strand-body intersections, poses, morphs, LOD, and rendering cost, not just a single static image.
- **Ecosystem:** preserve seeded/fixed-step reproducibility and keep needs, behavior, resources, physiology, and reproduction independent of the GL demo.

## 5. Recipes, the editor, and generated code

- `recipes/*.creature.json` are external versioned definitions. `recipes/catalog.txt` lists loaded recipes; external IDs override built-ins. Brumacerno uses ID 3; new external recipes normally start at ID 1000. Read `recipes/README.md` before changing identifiers or loading.
- The editor loads and saves recipes without recompiling the engine. For editor tasks obey the **nested** `tools/creature_editor/AGENTS.md`: use the revision-aware agent API, handle HTTP 409 conflicts instead of overwriting state, inspect/render before save, and honor `--catalog-override`/`--overwrite` safeguards.
- **Canonical Brumacerno source:** `recipes/brumacerno.creature.json` generates `src/CreatureRecipeBrumacerno.generated.inc` through `tools/generate_builtin_brumacerno.py`. `src/CreatureRecipeSchema.inc` comes from `tools/generate_recipe_schema.py`.
- `src/MonsterSDFShader.generated.h`, `src/MonsterSDFCompute.generated.h`, and `src/MonsterSurfaceShader.generated.h` are outputs of the GLSL generator scripts. **Never hand-edit any generated `.h` or `.inc`.** Edit their sources/generators, regenerate using the Makefile, and review generated diffs.
- For recipe schema, body-plan, or stage changes, verify JSON IO, unique IDs, catalog overrides/fallback, all four developmental stages, and existing species. Keep `CreatureRecipeDocument` data alive while creatures or async workers reference it.

## 6. Verification matrix

- **Every C change:** run `make test`. Tests use `TEST_ASSERT` and `FLOAT_NEAR` from `tests/test_utils.h`; `run_tests` executes the entire suite (no test selection flag).
- **Adding a C test suite requires three steps:** create `tests/test_<area>.c` with `run_<area>_tests(void)`, add it to `TEST_SRCS` in `Makefile`, and declare/call it in `tests/main_test.c`.
- **Editor/recipes:** `make editor-test` and relevant C tests (`test_creature_recipe_io.c`, `test_bovine_editor.c`, `test_cloven_hoof.c`). Verify save/load and external-catalog precedence.
- **Anatomy and SDF:** relevant C tests plus front/side/rear/three-quarter captures, multiple life stages and poses. Check silhouette, body connections, feet/contact, thin geometry, normals, disconnected components, SDF bounds, and materials. For Brumacerno, follow `docs/BRUMACERNO_BODY_AND_HOOVES.md`; `tools/validate_bovine_body.py` requires built capture binaries and Pillow.
- **Morph/async:** where GL is available, use `make visual-morph-ownership`, `make visual-morph-gpu-regressions`, and/or `make visual-ager-sparse`. Compare the **same pose, age, recipe and camera** against exact SDF. Cover intermediate ages, forward/reverse cycles, worker delay, and topology changes; inspect lag, ownership and seam diagnostics.
- **Surface, fur or shaders:** relevant `test_surface.c`, `test_fur.c`, and GPU tests, plus renderer captures. Check close/far views, motion, occlusion, body attachment, transitions, and CPU/GLSL agreement when shared formulas change.
- **AI/ecosystem:** `make validate-ecosystem`, relevant C tests, and a seeded headless run when useful:
  ```sh
  ./demos/demo_ecosystem_headless --scenario=B --seed=42 --days=1 --summary=/tmp/monster-ecosystem.json
  ```
  Check resources, bounded movement, births/deaths, and deterministic output. `make ecosystem-sanitize` is an expensive ASan/UBSan check for deeper memory investigations.
- **Performance-sensitive work:** compare baseline and modified results using the relevant `make benchmark*` target. Report frame/worker times, allocations, mesh updates, or ecosystem tick costs; do not claim a speedup from an unmatched run or trade away visual fidelity without explicit agreement.
- Keep disposable captures and logs under `build/` or `/tmp/`. Add substantial `docs/validation/` evidence only if useful for review, and avoid accidentally committing large generated artifacts.

## 7. Required agent workflow

1. Confirm task scope, `git status --short`, the nearest `AGENTS.md`, and existing local changes. Do not overwrite work already in progress.
2. Locate the canonical implementation and downstream consumers before editing. Use CodeGraph for symbol/call/impact exploration if available; otherwise read source and search normally. Do not trust an outdated index over current code.
3. Identify affected species, modes, stages, performance paths, and regression tests. Implement a minimal reusable fix, not a hard-coded exception to one demo.
4. Run applicable builds/tests, visuals, and benchmarks. State precisely which checks passed, failed, or could not run (especially on headless systems).
5. Review `git diff --check` and `git diff --stat`; summarize changed files, why, evidence, remaining risks, and unverified cases.

**Always:** preserve numerical sanity, valid bounds, memory ownership, module boundaries, recipe compatibility, and anatomical continuity. **Ask before:** broad architectural rewrites, incompatible data format changes, major dependency additions, or intentional quality reductions. **Never:** commit secrets, hand-edit generated sources, discard unrelated local edits, mask visual bugs by disabling features, or describe untested behavior as verified.

<!-- CODEGRAPH_START -->
## 8. Optional CodeGraph integration

`opencode.jsonc` configures the local `codegraph serve --mcp` server. When available and indexed, prefer `codegraph_search` (symbol definitions), `codegraph_callers`/`codegraph_callees` (relationships), `codegraph_impact` (affected code), and `codegraph_context` (focused context). Use text search for literals and source inspection for authoritative confirmation. When missing or stale, work normally without it; `make codegraph` can rebuild/sync the optional index if the CLI is installed.
<!-- CODEGRAPH_END -->
