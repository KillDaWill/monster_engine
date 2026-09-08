#include <stdio.h>

void run_color_tests(void);
void run_vector_tests(void);
void run_math_utils_tests(void);
void run_transform_tests(void);
void run_body_part_tests(void);
void run_monster_tests(void);
void run_ager_tests(void);
void run_sdf_tests(void);
void run_marching_cubes_tests(void);
void run_mesh_tests(void);
void run_primitive_mesh_tests(void);
void run_visual_async_tests(void);
void run_mouth_geometry_tests(void);
void run_head_tests(void);
void run_lizard_tests(void);
void run_local_detail_tests(void);
void run_perf_optimizations_tests(void);

int main(void) {
    printf("======================================\n");
    printf(" Ejecutando Test Suite (monster_engine)\n");
    printf("======================================\n");

    printf("\n--- Módulo Color & ColorPalette ---\n");
    run_color_tests();

    printf("\n--- Módulo Vector ---\n");
    run_vector_tests();

    printf("\n--- Módulo MathUtils ---\n");
    run_math_utils_tests();

    printf("\n--- Módulo Transform3D ---\n");
    run_transform_tests();

    printf("\n--- Módulo BodyPart ---\n");
    run_body_part_tests();

    printf("\n--- Módulo Monster ---\n");
    run_monster_tests();

    printf("\n--- Módulo MonsterAger ---\n");
    run_ager_tests();

    printf("\n--- Módulo Marching Cubes (tablas) ---\n");
    run_marching_cubes_tests();

    printf("\n--- Módulo Mesh ---\n");
    run_mesh_tests();

    printf("\n--- Módulo PrimitiveMesh ---\n");
    run_primitive_mesh_tests();

    printf("\n--- Módulo SDF Engine & Mesher ---\n");
    run_sdf_tests();

    printf("\n--- Módulo MonsterVisualAsync ---\n");
    run_visual_async_tests();

    run_mouth_geometry_tests();
    run_head_tests();
    run_lizard_tests();
    run_local_detail_tests();

    printf("\n--- Optimizaciones de Rendimiento y Regresión ---\n");
    run_perf_optimizations_tests();

    printf("\n======================================\n");
    printf(" ¡TODAS LAS SUITES PASARON CON ÉXITO! \n");
    printf("======================================\n");
    return 0;
}
