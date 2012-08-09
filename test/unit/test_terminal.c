#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include "minunit.h"
#include "config.h"
#include "util.h"
#include "terminal.h"

enum {
    OATTR_NONE = 0,
    OATTR_BOLD = 1,
    OATTR_UNDERLINE = 2,
};

/* "Screen" used to check output */
static struct {
    wchar_t *text;
    color_t *fgs;
    color_t *bgs;
    uint32_t *attrs;
    size_t   cols;
    size_t   rows;
    uint8_t  leds; /* LED bitmap. 0 = off, 1 = on */
} output;

static char response[256]; /* responses from the terminal */

size_t
oindex(size_t x, size_t y)
{
    assert(x < output.cols);
    assert(y < output.rows);

    return y * output.cols + x;
}

void
oresponse(const char *s, size_t n)
{
    strncpy(response, s, min(n, LENGTH(response) - 1));
}

void
oreset()
{
    term_write("\033c");
    term_resize(80, 24);
    memset(response, '\0', sizeof(response));
}

void
odestroy()
{
    free(output.text);
    output.text = NULL;
}

void
owrite_finished_cb()
{
    /* No-op */
}

void
oclear_cb(size_t col, size_t row, size_t length, color_t bg)
{
    size_t i;
    for (i = 0; i < length; i ++) /* iterate char by char to catch index errors */
    {
        output.text[oindex(col + i, row)] = '\0';
        output.bgs [oindex(col + i, row)] = bg;
    }
}

void
owrite_cb(size_t col, size_t row, wchar_t text[], size_t length, color_t fg, color_t bg, bool bold, bool underline)
{
    size_t i;
    size_t index;
    for (i = 0; i < length; i ++) /* iterate char by char to catch index errors */
    {
        index = oindex(col + i, row);
        output.text[index] = text[i];
        output.fgs[index]  = fg;
        output.bgs[index]  = bg;
        output.attrs[index]  = 0;
        if (bold)
            output.attrs[index] |= OATTR_BOLD;
        if (underline)
            output.attrs[index] |= OATTR_UNDERLINE;

    }
}

void
oreschange_cb(size_t cols, size_t rows)
{
    output.text = realloc(output.text, cols * rows * sizeof(output.text[0]));
    output.fgs = realloc(output.fgs, cols * rows * sizeof(output.fgs[0]));
    output.bgs = realloc(output.bgs, cols * rows * sizeof(output.bgs[0]));
    output.attrs = realloc(output.attrs, cols * rows * sizeof(output.attrs[0]));
    output.cols = cols;
    output.rows = rows;
}

void
oflush()
{
    term_flush();
}

/* Character at position */
static inline wchar_t
O(size_t col, size_t row)
{
    oflush();
    return output.text[oindex(col, row)];
}

/* Foreground color at position */
static inline color_t
F(size_t col, size_t row)
{
    oflush();
    return output.fgs[oindex(col, row)];
}

/* Background color at position */
static inline color_t
B(size_t col, size_t row)
{
    oflush();
    return output.bgs[oindex(col, row)];
}

/* Character attributes at position */
static inline uint32_t
A(size_t col, size_t row)
{
    oflush();
    return output.attrs[oindex(col, row)];
}

bool
oisempty()
{
    size_t x, y;
    for (y = 0; y < output.rows; y++) {
        for (x = 0; x < output.cols; x ++) {
            if (O(x, y) != L'\0') {
                return false;
            }
        }
    }
    return true;
}

void
odump()
/* Print screen -- useful to debug failed tests.
 * Prints only lower 7 bits of each character
 */
{
    size_t x, y;
    char c;
    for (y = 0; y < output.rows; y++) {
        for (x = 0; x < output.cols; x ++) {
            c = O(x,y) & 0x7f;
            if (c == '\0')
                c = ' ';
            printf("%c", c);
        }
        printf("\n");
    }
}

char *
test_reset()
{
    oreset();
    term_write("abcde");
    oflush();
    mu_assert(wcscmp(output.text, L"abcde") == 0);

    term_write("\033c");
    oflush();
    mu_assert(oisempty());

    return NULL;
}

char *
test_crlf()
{
    oreset();
    term_write("012");
    term_write("\033[20l"); /* disable '\n' == CRLF */
    term_write("\n3");
    mu_assert(O(3, 1) == '3');

    term_write("\033[20h"); /* enable '\n' == CRLF */
    term_write("\n4");
    mu_assert(O(3, 1) == '3');
    mu_assert(O(0, 2) == '4');

    return NULL;
}

