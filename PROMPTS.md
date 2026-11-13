# Monster Engine — Individual agent prompts

See `README.md` for the dependency plan and file ownership. Each prompt below is self-contained.

# Lead coordinator prompt — Monster Engine performance program

You are the **lead integration coordinator** for the Monster Engine long-hair and procedural-creature optimization program. Your role is to launch isolated agents, maintain a dependency graph, review correctness evidence and integrate accepted PRs. Read repository `AGENTS.md`, `OPTIMIZACION_CRECIMIENTO_PELO.md`, `INFORME_OPTIMIZACION_MONSTER_ENGINE_SDF_PELO_2026-11-05.md` and this package's `README.md` before starting. Reviewed reference commit: `bc83fd586d7b1b8edae881c65ee25e96536ac08f` (confirm current `main` and adjust for changes).

## Task graph — obey it strictly

1. Start **Agents A, B, C, D, J concurrently**. Give each the self-contained corresponding `.md` prompt and its own branch/worktree. Enforce exclusive file ownership; A/B/C/D/J may add their *own new files* but must not edit shared `src/LongHair.c`, `src/MonsterVisual.c`, `src/MonsterVisualAsync.c`, `src/MonsterSDF.c`, `src/SDFSamplingPool.c` or `Makefile` before integration approval. Let J profile unrelated creature generation in parallel, but give performance benchmark execution exclusive machine slots; don't compare timings collected under competing workloads.
2. Review and merge **A+B+C** only after their isolated tests and interface contracts pass. Then start **Agent E** as the sole writer of `LongHair.c`/`MonsterVisual.c` in this integration wave. E implements certified Bézier/AABB interval skip with **exact fallback**.
3. When **Agent D** has passed scheduling/concurrency tests and **E** has passed the horse baseline/visual/collision gates, integrate D and start **Agent F** as sole hot-path editor. F implements parallel guide/follicle construction with a persistent, non-reentrant, safely owned worker pool.
4. After measured and regression-passing F, start **Agent G** to profile/optimize the actual remaining bottleneck: shared guide-pair bounds, guide collision cost, or conservative candidate SDF pruning. Do not demand a cache or a rewrite if profiling shows a worse trade-off.
5. After G passes, start **Agent H** for invalidation/dependency-aware incremental reuse. Do not reuse hair certificates across geometry changes without proving they remain valid under the new **full body + jaw** field. Preserve deterministic visible output.
6. After H passes, start **Agent I** to make editor growth non-blocking with coherent, same-generation body+hair publication, bounded latest-request queue, cancellation and no stale jobs. The renderer/GPU upload stays on the graphics owner thread.
7. Read **Agent J**'s species benchmark report separately. Propose follow-up engine-wide improvements only when directly supported by per-species measurements. Don't insert speculative general SDF changes into a hair optimization PR.

## Hard invariants

The initial measured horse baseline was about 1.47 seconds / 857,381 exact field evaluations per rebuild: 288 guides, 9,216 rooted children, 72-point audit, ~101,097 guide/collision calls and 705,205 follicle audit calls. The goal is <=100ms per rebuild p95 on a documented target machine (<=50ms aspirational), 21 growth steps <=2.1s, while retaining all 288 visible roots, 9,216 individual roots, stable IDs, original visual appearance, deterministic seeds, transactional `LongHairSet_Build`, correct jaw collision, and zero observed penetration in official four-stage GPU sample validation. These are goalposts, **not** achieved measurements.

The rest collision field is `min(bodySDF, jawRestDistance)`. `MonsterSDF_GetCellRangeWrapper` is not guaranteed to support every body/face configuration. Never assume the approximate SDF is 1-Lipschitz. An interval that returns UNKNOWN must use the **exact existing audit**; an invalid/conservative proof must not be traded for lower sample count. Protect the original 72-audit comparator as an off/debug reference path, five retries, root projection, normal/flow filters and tubular fallback. Do not alter shader, recipe, density, collision margin or official reference captures to make the benchmark pass.

## Coordinator gates after every integration

- Inspect changed files and confirm no file owner conflict or accidental public API break.
- `make test`, `make editor-test`, `python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /tmp/long-hair-validation`, `git diff --check`. Where feasible run TSan/ASan. Do not overwrite `docs/validation/long-hair-styling/resultado/`.
- Compare across 4 stages: 288/288 visible guides; 9,216/9,216 follicle own roots; same stable root IDs; root/descriptor digest; correctness of body+jaw combined field; zero detected GPU penetration; side/front/rear/root and pose visual parity; growth path.
- Compare p50/p95/max build wall time, exact SDF distance calls, interval counts/cost, fallback rate, actual worker count and memory; serial-vs-parallel digest. Reject optimization that reduces `distance()` calls but increases end-to-end time unless there's a clear justified reason to keep an optional experimental path.
- Do not run official benchmark and heavy test/capture simultaneously. If an agent cannot test visual/GPU claims, label them unverified and hold acceptance.
- Stop dependency chain on a correctness regression. If a technical technique is not cost-effective, keep safe fallback and return a no-go report rather than forcing it into production.

## Communication protocol

Require every agent to send: `BASE_SHA`, `OUTPUT_SHA`, `CHANGED_FILES`, `REQUESTED_SHARED_EDITS`, `TESTS_AND_RESULTS`, `DIFFERENTIAL_RESULT`, `MEASURED_PERFORMANCE_OR_NOT_MEASURED`, `LIMITATIONS`, and `READY_FOR_NEXT_AGENT`. Keep this in a coordination ledger. Merge in dependency order with review, not by completion order. No force pushes, no direct commits to main, no concurrency on the same working tree.

## Your first action

Start A/B/C/D/J in separate worktrees or remote branches, supplying the matching prompt file to each. Confirm that their owned files are disjoint, record branch + commit per agent and reserve independent benchmark windows. **Do not launch E/F/G/H/I for production modifications until their listed dependencies are accepted.**


---

# Agent A — Baseline and regression harness

**Execution wave:** Wave 1 — can start immediately alongside B, C, D, J  
**Dependencies:** None; must finish before E performance acceptance  
**Exclusive file ownership in this task:** New `benchmarks/benchmark_long_hair_build.c`, new `tools/compare_long_hair_baseline.py`, new `docs/performance/long-hair-baseline-agent-a.md`; new `tests/test_long_hair_regression_*.c` if useful. Avoid modifying `src/LongHair.c`, `src/MonsterVisual.c`, `Makefile`, or current reference images (request integration patch only if necessary).

## Shared non-negotiable contract

Repository: https://github.com/KillDaWill/monster_engine. Reviewed baseline commit: `bc83fd586d7b1b8edae881c65ee25e96536ac08f`. Before coding, inspect the **current** `main` HEAD and `AGENTS.md`; if HEAD has advanced, record the new SHA, review the diff and adapt. Do not reset main, force-push, or overwrite other agents' work. Start an independent `agent/<task-name>` branch or git worktree. Respect the file ownership assigned below. If a shared existing file has to change, submit an explicit integration patch or ask the coordinator rather than silently editing it.

Primary baseline: 288 visible guides, 9,216 independently rooted children, 857,381 SDF distance calls per rebuild (705,205 audit, 51,079 root/lift and 101,097 guide/collision), approximately 1.47 s per growth step, four reference stages and 21 morph steps. Target <=100 ms per synchronous rebuild (aspirational <=50 ms) **without weakening collision checks or visual identity**. These values are previous reported measurements, not new tests. The adult static GPU time (~2.85 ms) is not the bottleneck.

