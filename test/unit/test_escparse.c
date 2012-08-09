#include <stdio.h>
#include <string.h>

#include "util.h"

#include "minunit.h"

#include "escparse.h"

char esc_function, esc_intermediate;
void
do_esc_dispatch(char function, char intermediate)
{
    esc_function = function, esc_intermediate = intermediate;
}

int32_t csi_param[16];
char csi_function, csi_privflag;
void
do_csi_dispatch(char function, int32_t params[], char privflag)
{
    csi_function = function, csi_privflag = privflag;
    memcpy(csi_param, params, sizeof(csi_param));
}

char osc_arg[1024];
void
do_osc_dispatch(char *arg, size_t length)
{
    strncpy(osc_arg, arg, max(length, LENGTH(osc_arg)));
}


/* Send all of s to escape handler */
void
escbatch(char *s)
{
    for (; *s; s++) {
        esc_handle(*s);
    }
}

void reset()
{
    escbatch("\024"); /* CAN */
    esc_function = esc_intermediate = csi_function = csi_privflag = '\0';
    memset(csi_param, 0, sizeof(csi_param));
}

char *
test_osc()
{
    escbatch("\033]1234567\007");
    mu_assert(strcmp(osc_arg, "1234567") == 0);

    return NULL;
}

char *
test_dcs()
{
    /* We're ignoring all DCS sequences; run one just to make sure we get past it */
    escbatch("\033P123456789\033\\");
    return NULL;
}

char *
test_csi()
{
    escbatch("\033[A");
    mu_assert(csi_function == 'A');
    mu_assert(csi_privflag == '\0');
    mu_assert(csi_param[0] == 0);
    mu_assert(csi_param[1] == -1);

    escbatch("\033[?B");
    mu_assert(csi_function == 'B');
    mu_assert(csi_privflag == '?');
    mu_assert(csi_param[0] == 0);
    mu_assert(csi_param[1] == -1);

    escbatch("\033[1C");
    mu_assert(csi_function == 'C');
    mu_assert(csi_privflag == '\0');
    mu_assert(csi_param[0] == 1);
    mu_assert(csi_param[1] == -1);

    escbatch("\033[1;2D");
    mu_assert(csi_function == 'D');
    mu_assert(csi_privflag == '\0');
    mu_assert(csi_param[0] == 1);
    mu_assert(csi_param[1] == 2);
    mu_assert(csi_param[2] == -1);

    escbatch("\033[;E");
    mu_assert(csi_function == 'E');
    mu_assert(csi_privflag == '\0');
    mu_assert(csi_param[0] == 0);
    mu_assert(csi_param[1] == 0);
    mu_assert(csi_param[2] == -1);

    return NULL;
}

static char *
test_csi_bad()
{
    /* Colon is never allowed */
    reset();
    escbatch("\033[1:A");
    mu_assert(csi_function == '\0');
    mu_assert(esc_handle('a') == false); /* Stopped parsing */

    /* Multiple private markers */
    reset();
    escbatch("\033[==A");
    mu_assert(csi_function == '\0');

    return NULL;
}

static char *
test_csi_too_long_param()
{
    int i;
    /* 1024 characters are OK */
    reset();
    escbatch("\033[");
    for (i = 0; i < 1023; i ++) {
        esc_handle('0');
    }
    esc_handle('1');
    mu_assert(esc_handle('A') == true);
    mu_assert(csi_function == 'A');
    mu_assert(csi_param[0] == 1);

    /* 1024+ is not. consume silently until dispatch character */
    reset();
    escbatch("\033[");
    for (i = 0; i < 1024; i ++) {
        esc_handle('0');
    }
    esc_handle('1');
    mu_assert(esc_handle('A') == true);
    mu_assert(csi_function == 'A');
    mu_assert(csi_param[0] == 0);

    return NULL;
}

static char *
test_csi_too_many_params()
{
    /* 16 params are OK */
    reset();
    escbatch("\033[0;1;2;3;4;5;6;7;8;9;0;1;2;3;4;5A");
    mu_assert(csi_function == 'A');
    mu_assert(csi_param[15] == 5);

    /* ... 17 are not */
    reset();
    escbatch("\033[0;1;2;3;4;5;6;7;8;9;0;1;2;3;4;5;6B");
    mu_assert(csi_function == '\0');

    return NULL;
}

static char *
test_csi_C0()
{
    char c;
    /* C0 codes should be passed through */
    reset();
    escbatch("\033[1;2");

    for (c = 0x00; c <= 0x1F; c++) {
        if (c == 0x1B || c == 0x18 || c == 0x1A) {
            continue; /* ESC resets, 0x18 and 0x1A cancels */
        }
        mu_assert(esc_handle(c) == false);
    }
    esc_handle('A');
    mu_assert(csi_function == 'A');
    mu_assert(csi_privflag == '\0');
    mu_assert(csi_param[0] == 1);
    mu_assert(csi_param[1] == 2);

    return NULL;
}

static char *
test_anywhere()
{
    /* An ESC reset the state and starts over */
    reset();
    escbatch("\033[=1;2\033[?A");
    mu_assert(csi_function == 'A');
    mu_assert(csi_privflag == '?');
    mu_assert(csi_param[0] == 0);
    mu_assert(csi_param[1] == -1);

    return NULL;
}


char *
run_tests()
{
    mu_run_test(test_anywhere);
    mu_run_test(test_csi);
    mu_run_test(test_csi_bad);
    mu_run_test(test_csi_too_many_params);
    mu_run_test(test_csi_too_long_param);
    mu_run_test(test_csi_C0);
    mu_run_test(test_osc);
    mu_run_test(test_dcs);
    return (char*)NULL;
}


int main()
{
    util_init();
    esc_init(do_esc_dispatch, do_csi_dispatch, do_osc_dispatch);
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