char *
test_movement()
{
    /* Test CSI controls */
    oreset();
    term_write("\033[2B1");     /* Down */
    term_write("\033[e1");      /* Down */
    term_write("\033[2A2");     /* Up */
    term_write("\033[C3");      /* Forward */
    term_write("\033[2a4");     /* Forward */
    term_write("\033[7D5");     /* Backward */
    term_write("\033[2E6");     /* Next line */
    term_write("\033[3F7");     /* Previous line */
    term_write("\033[2G8");     /* Col absolute */
    term_write("\033[10`9");    /* Col absolute */
    term_write("\033[5da");     /* Row absolute */
    term_write("\033[5;1Hb");   /* Absolute */
    term_write("\033[6;5fc");   /* Absolute */
    term_write("\033[2Id");     /* Forward tabulation */

    mu_assert(O(0, 2) == '1');
    mu_assert(O(1, 3) == '1');
    mu_assert(O(2, 1) == '2');
    mu_assert(O(4, 1) == '3');
    mu_assert(O(7, 1) == '4');
    mu_assert(O(1, 1) == '5');
    mu_assert(O(0, 3) == '6');
    mu_assert(O(0, 0) == '7');
    mu_assert(O(1, 0) == '8');
    mu_assert(O(9, 0) == '9');
    mu_assert(O(10,4) == 'a');
    mu_assert(O(0, 4) == 'b');
    mu_assert(O(4, 5) == 'c');

    mu_assert(config.tabsize == 8);
    mu_assert(O(16, 5) == 'd');

    /* Test plain esc controls */
    oreset();
    term_write("\033[2;2H");
    term_write("\033D1");
    term_write("\033M2");
    term_write("\033E3");
    term_write("\2044"); /* C1 0x84 */
    term_write("\2055"); /* C1 0x85 */
    mu_assert(O(1,2) == '1');
    mu_assert(O(2,1) == '2');
    mu_assert(O(0,2) == '3');
    mu_assert(O(1,3) == '4');
    mu_assert(O(0,4) == '5');

    /* Test store / restore */
    oreset();
    term_write("\033[11;11H");
    term_write("1");
    term_write("\0337"); /* store */
    term_write("\033[21;21H");
    term_write("2");
    term_write("\0338"); /* restore */
    term_write("3");

    mu_assert(O(10, 10) == '1');
    mu_assert(O(20, 20) == '2');
    mu_assert(O(11, 10) == '3');

    return NULL;
}

char *
test_erase_line()
{
    oreset();
    /* Erase in line right */
    term_write("\033[10;20H5678\033[10;22;H");
    mu_assert(O(20,9) == '6');
    mu_assert(O(21,9) == '7');
    mu_assert(O(22,9) == '8');
    term_write("\033[0K");
    mu_assert(O(20,9) == '6');
    mu_assert(O(21,9) == 0);
    mu_assert(O(22,9) == 0);

    /* Erase in line left */
    term_write("\033[10;20H5678\033[10;22;H");
    mu_assert(O(20,9) == '6');
    mu_assert(O(21,9) == '7');
    mu_assert(O(22,9) == '8');
    term_write("\033[1K");
    mu_assert(O(20,9) == 0);
    mu_assert(O(21,9) == 0);
    mu_assert(O(22,9) == '8');

    /* Erase whole line, this time with background color */
    term_write("\033[10;20H5678\033[10;22;H");
    mu_assert(O(20,9) == '6');
    mu_assert(O(21,9) == '7');
    mu_assert(O(22,9) == '8');
    mu_assert(B(20,9) == config.background);

    if (config.bce) {
        term_write("\033[42m"); /* Change background */
        term_write("\033[2K");
        mu_assert(O(20,9) == 0);
        mu_assert(O(21,9) == 0);
        mu_assert(O(22,9) == 0);
        mu_assert(B(20,9) != config.background);
    }

    return NULL;
}