Preserve `LongHairSet_Build` transactional semantics, anatomical/stable IDs, deterministic seeds, original 72-point exact audit as a fallback, original `.0005f * scale` collision criterion, five retry opportunities, tubular fallback, `rootFullness` semantics, the CPU/GPU interpolation contract, and the current JSON recipe. The actual construction field is the UNION `min(body SDF, rest jaw field)`; a bound valid for the body alone is not proof for the union. The SDF is not universally proven to be 1-Lipschitz. `range == unknown/false` ALWAYS means exact fallback, never free space. Never silently lower strand density, hide roots, replace the SDF with a visual approximation, reduce the audit to 8 samples, or write benchmark output into the frozen reference directory.

Verification for integrated runtime changes (run on an otherwise idle machine for trustworthy timings):
```sh
make test
make editor-test
python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /tmp/long-hair-validation
git diff --check
```
For standalone helper branches not yet wired into the build, provide an isolated build/test command and integration instructions; full integration tests belong to the integration agent. Prefer ASan/TSan for concurrency and memory. Compare against the original reference capture under `docs/validation/long-hair-styling/resultado/`, without overwriting it. Report p50/p95/max wall time and SDF distance + interval query counts separately; a reduction in distance() calls is not a win if range queries are slower.

Deliver one focused commit/PR (or patch) with: changed files, rationale, correctness invariants, test commands/results, measured performance (or explicit 'not measured'), blockers and handoff notes. Do not claim a visual or performance result that was not actually tested.


## Your mission

Implement a reproducible, standalone benchmarking/quality harness for long-hair construction, not an optimization. Read `OPTIMIZACION_CRECIMIENTO_PELO.md` and `docs/validation/long-hair-styling/RESULTADOS_CRIN.md` if available, plus the current source. Reproduce all four horse growth stages and the adult exact body+rest-jaw configuration used by production; do not accidentally benchmark the body-only SDF and label it production-equivalent.

Tasks:
1. Add build timing broken down into SDF build, guide root projection, collision/PBD/polish, guide certificate, follicle initial root projection, lift/retries, 72-sample audit, packing, pose and whole update. Add thread-local/per-phase call counters or an isolated counting-wrapper benchmark. Keep the hot production runtime free of uncontrolled logging.
2. Provide a **benchmark-only** 'skip follicle pass' control, with group `rootFullness` unchanged, to measure the true guide base cost. The old rootFullness=0 benchmark changes `LongHairGroup_Envelope` and is not a clean ablation; clearly explain this trap.
3. Save a machine-readable baseline (JSON/CSV) for stages 0–3: exact field config, compile flags, machine, commit SHA, 3+ warmed runs, p50/p95/max where sample count supports it, exact SDF calls and retry histogram; distinguish unknown/uninstrumented from zero.
4. Record a canonical comparison digest of all visible guide roots and each child's `{neighbor, weight, lift, validity}`, retaining stable IDs and order. Use a clear float tolerance and optionally binary digests; don't compare only a final image.
5. Support subsequent integration branches with commands comparing `certification=off` vs `on` on the same binary and a simple pass/fail report. Collect collision, GPU parity, root placement and visual validation evidence from official tools.
6. In a separate section, reserve schema fields for interval queries: `range_calls`, `range_unknown`, `range_certified`, `range_ms`, `certified_samples`, fallback ratios, and for threads count `threads`.

Do not edit other agents' implementation modules. If the baseline cannot be reproduced, investigate the discrepancy and halt optimization claims until it is explained.

## Completion / handoff

Provide baseline JSON/CSV, a benchmark CLI, exact invocation steps, a comparison script, reference hashes/tolerances, and the first bottleneck breakdown. Send Agent E and Agent F the comparison commands. The baseline harness should be usable regardless of whether E's optimization is enabled.


---

# Agent B — Conservative combined SDF ranges

**Execution wave:** Wave 1 — can start immediately alongside A, C, D, J  
**Dependencies:** None; Agent E integrates after review  
**Exclusive file ownership in this task:** New `include/LongHairFieldQuery.h`, `src/LongHairFieldQuery.c`, and new `tests/test_long_hair_field_query.c` only. Do NOT edit `src/LongHair.c`, `src/MonsterVisual.c` or `src/MonsterSDF.c` in this independent branch; propose specific integration diffs in handoff.

## Shared non-negotiable contract

Repository: https://github.com/KillDaWill/monster_engine. Reviewed baseline commit: `bc83fd586d7b1b8edae881c65ee25e96536ac08f`. Before coding, inspect the **current** `main` HEAD and `AGENTS.md`; if HEAD has advanced, record the new SHA, review the diff and adapt. Do not reset main, force-push, or overwrite other agents' work. Start an independent `agent/<task-name>` branch or git worktree. Respect the file ownership assigned below. If a shared existing file has to change, submit an explicit integration patch or ask the coordinator rather than silently editing it.

Primary baseline: 288 visible guides, 9,216 independently rooted children, 857,381 SDF distance calls per rebuild (705,205 audit, 51,079 root/lift and 101,097 guide/collision), approximately 1.47 s per growth step, four reference stages and 21 morph steps. Target <=100 ms per synchronous rebuild (aspirational <=50 ms) **without weakening collision checks or visual identity**. These values are previous reported measurements, not new tests. The adult static GPU time (~2.85 ms) is not the bottleneck.

Preserve `LongHairSet_Build` transactional semantics, anatomical/stable IDs, deterministic seeds, original 72-point exact audit as a fallback, original `.0005f * scale` collision criterion, five retry opportunities, tubular fallback, `rootFullness` semantics, the CPU/GPU interpolation contract, and the current JSON recipe. The actual construction field is the UNION `min(body SDF, rest jaw field)`; a bound valid for the body alone is not proof for the union. The SDF is not universally proven to be 1-Lipschitz. `range == unknown/false` ALWAYS means exact fallback, never free space. Never silently lower strand density, hide roots, replace the SDF with a visual approximation, reduce the audit to 8 samples, or write benchmark output into the frozen reference directory.

Verification for integrated runtime changes (run on an otherwise idle machine for trustworthy timings):
```sh
make test
make editor-test
python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /tmp/long-hair-validation
git diff --check
```
For standalone helper branches not yet wired into the build, provide an isolated build/test command and integration instructions; full integration tests belong to the integration agent. Prefer ASan/TSan for concurrency and memory. Compare against the original reference capture under `docs/validation/long-hair-styling/resultado/`, without overwriting it. Report p50/p95/max wall time and SDF distance + interval query counts separately; a reduction in distance() calls is not a win if range queries are slower.

Deliver one focused commit/PR (or patch) with: changed files, rationale, correctness invariants, test commands/results, measured performance (or explicit 'not measured'), blockers and handoff notes. Do not claim a visual or performance result that was not actually tested.


## Your mission

Build a safe optional field-query abstraction for hair collision certification. This is an interface + conservative *composition* task, not the Bézier classifier and not the final wiring. Study `SDFField.getCellRange`, `MonsterSDF_GetField`, `MonsterSDF_GetCellRangeWrapper`, `LongHairRestDistance`, `LongHairJawDistance` and the limitations of bodyPartCount, mouth/head and jaw bounds.

