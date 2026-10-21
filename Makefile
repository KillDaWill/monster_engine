# ==============================================================================
# Makefile para Monster Engine (Motor de Simulación de Monstruos en C)
# ==============================================================================

CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -O2 -Iinclude -Itests
LIBS = -lm -pthread
GL_LIBS = -lSDL2 -lGL -lGLU -lm -pthread
BUILD_REVISION := $(shell git describe --always --dirty 2>/dev/null)

# Directorios
SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
DEMO_DIR = demos
BUILD_DIR = build

# Archivos fuente del núcleo puro (Desacoplado de Render)
CORE_SRCS = $(SRC_DIR)/EcosystemScenario.c \
            $(SRC_DIR)/EcosystemAnatomy.c \
            $(SRC_DIR)/SimulationClock.c \
            $(SRC_DIR)/CreaturePhysiology.c \
            $(SRC_DIR)/CreatureGenome.c \
            $(SRC_DIR)/CreatureReproduction.c \
            $(SRC_DIR)/EcosystemResources.c \
            $(SRC_DIR)/EcosystemWorld.c \
            $(SRC_DIR)/AnatomicalReach.c \
            $(SRC_DIR)/BrainProfile.c \
            $(SRC_DIR)/CreatureNeeds.c \
            $(SRC_DIR)/AIWorld.c \
            $(SRC_DIR)/CreaturePerception.c \
            $(SRC_DIR)/CreatureNavigation.c \
            $(SRC_DIR)/CreatureMotor.c \
            $(SRC_DIR)/CreatureMind.c \
            $(SRC_DIR)/CreatureDecision.c \
            $(SRC_DIR)/CreatureActions.c \
            $(SRC_DIR)/GpuDevicePolicy.c \
            $(SRC_DIR)/NavigationProgress.c \
            $(SRC_DIR)/CreatureBoundary.c \
            $(SRC_DIR)/SDFInterval.c \
            $(SRC_DIR)/GpuSDFSurfaceCache.c \
            $(SRC_DIR)/CreatureMorph.c \
            $(SRC_DIR)/MorphModuleState.c \
            $(SRC_DIR)/MorphHandoff.c \
            $(SRC_DIR)/MorphSpatialCache.c \
            $(SRC_DIR)/MorphChunkManager.c \
            $(SRC_DIR)/MorphSurfaceTracker.c \
            $(SRC_DIR)/MorphSurfacePatcher.c \
            $(SRC_DIR)/HeadMorph.c \
            $(SRC_DIR)/Surface.c \
            $(SRC_DIR)/Fur.c \
            $(SRC_DIR)/SurfaceMapper.c \
            $(SRC_DIR)/SurfacePresets.c \
            $(SRC_DIR)/Color.c \
            $(SRC_DIR)/ColorPalette.c \
            $(SRC_DIR)/Vector.c \
            $(SRC_DIR)/MathUtils.c \
            $(SRC_DIR)/Transform3D.c \
            $(SRC_DIR)/AABB.c \
            $(SRC_DIR)/Mesh.c \
            $(SRC_DIR)/MeshPatch.c \
            $(SRC_DIR)/PrimitiveMesh.c \
            $(SRC_DIR)/SDFPrimitives.c \
            $(SRC_DIR)/SDFOperations.c \
            $(SRC_DIR)/SDFSampling.c \
            $(SRC_DIR)/SDFSamplingPool.c \
            $(SRC_DIR)/MonsterSDF.c \
            $(SRC_DIR)/MarchingCubesTables.c \
            $(SRC_DIR)/MarchingCubes.c \
            $(SRC_DIR)/SDFMesher.c \
            $(SRC_DIR)/SDFAdaptiveMesher.c \
            $(SRC_DIR)/MonsterVisual.c \
            $(SRC_DIR)/MonsterVisualAsync.c \
            $(SRC_DIR)/BodyPart.c \
            $(SRC_DIR)/Eye.c \
            $(SRC_DIR)/EyeTexture.c \
            $(SRC_DIR)/Mouth.c \
            $(SRC_DIR)/Head.c \
            $(SRC_DIR)/Anatomy.c \
            $(SRC_DIR)/CreaturePhenotype.c \
            $(SRC_DIR)/CreatureRecipe.c \
            $(SRC_DIR)/CreatureRecipeIO.c \
            $(SRC_DIR)/CreatureRecipeRegistry.c \
            $(SRC_DIR)/CreatureRecipes.c \
             $(SRC_DIR)/CreatureRecipeBrumacerno.c \
             $(SRC_DIR)/CreatureRecipeVelkar.c \
            $(SRC_DIR)/Attachment.c \
            $(SRC_DIR)/AxialBody.c \
            $(SRC_DIR)/AxialBodySprawlingTetrapod.c \
            $(SRC_DIR)/HeadModule.c \
            $(SRC_DIR)/Limb.c \
            $(SRC_DIR)/LimbReptile.c \
            $(SRC_DIR)/LimbMammal.c \
            $(SRC_DIR)/AxialBodyUprightTetrapod.c \
            $(SRC_DIR)/Tail.c \
            $(SRC_DIR)/Ornament.c \
            $(SRC_DIR)/Creature.c \
            $(SRC_DIR)/Monster.c \
            $(SRC_DIR)/MonsterQueries.c \
            $(SRC_DIR)/MonsterAger.c \
            $(SRC_DIR)/Quaternion.c \
            $(SRC_DIR)/Skeleton.c \
            $(SRC_DIR)/IK.c \
            $(SRC_DIR)/RigBuilder.c \
            $(SRC_DIR)/CreatureRig.c \
            $(SRC_DIR)/AnatomyDeformer.c \
            $(SRC_DIR)/WorldInterface.c \
            $(SRC_DIR)/Locomotion.c \
            $(SRC_DIR)/ProceduralAnimator.c \
            $(SRC_DIR)/GaitPresets.c \
            $(SRC_DIR)/MonsterAnimation.c \
            $(SRC_DIR)/AnimatedVisual.c \
            $(SRC_DIR)/PigmentPattern.c \
            $(SRC_DIR)/AttachmentPath.c \
            $(SRC_DIR)/CreatureVariation.c \
            $(SRC_DIR)/VisualVariation.c \
            $(SRC_DIR)/EcosystemVisual.c \
            $(SRC_DIR)/LizardVariations.c

