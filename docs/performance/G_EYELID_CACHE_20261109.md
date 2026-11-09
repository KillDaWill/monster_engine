# G3 per-mouth eyelid descriptor cache — implementation handoff

**Status: isolated prototype complete; NO-GO for default integration or merge. Ready for coordinator GPU review on this separate branch.** Branch `agent/perf-20261109-g-eyelid-cache`, based on F `b799f269e980a358161f880c5ed32c361da91943`; final source commit `7e5f944ef6b830004561391417ca540c040643ed`. G1 cache and G2 profile history remain excluded. No growth matrix was rerun for this hardening pass.

## Contract and implementation

- `MonsterSDFMouth` has an inline, derived cache of 16 ordered `(a,b,radius)` eyelid capsule descriptors and a dependency snapshot/validity bit. No pointers, heap allocation, recipe serialization, GPU layout, or API signature changes. The original capsule formulas, `SDF_Capsule`, smooth-union order, and all 16 descriptor queries remain unchanged.
- Cache eligibility is restricted to GNU/Clang x86-64 SSE2, `FLT_EVAL_METHOD == 0`, binary32 float characteristics, nearest rounding, masked exceptions, disabled MXCSR FTZ/DAZ, and default x87 precision-control bits. Unsupported compiler/FP modes use the original evaluator. `-mfpmath=387` reports `FLT_EVAL_METHOD==2` and therefore disables cache compilation/use.
- The generator enforces normalized original/cached upper-head body parity except the intended cached call and exact fallback prologue; its synthetic formula-drift self-test must fail. It also guards the cached loop for exactly one `SDF_Capsule` per descriptor and no `continue`/`break`. Generated GLSL/header outputs remain byte-identical.
- **ABI:** `sizeof(MonsterSDFMouth)` measured 1608 bytes on F and 2136 bytes on G3 (+528 bytes per mouth). Full rebuild is required; no binary-compatibility claim. Cache fields are inline and have no separate ownership/lifetime.

## Correctness and verification

- `make test` — PASS on final source; full native C suite.
- `make editor-test` — PASS on final source.
- `python3 tools/generate_sdf_shader.py`; `git diff --exit-code -- src/MonsterSDFShader.generated.h src/MonsterSDFCompute.generated.h src/MonsterSurfaceShader.generated.h` — PASS; generated GPU output unchanged.
- `python3 tests/test_long_hair_validation_helpers.py` and `python3 tests/test_benchmark_long_hair_parallel.py` — PASS.
- `make test-long-hair-failures test-long-hair-mesh-oversubscription` — PASS (linker-wrapped allocation/thread-creation fault injection and actual mesh oversubscription).
- Focused eyelid suite built with `-mfpmath=387` — PASS, including the unsupported-backend fallback test. The complete `-mfpmath=387 make test` stopped earlier at the unrelated `AppendageMeshContinuity` LOD assertion; this is not claimed as a full-suite pass.
- Frozen-source long-hair oracle on the hardened working-tree contents — PASS, all 116 scenarios. Manifest: `/mnt/storage/monster-engine-agents/scratch/g-eyelid-cache/long-hair-oracle-hardening-20261109/manifest.json`, binary SHA-256 `f1476da02f4fa88500f03a153fa3b04dfbec1754a69e66579fc281ffe1c78524`.
- Full-core adult horse body+jaw ASan/UBSan and TSAN — PASS at 1/4/8 workers, two independent owners. Artifacts/manifests: `.../fullcore-asan-hardening-v2/` and `.../fullcore-tsan-nopie-hardening/` under `/mnt/storage/monster-engine-agents/scratch/g-eyelid-cache/`. TSAN used `-fno-pie` and `-no-pie`; this is a non-PIE pass, not a PIE pass. The instrumented source contents match final commit `7e5f944`; manifests were produced before that source was committed.
- The prior 335-row growth correctness matrix is retained as prior-revision evidence only; it was not rerun. Its row-by-row hashes and parity details remain in the preceding handoff history and original scratch artifacts. Prior f6 performance evidence below is likewise valid only for f6, not this hardened revision.
- Historical decision trail is preserved: G1 remains the coordinator-accepted NO-GO; G2 remains diagnostic-only profiling, not a speedup claim. The coordinator ledger records G2's three diagnostic runs (857381 body / 21046 jaw / 836335 jaw-AABB early-outs; 41,154,288 capsule calls, 16 per query; gprof perturbed runtime by about 3x). See `/mnt/storage/monster-engine-agents/recovery-20261108/integration/docs/performance/RECOVERY_COORDINATION_20261108.md` (entry 19) and `/mnt/storage/monster-engine-agents/recovery-20261108/g-field-profile/docs/performance/G_FIELD_PROFILE_20261109.md`; neither G1 nor G2 artifacts are integrated or overwritten here.
- The earlier 335-row matrix remains the old revision's correctness evidence: F `baef5159edd2cc354214a5125120f004a3f5c684767e44c9b32acbe919a258b2`; G3 1/2/4/8 workers `bb8d0048d999795e4f25d2a06ed1fc2df06ac6ba0125482aac41b313838fe17f`, `3fb5bbe18a7a8c17929797458a888644a8338c5838dd56eab61a3af300dce999`, `81b345419401a1727f8c2fd9a3463bced17e048325ad31a29ce821c91b81a126`, `e88ac72c031f7e33eaa734c4862c4d8cc3020f96ff4eb2c7c0f643b1de2e1a`. Do not treat any supplemental continuation timings as controlled performance evidence.