Tasks:
1. Provide `LongHairFieldQuery` (or equally small and documented API) exposing the **unchanged exact** `distance(ctx,p)`, optional `range(ctx,AABB,&lo,&hi)` with explicit UNKNOWN, and generation/identity metadata. It must not own borrowed field memory. Keep the legacy distance callback usable.
2. Provide a safe composer for the union `min(body,jaw)` when both true conservative intervals exist: min of their respective lower bounds, min of their respective upper bounds, with outward float rounding. Verify the exact mathematical rule for `min` and nonfinite values.
3. When a valid jaw interval is unavailable, support **only demonstrably safe** exclusion from a conservatively computed jaw influence AABB with a documented lower-bound proof; otherwise return UNKNOWN. Do not invent a jaw bound from a sampling heuristic. If current APIs cannot establish a mathematically valid bound, return UNKNOWN and document the missing primitive.
4. Handle cases where `MonsterSDF_GetCellRangeWrapper` returns false: bodyParts, unsupported head, no candidate hierarchy, nonfinite/failed bounds. UNKNOWN is not a positive certificate. Check whether the reference horse actually has usable coverage and report coverage numbers when possible.
5. Write adversarial unit tests: intersecting jaw box, body-only box, jaw-only box, box near a cavity or smooth union, unknown body range, NaN/Inf, tiny box at the root, empty/disjoint influence, rotated/scaled jaw, geometry generation changes. Verify any claimed range contains dense sampled exact distances, but document that sampling is a regression test, not a proof.
6. Do not alter base SDF output or change `LongHairJawDistance` evaluation to make intervals easier. If a true interval is impossible in the allocated scope, implement safe UNKNOWN fallback and propose a separate bounded follow-up.

The final adapter must be C-only and renderer-independent; no OpenGL code.

## Completion / handoff

Deliver an independent, tested C API, clear memory/lifetime rules, coverage table for body/jaw reference field, and integration note with exact calls Agent E must add in `MonsterVisual.c`. No assertion of universal range coverage unless supported by proof/tests.


---

# Agent C — Bezier curve bounds library

**Execution wave:** Wave 1 — can start immediately alongside A, B, D, J  
**Dependencies:** None; Agent E wires it to the audit  
**Exclusive file ownership in this task:** New `include/HairCurveBounds.h`, `src/HairCurveBounds.c`, `tests/test_hair_curve_bounds.c`. Do not edit `src/LongHair.c`, `src/MonsterVisual.c`, `shaders/long_hair.vert` or existing generated validation files.

## Shared non-negotiable contract

Repository: https://github.com/KillDaWill/monster_engine. Reviewed baseline commit: `bc83fd586d7b1b8edae881c65ee25e96536ac08f`. Before coding, inspect the **current** `main` HEAD and `AGENTS.md`; if HEAD has advanced, record the new SHA, review the diff and adapt. Do not reset main, force-push, or overwrite other agents' work. Start an independent `agent/<task-name>` branch or git worktree. Respect the file ownership assigned below. If a shared existing file has to change, submit an explicit integration patch or ask the coordinator rather than silently editing it.

Primary baseline: 288 visible guides, 9,216 independently rooted children, 857,381 SDF distance calls per rebuild (705,205 audit, 51,079 root/lift and 101,097 guide/collision), approximately 1.47 s per growth step, four reference stages and 21 morph steps. Target <=100 ms per synchronous rebuild (aspirational <=50 ms) **without weakening collision checks or visual identity**. These values are previous reported measurements, not new tests. The adult static GPU time (~2.85 ms) is not the bottleneck.

Preserve `LongHairSet_Build` transactional semantics, anatomical/stable IDs, deterministic seeds, original 72-point exact audit as a fallback, original `.0005f * scale` collision criterion, five retry opportunities, tubular fallback, `rootFullness` semantics, the CPU/GPU interpolation contract, and the current JSON recipe. The actual construction field is the UNION `min(body SDF, rest jaw field)`; a bound valid for the body alone is not proof for the union. The SDF is not universally proven to be 1-Lipschitz. `range == unknown/false` ALWAYS means exact fallback, never free space. Never silently lower strand density, hide roots, replace the SDF with a visual approximation, reduce the audit to 8 samples, or write benchmark output into the frozen reference directory.

Verification for integrated runtime changes (run on an otherwise idle machine for trustworthy timings):
```sh
make test
make editor-test
python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /tmp/long-hair-validation
git diff --check
```
For standalone helper branches not yet wired into the build, provide an isolated build/test command and integration instructions; full integration tests belong to the integration agent. Prefer ASan/TSan for concurrency and memory. Compare against the original reference capture under `docs/validation/long-hair-styling/resultado/`, without overwriting it. Report p50/p95/max wall time and SDF distance + interval query counts separately; a reduction in distance() calls is not a win if range queries are slower.

Deliver one focused commit/PR (or patch) with: changed files, rationale, correctness invariants, test commands/results, measured performance (or explicit 'not measured'), blockers and handoff notes. Do not claim a visual or performance result that was not actually tested.


## Your mission

Implement the standalone math needed to conservatively bound the **actual** Catmull–Rom child fibers in the CPU/GPU contract. Read `LongHairGuide_Evaluate` and shader `Curve()` / `Child()` in `shaders/long_hair.vert` before implementing. A rooted child's curve is `(1-w)*GuideA(t)+w*GuideB(t)+normalize(lerp(normalA,normalB,w))*lift`. The normal/lift are constant *per fiber*, not per segment; no new per-vertex displacement may be assumed.

Tasks:
1. For every interval of 10-point Catmull control points (9 cubic segments), implement exact algebraic conversion to cubic Bézier: `P0=b`, `P1=b+(c-a)/6`, `P2=c-(d-b)/6`, `P3=c`, including duplicated endpoint controls.
2. Build the child's four Bézier controls from the same weighted combination and constant normal/lift as the old audit, with a numerically conservative AABB padded for float rounding and interpolation differences.
3. Implement midpoint de Casteljau subdivision into two cubics and child AABB computation; document bounding-box convex-hull containment. Expose small C functions with no SDF dependency and no global state.
4. Provide a mapping from original `q=1..72` / `t=q/72.f` to segments, including exact segment boundaries, `t=1`, and samples on shared borders. Optimized auditing must never miss an original sample.
5. Differential tests versus the exact `LongHairGuide_Evaluate`, including randomized guides, high curvature, repeated/degenerate controls, weights near 0 and 1, near-normal cancellation, negative lift, all segment endpoints, and dense random t values. Verify every sample lies in an expanded conservative box and detect invalid/nonfinite inputs.
6. Do not introduce hair rendering or density changes. Do not optimize curve evaluation by changing its numerical meaning; small numerical differences in the helper must be absorbed into conservative padding, not used to change visible interpolation.

Measure the helper's own cost and report whether subdividing excessively would cost more than 72 exact SDF queries.

## Completion / handoff

Deliver a standalone verified Bézier/AABB library, direct C test command, public API comments, benchmark of bounds/subdivision overhead, and integration recipe for Agent E. No edits to runtime integration files.


---

# Agent D — Deterministic hair worker scheduler

