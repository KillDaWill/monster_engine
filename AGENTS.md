# AGENTS.md

Monster Engine: procedural monster simulation in idiomatic C (gcc, `-Wall -Wextra -Wpedantic -O2`). Render-agnostic core + one SDL2/OpenGL viewer. No third-party deps for core; only `-lm` for tests.

**Language convention**: all comments, docs, and README are in **Spanish** — write new comments/docs in Spanish to match.

## Build & test

- `make test` — build + run the unit test binary `run_tests` (core + tests only, links `-lm`, **no GL needed**). This is the fast verification loop.
- `make all` — everything, including Doxygen docs.
- `make demos` — `demos/demo_ager_3d` (GL) and `demos/demo_lizard_console` (console, no GL).
- `make docs` — Doxygen from `include src README.md` into `doc/html`; prints a warning (not failure) if doxygen is missing.
- `make clean` — removes `build/`, all binaries, `doc/html`, `doc/latex`.
- `make codegraph` — sincroniza/actualiza el grafo de conocimiento para Codex y OpenCode (`tools/update_codegraph.sh`).
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

<!-- CODEGRAPH_START -->
## CodeGraph

Este proyecto cuenta con el servidor MCP de CodeGraph (herramientas `codegraph_*`) configurado. CodeGraph es un grafo de conocimiento analizado con Tree-Sitter para cada símbolo, relación y archivo. Las lecturas son sub-milisegundo y retornan información estructural que herramientas textuales como grep no pueden proporcionar.

### Cuándo preferir codegraph sobre búsqueda nativa

Usa codegraph para preguntas **estructurales** — qué llama a qué, qué se rompería, dónde está definido X, cuál es la firma de X. Usa grep/lectura nativa solo para consultas de **texto literal** (cadenas exactas, comentarios, mensajes de log) o cuando ya tengas un archivo específico abierto.

| Pregunta | Herramienta |
|---|---|
| "¿Dónde está definido X?" / "Buscar símbolo llamado X" | `codegraph_search` |
| "¿Qué llama a la función Y?" | `codegraph_callers` |
| "¿A qué funciones llama Y?" | `codegraph_callees` |
| "¿Qué se rompería si modifico Z?" | `codegraph_impact` |
| "Muéstrame la firma / código fuente / docstring de Y" | `codegraph_node` |
| "Dame contexto enfocado para una tarea o área" | `codegraph_context` |
| "Inspeccionar un módulo o tema desconocido" | `codegraph_explore` |
| "¿Qué archivos existen bajo la ruta X/?" | `codegraph_files` |
| "¿El índice está íntegro y actualizado?" | `codegraph_status` |

### Reglas generales

- **Confía en los resultados de codegraph.** Provienen de un análisis sintáctico completo del AST. No los re-verifiques con grep innecesariamente.
- **No uses grep primero** al buscar un símbolo por nombre. `codegraph_search` es más rápido y devuelve tipo + ubicación + firma en una única llamada.
- **No encadenes `codegraph_search` + `codegraph_node`** cuando solo quieras contexto — `codegraph_context` lo resuelve en una única llamada.
- **`codegraph_explore` es para análisis amplios** en áreas desconocidas — devuelve el código fuente completo de los archivos relevantes, pero consume más tokens.
- **Latencia de indexación**: el vigilante de archivos debouncea ~500ms tras escrituras; no re-consultes inmediatamente después de editar un archivo en el mismo turno.

### Si `.codegraph/` no estuviera inicializado

El servidor MCP indicará "not initialized." Ejecuta `make codegraph` o `codegraph init -i` para reconstruir el índice.
<!-- CODEGRAPH_END -->

