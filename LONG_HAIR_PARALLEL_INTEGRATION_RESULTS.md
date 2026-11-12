# Long-Hair Parallel Integration Results

Date: 2026-11-12
Repository: <https://github.com/KillDaWill/monster_engine>
Base `main`: `34a9f2d367fd0c93cae004657cdf4cce7aa2752c`
Integration branch: `integrate/long-hair-f-g3-20261112`
Integration worktree: `/mnt/storage/monster-engine-agents/integration-20261112/long-hair-f-g3`
Implementation integration head: `24a3b82` (the report commit follows this commit)
Final `main` tip: the commit containing this report

## 1. Recovery provenance

The original work was recovered from existing historical worktrees, not rebuilt from a design document.

- Historical root: `/mnt/storage/monster-engine-agents/recovery-20261108/`
- Agent F worktree: `f-exact`, branch `agent/perf-20261108-f-exact`, tip `c9fc96e`
- Agent F commits actually inspected and recovered:
  - `a9c57ca` — persistent long-hair scheduler, exact parallel builder, recovery harnesses, tests
  - `1ef6fe2` — opt-in scheduler path in the runtime
  - `dbd0603` — exact parallel long-hair construction
  - `94bf443` — recovery-gate tests and validation tools
  - `c9fc96e` — helper cleanup
- Agent F merge/reference commits: `c98fe5b`, docs `8a407bf`
- Agent G3 accepted merge commit: `30dd3475b260798f0b2c19c58894c5fde8904af8`
  - Parents: `d08bfbe`, `4e8008d`
  - Applied with `git cherry-pick -m 1 30dd3475b260798f0b2c19c58894c5fde8904af8`

The integration branch is based directly on `34a9f2d`. The F commits applied cleanly in order, followed by the G3 merge diff. No unrelated historical commits were cherry-picked.

## 2. Integration commits and files

Integration commits:

1. `99b5c36` — recovered reviewed harnesses and exact hair baseline tooling
2. `45ae25f` — opt-in exact long-hair scheduler path
3. `35cdb10` — exact opt-in parallel long-hair build
4. `b84bb7e` — long-hair parallel recovery gates
5. `fad7657` — helper process cleanup hardening
6. `24a3b82` — exact eyelid cache after GPU and growth gates

Diff against `main`: 40 files, +9536/−28.

Runtime files changed:

- `Makefile` — `LongHairScheduler.c` in `CORE_SRCS`; new tests and validation targets
- `include/LongHair.h` — parallel/profiled build API and build statistics
- `include/LongHairScheduler.h` — persistent worker scheduler API
- `src/LongHair.c` — exact parallel guide/follicle phases with phase barriers
- `src/LongHairScheduler.c` — persistent worker scheduler over `SDFSamplingPool`
- `include/MonsterSDF.h` — exact eyelid-capsule cache fields
- `src/MonsterSDF.c` — 16 precomputed eyelid capsule descriptors per mouth, exact fallback
- `src/MonsterVisual.c` — runtime selection of the parallel builder
- `tests/main_test.c` — scheduler, concurrency, eyelid-cache suites
- `tests/test_long_hair_scheduler.c`, `tests/test_long_hair_concurrency.c`, `tests/test_eyelid_cache.c`
- `tools/generate_sdf_shader.py` — generator parity guards for the G3 cache
- Recovered benchmarks, frozen-oracle helpers, validation tools, and provenance docs

No generated shader header was hand-edited or changed by the integration.

## 3. Runtime behavior

`MonsterVisual_UpdateLongHair()` reads `MONSTER_LONG_HAIR_THREADS`.

- Accepted values: `1`, `2`, `4`, `8`
- Default: `1` (original serial path)
- Invalid or absent values fall back to `1`
- The scheduler is persistent and its worker count is fixed on first hair construction in a process
- Changing `MONSTER_LONG_HAIR_THREADS` after the first build does not reconfigure live workers; a mismatch falls back to serial
- The parallel builder is called only when the requested count is greater than 1 and the scheduler thread count matches
- The serial builder remains the correctness reference and fallback

The G3 cache coexists with the newer anatomical-index reuse in `MonsterSDF_Build`. G3 changes only the repeated eyelid-capsule descriptor computation; it does not change SDF equations, thresholds, query counts, materials, or colors. The cache key is bitwise and the exact fallback is retained.

## 4. Correctness evidence

### Tests

- `make test -j4`: passed; direct `./run_tests` exited 0 with the final success banner
- `make editor-test`: passed all 10 editor tests
- New eyelid-cache suite: 4/4 passed, including five-species/four-stage parity
- New scheduler suite: 12/12 passed
- Long-hair concurrency and failure-injection suites passed
- No generated-header drift

### Official long-hair validation

Command:

```sh
python3 tools/validate_long_hair.py \
  --rooted-mane --all-stages --case caballo-curve \
  --output /mnt/storage/monster-engine-agents/integration-20261112/long-hair-validation
```

Result: exit 0 for all four stages.

- 288 roots, 288 visible guides
- 9,216 individually rooted children
- 25,920 rest collision samples, 0 penetrating
- GPU guide parity 1, maximum error approximately `9.83e-7`
- Renderer: 288 guides, 9,216 strands
- GPU: NVIDIA GeForce RTX 3050 Laptop GPU

### Frozen-source parallel oracle

Command:

```sh
make validate-long-hair-parallel-oracle \
  LONG_HAIR_ORACLE_OUT=/mnt/storage/monster-engine-agents/integration-20261112/long-hair-oracle
```

Result: exit 0; 116 frozen-source differential scenarios passed; manifest result `PASS`; `repo_head=24a3b82`.

