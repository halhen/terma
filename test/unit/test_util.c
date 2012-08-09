#include <stdio.h>

#include "minunit.h"
#include "util.h"



static char *
test_utf8toucs2()
{
    wchar_t enc[100];
	size_t length;

    length = utf8towchar("a", enc);
    mu_assert(enc[0] == 0x0061);
	mu_assert(length == 1);

    length = utf8towchar("ö", enc);
    mu_assert(enc[0] == 0x00f6);
	mu_assert(length == 2);

    return (char*)NULL;
}

char *
test_helpers()
{
    uint32_t array[10];
    mu_assert(LENGTH(array) == 10);

    mu_assert(min(0, 10) == 0);

    mu_assert(max(0, 10) == 10);

    mu_assert(limit(5, 0, 10) == 5);
    mu_assert(limit(-1, 0, 10) == 0);
    mu_assert(limit(10, 0, 10) == 10);

    mu_assert(between( 0, 0, 10) == true);
    mu_assert(between( 5, 0, 10) == true);
    mu_assert(between(10, 0, 10) == true);
    mu_assert(between(-1, 0, 10) == false);
    mu_assert(between(11, 0, 10) == false);

    return NULL;
}


char *
run_tests()
{
    mu_run_test(test_utf8toucs2);
    mu_run_test(test_helpers);
    return (char*)NULL;
}


int main()
{
    util_init();
    char *result = run_tests();

    printf("Run %d test(s) with %d check(s)\n", tests_run, tests_checks);
    if (result != NULL) {
        printf("FAIL: %s\n", result);
    }
    else {
        printf("OK\n");
    }

    return result != NULL;
}