**Execution wave:** Wave 1 — can start immediately alongside A, B, C, J  
**Dependencies:** None; Agent F wires it after E is merged  
**Exclusive file ownership in this task:** New `include/LongHairScheduler.h`, `src/LongHairScheduler.c`, `tests/test_long_hair_scheduler.c`; optionally a separate design note. Do NOT modify `src/LongHair.c`, `src/MonsterVisualAsync.c`, `src/SDFSamplingPool.c` or shared Makefile yet.

## Shared non-negotiable contract

Repository: https://github.com/KillDaWill/monster_engine. Reviewed baseline commit: `bc83fd586d7b1b8edae881c65ee25e96536ac08f`. Before coding, inspect the **current** `main` HEAD and `AGENTS.md`; if HEAD has advanced, record the new SHA, review the diff and adapt. Do not reset main, force-push, or overwrite other agents' work. Start an independent `agent/<task-name>` branch or git worktree. Respect the file ownership assigned below. If a shared existing file has to change, submit an explicit integration patch or ask the coordinator rather than silently editing it.

Primary baseline: 288 visible guides, 9,216 independently rooted children, 857,381 SDF distance calls per rebuild (705,205 audit, 51,079 root/lift and 101,097 guide/collision), approximately 1.47 s per growth step, four reference stages and 21 morph steps. Target <=100 ms per synchronous rebuild (aspirational <=50 ms) **without weakening collision checks or visual identity**. These values are previous reported measurements, not new tests. The adult static GPU time (~2.85 ms) is not the bottleneck.

Preserve `LongHairSet_Build` transactional semantics, anatomical/stable IDs, deterministic seeds, original 72-point exact audit as a fallback, original `.0005f * scale` collision criterion, five retry opportunities, tubular fallback, `rootFullness` semantics, the CPU/GPU interpolation contract, and the current JSON recipe. The actual construction field is the UNION `min(body SDF, rest jaw field)`; a bound valid for the body alone is not proof for the union. The SDF is not universally proven to be 1-Lipschitz. `range == unknown/false` ALWAYS means exact fallback, never free space. Never silently lower strand density, hide roots, replace the SDF with a visual approximation, reduce the audit to 8 samples, or write benchmark output into the frozen reference directory.

Verification for integrated runtime changes (run on an otherwise idle machine for trustworthy timings):
```sh
make test
make editor-test
python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /tmp/long-hair-validation
git diff --check
```
For standalone helper branches not yet wired into the build, provide an isolated build/test command and integration instructions; full integration tests belong to the integration agent. Prefer ASan/TSan for concurrency and memory. Compare against the original reference capture under `docs/validation/long-hair-styling/resultado/`, without overwriting it. Report p50/p95/max wall time and SDF distance + interval query counts separately; a reduction in distance() calls is not a win if range queries are slower.

Deliver one focused commit/PR (or patch) with: changed files, rationale, correctness invariants, test commands/results, measured performance (or explicit 'not measured'), blockers and handoff notes. Do not claim a visual or performance result that was not actually tested.


## Your mission

Provide a safe reusable scheduling adapter for long-hair jobs. Review `SDFSamplingPool_ParallelFor` in `src/SDFSamplingPool.c`, its call-site usage in meshing and morph, and the fact that one pool instance holds one shared `currentWorkFn/currentContext/jobGeneration`. It is NOT reentrant. The caller participates in the work; auto thread count typically tops out at 4, not 8.

Tasks:
1. Implement a small explicitly owned scheduler wrapper, preferably around a **private** persistent `SDFSamplingPool` or a separately managed worker budget, which rejects/serializes concurrent calls instead of invoking `ParallelFor` concurrently on one pool. Never nest a call into the same scheduler and never create/destroy threads every age step.
2. Provide a deterministic `parallel_for(totalGuides,callback)` with fixed partitions, stable thread indices, valid 1-thread fallback, clear return semantics and a strong stop/shutdown lifetime contract. No worker may outlive its `MonsterSDF`, jaw snapshot, destination `LongHairSet`, or callback context.
3. Design three-phase execution with explicit barriers: guides/root placement (if eventually parallelized), collision/guide certificates, and follicles/neighbors reading completed guides. Actual wiring into `LongHair.c` is for Agent F, not you.
4. Report how CPU contention with existing mesher/morph workers is limited; benchmark thread counts 1/2/4/8 in a synthetic read-only workload without claiming real hair speedups. Never assume 8x speedup.
5. Add stress tests for repeated jobs, two callers, shutdown while idle/active, empty input, highly imbalanced workloads, determinism, failure handling, race detection (TSan when available), and nested-call rejection.
6. If you discover that safely handling concurrency requires changes to the existing global SDF pool, **do not edit it** in this branch: propose a minimal follow-up patch and document trade-offs.

Keep library renderer-independent; do not schedule OpenGL calls off render thread.

## Completion / handoff

Deliver testable scheduler API, ownership diagram, thread budget policy, no-deadlock/no-race evidence, tests and an integration note for Agent F. If a shared pool cannot be used safely, implement an isolated serial fallback rather than unsafe parallelism.


---

# Agent E — Certifying follicle audits end to end

**Execution wave:** Wave 2 — starts after A, B and C are merged  
**Dependencies:** A + B + C; D may continue in parallel  
**Exclusive file ownership in this task:** Integration owner for `src/LongHair.c`, `include/LongHair.h`, `src/MonsterVisual.c`, `Makefile` (and any small glue). Only Agent E edits these shared files during this wave. Reuse A/B/C modules unchanged wherever feasible.

## Shared non-negotiable contract

Repository: https://github.com/KillDaWill/monster_engine. Reviewed baseline commit: `bc83fd586d7b1b8edae881c65ee25e96536ac08f`. Before coding, inspect the **current** `main` HEAD and `AGENTS.md`; if HEAD has advanced, record the new SHA, review the diff and adapt. Do not reset main, force-push, or overwrite other agents' work. Start an independent `agent/<task-name>` branch or git worktree. Respect the file ownership assigned below. If a shared existing file has to change, submit an explicit integration patch or ask the coordinator rather than silently editing it.

Primary baseline: 288 visible guides, 9,216 independently rooted children, 857,381 SDF distance calls per rebuild (705,205 audit, 51,079 root/lift and 101,097 guide/collision), approximately 1.47 s per growth step, four reference stages and 21 morph steps. Target <=100 ms per synchronous rebuild (aspirational <=50 ms) **without weakening collision checks or visual identity**. These values are previous reported measurements, not new tests. The adult static GPU time (~2.85 ms) is not the bottleneck.

Preserve `LongHairSet_Build` transactional semantics, anatomical/stable IDs, deterministic seeds, original 72-point exact audit as a fallback, original `.0005f * scale` collision criterion, five retry opportunities, tubular fallback, `rootFullness` semantics, the CPU/GPU interpolation contract, and the current JSON recipe. The actual construction field is the UNION `min(body SDF, rest jaw field)`; a bound valid for the body alone is not proof for the union. The SDF is not universally proven to be 1-Lipschitz. `range == unknown/false` ALWAYS means exact fallback, never free space. Never silently lower strand density, hide roots, replace the SDF with a visual approximation, reduce the audit to 8 samples, or write benchmark output into the frozen reference directory.

