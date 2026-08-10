# AGENTS.md

Monster Engine: procedural monster simulation in idiomatic C (gcc, `-Wall -Wextra -Wpedantic -O2`). Render-agnostic core + one SDL2/OpenGL viewer. No third-party deps for core; only `-lm` for tests.

**Language convention**: all comments, docs, and README are in **Spanish** — write new comments/docs in Spanish to match.

## Build & test

- `make test` — build + run the unit test binary `run_tests` (core + tests only, links `-lm`, **no GL needed**). This is the fast verification loop.
- `make all` — everything, including Doxygen docs.
- `make demos` — `demos/demo_ager_3d` (GL) and `demos/demo_lizard_console` (console, no GL).
- `make docs` — Doxygen from `include src README.md` into `doc/html`; prints a warning (not failure) if doxygen is missing.
- `make clean` — removes `build/`, all binaries, `doc/html`, `doc/latex`.
- GL targets (`lizard_viewer`, `demo_ager_3d`) require SDL2/GL dev libraries; on headless machines build only core/test targets.
- Root-level `run_tests` / `run_demo` are gitignored **build artifacts**, not sources. `run_demo` is stale (no Makefile target builds it).

## Architecture — the one rule that matters

- **`src/OpenGLRenderer.c` is the ONLY file allowed to include SDL/GL headers.** Everything else in `CORE_SRCS` must stay render-agnostic: `make test` links core objects with only `-lm`, so any GL dependency in a core module breaks the test build.
- Module pattern: `include/X.h` + `src/X.c`, `Module_Action` naming (`Color_FromRGB`, `Monster_Create`), functions take/return structs (pass-by-value or pointer), Doxygen annotations (`@file`, `@brief`, `@param`, `@return`) on every header.
- Entrypoints: `src/main_lizard_viewer.c` (SDL2+OpenGL SDF viewer); console/GL demos in `demos/`.
- Header-only interfaces to respect when extending: `RenderInterfaces.h` (`MonsterRenderer` VTable, `ICamera`), `Trait.h` / `VisualTrait.h` (polymorphic traits), `WorldInterface.h`.
- SDF pipeline layering: `SDFPrimitives/SDFOperations/SDFSampling` → `MonsterSDF` (creature) → `MarchingCubes` → `SDFMesher` → `MonsterVisual` (mesh gen).

## Makefile gotchas

- **New source files are NOT auto-discovered.** Add new files to `CORE_SRCS` (core) or `RENDER_SRCS` (GL-only) in the Makefile, or they silently won't link.

## Tests — custom framework, no runner

- `tests/test_*.c` use `TEST_ASSERT(cond, msg)` and `FLOAT_NEAR(a, b)` (tolerance 0.001) from `tests/test_utils.h`. A failure prints `[FAIL]` and `exit(1)`; no pass/fail counting.
- Adding a suite requires **three steps**: create `tests/test_x.c` exposing `run_x_tests(void)`, add it to `TEST_SRCS` in the Makefile, and declare + call `run_x_tests()` from `tests/main_test.c`.
- No test filtering: the single binary runs all suites. No fixtures, no external services.