char *
test_erase_display()
{
    oreset();
    /* Erase lines below */
    term_write("\033[8;10H1234\033[9;10Habcd\033[10;10H5678\033[9;11H");
    mu_assert(O(9,7)  == '1');
    mu_assert(O(9,8)  == 'a');
    mu_assert(O(10,8) == 'b');
    mu_assert(O(11,8) == 'c');
    mu_assert(O(9,9)  == '5');
    term_write("\033[0J");
    mu_assert(O(9,7)  == '1');
    mu_assert(O(9,8)  == 'a');
    mu_assert(O(10,8) == 0);
    mu_assert(O(11,8) == 0);
    mu_assert(O(9,9)  == 0);

    /* Erase lines above */
    term_write("\033[8;10H1234\033[9;10Habcd\033[10;10H5678\033[9;11H");
    mu_assert(O(9,7)  == '1');
    mu_assert(O(9,8)  == 'a');
    mu_assert(O(10,8) == 'b');
    mu_assert(O(11,8) == 'c');
    mu_assert(O(9,9)  == '5');
    term_write("\033[1J");
    mu_assert(O(9,7)  == 0);
    mu_assert(O(9,8)  == 0);
    mu_assert(O(10,8) == 0);
    mu_assert(O(11,8) == 'c');
    mu_assert(O(9,9)  == '5');

    /* Erase all lines */
    term_write("\033[8;10H1234\033[9;10Habcd\033[10;10H5678\033[9;11H");
    mu_assert(O(9,7)  == '1');
    mu_assert(O(9,8)  == 'a');
    mu_assert(O(10,8) == 'b');
    mu_assert(O(11,8) == 'c');
    mu_assert(O(9,9)  == '5');
    term_write("\033[2J");
    mu_assert(oisempty());

    return NULL;
}

char *
test_newline()
{
    oreset();
    size_t i;
    term_write("1\n");
    for (i = 0; i < output.rows-2; i++) {
        term_write("2\n");
    }
    term_write("3");
    mu_assert(O(0, 0) == '1');
    mu_assert(O(0, 1) == '2');
    mu_assert(O(0, output.rows - 1) == '3');

    /* Expect scroll up */
    term_write("\n");
    mu_assert(O(0, 0) == '2');
    mu_assert(O(0, output.rows - 2) == '3');
    mu_assert(O(0, output.rows - 1) == 0);

    return NULL;
}

char *
test_control_characters()
{
    oreset();
    term_write("\n\v\f1");  /* newlines */
    mu_assert(O(0, 3) == '1');
    term_write("\n\t2");    /* tab */
    mu_assert(O(8, 4) == '2');
    term_write("\b\b3");    /* back */
    mu_assert(O(7, 4) == '3');
    term_write("\r4");      /* carriage return */
    mu_assert(O(0, 4) == '4');

    /* Cancel codes */
    oreset();
    term_write("\033[12\030a");
    mu_assert(O(0, 0) == 'a');
    term_write("\033 \032b");
    mu_assert(O(1, 0) == 'b');

    /* C1 codes not tested elsewhere */
    oreset();
    /* CSI */
    mu_assert(strcmp(response, "") == 0);
    term_write("\233c"); /* should be same as "\033[c" */
    mu_assert(strcmp(response, "\033[?1;0c") == 0);

    return NULL;
}

char *
test_statusreport()
{
    oreset();
    term_write("\033[5n"); /* status? */
    mu_assert(strcmp(response, "\033[0n") == 0); /* OK */

    term_write("\033[6n"); /* cursor position? */
    mu_assert(strcmp(response, "\033[1;1R") == 0);
    term_write("\033[7;12H");
    term_write("\033[6n"); /* cursor position? */
    mu_assert(strcmp(response, "\033[7;12R") == 0);

    term_write("\033[c");
    mu_assert(strcmp(response, "\033[?1;0c") == 0);
    term_write("\033Z"); /* Deprecated version */
    mu_assert(strcmp(response, "\033[?1;0c") == 0);

    return NULL;
}

char *
test_DECALN()
{
    oreset();
    term_write("1234");
    term_write("\033#8");
    mu_assert(O(0, 0) == 'E');
    mu_assert(O(10, 10) == 'E');
    mu_assert(O(output.cols - 1, output.rows - 1) == 'E');

    return NULL;
}

char *
test_ignored_controls()
{
    /* Everything between SOS, PM, and APC and ST is ignored */
    oreset();
    term_write("\033X1\033\\"); /* ST */
    mu_assert(O(0,0) == '\0');

    term_write("\033^1\033\\"); /* PM */
    mu_assert(O(0,0) == '\0');

    term_write("\033_1\033\\"); /* APC */
    mu_assert(O(0,0) == '\0');

    return NULL;
}