Verification for integrated runtime changes (run on an otherwise idle machine for trustworthy timings):
```sh
make test
make editor-test
python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /tmp/long-hair-validation
git diff --check
```
For standalone helper branches not yet wired into the build, provide an isolated build/test command and integration instructions; full integration tests belong to the integration agent. Prefer ASan/TSan for concurrency and memory. Compare against the original reference capture under `docs/validation/long-hair-styling/resultado/`, without overwriting it. Report p50/p95/max wall time and SDF distance + interval query counts separately; a reduction in distance() calls is not a win if range queries are slower.

Deliver one focused commit/PR (or patch) with: changed files, rationale, correctness invariants, test commands/results, measured performance (or explicit 'not measured'), blockers and handoff notes. Do not claim a visual or performance result that was not actually tested.


## Your mission

You are the **single integration owner** of the main performance feature: skip expensive `distance()` calls only when conservatively proven safe. Start from an integration branch containing accepted A, B and C commits. Re-review changed code and `AGENTS.md` at current HEAD.

Tasks:
1. Wire the new `LongHairFieldQuery` / conservative interval provider through an additive `LongHairSet_BuildEx` (or equivalent), preserving old `LongHairSet_Build` and `MonsterVisual_UpdateLongHair` behavior with a legacy exact-distance wrapper. The real path must use the **body + jaw** rest field, never body alone.
2. Add a bounded classifier per child: form Bézier controls for each cubic Catmull segment, construct conservative AABB; call interval provider; if its lower bound is known and >= `.0005f*scale + safety_margin`, certify the **entire** segment. If unknown/near/expensive, use original point checks.
3. Preserve exact original `q=1..72` order and early-reject behavior for uncertified samples, with max 5 attempts and original lift/root projection, stable neighbor/weight selection and tubular fallback. Any fully certified segment skips its corresponding original sample calls. Keep a debug 'force exact audit' mode and an optional differential shadow mode.
4. Cap interval calls/subdivision depth **per fiber**. Record the raw total wall clock, cost of intervals, false/unknown returns, certified segments/samples, and any cases where range checks make performance worse. The optimization must not be enabled by default until coverage, correctness and net time improvement are demonstrated. For difficult fields, exact fallback must remain fast and correct.
5. Compare ALL 9,216 child descriptors with baseline across 4 age stages (neighbors, validity, weight, lift), guide geometry/stable IDs and whole GPU capture. For diagnostic stress, dense-sample certified segments and verify no sampled negative distances; this is a regression test, not formal proof of an approximate SDF's bound.
6. Run baseline and optimized mode on idle machine, report p50/p95/max; do not claim goal <=100ms if merely fewer distance calls but interval wall time explodes.
7. Do NOT alter shader, recipe, guide density, collision thresholds or sampling precision. Do not implement pooling, incremental age cache or async editor in this PR; those follow later.

If `getCellRange` is unsupported over the actual mane region and you get near-0% certification, **stop** and report this blocker, with evidence and a safe plan to expand interval coverage. Never fake a positive certificate.

## Completion / handoff

Commit a merged, benchmarked, regression-checked build with certification off/on modes, visual parity and a quantifiable SDF/range/time breakdown. Send Agent F the final APIs and precise files/functions used for guide and follicle phases.


---

# Agent F — Parallelize guide and follicle phases

**Execution wave:** Wave 3 — starts after D and E merge  
**Dependencies:** D + E  
**Exclusive file ownership in this task:** Runtime integration owner for `src/LongHair.c`, `include/LongHair.h`, `src/MonsterVisual.c`, build glue and relevant concurrency tests during this wave. Do not change `src/MonsterVisualAsync.c` for full async editor; Agent I owns that later.

## Shared non-negotiable contract

Repository: https://github.com/KillDaWill/monster_engine. Reviewed baseline commit: `bc83fd586d7b1b8edae881c65ee25e96536ac08f`. Before coding, inspect the **current** `main` HEAD and `AGENTS.md`; if HEAD has advanced, record the new SHA, review the diff and adapt. Do not reset main, force-push, or overwrite other agents' work. Start an independent `agent/<task-name>` branch or git worktree. Respect the file ownership assigned below. If a shared existing file has to change, submit an explicit integration patch or ask the coordinator rather than silently editing it.

Primary baseline: 288 visible guides, 9,216 independently rooted children, 857,381 SDF distance calls per rebuild (705,205 audit, 51,079 root/lift and 101,097 guide/collision), approximately 1.47 s per growth step, four reference stages and 21 morph steps. Target <=100 ms per synchronous rebuild (aspirational <=50 ms) **without weakening collision checks or visual identity**. These values are previous reported measurements, not new tests. The adult static GPU time (~2.85 ms) is not the bottleneck.

Preserve `LongHairSet_Build` transactional semantics, anatomical/stable IDs, deterministic seeds, original 72-point exact audit as a fallback, original `.0005f * scale` collision criterion, five retry opportunities, tubular fallback, `rootFullness` semantics, the CPU/GPU interpolation contract, and the current JSON recipe. The actual construction field is the UNION `min(body SDF, rest jaw field)`; a bound valid for the body alone is not proof for the union. The SDF is not universally proven to be 1-Lipschitz. `range == unknown/false` ALWAYS means exact fallback, never free space. Never silently lower strand density, hide roots, replace the SDF with a visual approximation, reduce the audit to 8 samples, or write benchmark output into the frozen reference directory.

Verification for integrated runtime changes (run on an otherwise idle machine for trustworthy timings):
```sh
make test
make editor-test
python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /tmp/long-hair-validation
git diff --check
```
For standalone helper branches not yet wired into the build, provide an isolated build/test command and integration instructions; full integration tests belong to the integration agent. Prefer ASan/TSan for concurrency and memory. Compare against the original reference capture under `docs/validation/long-hair-styling/resultado/`, without overwriting it. Report p50/p95/max wall time and SDF distance + interval query counts separately; a reduction in distance() calls is not a win if range queries are slower.

Deliver one focused commit/PR (or patch) with: changed files, rationale, correctness invariants, test commands/results, measured performance (or explicit 'not measured'), blockers and handoff notes. Do not claim a visual or performance result that was not actually tested.


## Your mission

Parallelize the existing long-hair construction with the accepted Agent D scheduler **without** altering geometrical decisions or replacing certification logic. Start from a branch containing Agent E's verified certification and Agent D's scheduler.

Tasks:
1. Identify independent stages in `LongHairSet_Build`: guide generation/root projection; `LongHairSet_Collide` (separate guide state only); neighbor selection + follicle projection/audit (read all guides, write only the current guide). Separate loops with explicit barriers. Verify the actual `LongHairSet` state and reads before splitting; beware guide array resizing and group index mapping.
2. Use fixed, contiguous guide partitions. No two workers may modify a given `LongHairGuide` or the destination pointer simultaneously. Per-worker contexts are read-only for SDF/jaw and hold thread-local diagnostic counters. Avoid contention on global `++call_count` and errors from non-thread-safe scratch buffers/TLS.
3. Keep allocator and `LongHairSet` transaction commit on the owning thread. The SDF and `LongHairJawField` must outlive all workers. Preserve error rollbacks and cancellation handling.
4. Provide serial fallback if pool allocation fails, nested dispatch is detected, the guide count is small or CPU is oversubscribed by meshing. Expose explicit `threadCount`/policy for benchmark (1,2,4,8) while default remains conservative and coordinated.
5. Test deterministic output across thread counts for all 4 horse stages and 21 growth steps (exact descriptors + roots + images). Stress two simultaneously updating creatures and mesher worker activity; ensure separate scheduler ownership / serialization per pool.
6. Measure p50/p95/max end-to-end time, not only total work/threads. Include actual threads used, contention, SDF calls, range calls, memory, TSan/ASan results when available. Never state 100ms is achieved unless p95 of actual build proves it.
7. Do not work on age-dependent cache or rendering async publishing. Those are separate future waves.

