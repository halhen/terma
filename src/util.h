#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <sys/time.h>

#define TERM_NAME "terma"

size_t utf8towchar(char *source, wchar_t *dest);
void* emalloc(size_t size);
void* erealloc(void *ptr, size_t size);

void util_init();

#define LENGTH(array) (sizeof(array) / sizeof(array[0]))

#ifndef NDEBUG
#define debug(...) do {fprintf(stderr, "%-10s (%s:%d): ", __func__, __FILE__, __LINE__); \
                       fprintf(stderr, __VA_ARGS__); \
                       fprintf(stderr, "\n");} while(0)
#else
#define debug(...)
#endif

#define warning(...) do {fprintf(stderr, "%s (%s:%d): ", __func__, __FILE__, __LINE__); \
                         fprintf(stderr, __VA_ARGS__); \
                         fprintf(stderr, "\n");} while(0)

#define die(...)   do {fprintf(stderr, "%s (%s:%d): ", __func__, __FILE__, __LINE__); \
                       fprintf(stderr, __VA_ARGS__); \
                       fprintf(stderr, "\n"); \
                       exit(1); } while(0); \

/* To get rid of warnings for unused variables */
#define unused __attribute__ ((unused))

static inline uint32_t min(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static inline uint32_t max(uint32_t a, uint32_t b)
{
    return a > b ? a : b;
}

static inline int32_t limit(int32_t n, int32_t a, int32_t b)
{
    assert(a <= b);

    if (n > b) {
        n = b;
    }
    else if (n < a) {
        n = a;
    }

    return n;
}


static inline bool between(int32_t n, int32_t low, int32_t high)
{
    return (low <= n) && (n <= high);
}

static inline uint64_t timediff_usec(struct timeval t1, struct timeval t2)
{
    return (t1.tv_sec - t2.tv_sec) * 1000000 + (t1.tv_usec - t2.tv_usec);
}