char *
test_scrollregion()
{
    oreset();
    term_write("\033[2;3r"); /* Set scrolling region O(*,1) - O(*,2) */
    term_write("\033[?6h");  /* Enable scroll region */
    term_write("1\n2\n3");

    mu_assert(O(0,0) == '\0');
    mu_assert(O(0,1) == '2');
    mu_assert(O(0,2) == '3');
    mu_assert(O(0,3) == '\0');

    term_write("\033[r"); /* Set scrolling region to full screen */
    term_write("\033[3;1H"); /* Move down again, after scrolling region moves cursor */
    for(int i = 0; i < 22; i ++) { /* Write 4 to push '2' to top row */
        term_write("\n4");
    }
    mu_assert(O(0,0)  == '2');
    mu_assert(O(0,1)  == '3');
    mu_assert(O(0,23) == '4');

    /* Redo test without scroll region active */
    oreset();
    term_write("\033[2;3r"); /* Set scrolling region O(*,1) - O(*,2) */
    term_write("\033[?6h");  /* Enable scroll region */
    term_write("\033[r"); /* Reset scroll region */
    term_write("\033[2;1H"); /* Move to O(0,1) */
    term_write("1\n2\n3");

    mu_assert(O(0,0) == '\0');
    mu_assert(O(0,1) == '1');
    mu_assert(O(0,2) == '2');
    mu_assert(O(0,3) == '3');

    return NULL;
}

char *
test_character_attributes()
{
    oreset();
    term_write("1\033[1;33m2\033[0m3");
    mu_assert(F(0,0) == config.foreground);
    mu_assert(B(0,0) == config.background);
    mu_assert(A(0,0) == OATTR_NONE);

    mu_assert(F(1,0) != config.foreground);
    mu_assert(B(1,0) != config.background);
    mu_assert(A(1,0) == OATTR_BOLD);

    mu_assert(F(2,0) == config.foreground);
    mu_assert(B(2,0) == config.background);
    mu_assert(A(2,0) == OATTR_NONE);
    return NULL;
}

char *
test_wraparound()
{
    oreset(); /* No wraparound by default */
    term_write("\033[1;80H"); /* Final cell of first line */
    term_write("\033[?7l"); /* No wraparound */
    term_write("1");
    mu_assert(O(79, 0) == '1');

    term_write("2");
    mu_assert(O(79, 0) == '2'); /* Replace last char without wraparound */

    term_write("\033[?7h"); /* Enable wraparound again */
    term_write("3");
    mu_assert(O(0, 1)  == '3');

    return NULL;
}

char *
test_editing()
{
    oreset();
    term_write("1234567890"); /* Erase characters (no shift) */
    term_write("\033[1;4H");
    term_write("\033[3X"); /* Delete characters */
    mu_assert(O(2,0) == '3');
    mu_assert(O(3,0) == '\0');
    mu_assert(O(4,0) == '\0');
    mu_assert(O(5,0) == '\0');
    mu_assert(O(6,0) == '7');
    mu_assert(O(9,0) == '0');

    oreset();
    term_write("1234567890");
    term_write("\033[1;4H");
    term_write("\033[3P"); /* Delete characters */
    mu_assert(O(2,0) == '3');
    mu_assert(O(3,0) == '7');
    mu_assert(O(6,0) == '0');
    mu_assert(O(7,0) == '\0');

    oreset();
    term_write("1234567890");
    term_write("\033[1;4H");
    term_write("\033[3@"); /* Insert characters */
    mu_assert(O(2,0)  == '3');
    mu_assert(O(3,0)  == '\0');
    mu_assert(O(4,0)  == '\0');
    mu_assert(O(5,0)  == '\0');
    mu_assert(O(6,0)  == '4');
    mu_assert(O(12,0) == '0');
    mu_assert(O(13,0) == '\0');

    oreset();
    term_write("1\n2\n3\n4\n5\n6\n7\n8\n9\n0\n");
    term_write("\033[4;4H");
    term_write("\033[3M"); /* Delete lines */
    mu_assert(O(0,2) == '3');
    mu_assert(O(0,3) == '7');
    mu_assert(O(0,6) == '0');
    mu_assert(O(0,7) == '\0');

    oreset();
    term_write("1\n2\n3\n4\n5\n6\n7\n8\n9\n0\n");
    term_write("\033[4;4H");
    term_write("\033[3L"); /* Insert 3 lines */
    mu_assert(O(0,2)  == '3');
    mu_assert(O(0,3)  == '\0');
    mu_assert(O(0,4)  == '\0');
    mu_assert(O(0,5)  == '\0');
    mu_assert(O(0,6)  == '4');
    mu_assert(O(0,12) == '0');
    mu_assert(O(0,13) == '\0');

    return NULL;
}