If parallelizing root projection is risky, parallelize collision and follicle phases first; retain a serial baseline and publish the reason.

## Completion / handoff

One integration PR with deterministic output, verified race-free behavior and controlled speedup. Provide Agent G with post-parallel bottleneck profile, and Agent H with stable runtime APIs for incremental reuse.


---

# Agent G — Shared pair certificates and guide base cost

**Execution wave:** Wave 4 — starts after F; can run alongside J profiling  
**Dependencies:** F, with E metrics showing which costs remain  
**Exclusive file ownership in this task:** Integration owner for `src/LongHair.c` and any new hair helper during this wave. Agent H/I must not simultaneously edit it. Keep modifications outside unrelated core systems.

## Shared non-negotiable contract

Repository: https://github.com/KillDaWill/monster_engine. Reviewed baseline commit: `bc83fd586d7b1b8edae881c65ee25e96536ac08f`. Before coding, inspect the **current** `main` HEAD and `AGENTS.md`; if HEAD has advanced, record the new SHA, review the diff and adapt. Do not reset main, force-push, or overwrite other agents' work. Start an independent `agent/<task-name>` branch or git worktree. Respect the file ownership assigned below. If a shared existing file has to change, submit an explicit integration patch or ask the coordinator rather than silently editing it.

Primary baseline: 288 visible guides, 9,216 independently rooted children, 857,381 SDF distance calls per rebuild (705,205 audit, 51,079 root/lift and 101,097 guide/collision), approximately 1.47 s per growth step, four reference stages and 21 morph steps. Target <=100 ms per synchronous rebuild (aspirational <=50 ms) **without weakening collision checks or visual identity**. These values are previous reported measurements, not new tests. The adult static GPU time (~2.85 ms) is not the bottleneck.

Preserve `LongHairSet_Build` transactional semantics, anatomical/stable IDs, deterministic seeds, original 72-point exact audit as a fallback, original `.0005f * scale` collision criterion, five retry opportunities, tubular fallback, `rootFullness` semantics, the CPU/GPU interpolation contract, and the current JSON recipe. The actual construction field is the UNION `min(body SDF, rest jaw field)`; a bound valid for the body alone is not proof for the union. The SDF is not universally proven to be 1-Lipschitz. `range == unknown/false` ALWAYS means exact fallback, never free space. Never silently lower strand density, hide roots, replace the SDF with a visual approximation, reduce the audit to 8 samples, or write benchmark output into the frozen reference directory.

Verification for integrated runtime changes (run on an otherwise idle machine for trustworthy timings):
```sh
make test
make editor-test
python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /tmp/long-hair-validation
git diff --check
```
For standalone helper branches not yet wired into the build, provide an isolated build/test command and integration instructions; full integration tests belong to the integration agent. Prefer ASan/TSan for concurrency and memory. Compare against the original reference capture under `docs/validation/long-hair-styling/resultado/`, without overwriting it. Report p50/p95/max wall time and SDF distance + interval query counts separately; a reduction in distance() calls is not a win if range queries are slower.

Deliver one focused commit/PR (or patch) with: changed files, rationale, correctness invariants, test commands/results, measured performance (or explicit 'not measured'), blockers and handoff notes. Do not claim a visual or performance result that was not actually tested.


## Your mission

Optimize the actual **remaining** cost after certified audits and parallelization. Do not implement speculative caches if profiling shows another bottleneck. Start from Agent F's merged, deterministic version.

Tasks:
1. Read fresh output from Agent A's benchmark. Quantify remaining guide/collision calls (~101,097 was original), range calls, root projection/lift and exact child audit. Set a realistic per-phase budget for <=100ms p95.
2. If per-child range queries dominate, implement **temporary per-build, per-guide-pair** conversion of shared Catmull->Bézier controls. Optionally test a conservative sweep AABB for subsets of child `weight/lift` values; normalize varying normals correctly or conservatively bound displacement by `maxAbsLift`. Only reuse when *the whole corridor* is proven outside the exact combined field, and scoped to a field generation. Otherwise use Agent E per-child method.
3. Preserve the seed, neighbor selection, child index, 5 attempts, root lift and descriptor generation; cache only proofs or shared math, not unverified acceptance results. No persistent 72 samples x 9,216 cache.
4. Profile guide PBD/polish/certificate separately; optimize duplicate calls to exactly the same (context,point), reuse local data safely, or prove block clearance with valid interval methods. Do not weaken contacts, PBD iterations, root attachment, or guide clearance without exact alternative validation.
5. Review `MonsterSDF_CompileConnectorCandidates` and `MonsterSDF_EvaluateDistanceCandidates` for a conservative local SDF query fast path, but avoid rewriting base SDF or omitting head/mouth/cavities. If anything would change exact results, leave as an experiment with a report rather than merging.
6. Preserve correct behavior for curves of high curvature, jaw nearby, morph stages, head cavities and fields returning UNKNOWN. Measure total ms of all caches and math; discard optimizations that slow down the build or add outsized memory.
7. Compare descriptors, roots and visual outputs against the exact reference and E/F baseline; run full suite and 1 vs N thread tests.

Aim for <=100ms p95 after the combined changes, but report not achieved truthfully if the hardware/field coverage won't permit it.

## Completion / handoff

Deliver measurable improvement (or a defensible no-go report), remaining cost breakdown, caches scoped to correct field generation, memory impact and exact regression evidence. Send Agent H the final stable guide/descriptor data model.


---

# Agent H — Incremental growth and dependency invalidation

**Execution wave:** Wave 5 — starts after G; agent may design privately earlier, but no runtime edits before G merge  
**Dependencies:** G + accepted correctness gates  
**Exclusive file ownership in this task:** Integration owner for `src/LongHair.c`, `src/MonsterVisual.c`, `include/LongHair.h`, new incremental cache helpers. Agent I must wait to edit visual async integration.

## Shared non-negotiable contract

Repository: https://github.com/KillDaWill/monster_engine. Reviewed baseline commit: `bc83fd586d7b1b8edae881c65ee25e96536ac08f`. Before coding, inspect the **current** `main` HEAD and `AGENTS.md`; if HEAD has advanced, record the new SHA, review the diff and adapt. Do not reset main, force-push, or overwrite other agents' work. Start an independent `agent/<task-name>` branch or git worktree. Respect the file ownership assigned below. If a shared existing file has to change, submit an explicit integration patch or ask the coordinator rather than silently editing it.

Primary baseline: 288 visible guides, 9,216 independently rooted children, 857,381 SDF distance calls per rebuild (705,205 audit, 51,079 root/lift and 101,097 guide/collision), approximately 1.47 s per growth step, four reference stages and 21 morph steps. Target <=100 ms per synchronous rebuild (aspirational <=50 ms) **without weakening collision checks or visual identity**. These values are previous reported measurements, not new tests. The adult static GPU time (~2.85 ms) is not the bottleneck.

