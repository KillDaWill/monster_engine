#ifndef UI_TEXT_H
#define UI_TEXT_H
#include <stddef.h>
#include <stdint.h>

/* Lee un punto de código. Una secuencia inválida consume un byte y devuelve U+FFFD. */
uint32_t UiText_Next(const char *text, size_t *offset);
size_t UiText_Previous(const char *text, size_t offset);
size_t UiText_Count(const char *text, size_t bytes);
/* Devuelve bytes de la próxima línea visual y cuántos bytes avanzar. */
size_t UiText_Wrap(const char *text, int columns, size_t *advance);
#endif
