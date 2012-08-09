#include <stdio.h>
#include <stdarg.h>

#include "config.h"
#include "keymap.h"

#define LENGTH(a) (sizeof(a) / sizeof(a[0]))

static void
set(char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    printf("\t");
    (void)vprintf(fmt, args);
    printf(",\n");
    va_end(args);

}

static void
set_key(const char *capname, const char *out)
{
    size_t o, b;
    char buf[100];

    for (o = b = 0; out[o] != '\0' && b < LENGTH(buf) - 1; o++) {
        /* Convert \033 to \E */
        if (out[o] == '\033') {
            buf[b++] = '\\';
            buf[b++] = 'E';
        }
        /* Convert non-printing characters to their accoring CTRL sequence */
        else if (out[o] < (char)0x20) {
            buf[b++] = '^';
            buf[b++] = out[o] + (char)0x40;
        } else {
            buf[b++] = out[o];
        }
    }
    buf[b] = '\0';

    set("%s=%s", capname, buf);
    return;
}

int main()
{
    size_t i;

    printf("\n\n# Values generated from config.h\n");
    set("cols#%d", config.cols);
    set("lines#%d", config.rows);

    set("colors#%d", LENGTH(config.color));
    set("pairs#%d", LENGTH(config.color) * LENGTH(config.color));

    set("it#%d", config.tabsize);

    if (config.bce) {
        set("bce");
    }

    /* Key mappings */
    for (i = 0; i < LENGTH(keymap); i++) {
        if (keymap[i].capname != NULL) {
            set_key(keymap[i].capname, keymap[i].out);
        }
    }

    return 0;

}