### Serial vs parallel visual equivalence

The same generated `caballo-curve.creature.json`, stage 3, camera path, SDF configuration, and quality settings were captured with:

- `MONSTER_LONG_HAIR_THREADS=1`
- `MONSTER_LONG_HAIR_THREADS=4`
- `MONSTER_LONG_HAIR_THREADS=8`

All 20 PPM images per run were byte-identical across the three thread counts. The complete `roots.json` files were byte-identical, and every root descriptor field matched: stable ID, group, module, from/to node, parameter, visibility, rest position, and tip. All three runs produced 288 guides, 9,216 children, 25,920 collision samples with 0 penetrations, and identical GPU parity.

### Ager growth, forward and reverse

The recovered growth benchmark ran a 21-step Ager cycle in forward, reverse, and random order:

- `MONSTER_LONG_HAIR_THREADS=1`: forward/reverse/random medians 1208.8 / 1201.0 / 1196.3 ms
- `MONSTER_LONG_HAIR_THREADS=8`: forward/reverse/random medians 254.9 / 254.8 / 256.3 ms
- Stable-ID hashes were identical at every age and path
- Packed GPU hashes matched exactly between 1 and 8 threads for the same mode/step
- No thread-related failures or geometry changes

### SDF evaluation count

The recovered CPU profiler was compiled against the integrated code and run on the same stage-3 horse recipe (3 samples):

- Combined distance-callback calls: `857,381` per sample
- Body evaluations: `857,381`
- Jaw snapshot evaluations: `21,046`
- Jaw AABB early-outs: `836,335`
- Component evaluations: `878,427` (857,381 body + 21,046 jaw)
- Canonical descriptor digest: `caffa9480bf7f4a8` in every sample
- 288 guides, 9,216 valid follicle descriptors

The count is deterministic and unchanged from the recovered baseline. G3 reduces repeated internal eyelid-capsule work without reducing the number of SDF queries or changing exactness.

## 5. Performance

### Adult horse reconstruction

Generated `caballo-curve` recipe, stage 3, 3 warmups + 7 samples, CPU wall time:

| Threads | Median hair build | Median process CPU | CPU utilization | Speedup vs serial |
|---:|---:|---:|---:|---:|
| 1 | 1193.196 ms | 1191.192 ms | 0.998 cores | 1.00× |
| 2 | 627.485 ms | 1218.915 ms | 1.943 cores | 1.90× |
| 4 | 336.935 ms | 1268.651 ms | 3.765 cores | 3.54× |
| 8 | 242.640 ms | 1540.673 ms | 6.350 cores | 4.92× |

The historical approximately 242 ms figure was reproduced at 8 threads (242.6 ms).

### Capture: construction, main-thread blocking, and GPU render

Stage 3, 900×900, RTX 3050:

| Threads | `root_build_ms` | 21-step `morph_hair_update_ms` | Quality-3 frame | Hair GPU |
|---:|---:|---:|---:|---:|
| 1 | 1191.236 | 1196.551 | 4.737823 ms | 0.840021 ms |
| 4 | 351.286 | 337.963 | 4.718774 ms | 0.834901 ms |
| 8 | 242.868 | 244.906 | 4.722582 ms | 0.832614 ms |

`root_build_ms` and `morph_hair_update_ms` are synchronous calls on the main thread; the scheduler parallelizes work inside them. Frame time and hair GPU time are rendering costs and are reported separately from CPU construction.

### GPU actually used

- NVIDIA GeForce RTX 3050 Laptop GPU/PCIe/SSE2
- Driver 580.178.04, OpenGL 4.6.0
- PCI `0000:01:00.0`
- `make run-morph GPU=0000:01:00.0` verified with `[GPU actual]` and `[GPU verificación]` reporting the dedicated RTX 3050 context

The real demo launch was tested for 30 frames with `MONSTER_LONG_HAIR_THREADS=8`; it exited 0 and reported the correct GPU. The dog-to-lizard demo is not a horse-mane benchmark, so the horse long-hair GPU numbers above come from `build/capture_long_hair`.

## 6. Launch commands

Serial (safe default):

```sh
make run-morph GPU=0000:01:00.0 ARGS='--view=3 --realtime'
```

Four hair workers:

```sh
MONSTER_LONG_HAIR_THREADS=4 make run-morph \
  GPU=0000:01:00.0 \
  ARGS='--view=3 --realtime'
```

Eight hair workers:

```sh
MONSTER_LONG_HAIR_THREADS=8 make run-morph \
  GPU=0000:01:00.0 \
  ARGS='--view=3 --realtime'
```

The environment variable is process-wide and is read when the first long-hair build occurs. Restart the process to change the worker count. The default of one thread avoids CPU oversubscription when meshing, animation, and rendering are active; users opt in explicitly.

Validation/capture example:

```sh
MONSTER_LONG_HAIR_THREADS=8 ./build/capture_long_hair \
  /path/to/caballo-curve.creature.json /tmp/long-hair-t8 3
```

## 7. Merge and push status

The integration branch was fast-forwarded into `main` after all checks above, without force-pushing or touching the user's uncommitted worktree changes. `origin/main` was updated to the report commit.

## 8. Remaining issues

- Worker count is fixed at first long-hair construction per process; there is no live resizing.
- The default is serial; users must select 4 or 8 threads explicitly.
- The SDF evaluation count is a CPU callback count from the recovered profiler, not a GPU counter.
- The complete dog-to-lizard demo was verified for GPU selection and clean execution, but its frame timing is not used as a horse long-hair performance claim.