## Adult stage-3 diagnostic (separate from performance)

From `benchmark_long_hair_build` on final commit `7e5f944` (288 guides; certification was unsupported/unacknowledged):

| Counter | Result |
|---|---:|
| Distance/body evaluations | 857381 |
| Jaw snapshot evaluations | 21046 |
| Jaw AABB early-outs | 836335 |
| Jaw disabled-scale calls | 0 |
| Cached eyelid capsule-query skips | 0 by source/generator audit, **not a runtime counter** |

The zero-skip audit means the guarded cached loop has no branch that can skip any of its 16 capsule calls when that path is used. It is not inferred from the jaw AABB early-outs; those are a separate jaw-field optimization/count. The adult diagnostic JSON is `/mnt/storage/monster-engine-agents/scratch/g-eyelid-cache/adult-profile-hardening-7e5f944.json`.

## Fresh matched uninstrumented ABBA on final source

Protocol: exclusive slot command `timeout --kill-after=60s 1800s /mnt/storage/monster-engine-agents/run-bench.sh python3 /tmp/opencode/g3-abba-hardening.py`; GCC 13.3.0; `-Wall -Wextra -Wpedantic -O2 -Iinclude -Itests -lm -pthread`; same recipe and benchmark source; 3 warmups + 9 measured samples per stage per invocation; separately at 1 and 8 workers in order F-G3-G3-F. Each invocation emitted 36 matched data rows. All four F/G3 pairs passed exact matching of stage, rep, step, age, guide/visible/child counts, stable-ID digest, and packed-output digest. **No pooling across invocations or workers:** the table reports all 32 `(worker, invocation, stage)` groups. p95 is nearest-rank for n=9 (therefore the sample maximum); max is also listed explicitly.