# Módulo de Renderizador OpenGL
RENDER_SRCS = $(SRC_DIR)/OpenGLRenderer.c

CORE_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(CORE_SRCS))
RENDER_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(RENDER_SRCS))

# Archivos de Pruebas
TEST_SRCS = $(TEST_DIR)/test_ecosystem.c \
            $(TEST_DIR)/test_creature_recipe_io.c \
            $(TEST_DIR)/test_cloven_hoof.c \
            $(TEST_DIR)/test_bovine_editor.c \
            $(TEST_DIR)/test_anatomical_reach.c \
            $(TEST_DIR)/test_brain_profile.c \
            $(TEST_DIR)/test_creature_needs.c \
            $(TEST_DIR)/test_creature_perception.c \
            $(TEST_DIR)/test_creature_decision.c \
            $(TEST_DIR)/test_creature_navigation.c \
            $(TEST_DIR)/test_creature_actions.c \
            $(TEST_DIR)/main_test.c \
            $(TEST_DIR)/test_gpu_device_policy.c \
            $(TEST_DIR)/test_navigation.c \
            $(TEST_DIR)/test_gpu_sdf_surface.c \
            $(TEST_DIR)/test_surface.c \
            $(TEST_DIR)/test_color.c \
            $(TEST_DIR)/test_vector.c \
            $(TEST_DIR)/test_math_utils.c \
            $(TEST_DIR)/test_transform.c \
            $(TEST_DIR)/test_body_part.c \
            $(TEST_DIR)/test_monster.c \
            $(TEST_DIR)/test_ager.c \
            $(TEST_DIR)/test_sdf.c \
            $(TEST_DIR)/test_sdf_ray_bounds.c \
            $(TEST_DIR)/test_marching_cubes.c \
            $(TEST_DIR)/test_mesh.c \
            $(TEST_DIR)/test_primitive_mesh.c \
            $(TEST_DIR)/test_visual_async.c \
            $(TEST_DIR)/test_mouth_geometry.c \
            $(TEST_DIR)/test_head.c \
            $(TEST_DIR)/test_lizard.c \
            $(TEST_DIR)/test_creature_recipe.c \
            $(TEST_DIR)/test_dog.c \
            $(TEST_DIR)/test_local_detail.c \
            $(TEST_DIR)/test_perf_optimizations.c \
            $(TEST_DIR)/test_morph.c \
            $(TEST_DIR)/test_morph_handoff.c \
            $(TEST_DIR)/test_animation.c \
            $(TEST_DIR)/test_creature_variation.c \
            $(TEST_DIR)/test_creature_visual_variation.c \
            $(TEST_DIR)/test_ecosystem_creatures.c \
            $(TEST_DIR)/test_morph_spatial_cache.c \
            $(TEST_DIR)/test_morph_chunk_manager.c \
            $(TEST_DIR)/test_morph_cross.c \
            $(TEST_DIR)/test_fur.c

