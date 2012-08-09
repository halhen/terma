#ifndef _TYPES_H
#define _TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <sys/time.h>
#include <X11/X.h>
#include <X11/keysym.h>

typedef uint32_t  color_t;

struct keymap_t {
    KeySym          key;
    unsigned int    mod;
    const char     *out;
    const char     *capname;
};

struct config_t {
    char  *font;
    char  *bold_font;
    int   tabsize;

    unsigned int   foreground;
    unsigned int   background;

    int   rows, cols;

    bool    bce; /* Background color erase */

    int   HZ;
    int   HZ_passive;

    unsigned int    color[256];
    struct timeval  blink_delay;
};


#endif