| Workers | Invocation | Arm | Stage | p50 ms | p95 ms | Max ms |
|---:|---:|:---:|---:|---:|---:|---:|
| 1 | 1 | F | 0 | 1599.450 | 1602.831 | 1602.831 |
| 1 | 1 | F | 1 | 1584.422 | 1585.930 | 1585.930 |
| 1 | 1 | F | 2 | 1565.357 | 1571.971 | 1571.971 |
| 1 | 1 | F | 3 | 1558.469 | 1560.330 | 1560.330 |
| 1 | 2 | G3 | 0 | 1198.483 | 1200.345 | 1200.345 |
| 1 | 2 | G3 | 1 | 1189.668 | 1195.393 | 1195.393 |
| 1 | 2 | G3 | 2 | 1186.654 | 1206.075 | 1206.075 |
| 1 | 2 | G3 | 3 | 1177.176 | 1187.606 | 1187.606 |
| 1 | 3 | G3 | 0 | 1196.537 | 1199.078 | 1199.078 |
| 1 | 3 | G3 | 1 | 1186.820 | 1188.365 | 1188.365 |
| 1 | 3 | G3 | 2 | 1177.014 | 1189.733 | 1189.733 |
| 1 | 3 | G3 | 3 | 1174.115 | 1184.018 | 1184.018 |
| 1 | 4 | F | 0 | 1415.756 | 1421.464 | 1421.464 |
| 1 | 4 | F | 1 | 1399.213 | 1404.174 | 1404.174 |
| 1 | 4 | F | 2 | 1400.791 | 1403.010 | 1403.010 |
| 1 | 4 | F | 3 | 1383.654 | 1389.374 | 1389.374 |
| 8 | 1 | F | 0 | 270.974 | 276.168 | 276.168 |
| 8 | 1 | F | 1 | 271.287 | 276.351 | 276.351 |
| 8 | 1 | F | 2 | 271.662 | 277.997 | 277.997 |
| 8 | 1 | F | 3 | 271.334 | 273.177 | 273.177 |
| 8 | 2 | G3 | 0 | 242.063 | 248.020 | 248.020 |
| 8 | 2 | G3 | 1 | 241.915 | 246.710 | 246.710 |
| 8 | 2 | G3 | 2 | 244.976 | 247.119 | 247.119 |
| 8 | 2 | G3 | 3 | 241.907 | 244.894 | 244.894 |
| 8 | 3 | G3 | 0 | 245.118 | 247.603 | 247.603 |
| 8 | 3 | G3 | 1 | 242.613 | 244.993 | 244.993 |
| 8 | 3 | G3 | 2 | 239.445 | 248.187 | 248.187 |
| 8 | 3 | G3 | 3 | 241.104 | 244.248 | 244.248 |
| 8 | 4 | F | 0 | 276.524 | 284.631 | 284.631 |
| 8 | 4 | F | 1 | 271.531 | 274.391 | 274.391 |
| 8 | 4 | F | 2 | 271.346 | 274.537 | 274.537 |
| 8 | 4 | F | 3 | 272.606 | 275.888 | 275.888 |

Raw CSVs, binaries, per-run stderr, and manifest are in `/mnt/storage/monster-engine-agents/scratch/g-eyelid-cache/abba-hardening-7e5f944/`. Manifest SHA-256: `442dcd7b0cd3da926c191a46b0aea2bcc3be6ad80c981d03946dbe7317256b09`. F/G3 benchmark binary SHA-256: `e5afce856f8ec34f178a647d06b6776706e7dd071d2c8521a32c32628565c49c` / `9be31031dc925cfc747ba13dae0bf39c3060771274ed3f9fec86bb5935a51013`. Source SHA-256: F `MonsterSDF.c` `4887d7431670c867582806b3884b536ebc8ff4bda36ed653334f674a984a0f0c`; hardened G3 `MonsterSDF.c` `818bdd742c0d4f435f32cfce4af791aaed636aab165a47a70f18c84c62d39b54`; `MonsterSDF.h` `72f0981bac1fd1a713e43b0948777e3ddc9cfe66c08ae273c0adc446c7fb63e4`; generator `tools/generate_sdf_shader.py` `3b6ecebd15ccc9faa192871bbec4a3e839cb1df4f9e5a96ce187847f205aff6b`; validator `tools/validate_long_hair_parallel.py` `754af994122e738dc0d4f0afa9a3b0f529dfd21ce3c5bb9ada983999ced1f65b`; test `tests/test_eyelid_cache.c` `0acccc631c841ff68255875c63ca15d70cc317ffa0d945fde89a3b6e5fdac3fb`.

Historical f6 ABBA (`f6f9339`) reported 1-worker median reductions of 16.6–17.4% and 8-worker reductions of 10.6–12.9%, but its source/hash differs and its pooled n=18 tail summaries are descriptive only. Do not combine it with or substitute it for the final-source n=9-per-invocation table above. No performance claim is made from the growth matrix or G2 profile.

## Decision and handoff

**GO:** isolated prototype is ready for coordinator GPU review. **NO-GO:** default integration, merge, or binary compatibility. Retain the branch/worktree separation and public-struct ABI caveat; coordinator decides adoption after GPU review. Final source commit: `7e5f944ef6b830004561391417ca540c040643ed`. Final ABBA batch released the exclusive slot at `2026-11-09T19:07:00+01:00`; no owned benchmark/test process remains active. The report commit records this completed handoff.