TEST_OBJS = $(patsubst $(TEST_DIR)/%.o, $(BUILD_DIR)/%.o, $(TEST_SRCS:.c=.o))

# Ejecutables
TEST_BIN = run_tests
BENCHMARK_BIN = benchmarks/benchmark_sdf
DEMO_AGER_BIN = $(DEMO_DIR)/demo_ager_3d
DEMO_LIZARD_BIN = $(DEMO_DIR)/demo_lizard_console
DEMO_MOUTH_BIN = $(DEMO_DIR)/demo_mouth_animation
DEMO_ANIMATION_BIN = $(DEMO_DIR)/demo_lizard_animation
DEMO_DIVERSITY_BIN = $(DEMO_DIR)/demo_lizard_diversity
DEMO_MORPH_BIN = $(DEMO_DIR)/demo_morph_dog_lizard
DEMO_GAME_SCENE_BIN = $(DEMO_DIR)/demo_game_scene
LIZARD_VIEWER_BIN = lizard_viewer

.PHONY: all clean test benchmark docs demos visual-morph-ownership

all: $(TEST_BIN) $(LIZARD_VIEWER_BIN) demos docs

# Editor local; las recetas se cargan como datos sin recompilar el núcleo.
.PHONY: creature-editor editor-test
$(BUILD_DIR)/creature_editor_bridge: $(CORE_OBJS) tools/creature_editor_bridge.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

creature-editor: $(BUILD_DIR)/creature_editor_bridge
	python3 tools/creature_editor/server.py

editor-test: $(BUILD_DIR)/creature_editor_bridge
	python3 tools/creature_editor/test_editor.py

$(BUILD_DIR)/inspect_brumacerno: $(CORE_OBJS) tools/inspect_brumacerno.c
	$(CC) $(CFLAGS) $^ -lm -pthread -o $@

$(BUILD_DIR)/validate_recipe_gpu: $(CORE_OBJS) $(RENDER_OBJS) tools/validate_recipe_gpu.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

$(BUILD_DIR)/capture_bovine_anatomy: $(CORE_OBJS) $(RENDER_OBJS) tools/capture_bovine_anatomy.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

$(BUILD_DIR)/CreatureRecipeBrumacerno.o: src/CreatureRecipeBrumacerno.generated.inc

src/CreatureRecipeBrumacerno.generated.inc: recipes/brumacerno.creature.json tools/generate_builtin_brumacerno.py
	python3 tools/generate_builtin_brumacerno.py

$(BUILD_DIR)/CreatureRecipeIO.o: src/CreatureRecipeSchema.inc

