
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>

#include "types.h"

/* Callback to draw function */
typedef void (*write_screen_t)(size_t col, size_t row, wchar_t text[], size_t length, color_t fg, color_t bg, bool bold, bool underline);
typedef void (*clear_line_t)(size_t col, size_t row, size_t length, color_t bg);
typedef void (*write_finished_t)();
typedef void (*write_host_t)(const char *str, size_t n);
typedef void (*res_change_t)(size_t cols, size_t rows);

struct term_push_callbacks {
    write_host_t        write_host;
    write_screen_t      write_screen;
    write_finished_t    write_finished;
    clear_line_t        clear_line;
    res_change_t        res_change;
};

void term_gc();
void term_flush();
bool term_handle_keypress(KeySym key, uint32_t mod);
void term_init(struct term_push_callbacks*);
void term_invalidate();
void term_resize(size_t cols, size_t rows);
size_t term_write(char *utf8s);
