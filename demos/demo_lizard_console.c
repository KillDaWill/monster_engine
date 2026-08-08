/**
 * @file demo_lizard.c
 * @brief Módulo de prueba lógica que construye un Monstruo Lagarto y muestra su estructura y datos.
 * @author Monster Engine Team
 * @date 2026
 */

#include <stdio.h>
#include <stdlib.h>
#include "Monster.h"
#include "ColorPalette.h"
#include "Vector.h"

/* IDs de Traits de prueba para el lagarto */
#define TRAIT_LEGS 101
#define TRAIT_WIGGLE_TAIL 102
#define TRAIT_ANTENNA 103

/* Callbacks para un Trait de Cola Ondulante (Wiggle Tail) */
static void WiggleTail_Update(Trait* self, Monster* monster, int index, double diff) {
    (void)self; (void)monster; (void)index; (void)diff;
}

static void WiggleTail_RenderUpdate(Trait* self, Monster* monster, int index, double diff, double renderPercent) {
    (void)self; (void)monster; (void)index; (void)diff; (void)renderPercent;
}

int main(void) {
    printf("========================================================\n");
    printf("     DEMO: Creación y Análisis de Monstruo Lagarto      \n");
    printf("========================================================\n\n");

    /* 1. Crear el Monstruo */
    Monster lizard = Monster_Create();
    Monster_Init(&lizard);
    Monster_SetId(&lizard, 777);
    Monster_SetAngle(&lizard, 0.0f);

    /* 2. Asignar Paleta de Colores de Escamas (Verde Esmeralda a Amarillo Reptil) */
    Color emeraldGreen = Color_FromRGB(16, 124, 65);
    Color reptileYellow = Color_FromRGB(212, 175, 55);
    lizard.colorPalette = ColorPalette_CreateGradient(emeraldGreen, reptileYellow, 5);

    /* 3. Definir Anatomía del Lagarto (Cabeza + Pecho + Abdomen + 3 Secciones de Cola) */
    /* Cabeza (ya fue añadida en Monster_Init, modificamos o sustituimos) */
    BodyPart* head = Monster_GetHead(&lizard);
    if (head) {
        head->width = 1.8f;
        head->height = 1.2f;
        head->length = 2.0f;
        head->color.index = 0; // Emerald Green
        head->bellyColor.index = 4; // Yellow
    }

    /* Pecho (con Patas delanteras) */
    BodyPart chest = BodyPart_Create(0.0f, 0.0f, -2.0f, 2.2f, 2.5f, 1.5f, 0.2f);
    chest.color.index = 1;
    chest.bellyColor.index = 4;

    /* Abdomen (con Patas traseras) */
    BodyPart abdomen = BodyPart_Create(0.0f, 0.0f, -4.5f, 2.0f, 2.5f, 1.3f, 0.2f);
    abdomen.color.index = 2;
    abdomen.bellyColor.index = 4;

    /* Cola 1 */
    BodyPart tail1 = BodyPart_Create(0.0f, 0.0f, -7.0f, 1.4f, 2.0f, 1.0f, 0.1f);
    tail1.color.index = 3;
    tail1.bellyColor.index = 4;

    /* Cola 2 (Punta) */
    BodyPart tail2 = BodyPart_Create(0.0f, 0.0f, -9.0f, 0.7f, 2.0f, 0.6f, 0.0f);
    tail2.color.index = 4;
    tail2.bellyColor.index = 4;

    /* Añadir partes */
    Monster_AddBodyPart(&lizard, chest);
    Monster_AddBodyPart(&lizard, abdomen);
    Monster_AddBodyPart(&lizard, tail1);
    Monster_AddBodyPart(&lizard, tail2);

    /* 4. Añadir Traits al Lagarto */
    Trait tailWiggle = {
        .type = TRAIT_WIGGLE_TAIL,
        .update = WiggleTail_Update,
        .renderUpdate = WiggleTail_RenderUpdate,
        .render = NULL
    };
    Monster_AddBodyPart(&lizard, tail1); // Secciones adicionales si se desea
    BodyPart_AddTrait(&lizard.bodyParts[3], &tailWiggle);

    /* 5. Simular tick de física y renderUpdate */
    Monster_RenderUpdate(&lizard, 0.016, 1.0);

    /* 6. Mostrar Datos del Lagarto por Consola */
    printf("[DATOS GENERALES]\n");
    printf(" - ID Monstruo        : %d\n", Monster_GetId(&lizard));
    printf(" - Ángulo             : %.2f rad\n", Monster_GetAngle(&lizard));
    printf(" - Cantidad de Nodos  : %zu partes del cuerpo\n", lizard.bodyPartCount);
    printf(" - Longitud Total     : %.2f unidades\n", Monster_GetTotalLength(&lizard));

    Vector3 center = Monster_GetCenter(&lizard);
    printf(" - Centro Geométrico  : X: %.2f, Y: %.2f, Z: %.2f\n\n", center.x, center.y, center.z);

    printf("[PALETA DE COLORES]\n");
    printf(" - Colores en Paleta  : %zu\n", ColorPalette_GetCount(&lizard.colorPalette));
    for (size_t i = 0; i < ColorPalette_GetCount(&lizard.colorPalette); ++i) {
        Color c = ColorPalette_GetColor(&lizard.colorPalette, i);
        printf("   Col %zu: R=%3d, G=%3d, B=%3d, A=%3d\n", i, c.r, c.g, c.b, c.a);
    }

    printf("\n[DETALLE DE PARTES ANATÓMICAS]\n");
    for (size_t i = 0; i < lizard.bodyPartCount; ++i) {
        BodyPart* p = &lizard.bodyParts[i];
        Color cPrim = Monster_GetColorFromIndexStruct(&lizard, p->color);
        Color cBelly = Monster_GetColorFromIndexStruct(&lizard, p->bellyColor);

        printf(" Part [%zu]: Dim(Ancho=%.1f, Alto=%.1f, Largo=%.1f) | PosRender(Z=%.1f)\n",
               i, p->width, p->height, p->length, p->positionRender.z);
        printf("         Color Primario: R=%d G=%d B=%d | Color Vientre: R=%d G=%d B=%d\n",
               cPrim.r, cPrim.g, cPrim.b, cBelly.r, cBelly.g, cBelly.b);
        printf("         Traits Adjuntos: %zu\n", p->traitCount);
    }

    /* Muestreo de posición al 0%, 50% y 100% de la longitud */
    printf("\n[MUESTREO DE POSICIÓN AL LARGO DEL CUERPO]\n");
    Vector3 posHead = Monster_GetPosition(&lizard, 0.0);
    Vector3 posMid  = Monster_GetPosition(&lizard, 0.5);
    Vector3 posTail = Monster_GetPosition(&lizard, 1.0);

    printf(" - Cabeza   ( 0%%) : Z = %.2f\n", posHead.z);
    printf(" - Lomo     (50%%) : Z = %.2f\n", posMid.z);
    printf(" - Cola     (100%%): Z = %.2f\n", posTail.z);

    AABB3D box = Monster_GetBoundingBox(&lizard, (AABB3D){0});
    printf("\n[BOUNDING BOX (AABB3D)]\n");
    printf(" - Min (Start): X=%.2f, Y=%.2f, Z=%.2f\n", box.start.x, box.start.y, box.start.z);
    printf(" - Max (End)  : X=%.2f, Y=%.2f, Z=%.2f\n", box.end.x, box.end.y, box.end.z);

    /* Limpiar memoria */
    Monster_Free(&lizard);

    printf("\n========================================================\n");
    printf("            Prueba Lógica Finalizada con Éxito          \n");
    printf("========================================================\n");

    return 0;
}