char *
test_repeat()
{
    oreset();
    term_write("1");
    term_write("\033[2b"); /* Repeat two times */
    mu_assert(O(0,0) == '1');
    mu_assert(O(1,0) == '1');
    mu_assert(O(2,0) == '1');
    mu_assert(O(3,0) == '\0');
    return NULL;
}

char *
test_col_modes()
{
    oreset();
    term_write("\033[?40h"); /* Allow mode change */
    term_write("012");
    mu_assert(O(0,0) == '0');
    mu_assert(O(1,0) == '1');
    mu_assert(O(2,0) == '2');
    term_write("\033[?3h");
    mu_assert(output.cols == 132);
    mu_assert(output.rows ==  24);
    mu_assert(O(0,0) == '\0'); /* Columns are also cleared */
    mu_assert(O(1,0) == '\0');
    mu_assert(O(2,0) == '\0');

    term_write("012");
    term_write("\033[?95h"); /* Don't clear screen columns change */
    term_write("\033[?3l");
    mu_assert(output.cols == 80);
    mu_assert(output.rows == 24);
    mu_assert(O(0,0) == '0');
    mu_assert(O(1,0) == '1');
    mu_assert(O(2,0) == '2');

    return NULL;
}

char *
test_tabstops()
{
    oreset();
    term_write("\033[3g"); /* no tab stops */
    term_write("\t1");

    term_write("\033[?5W"); /* tabs every eight stop */
    term_write("\033[2;1H");
    term_write("\t2");

    term_write("\033[D\033[0g"); /* remove this */
    term_write("\033[2;12H");
    term_write("\033H"); /* set tabstop */
    term_write("\033[2;1H");
    term_write("\t3\t4");

    mu_assert(O(79,0) == '1');
    mu_assert(O(8,1) == '2');
    mu_assert(O(11,1) == '3');
    mu_assert(O(16,1) == '4');

    return NULL; 
}


char *
test_style()
{
    oreset();
    term_write("1");
    term_write("\033[7m"); /* Set inverse colors for char */
    term_write("2");
    mu_assert(B(0,0) == config.background);
    mu_assert(B(1,0) == config.foreground);
    term_write("\033[?5h"); /* Now invert the whole screen */
    mu_assert(B(0,0) == config.foreground);
    mu_assert(B(1,0) == config.background);
    return NULL;
}

char *
test_cursor()
{
    oreset();
    term_write(" \033[1;1H"); /* Must have a character to have a foreground color */
    term_write("\033[?25l"); /* Disable cursor */
    mu_assert(F(0,0) == config.foreground);
    mu_assert(B(0,0) == config.background);

    term_write("\033[?25h"); /* Enable cursor */
    mu_assert(F(0,0) == config.background);
    mu_assert(B(0,0) == config.foreground);

    return NULL;
}

char *
run_tests()
{
    mu_run_test(test_reset);
    mu_run_test(test_movement);
    mu_run_test(test_crlf);
    mu_run_test(test_erase_line);
    mu_run_test(test_erase_display);
    mu_run_test(test_newline);
    mu_run_test(test_control_characters);
    mu_run_test(test_ignored_controls);
    mu_run_test(test_statusreport);
    mu_run_test(test_DECALN);
    mu_run_test(test_scrollregion);
    mu_run_test(test_wraparound);
    mu_run_test(test_editing);
    mu_run_test(test_repeat);
    mu_run_test(test_col_modes);
    mu_run_test(test_style);
    mu_run_test(test_tabstops);
    mu_run_test(test_cursor);
    return (char*)NULL;
}

int main()
{
    struct term_push_callbacks cb = {
        .write_host = oresponse,
        .write_screen = owrite_cb,
        .write_finished = owrite_finished_cb,
        .clear_line = oclear_cb,
        .res_change = oreschange_cb,
    };
    util_init();
    term_init(&cb);
    term_resize(80, 24);

    char *result = run_tests();

    printf("Run %d test(s) with %d check(s)\n", tests_run, tests_checks);
    if (result != NULL) {
        printf("FAIL: %s\n", result);
    }
    else {
        printf("OK\n");
    }

    odestroy();
    return result != NULL;
}