Preserve `LongHairSet_Build` transactional semantics, anatomical/stable IDs, deterministic seeds, original 72-point exact audit as a fallback, original `.0005f * scale` collision criterion, five retry opportunities, tubular fallback, `rootFullness` semantics, the CPU/GPU interpolation contract, and the current JSON recipe. The actual construction field is the UNION `min(body SDF, rest jaw field)`; a bound valid for the body alone is not proof for the union. The SDF is not universally proven to be 1-Lipschitz. `range == unknown/false` ALWAYS means exact fallback, never free space. Never silently lower strand density, hide roots, replace the SDF with a visual approximation, reduce the audit to 8 samples, or write benchmark output into the frozen reference directory.

Verification for integrated runtime changes (run on an otherwise idle machine for trustworthy timings):
```sh
make test
make editor-test
python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /tmp/long-hair-validation
git diff --check
```
For standalone helper branches not yet wired into the build, provide an isolated build/test command and integration instructions; full integration tests belong to the integration agent. Prefer ASan/TSan for concurrency and memory. Compare against the original reference capture under `docs/validation/long-hair-styling/resultado/`, without overwriting it. Report p50/p95/max wall time and SDF distance + interval query counts separately; a reduction in distance() calls is not a win if range queries are slower.

Deliver one focused commit/PR (or patch) with: changed files, rationale, correctness invariants, test commands/results, measured performance (or explicit 'not measured'), blockers and handoff notes. Do not claim a visual or performance result that was not actually tested.


## Your mission

Implement true incremental rebuilding during monster growth without unsafe reuse of collision results. Start from Agent G's merged and regression-validated code, read how `MonsterVisual_UpdateLongHair` fingerprints group, anatomy graph, scale, rig, head and mouth.

Tasks:
1. Build an explicit dependency matrix that distinguishes (a) material/color/transparency/density changes, (b) local grooming/group geometry changes, (c) anatomy or jaw rest-shape change, (d) pose-only update, (e) topology/anchor/recipe changes. Audit *each* `LongHairGroup` field and shader use; `rootFullness` affects both roots and clearance, so it is not merely visual.
2. Add versioned per-guide/per-group cache keys based on creature instance, recipe/groom, stableRootId/groupId, anatomy generation, jaw/body field generation and pose. Document ownership/lifetimes, maximum memory and invalidation behavior. Do not reuse a valid descriptor against a changed field without reproving safety.
3. First implement safe fast paths: unchanged geometry -> pose-only; purely render-color/visibility changes -> update relevant packed attributes if provably independent from geometry; unchanged per-module data -> reuse only after conservative influence/dirty-region checking.
4. During age morph, recompute surface correspondence and roots where geometry genuinely changed. Revalidation of hair children must use the **current** combined SDF and its certified ranges; stale `neighbor/weight/lift` from prior age cannot silently replace the deterministic reference decision. For exact parity, fall back to full regeneration as needed.
5. Ensure cache writes and commit are transactional: on allocation failure/invalid state retain last fully valid runtime. Prevent dangling pointers to borrowed SDF/jaw context; handle delete/recreate, reverse age, recipe edit, open/close jaw and rapid nonmonotonic scrubbing.
6. Expose diagnostics: dirtyGuides, updatedGroups, skippedBuilds, reusedMath, revalidatedFollicles, cacheHit%, cacheBytes and p95, with new vs baseline digest comparisons on stages and random age jumps.
7. Do NOT implement editor background scheduling in this task. Agent I will integrate async publications later. Do not invent general mutable global cache when per-instance state is enough.

If no rigorous proof permits skipping a rebuild for a given change class, mark it dirty and use the original complete build. Prefer fewer correct cache hits to one stale hair result.

## Completion / handoff

Deliver an explicit invalidation table, testable incremental APIs, same visual/collision output as reference, memory ceiling, and a benchmark showing when it actually saves work. Hand off generation/commit semantics to Agent I.


---

# Agent I — Async age scrubbing and coherent presentation

**Execution wave:** Wave 6 — starts after H; J may continue separate global profile  
**Dependencies:** H + all prior integrated performance/correctness work  
**Exclusive file ownership in this task:** Integration owner for `src/MonsterVisualAsync.c`, `include/MonsterVisualAsync.h`, relevant new async hair job helper and tests. Only edit `src/MonsterVisual.c`/`src/LongHair.c` with coordinator approval and separate follow-up patch.

## Shared non-negotiable contract

Repository: https://github.com/KillDaWill/monster_engine. Reviewed baseline commit: `bc83fd586d7b1b8edae881c65ee25e96536ac08f`. Before coding, inspect the **current** `main` HEAD and `AGENTS.md`; if HEAD has advanced, record the new SHA, review the diff and adapt. Do not reset main, force-push, or overwrite other agents' work. Start an independent `agent/<task-name>` branch or git worktree. Respect the file ownership assigned below. If a shared existing file has to change, submit an explicit integration patch or ask the coordinator rather than silently editing it.

Primary baseline: 288 visible guides, 9,216 independently rooted children, 857,381 SDF distance calls per rebuild (705,205 audit, 51,079 root/lift and 101,097 guide/collision), approximately 1.47 s per growth step, four reference stages and 21 morph steps. Target <=100 ms per synchronous rebuild (aspirational <=50 ms) **without weakening collision checks or visual identity**. These values are previous reported measurements, not new tests. The adult static GPU time (~2.85 ms) is not the bottleneck.

Preserve `LongHairSet_Build` transactional semantics, anatomical/stable IDs, deterministic seeds, original 72-point exact audit as a fallback, original `.0005f * scale` collision criterion, five retry opportunities, tubular fallback, `rootFullness` semantics, the CPU/GPU interpolation contract, and the current JSON recipe. The actual construction field is the UNION `min(body SDF, rest jaw field)`; a bound valid for the body alone is not proof for the union. The SDF is not universally proven to be 1-Lipschitz. `range == unknown/false` ALWAYS means exact fallback, never free space. Never silently lower strand density, hide roots, replace the SDF with a visual approximation, reduce the audit to 8 samples, or write benchmark output into the frozen reference directory.

Verification for integrated runtime changes (run on an otherwise idle machine for trustworthy timings):
```sh
make test
make editor-test
python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /tmp/long-hair-validation
git diff --check
```
For standalone helper branches not yet wired into the build, provide an isolated build/test command and integration instructions; full integration tests belong to the integration agent. Prefer ASan/TSan for concurrency and memory. Compare against the original reference capture under `docs/validation/long-hair-styling/resultado/`, without overwriting it. Report p50/p95/max wall time and SDF distance + interval query counts separately; a reduction in distance() calls is not a win if range queries are slower.

Deliver one focused commit/PR (or patch) with: changed files, rationale, correctness invariants, test commands/results, measured performance (or explicit 'not measured'), blockers and handoff notes. Do not claim a visual or performance result that was not actually tested.


## Your mission

Make growth/scrubbing non-blocking **without ever showing hair from one age penetrating the body from another**. The current code calls `MonsterVisual_UpdateLongHair(&asyncMgr->longHair,appearance)` synchronously at entry to `MonsterVisualAsync_UpdateWithAppearance` before the existing async mesh pipeline. Start from Agent H's generation-aware incremental build.

