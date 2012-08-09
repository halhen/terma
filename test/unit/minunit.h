#include <stdio.h>

#define S(x) #x
#define S_(x) S(x)
#define S__LINE__ S_(__LINE__)

/* Source: http://www.jera.com/techinfo/jtns/jtn002.html */

#define mu_assert(test) do { tests_checks++; \
                            if (!(test)) return __FILE__ "(" S__LINE__ "): " #test " failed"; } while (0)
#define mu_run_test(test) do { printf("--> %s\n", #test); \
                               char *message = test(); tests_run++; \
                               if (message) return message; } while (0)
int tests_run;
int tests_checks;