src/CreatureRecipeSchema.inc: tools/generate_recipe_schema.py $(wildcard include/*.h)
	python3 tools/generate_recipe_schema.py

# Regla para compilar objetos del núcleo
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Regla para compilar objetos de test
$(BUILD_DIR)/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

# Ejecutable de la Suite de Pruebas Unitarias (Puro C sin librerías gráficas)
$(TEST_BIN): $(CORE_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

# Ejecutable de Benchmark de Rendimiento (Puro C sin librerías gráficas)
$(BENCHMARK_BIN): $(CORE_OBJS) benchmarks/benchmark_sdf.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

benchmark: $(BENCHMARK_BIN)
	./$(BENCHMARK_BIN)

# Ejecutable del visor 3D principal
$(LIZARD_VIEWER_BIN): $(CORE_OBJS) $(RENDER_OBJS) $(SRC_DIR)/main_lizard_viewer.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

# Demos
demos: demos/demo_ecosystem demos/demo_ecosystem_headless $(DEMO_AGER_BIN) $(DEMO_LIZARD_BIN) $(DEMO_MOUTH_BIN) $(DEMO_ANIMATION_BIN) $(DEMO_DIVERSITY_BIN) demos/demo_dog $(DEMO_MORPH_BIN) $(DEMO_GAME_SCENE_BIN)

$(DEMO_GAME_SCENE_BIN): $(CORE_OBJS) $(RENDER_OBJS) $(DEMO_DIR)/demo_game_scene.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

$(DEMO_MORPH_BIN): $(CORE_OBJS) $(RENDER_OBJS) $(DEMO_DIR)/demo_morph_dog_lizard.c
	$(CC) $(CFLAGS) -DMONSTER_BUILD_REVISION='"$(BUILD_REVISION)"' $^ $(GL_LIBS) -o $@

# Compara la silueta final retardada con el campo exacto del mismo snapshot.
visual-morph-ownership: $(DEMO_MORPH_BIN)
	@mkdir -p $(BUILD_DIR)/morph-ownership
	./$(DEMO_MORPH_BIN) --surface-mode=legacy-handoff --validate-cycle=$(BUILD_DIR)/morph-ownership/delayed --worker-delay=3000 --view=3 --no-fur
	./$(DEMO_MORPH_BIN) --surface-mode=exact-sdf --age=1 --capture-prefix=$(BUILD_DIR)/morph-ownership/exact --view=3 --no-fur
	python3 tools/verify_morph_ownership.py $(BUILD_DIR)/morph-ownership/exact.ppm $(BUILD_DIR)/morph-ownership/delayed-06.ppm

$(DEMO_AGER_BIN): $(CORE_OBJS) $(RENDER_OBJS) $(DEMO_DIR)/demo_ager_3d.c $(DEMO_DIR)/demo_ager_realtime.h
	$(CC) $(CFLAGS) $(filter %.o %.c,$^) $(GL_LIBS) -o $@

$(DEMO_LIZARD_BIN): $(CORE_OBJS) $(DEMO_DIR)/demo_lizard_console.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

$(DEMO_MOUTH_BIN): $(CORE_OBJS) $(RENDER_OBJS) $(DEMO_DIR)/demo_mouth_animation.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

$(DEMO_ANIMATION_BIN): $(CORE_OBJS) $(RENDER_OBJS) $(DEMO_DIR)/demo_lizard_animation.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

$(DEMO_DIVERSITY_BIN): $(CORE_OBJS) $(RENDER_OBJS) $(DEMO_DIR)/demo_lizard_diversity.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

# Ejecutar tests automáticamente
test: $(TEST_BIN)
	./$(TEST_BIN)

# Generación de documentación Doxygen
docs:
	@doxygen Doxyfile || echo "Doxygen no está instalado o falló la generación."

# Actualización del grafo de código para asistentes AI (Codex, OpenCode)
.PHONY: codegraph
codegraph:
	./tools/update_codegraph.sh

# Limpieza de binarios y archivos temporales de compilación
clean:
	rm -rf demos/demo_ecosystem demos/demo_ecosystem_headless benchmarks/benchmark_ecosystem $(DEMO_MORPH_BIN) $(DEMO_GAME_SCENE_BIN) demos/demo_dog benchmarks/benchmark_animation $(ANIMATION_DEMOS) $(BUILD_DIR) $(TEST_BIN) $(BENCHMARK_BIN) $(LIZARD_VIEWER_BIN) $(DEMO_AGER_BIN) $(DEMO_LIZARD_BIN) $(DEMO_MOUTH_BIN) $(DEMO_ANIMATION_BIN) $(DEMO_DIVERSITY_BIN) benchmarks/benchmark_lizard benchmarks/benchmark_appendages benchmarks/benchmark_ager_realtime benchmarks/benchmark_morph_dynamic benchmarks/benchmark_gpu_sparse_surface benchmarks/benchmark_growth benchmarks/benchmark_head_morph benchmarks/benchmark_ai benchmarks/benchmark_interactions doc/html doc/latex

# Dependencias de cabeceras: evita mezclar layouts de structs antiguos y nuevos.
-include $(CORE_OBJS:.o=.d) $(RENDER_OBJS:.o=.d) $(TEST_OBJS:.o=.d)

# Benchmark del mismo preset y configuraciones que usa el demo de crecimiento.
benchmarks/benchmark_lizard: $(CORE_OBJS) benchmarks/benchmark_lizard.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

.PHONY: benchmark-lizard
benchmark-lizard: benchmarks/benchmark_lizard
	./benchmarks/benchmark_lizard

# Visibilidad estática y comparación A/B de optimizaciones en apéndices finos.
benchmarks/benchmark_appendages: $(CORE_OBJS) benchmarks/benchmark_appendages.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

.PHONY: benchmark-appendages
benchmark-appendages: benchmarks/benchmark_appendages
	./benchmarks/benchmark_appendages --ab

# Benchmark de coherencia temporal y lag de deformación en tiempo real
benchmarks/benchmark_ager_realtime: $(CORE_OBJS) benchmarks/benchmark_ager_realtime.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

benchmarks/benchmark_morph_dynamic: $(CORE_OBJS) benchmarks/benchmark_morph_dynamic.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

benchmarks/benchmark_gpu_sparse_surface: $(CORE_OBJS) benchmarks/benchmark_gpu_sparse_surface.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

$(BUILD_DIR)/validate_morph_gpu_regressions: $(CORE_OBJS) $(RENDER_OBJS) tools/validate_morph_gpu_regressions.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

.PHONY: visual-morph-gpu-regressions
visual-morph-gpu-regressions: $(BUILD_DIR)/validate_morph_gpu_regressions
	./$(BUILD_DIR)/validate_morph_gpu_regressions

.PHONY: benchmark-ager-realtime
benchmark-ager-realtime: benchmarks/benchmark_ager_realtime
	./benchmarks/benchmark_ager_realtime

src/MonsterSDFShader.generated.h: tools/generate_sdf_shader.py shaders/monster_sdf_trace.glsl shaders/sdf_scene.glsl shaders/sdf_surface_material.glsl $(wildcard shaders/sdf_surface_*) $(wildcard shaders/sdf_sparse_*) shaders/sdf_interval_validate.comp shaders/morph_handoff.glsl $(wildcard shaders/surface/*.glsl) src/MonsterSDF.c src/SDFPrimitives.c src/SDFOperations.c src/SDFInterval.c include/SDFInterval.h include/MonsterSDF.h
	python3 tools/generate_sdf_shader.py

src/MonsterSDFCompute.generated.h: src/MonsterSDFShader.generated.h
	@test -f $@ || python3 tools/generate_sdf_shader.py

$(BUILD_DIR)/OpenGLRenderer.o: src/MonsterSDFShader.generated.h src/MonsterSDFCompute.generated.h

# Etapas de animación: comparten infraestructura, cada ejecutable selecciona su controlador.
ANIMATION_DEMOS = demos/demo_lizard_surface demos/demo_lizard_ik demos/demo_lizard_jaw demos/demo_lizard_walk
demos: $(ANIMATION_DEMOS)
$(ANIMATION_DEMOS): $(CORE_OBJS) $(RENDER_OBJS) demos/demo_lizard_animation.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

# Auditoría CPU independiente de SDL/GL, con instrumentación del enlazador GNU.
benchmarks/benchmark_animation: $(CORE_OBJS) benchmarks/benchmark_animation.c
	$(CC) $(CFLAGS) $^ $(LIBS) -Wl,--wrap=malloc,--wrap=calloc,--wrap=realloc,--wrap=MonsterSDF_Build,--wrap=SDFMesher_GenerateMesh,--wrap=SDFMesher_GenerateMeshDetailed -o $@
.PHONY: benchmark-animation
benchmark-animation: benchmarks/benchmark_animation
	./benchmarks/benchmark_animation

src/MonsterSurfaceShader.generated.h: tools/generate_surface_shader.py $(wildcard shaders/surface/*.glsl) shaders/monster_mesh.vert shaders/monster_surface.frag shaders/fur_guard.vert shaders/fur_guard.frag shaders/morph_handoff.glsl
	python3 tools/generate_surface_shader.py
$(BUILD_DIR)/OpenGLRenderer.o: src/MonsterSurfaceShader.generated.h

benchmarks/benchmark_growth: $(CORE_OBJS) benchmarks/benchmark_growth.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

.PHONY: benchmark-growth
benchmark-growth: benchmarks/benchmark_growth
	./benchmarks/benchmark_growth

benchmarks/benchmark_head_morph: $(CORE_OBJS) benchmarks/benchmark_head_morph.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

# Primer mamífero estático, sin SDL/GL fuera del backend.
demos/demo_dog: $(CORE_OBJS) $(RENDER_OBJS) demos/demo_dog.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

# Regresión anatómica del campo disperso: no oculta fallos regionales.
.PHONY: visual-ager-sparse
visual-ager-sparse: demos/demo_morph_dog_lizard
	python3 tools/validate_ager_features.py --mode=gpu-sdf-sparse --history --out=build/ager-sparse-validation

# Benchmark de inteligencia independiente de GL, superficies y Ager.
benchmarks/benchmark_ai: $(CORE_OBJS) benchmarks/benchmark_ai.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

.PHONY: benchmark-ai
benchmark-ai: benchmarks/benchmark_ai
	./benchmarks/benchmark_ai 2
	./benchmarks/benchmark_ai 100

# Instrumentación corporal sin dependencias GL.
benchmarks/benchmark_interactions: $(CORE_OBJS) benchmarks/benchmark_interactions.c
	$(CC) $(CFLAGS) benchmarks/benchmark_interactions.c $(CORE_OBJS) -o $@ $(LIBS)

# Ecosistema: simulación sin dependencias gráficas.
demos/demo_ecosystem_headless: $(CORE_OBJS) demos/demo_ecosystem_headless.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

demos/demo_ecosystem: $(CORE_OBJS) $(RENDER_OBJS) demos/demo_ecosystem.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

benchmarks/benchmark_ecosystem: $(CORE_OBJS) benchmarks/benchmark_ecosystem.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

# Diagnóstico de memoria de la simulación, sin recompilar objetos de producción.
ECOSYSTEM_SANITIZER_SRCS = $(CORE_SRCS)
.PHONY: ecosystem-sanitize
ecosystem-sanitize:
	@mkdir -p $(BUILD_DIR)
	$(CC) -Wall -Wextra -Wpedantic -O1 -g -Iinclude -Itests -fsanitize=address,undefined -fno-omit-frame-pointer -ffunction-sections -fdata-sections $(ECOSYSTEM_SANITIZER_SRCS) benchmarks/benchmark_ecosystem.c -Wl,--gc-sections -lm -pthread -o $(BUILD_DIR)/ecosystem-sanitize
	ASAN_OPTIONS=detect_leaks=1 $(BUILD_DIR)/ecosystem-sanitize 100 3600 10000

.PHONY: validate-ecosystem
validate-ecosystem: demos/demo_ecosystem_headless
	python3 tools/validate_ecosystem.py
