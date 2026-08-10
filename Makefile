# ==============================================================================
# Makefile para Monster Engine (Motor de Simulación de Monstruos en C)
# ==============================================================================

CC = gcc
CFLAGS = -Wall -Wextra -Wpedantic -O2 -Iinclude -Itests
LIBS = -lm
GL_LIBS = -lSDL2 -lGL -lGLU -lm

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
            $(SRC_DIR)/MonsterSDF.c \
            $(SRC_DIR)/MarchingCubesTables.c \
            $(SRC_DIR)/MarchingCubes.c \
            $(SRC_DIR)/SDFMesher.c \
            $(SRC_DIR)/MonsterVisual.c \
            $(SRC_DIR)/BodyPart.c \
            $(SRC_DIR)/Eye.c \
            $(SRC_DIR)/Mouth.c \
            $(SRC_DIR)/Monster.c \
            $(SRC_DIR)/MonsterQueries.c \
            $(SRC_DIR)/MonsterAger.c

# Módulo de Renderizador OpenGL
RENDER_SRCS = $(SRC_DIR)/OpenGLRenderer.c

CORE_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(CORE_SRCS))
RENDER_OBJS = $(patsubst $(SRC_DIR)/%.c, $(BUILD_DIR)/%.o, $(RENDER_SRCS))

# Archivos de Pruebas
TEST_SRCS = $(TEST_DIR)/main_test.c \
            $(TEST_DIR)/test_color.c \
            $(TEST_DIR)/test_vector.c \
            $(TEST_DIR)/test_math_utils.c \
            $(TEST_DIR)/test_body_part.c \
            $(TEST_DIR)/test_monster.c \
            $(TEST_DIR)/test_ager.c \
            $(TEST_DIR)/test_sdf.c \
            $(TEST_DIR)/test_marching_cubes.c \
            $(TEST_DIR)/test_mesh.c \
            $(TEST_DIR)/test_primitive_mesh.c

TEST_OBJS = $(patsubst $(TEST_DIR)/%.o, $(BUILD_DIR)/%.o, $(TEST_SRCS:.c=.o))

# Ejecutables
TEST_BIN = run_tests
DEMO_AGER_BIN = $(DEMO_DIR)/demo_ager_3d
DEMO_LIZARD_BIN = $(DEMO_DIR)/demo_lizard_console
LIZARD_VIEWER_BIN = lizard_viewer

.PHONY: all clean test docs demos

all: $(TEST_BIN) $(LIZARD_VIEWER_BIN) demos docs

# Regla para compilar objetos del núcleo
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Regla para compilar objetos de test
$(BUILD_DIR)/%.o: $(TEST_DIR)/%.c
	@mkdir -p $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Ejecutable de la Suite de Pruebas Unitarias (Puro C sin librerías gráficas)
$(TEST_BIN): $(CORE_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

# Ejecutable del visor 3D principal
$(LIZARD_VIEWER_BIN): $(CORE_OBJS) $(RENDER_OBJS) $(SRC_DIR)/main_lizard_viewer.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

# Demos
demos: $(DEMO_AGER_BIN) $(DEMO_LIZARD_BIN)

$(DEMO_AGER_BIN): $(CORE_OBJS) $(RENDER_OBJS) $(DEMO_DIR)/demo_ager_3d.c
	$(CC) $(CFLAGS) $^ $(GL_LIBS) -o $@

$(DEMO_LIZARD_BIN): $(CORE_OBJS) $(DEMO_DIR)/demo_lizard_console.c
	$(CC) $(CFLAGS) $^ $(LIBS) -o $@

# Ejecutar tests automáticamente
test: $(TEST_BIN)
	./$(TEST_BIN)

# Generación de documentación Doxygen
docs:
	@doxygen Doxyfile || echo "Doxygen no está instalado o falló la generación."

# Limpieza de binarios y archivos temporales de compilación
clean:
	rm -rf $(BUILD_DIR) $(TEST_BIN) $(LIZARD_VIEWER_BIN) $(DEMO_AGER_BIN) $(DEMO_LIZARD_BIN) doc/html doc/latex
