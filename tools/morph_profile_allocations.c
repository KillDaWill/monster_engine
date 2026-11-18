/* Sonda opcional para Linux/glibc. Cuenta llamadas de heap por hilo y
 * publica el contador del hilo SDL entre dos presentaciones sucesivas. */
#define _GNU_SOURCE
#include <SDL2/SDL_video.h>
#include <dlfcn.h>
#include <inttypes.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef __GLIBC__
#error Esta sonda requiere glibc.
#endif

/* Las entradas de glibc evitan la recursión de dlsym durante la inicialización. */
extern void* __libc_malloc(size_t);
extern void* __libc_calloc(size_t, size_t);
extern void* __libc_realloc(void*, size_t);

static _Thread_local uint64_t allocationCalls __attribute__((tls_model("initial-exec")));
static _Thread_local unsigned frame;

void* malloc(size_t bytes) {
    ++allocationCalls;
    return __libc_malloc(bytes);
}

void* calloc(size_t count, size_t bytes) {
    ++allocationCalls;
    return __libc_calloc(count, bytes);
}

void* realloc(void* memory, size_t bytes) {
    ++allocationCalls;
    return __libc_realloc(memory, bytes);
}

void SDL_GL_SwapWindow(SDL_Window* window) {
    static _Thread_local void (*swap)(SDL_Window*);
    if (!swap) {
        swap = (void (*)(SDL_Window*))dlsym(RTLD_NEXT, "SDL_GL_SwapWindow");
        if (!swap) abort();
    }
    swap(window);
    fprintf(stdout, "PROFILE_ALLOCATIONS,%u,%" PRIu64 "\n", frame++, allocationCalls);
    allocationCalls = 0;
}
