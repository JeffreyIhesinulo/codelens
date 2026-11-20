#pragma once
#ifdef __APPLE__
namespace mac_memory {
inline const char* tracker = R"(
#include <stdlib.h>
#include <stdio.h>
struct cl_block { void *addr; size_t size; int freed; int line; };
struct cl_block cl_heap[256];
int cl_heap_n = 0;
static void record(void *p, size_t n, int line) {
    if (p && cl_heap_n < 256) cl_heap[cl_heap_n++] = (struct cl_block){p, n, 0, line};
}
static void release(void *p) {
    for (int i = cl_heap_n - 1; i >= 0; --i)
        if (cl_heap[i].addr == p && !cl_heap[i].freed) { cl_heap[i].freed = 1; break; }
}
void *cl_malloc(size_t n, int line) { void *p = malloc(n); record(p, n, line); return p; }
void *cl_calloc(size_t n, size_t s, int line) { void *p = calloc(n, s); record(p, n*s, line); return p; }
void *cl_realloc(void *old, size_t n, int line) {
    void *p = realloc(old, n);
    if (p || n == 0) { release(old); record(p, n, line); }
    return p;
}
void cl_free(void *p) { release(p); free(p); }
__attribute__((constructor)) static void cl_init(void) { setvbuf(stdout, NULL, _IONBF, 0); }
__attribute__((destructor)) static void cl_done(void) {
    FILE *f = fopen("cl_heap.txt", "w");
    if (!f) return;
    for (int i = 0; i < cl_heap_n; ++i)
        fprintf(f, "%p %zu %d %d\n", cl_heap[i].addr, cl_heap[i].size, cl_heap[i].freed, cl_heap[i].line);
    fclose(f);
}
)";
inline const char* declarations = R"(
#include <stdlib.h>
#ifdef __cplusplus
#include <iostream>
#include <vector>
#include <string>
#include <locale>
extern "C" {
#endif
void *cl_malloc(size_t, int);
void *cl_calloc(size_t, size_t, int);
void *cl_realloc(void *, size_t, int);
void cl_free(void *);
#ifdef __cplusplus
}
namespace std { using ::cl_malloc; using ::cl_calloc; using ::cl_realloc; using ::cl_free; }
#endif
#define malloc(n) cl_malloc((n), __LINE__)
#define calloc(n, s) cl_calloc((n), (s), __LINE__)
#define realloc(p, n) cl_realloc((p), (n), __LINE__)
#define free cl_free
)";
}
#endif