Tasks:
1. Design a `LongHairBuildJob` or equivalent with immutable age/anatomy/head/jaw snapshots, result-owned temporary `LongHairSet`, request/geometry generation IDs, cancellation/stale-result flags and clear lifecycle. It must use a safe scheduler policy coordinated with mesher workers; no nested calls on `SDFSamplingPool` and no detached borrowed contexts.
2. Remove the synchronous long-hair rebuild from the presentation hot path by queueing/coalescing the **latest** requested age, keeping at most a bounded number of active/pending jobs per creature. A worker must finish safely before its snapshot can be freed.
3. Publish only a coherent bundle of body display geometry + long-hair belonging to the **same accepted geometry/age generation**. If the new bundle is not ready, keep the last complete bundle or a clearly marked pending mode that cannot masquerade as the new correct age. Never cross-mix new body with old hair; never publish obsolete results after newer requests.
4. Keep all OpenGL state/texture-buffer updates on the owning graphics thread. The worker only builds CPU-side data. Commit must be atomic with respect to rendering, with lock scopes minimized and without holding a mutex during expensive SDF work.
5. Handle fast age jumps, morph reverse, change of species recipe, creature deletion, application shutdown, worker failure, scene with multiple long-haired creatures, jaw animation and camera scrubbing. Ensure cancellation is cooperative and no use-after-free/leaks. Never hide entire mane as a performance shortcut.
6. Add diagnostics for queue depth, generation discarded, produced/accepted age, end-to-end presentation latency, main-thread update p95, actual build p95 and frame time. Compare debug mode synchronous vs async final results and exact collision/visual captures.
7. Run sanitizer stress tests, all hair validation stages, 21-step growth, 1/N threads and editor tests. Distinguish frame responsiveness improvement from actual reduction of SDF work.

If the existing body display can publish independently and there is no safe generation barrier, implement the barrier before claiming the task finished.

## Completion / handoff

Deliver complete nonblocking, generation-consistent rendering with failure safe behavior, bounded memory, stale-job tests and measured main-thread stall/presentation latency. Document user-visible behavior while a requested age is pending.


---

# Agent J — Cross-species construction profiling

**Execution wave:** Wave 1 — can start independently; continue in parallel with E/F/G/H/I if avoiding overlapping performance runs  
**Dependencies:** None; general-engine improvements should be proposed after validated hair path  
**Exclusive file ownership in this task:** New `benchmarks/benchmark_creature_construction.c`, `docs/performance/creature-generation-agent-j.md`, optionally new `tools/benchmark_creatures.py`. Do not edit `src/LongHair.c`, `src/MonsterVisual.c`, `src/MonsterVisualAsync.c`, `src/MonsterSDF.c`, `MorphSpatialCache.c` or shared Makefile in this task.

## Shared non-negotiable contract

Repository: https://github.com/KillDaWill/monster_engine. Reviewed baseline commit: `bc83fd586d7b1b8edae881c65ee25e96536ac08f`. Before coding, inspect the **current** `main` HEAD and `AGENTS.md`; if HEAD has advanced, record the new SHA, review the diff and adapt. Do not reset main, force-push, or overwrite other agents' work. Start an independent `agent/<task-name>` branch or git worktree. Respect the file ownership assigned below. If a shared existing file has to change, submit an explicit integration patch or ask the coordinator rather than silently editing it.

Primary baseline: 288 visible guides, 9,216 independently rooted children, 857,381 SDF distance calls per rebuild (705,205 audit, 51,079 root/lift and 101,097 guide/collision), approximately 1.47 s per growth step, four reference stages and 21 morph steps. Target <=100 ms per synchronous rebuild (aspirational <=50 ms) **without weakening collision checks or visual identity**. These values are previous reported measurements, not new tests. The adult static GPU time (~2.85 ms) is not the bottleneck.

Preserve `LongHairSet_Build` transactional semantics, anatomical/stable IDs, deterministic seeds, original 72-point exact audit as a fallback, original `.0005f * scale` collision criterion, five retry opportunities, tubular fallback, `rootFullness` semantics, the CPU/GPU interpolation contract, and the current JSON recipe. The actual construction field is the UNION `min(body SDF, rest jaw field)`; a bound valid for the body alone is not proof for the union. The SDF is not universally proven to be 1-Lipschitz. `range == unknown/false` ALWAYS means exact fallback, never free space. Never silently lower strand density, hide roots, replace the SDF with a visual approximation, reduce the audit to 8 samples, or write benchmark output into the frozen reference directory.

Verification for integrated runtime changes (run on an otherwise idle machine for trustworthy timings):
```sh
make test
make editor-test
python3 tools/validate_long_hair.py --rooted-mane --all-stages --case caballo-curve --output /tmp/long-hair-validation
git diff --check
```
For standalone helper branches not yet wired into the build, provide an isolated build/test command and integration instructions; full integration tests belong to the integration agent. Prefer ASan/TSan for concurrency and memory. Compare against the original reference capture under `docs/validation/long-hair-styling/resultado/`, without overwriting it. Report p50/p95/max wall time and SDF distance + interval query counts separately; a reduction in distance() calls is not a win if range queries are slower.

Deliver one focused commit/PR (or patch) with: changed files, rationale, correctness invariants, test commands/results, measured performance (or explicit 'not measured'), blockers and handoff notes. Do not claim a visual or performance result that was not actually tested.


## Your mission

Investigate optimization of **creature creation in general**, separately from the horse mane. This is a measurement and architecture-recommendation task. Read `AGENTS.md`, `CreatureRecipe.c`, phenotype/anatomy construction, `MonsterSDF_Build`, SDF sampling, meshing, `MorphSpatialCache`, `MorphChunkManager` and relevant rendering/generation code.

Tasks:
1. Find actual included recipes/fixtures for horse, dog, lizard and brumacerno/bovine; do not assume file names. For at least one valid fixture per available species, measure recipe parse, phenotype synthesis, anatomy, SDF build, exact distance sampling, mesh generation, surface mapping, long/short fur, pose, end-to-end creation and a representative growth update. Record when a phase is not present.
2. Compare cold start, steady rebuild, pose-only, color-only and age variation where supported. Include memory (CPU and GPU where available), p50/p95 and number of SDF queries/connector candidates, vs rendering frame time. Avoid mislabeling horse hair baseline as a general creature bottleneck.
3. Profile repeated SDF queries by local spatial region and examine `MonsterSDF_CompileConnectorCandidates`, `MonsterSDF_EvaluateDistanceCandidates` and region/brick dirty flags already used by morph. Identify low-risk reuse patterns with proof requirements for smooth unions and cutters; no unsafe AABB pruning.
4. Map a future shared dependency graph (recipe->phenotype->anatomy->SDF->meshing->surface/hair->pose/render) and suggest invalidation keys per change class; distinguish reusable primitives from hair-specific caches.
5. Run controlled benchmarks **sequentially**, not concurrently with Agent A's timings or visual captures, on the same machine if possible. If hardware isn't available, create the reproducible harness and explicitly label performance unmeasured.
6. Present prioritized opportunities with estimated implementation effort, uncertainty, proof burden and likely owners; make no runtime modifications without future authorization/coordinator merge plan.

This work is deliberately isolated so that SDF/meshing generalization can be prioritized by evidence rather than used as an unproven way to speed horse hair.

## Completion / handoff

Deliver a species-by-phase performance matrix and a scoped follow-up plan. It must state which optimizations are supported by direct measurement vs inferred from code. Propose new independent agent tasks only after the data supports them.


---

