# ==============================================================================
# Makefile para Monster Engine (Motor de Simulación de Monstruos en C)
# ==============================================================================

CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -O2 -Iinclude -Itests
LIBS = -lm -pthread
GL_LIBS = -lSDL2 -lGL -lGLU -lm -pthread

# Directorios
SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
DEMO_DIR = demos
BUILD_DIR = build

# Archivos fuente del núcleo puro (Desacoplado de Render)
CORE_SRCS = $(SRC_DIR)/Color.c \
            $(SRC_DIR)/ColorPalette.c \
            $(SRC_DIR)/Vector.c \
            $(SRC_DIR)/MathUtils.c \
            $(SRC_DIR)/Transform3D.c \
            $(SRC_DIR)/AABB.c \
            $(SRC_DIR)/Mesh.c \
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
            $(SRC_DIR)/Mouth.c \
            $(SRC_DIR)/Head.c \
            $(SRC_DIR)/Anatomy.c \
            $(SRC_DIR)/Lizard.c \
            $(SRC_DIR)/Monster.c \
            $(SRC_DIR)/MonsterQueries.c \
            $(SRC_DIR)/MonsterAger.c \
            $(SRC_DIR)/LizardMorph.c

# Módulo de Renderizador OpenGL
RENDER_SRCS = $(SRC_DIR)/OpenGLRenderer.c

CORE_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(CORE_SRCS))
RENDER_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(RENDER_SRCS))

# Archivos de Pruebas
TEST_SRCS = $(TEST_DIR)/main_test.c \
            $(TEST_DIR)/test_color.c \
            $(TEST_DIR)/test_vector.c \
            $(TEST_DIR)/test_math_utils.c \
            $(TEST_DIR)/test_transform.c \
            $(TEST_DIR)/test_body_part.c \
            $(TEST_DIR)/test_monster.c \
            $(TEST_DIR)/test_ager.c \
            $(TEST_DIR)/test_sdf.c \
            $(TEST_DIR)/test_marching_cubes.c \
            $(TEST_DIR)/test_mesh.c \
            $(TEST_DIR)/test_primitive_mesh.c \
            $(TEST_DIR)/test_visual_async.c \
            $(TEST_DIR)/test_mouth_geometry.c \
            $(TEST_DIR)/test_head.c \
            $(TEST_DIR)/test_lizard.c \
            $(TEST_DIR)/test_local_detail.c \
            $(TEST_DIR)/test_perf_optimizations.c \
            $(TEST_DIR)/test_morph.c

TEST_OBJS = $(patsubst $(TEST_DIR)/%.o, $(BUILD_DIR)/%.o, $(TEST_SRCS:.c=.o))

# Ejecutables
TEST_BIN = run_tests
BENCHMARK_BIN = benchmarks/benchmark_sdf
DEMO_AGER_BIN = $(DEMO_DIR)/demo_ager_3d
DEMO_LIZARD_BIN = $(DEMO_DIR)/demo_lizard_console
DEMO_MOUTH_BIN = $(DEMO_DIR)/demo_mouth_animation
LIZARD_VIEWER_BIN = lizard_viewer

.PHONY: all clean test benchmark docs demos

all: $(TEST_BIN) $(LIZARD_VIEWER_BIN) demos docs

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
demos: $(DEMO_AGER_BIN) $(DEMO_LIZARD_BIN) $(DEMO_MOUTH_BIN)

$(DEMO_AGER_BIN): $(CORE_OBJS) $(RENDER_OBJS) $(DEMO_DIR)/demo_ager_3d.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

$(DEMO_LIZARD_BIN): $(CORE_OBJS) $(DEMO_DIR)/demo_lizard_console.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

$(DEMO_MOUTH_BIN): $(CORE_OBJS) $(RENDER_OBJS) $(DEMO_DIR)/demo_mouth_animation.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

# Ejecutar tests automáticamente
test: $(TEST_BIN)
	./$(TEST_BIN)

# Generación de documentación Doxygen
docs:
	@doxygen Doxyfile || echo "Doxygen no está instalado o falló la generación."

# Limpieza de binarios y archivos temporales de compilación
clean:
	rm -rf $(BUILD_DIR) $(TEST_BIN) $(BENCHMARK_BIN) $(LIZARD_VIEWER_BIN) $(DEMO_AGER_BIN) $(DEMO_LIZARD_BIN) $(DEMO_MOUTH_BIN) benchmarks/benchmark_lizard benchmarks/benchmark_appendages benchmarks/benchmark_ager_realtime doc/html doc/latex

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

.PHONY: benchmark-ager-realtime
benchmark-ager-realtime: benchmarks/benchmark_ager_realtime
	./benchmarks/benchmark_ager_realtime

src/MonsterSDFShader.generated.h: tools/generate_sdf_shader.py shaders/monster_sdf_trace.glsl src/MonsterSDF.c src/SDFPrimitives.c src/SDFOperations.c include/MonsterSDF.h
	python3 tools/generate_sdf_shader.py

$(BUILD_DIR)/OpenGLRenderer.o: src/MonsterSDFShader.generated.h
