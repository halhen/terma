#include <assert.h>
#include <errno.h>
#include <string.h>

#include <sys/time.h>

/* term_function_key constants */
#include <X11/keysym.h>

#include "terminal.h"
#include "escparse.h"
#include "util.h"

#include "config.h"
#include "keymap.h"

void term_cursor(size_t x, size_t y); /* set cursor position to (x, y) within page */
void term_delete(size_t from, size_t to, size_t stop);
void term_destroy();
bool term_do_control_char(char c);
void term_erase(size_t from, size_t to);
void term_invalidate_range(size_t from, size_t to);
void term_newline(bool carriage_return);
void term_reset();
void term_setcharattributes(int32_t arg[]);
void term_setscrollregion(size_t top, size_t bottom);
void term_tab_move(int n);
void term_writechar(const wchar_t ucs2char);

void esc_dispatch(char function, char intermediate);
void csi_dispatch(char function, int32_t arg[], char privflag);
void osc_dispatch(char *arg, size_t length);

/* Data types and globals {{{ */

enum charset_mode_t {
    G0 = 0,
    G1 = 1,
    G2 = 2,
    G3 = 3,
    NUM_CHARSET_MODES
};

enum col_mode_t {
    COL_ANY, /*    any */
    COL_80,  /*  80x24 */
    COL_132, /* 132x24 */
};

enum charset_t {
    CHARSET_UTF8 = 0,
    CHARSET_DEC,
    CHARSET_UK,
    CHARSET_US,
    CHARSET_NL,
    CHARSET_FI,
    CHARSET_NO,
    CHARSET_FR,
    CHARSET_CA,
    CHARSET_GE,
    CHARSET_IT,
    CHARSET_SP,
    CHARSET_SW,
    CHARSET_CH,
    NUM_CHARSETS
};

typedef uint8_t char_attr_t;
enum {
    CHAR_ATTR_NONE      = 0x00,
    CHAR_ATTR_BOLD      = 0x01,
    CHAR_ATTR_UNDERLINE = 0x02,
    CHAR_ATTR_BLINK     = 0x04,
    CHAR_ATTR_INVERSE   = 0x08,
    CHAR_ATTR_INVISIBLE = 0x10,
};

struct glyph_t {
    wchar_t     c;
    color_t     foreground;
    color_t     background;
    char_attr_t attr;
};


static struct {
    size_t          cols, rows;
    struct glyph_t *text; /* circular buffer */
    size_t          ring_top; /* top of scroll ring within page address space */

    size_t          x, y; /* cursor position (scren address space) */

    struct {
        size_t      top; /* First row of screen */
        size_t      bottom; /* Last rows of screen */
        size_t      height; /* Precalculated number of rows */
    } page, margin; /* margins is the latest configured top and bottom margins,
                     * page is the current active (depenedent on e.g. origin mode
                     */
    struct {
        size_t      left;
        size_t      right; /* First *clean* character */
    }              *dirty; /* For each line, what is the leftmost and rightmost dirty character */
    bool            cursor_dirty;
    wchar_t         lastchar;   /* Most recently printed character. TODO: remove for speed? */

    /* mode flags */
    /* Some of these flags are strangely named, so to make "false" be the default */
    bool            crlf;       /* Does \n also include \r ?*/
    bool            autowrap;   /* Do we automatically newline at when adding at end-of-line? */
    bool            reverse_vid;/* Should the colors be reversed? */
    bool            allow_deccolm; /* Is DECCOLM allowed? */
    bool            show_cursor;
    bool            blink_cursor;
    bool            insert;     /* Insert characters instead of replace? */
    bool            origin_mode;

    bool            wrap_next;  /* True when a char is written at the right edge,
                                   and the next char may be put on the next line */

    enum charset_t  charset[NUM_CHARSET_MODES];
    enum charset_mode_t  charset_mode;

    enum col_mode_t col_mode;
    bool            no_clear_on_col_mode_change; /* DECNCSM */

    struct {
        uint32_t    foreground;
        uint32_t    background;
        char_attr_t attr;
    } style;
    bool            blinked;    /* true if blinked characters are currently hidden */

    struct {
        size_t      x, y; /* cursor position */
        bool        autowrap;
        bool        origin_mode;
        uint32_t    foreground;
        uint32_t    background;
        char_attr_t attr;
        enum charset_t  charset[NUM_CHARSET_MODES];
        enum charset_mode_t  charset_mode;
    } saved_cur;  /* Store DECSC / DECRC info */

    bool           *tabstop; /* array, one element per col */
} terminal;

static struct term_push_callbacks *term_cb;

/* }}} */

/* Helper functions {{{ */

#define X (terminal.x)
#define BOL    0
#define EOL    (terminal.cols - 1)

#define Y (terminal.y - terminal.page.top)
#define TOP    0
#define BOTTOM (terminal.rows - 1)


/* Return cell index within current page. This is typically
 * the address function one is to use when editing, scrolling, etc.
 */
static inline size_t PAGE(size_t x, size_t y)
{
    y = min(y, terminal.page.height - 1);
    y += terminal.ring_top;
    if (y >= terminal.page.height) {
        y -= terminal.page.height;
    }

    assert (y < terminal.page.height);
    y += terminal.page.top;

    return y * terminal.cols + min(x, EOL);
}

/* Return cell index, i.e. any character cell on the screen regardless of
 * the current page. 0-indexed, (x, y), i.e. on a 80x24 terminal, the top
 * left cell is 0,0 and the bottom right is 79,23.
 */
static inline size_t SCREEN(size_t x, size_t y)
{
    y = min(y, BOTTOM);

    if (between(y, terminal.page.top, terminal.page.bottom)) {
        return PAGE(x, y - terminal.page.top);
    }
    else {
        return y * terminal.cols + min(x, EOL);
    }
}

void
term_align(size_t *p1, size_t *p2, size_t *p3)
/* Ring buffer gotcha */
/* Call this before code that requires the top row to start at terminal.x */
/* arguments are updated to reflect the new alignment */
/* an aligned buffer also makes execution a little faster */
{
    if (terminal.ring_top != 0) {
        debug("Realigning");
        struct glyph_t *newtext = emalloc(terminal.cols * terminal.rows * sizeof(*newtext));

        size_t rowbytes = terminal.cols * sizeof(*newtext);

        size_t rowsdone = 0, numrows = 0;

        /* 1. Above top margin */
        numrows = terminal.margin.top;
        memcpy(newtext,
                    terminal.text,
                    numrows * rowbytes);
        rowsdone += numrows;

        /* 2. ring_top to bottom margin */
        numrows = terminal.margin.height - terminal.ring_top;
        memcpy(newtext + rowsdone * terminal.cols,
                    terminal.text + (terminal.margin.top + terminal.ring_top) * terminal.cols,
                    numrows * rowbytes);
        rowsdone += numrows;

        /* 3. top margin to ring_top */
        numrows = terminal.ring_top;
        memcpy(newtext + rowsdone * terminal.cols,
                    terminal.text + terminal.margin.top * terminal.cols,
                    numrows * rowbytes);
        rowsdone += numrows;

        /* 4. Below bottom margin */
        numrows = terminal.rows - (terminal.margin.top + terminal.margin.height);
        memcpy(newtext + rowsdone * terminal.cols,
                    terminal.text + (terminal.margin.bottom+ 1) * terminal.cols,
                    numrows * rowbytes);
        rowsdone += numrows;

        size_t topmargin = terminal.margin.top * terminal.cols;
        size_t bottommargin = terminal.margin.bottom * terminal.cols;
        size_t topring = (terminal.margin.top + terminal.ring_top) * terminal.cols;
#define REALIGN(p) do {if (p) {\
                        if(between(*p, topmargin, bottommargin)) { \
                            if (between(*p, topmargin, topring - 1)) \
                                *p += terminal.margin.height * terminal.cols; \
                            *p -= terminal.ring_top * terminal.cols; \
                        } \
                    }} while(0)

        REALIGN(p1);
        REALIGN(p2);
        REALIGN(p3);
#undef REALIGN

        free(terminal.text);
        terminal.text = newtext;
        terminal.ring_top  = 0;

    }
}


void
term_dump(struct glyph_t *text)
/* Useful for debugging */
{
    size_t y, x;
    wchar_t t;

    printf("-----------------------------\n");
    for (y = 0; y < terminal.rows; y ++) {
        for (x = 0; x < terminal.cols; x ++) {
            /*t = text[SCREEN(x, y)].c;*/
            t = text[y * terminal.cols + x].c;
            printf("%c", t != '\0' ? (char)t & 0xFF : ' ');
        }
        printf("\n");
    }
    printf("-----------------------------\n");
}

/* }}} */

void
term_gc()
/* Set terminal in an optimal state. Not nescessary, but may improve
 * performance later
 */
{
    term_align(NULL, NULL, NULL);
}

void
term_cursor(size_t x, size_t y)
{
    terminal.x = min(x, EOL);
    terminal.y = min(y + terminal.page.top, terminal.page.bottom);

    terminal.wrap_next = false;
    terminal.cursor_dirty = true;
}



void
term_fill(size_t from, size_t to, wchar_t c)
{
    if (from > to) {
        term_align(&from, &to, NULL);
    }

    size_t i;
    struct glyph_t *current;

    for (i = from, current = terminal.text + from; i <= to; i++) {
        current->c = c;
        current->foreground = terminal.style.foreground;
        current->background = terminal.style.background;
        current->attr       = terminal.style.attr;

        current += 1;
    }

    term_invalidate_range(from, to);
}


void
term_erase(size_t from, size_t to)
{
    if (from > to) {
        term_align(&from, &to, NULL);
    }

    memset(terminal.text + from, 0, (to - from + 1) * sizeof(*terminal.text));

    /* BCE - Background Color Erase */
    if (config.bce) {
        size_t i;
        struct glyph_t *g;
        for (i = from, g = terminal.text + from; i <= to; i++, g++) {
            g->foreground = terminal.style.foreground;
            g->background = terminal.style.background;
        }
    }

    term_invalidate_range(from, to);
}

void
term_delete(size_t from, size_t to, size_t stop)
/* Delete characters, i.e. move the following back and erase those at the end
 * from: cell index to start erasing (inclusive)
 * to  : cell index to end erasing (inclusive)
 * stop: cell index of last character to move (inclusive)
 * Arguments are cell indexes */
{
    if (from > to || to > stop) {
        term_align(&from, &to, &stop);
    }

    struct glyph_t *start;
    size_t to_delete = to - from + 1;
    size_t to_move   = stop - to;

    start = terminal.text + from;

    memmove(start, start + to_delete, to_move * sizeof(*start));
    term_erase(from + to_move, stop);

    term_invalidate_range(from, stop);
}

void
term_insert(size_t from, size_t num, size_t stop)
/* Insert num blank cells starting at from and push the following forward,
   no longer than to stop
 */
{
    if (from > stop) {
        term_align(&from, &stop, NULL);
    }

    struct glyph_t *start;

    num = min(num, stop - from);
    start = terminal.text + from;

    memmove(start + num, start, (stop + 1 - (from + num)) * sizeof(*start));
    term_erase(from, from + num - 1);

    term_invalidate_range(from, stop);
}

void
term_newline(bool carriage_return)
{
    size_t bottom = terminal.margin.bottom;

    if (terminal.y >= bottom) {
        terminal.ring_top += 1;
        if (terminal.ring_top >= terminal.margin.height) {
            terminal.ring_top = 0;
        }
        term_erase(PAGE(BOL,BOTTOM), PAGE(EOL,BOTTOM));
        term_invalidate();
    }

    size_t x = carriage_return ? BOL : X;
    term_cursor(x, Y + 1);
}


void
term_invalidate()
{
    size_t i;
    for (i = 0; i < terminal.rows; i ++) {
        terminal.dirty[i].left  = BOL;
        terminal.dirty[i].right = EOL + 1;
    }
}

void
term_invalidate_range(size_t start, size_t end)
{
    size_t y;
    size_t xstart, ystart, xend, yend;


    if (start > end) {
        term_align(&start, &end, NULL);
    }
    assert(start <= end);

    xstart = start % terminal.cols;
    xend   = end   % terminal.cols;

    ystart = start / terminal.cols;
    yend   = end   / terminal.cols;

    assert(ystart < terminal.rows);
    assert(yend < terminal.rows);

    for (y = ystart; y <= yend; y++) {
        terminal.dirty[y].left  = min((y == ystart ? xstart : BOL), terminal.dirty[y].left);
        terminal.dirty[y].right = max((y == yend   ? xend   : EOL) + 1, terminal.dirty[y].right);
    }
}

void
term_invalidate_blinkers() {
    size_t row, col;
    struct glyph_t *g;

    for (row = 0; row < terminal.rows; row ++) {
        g = terminal.text + SCREEN(BOL, row);
        for (col = 0; col < terminal.cols; col ++, g++) {
            if (g->attr & CHAR_ATTR_BLINK) {
                terminal.dirty[row].left  = min(terminal.dirty[row].left, col);
                terminal.dirty[row].right = max(terminal.dirty[row].right, col + 1);
            }
        }
    }
}



void
term_flush_section(size_t col, size_t row, wchar_t *text, size_t length, color_t fg, color_t bg, char_attr_t attr)
{
    if (*text == '\0') {
        bool reverse = terminal.reverse_vid ^ (bool)(attr & CHAR_ATTR_INVERSE);
        if (config.bce) {
            (*term_cb->clear_line)(col, row, length,
                    reverse ? fg : bg);
        }
        else {
            (*term_cb->clear_line)(col, row, length,
                    reverse ? config.foreground : config.background);
        }
    }
    else if ((attr & CHAR_ATTR_INVISIBLE) ||
            ((attr & CHAR_ATTR_BLINK) && terminal.blinked)) {
        (*term_cb->clear_line)(col, row, length,
                terminal.reverse_vid ? fg : bg);
    }
    else {
        if (attr & CHAR_ATTR_INVERSE) {
            color_t tmp = fg;
            fg = bg;
            bg = tmp;
        }

        if (terminal.reverse_vid) {
            color_t tmp = fg;
            fg = bg;
            bg = tmp;
        }

        (*term_cb->write_screen)(col, row, text, length,
                                 fg, bg,
                                 attr & CHAR_ATTR_BOLD,
                                 attr & CHAR_ATTR_UNDERLINE);
    }
}

void
term_flush_cursor()
{
    wchar_t c;
    static size_t last_cursor_X, last_cursor_Y;

    size_t last_cursor = SCREEN(last_cursor_X, last_cursor_Y);

    c = terminal.text[last_cursor].c;
    term_flush_section(last_cursor_X, last_cursor_Y,
                       &c, 1,
                       terminal.text[last_cursor].foreground,
                       terminal.text[last_cursor].background,
                       terminal.text[last_cursor].attr);

    last_cursor_X = X;
    last_cursor_Y = Y;
    terminal.cursor_dirty = false;

    if (terminal.show_cursor && (!terminal.blink_cursor || !terminal.blinked)) {
        size_t cursor = SCREEN(X, Y);

        c = terminal.text[cursor].c;
        term_flush_section(X, Y,
                           &c, 1,
                           config.foreground,
                           config.background,
                           terminal.text[cursor].attr ^ CHAR_ATTR_INVERSE);

    }
}

bool /* Return true if we painted */
term_flushlines()
{
    size_t row;
    size_t col_start, col_stop, col_this;
    struct glyph_t *start, *this;

    bool retval = false;
    wchar_t *buffer = emalloc(terminal.cols * sizeof(*buffer));

    for (row = 0; row < terminal.rows; row ++) {
        col_start = terminal.dirty[row].left;
        col_stop  = terminal.dirty[row].right;
        start = terminal.text + SCREEN(col_start, row);

        for (col_this = col_start, this = start;
             col_this < col_stop;
             col_this ++, this++) {

            buffer[col_this] = this->c;

            if ((start->c != '\0')!=(this->c != '\0') || /* NULL vs non-NULL */
                start->background != this->background ||
                start->foreground != this->foreground ||
                start->attr       != this->attr)
            {
                term_flush_section(col_start, row,
                                   buffer + col_start,
                                   col_this - col_start,
                                   start->foreground,
                                   start->background,
                                   start->attr);
                col_start = col_this;
                start = this;
                retval = true;
            }

        }
        if (col_stop > col_start) {
            term_flush_section(col_start, row,
                               buffer + col_start,
                               col_this - col_start,
                               start->foreground,
                               start->background,
                               start->attr);
            retval = true;
        }

        terminal.dirty[row].left = terminal.dirty[row].right = 0;
    }

    free(buffer);
    return retval;
}

void
term_flush()
{
    struct timeval now;
    static struct timeval next_blink;

    gettimeofday(&now, NULL);
    if (timercmp(&now, &next_blink, >)) {
        terminal.blinked = !terminal.blinked;
        timeradd(&now, &config.blink_delay, &next_blink);
        term_invalidate_blinkers();
    }

    if (term_flushlines() || terminal.cursor_dirty) {
        term_flush_cursor();
        (*term_cb->write_finished)();
    }
}

size_t /* Return the number of chars used from utf8s */
term_write(char *utf8s)
{
    size_t n;
    wchar_t ucs2char;
    char* utf8s_orig = utf8s;

    do {
        if (*utf8s == '\0') {
            break;
        }
        else if (term_do_control_char(*utf8s)) {
            n = 1;
        }
        else if ((n = utf8towchar(utf8s, &ucs2char)) > 0) {
            term_writechar(ucs2char);
        }

        utf8s += n;
    } while (n);

    return utf8s - utf8s_orig;
}

void
term_init(struct term_push_callbacks *callbacks)
{
    term_cb = callbacks;
    esc_init(esc_dispatch, csi_dispatch, osc_dispatch);

    term_reset();

    atexit(term_destroy);
}

void
term_tabs_clear()
{
    size_t i;
    for (i = 0; i < terminal.cols; i ++) {
        terminal.tabstop[i] = false;
    }
}

void
term_tabs_every(size_t n)
{
    size_t i;
    for (i = n; i < terminal.cols; i += n) {
        terminal.tabstop[i] = true;
    }
}

void
term_tab_move(int n)
/* Go n tabs forward (or backward if negative) */
{
    size_t x;
    int direction = n > 0 ? 1 : -1;
    n *= direction; /* abs(n) */

    for (x = X; x <= EOL && n; x += direction) {
        if (terminal.tabstop[x]) {
            n -= 1;
            if (n == 0)
                break;
        }
    }
    term_cursor(x, Y);
}

void
term_resize(size_t cols, size_t rows)
{
    debug("%lux%lu", (unsigned long)cols, (unsigned long)rows);
    switch(terminal.col_mode) {
        case COL_ANY:
            /* Use resolution in arguments */
            break;
        case COL_80:
            debug("80 cols resolution set, overriding parameters");
            cols = 80;
            rows = 24;
            break;
        case COL_132:
            debug("132 cols resolution set, overriding parameters");
            cols = 132;
            rows = 24;
            break;
    }

    if (cols < 1 || rows < 1) {
        return;
    }

    if (terminal.cols  == cols && terminal.rows == rows) {
        return;
    }

    term_align(NULL, NULL, NULL);

    size_t i;
    size_t newsize = cols * rows * sizeof(*terminal.text);

    struct glyph_t *newtext = emalloc(newsize);
    memset(newtext, 0, newsize);
    /* Transfer old lines */
    for (i = 0; i < min(rows, terminal.rows); i ++) {
        memcpy(newtext +       i * cols,
               terminal.text + i * terminal.cols,
               sizeof(*terminal.text) * min(cols, terminal.cols));
    }

    if (terminal.text != NULL) {
        free(terminal.text);
    }

    terminal.text = newtext;
    terminal.cols = cols;
    terminal.rows = rows;

    /* TODO: really reset tabstops here ? */
    terminal.tabstop = erealloc(terminal.tabstop, cols);
    term_tabs_clear();
    if (config.tabsize > 0)
        term_tabs_every(config.tabsize);

    term_setscrollregion(-1, -1);
    term_cursor(X, Y); /* Reset cursor */

    terminal.dirty = realloc(terminal.dirty, rows * sizeof(*terminal.dirty));
    term_invalidate();

    /* Notify */
    if (term_cb->res_change != NULL) {
        (*term_cb->res_change)(cols, rows);
    }
}


void
term_destroy()
{
    debug(".");
    free(terminal.text);
    free(terminal.dirty);
    free(terminal.tabstop);
}

bool
term_do_control_char(char c)
/* Test if c is a control char; if so execute the control and return true
 * Else return false
 */
{
    unsigned char uc = (unsigned char)c;

    if (esc_handle(uc))
        return true;

    if (uc <= 0033) {
        /* C0 control characters */
        /* Digital VT100 User Guide, Chapter 3, Table 3-10 */
        switch (uc) {
        case 0005: /* ENQ */
            /* TODO: implement*/
            break;
        case '\a': /* 0007 */
            /* TODO: implement */
            break;
        case '\b': /* 0010 */
            term_cursor(X - 1, Y);
            break;
        case '\t': /* 0011 */
            term_tab_move(1);
            break;
        case '\n': /* 0012 */
        case '\v': /* 0013 */
        case '\f': /* 0014 */
            term_newline(terminal.crlf);
            break;
        case '\r': /* 0015 */
            term_cursor(BOL, Y);
            break;
        case 0016: /* SO */
            /* TODO: Implement */
            terminal.charset_mode = G1;
            break;
        case 0017: /* SI */
            terminal.charset_mode = G0;
            break;
        case 0021: /* XON */
            /* TODO: Implement */
            break;
        case 0023: /* XOFF */
            /* TODO: Implement */
            break;
        case 0030: /* CAN */
        case 0032: /* SUB */
        case 0033: /* ESC */
            warning("Control character 0%o should have been handled in esc_handle", uc);
            break;
        default: /* Other characters are silently consumed */
            break;
        }

        return true;
    }

    if (between(uc, 0x80, 0x9f)) {
        /* C1 control characters */
        /* http://invisible-island.net/xterm/ctlseqs/ctlseqs.html */
        switch(uc) {
        case 0x84: /* IND */
        case 0x85: /* NEL */
        case 0x88: /* HTS */
        case 0x8d: /* RI */
        case 0x8e: /* SS2 */
        case 0x8f: /* SS3 */
        case 0x90: /* DCS */
        case 0x96: /* SPA */
        case 0x97: /* EPA */
        case 0x98: /* SOS */
        case 0x9a: /* DECID */
        case 0x9b: /* CSI */
        case 0x9c: /* ST */
        case 0x9d: /* OCS */
        case 0x9e: /* PM */
        case 0x9f: /* APC */
            /* TODO: Breaks ongoing sequence? */
            debug("C1 control char: 0x%02x", uc);
            term_do_control_char('\033');
            term_do_control_char(uc - 0x40);
            return true;
        }
    }

    return false;
}

void
term_writechar(wchar_t ch)
{
    if (terminal.charset[terminal.charset_mode] == CHARSET_DEC) {
        if (ch > 0x5f) {
            ch -= 0x5f;
        }
    }

    debug("%lc (%02x)", ch, ch);

    if (terminal.insert) {
        term_insert(PAGE(X,Y), 1, PAGE(EOL,Y));
    }

    if ((X >= EOL) && terminal.wrap_next && terminal.autowrap) {
        term_newline(true);
    }


    struct glyph_t *g = terminal.text + PAGE(X,Y);

    g->c = ch;
    g->foreground = terminal.style.foreground;
    g->background = terminal.style.background;
    g->attr       = terminal.style.attr;

    terminal.dirty[terminal.y].left  = min(X, terminal.dirty[terminal.y].left);
    terminal.dirty[terminal.y].right = max(X + 1, terminal.dirty[terminal.y].right);

    if (X < EOL) {
        term_cursor(X + 1, Y);
        terminal.wrap_next = false;
    }
    else {
        terminal.wrap_next = true;
    }

    terminal.lastchar = ch;
}

void
term_writechar_times(const wchar_t ch, size_t times)
{
    if (ch) {
        while (times--) {
            term_writechar(ch);

        }
    }
}


void
term_reset()
{
    terminal.style.foreground = config.foreground;
    terminal.style.background = config.background;
    terminal.style.attr       = CHAR_ATTR_NONE;

    terminal.autowrap = true;
    terminal.lastchar = '\0';
    terminal.wrap_next = false;
    terminal.reverse_vid = false;
    terminal.reverse_vid = false;
    terminal.insert = false;
    terminal.origin_mode = true;

    terminal.show_cursor = true;
    terminal.blink_cursor = false;

    memset(terminal.charset, 0, sizeof(terminal.charset));
    terminal.charset_mode = G0;

    terminal.col_mode = COL_ANY;
    terminal.no_clear_on_col_mode_change = false;

    if (terminal.cols != 0 && terminal.rows != 0) {
        term_cursor(BOL, TOP);
        if (terminal.text != NULL)
            memset(terminal.text, 0, terminal.cols * terminal.rows * sizeof(*terminal.text));
        terminal.ring_top = 0;

        term_setscrollregion(-1, -1);
    }

    term_invalidate();
}

void
term_report_cursor_pos()
{
    char buf[30];
    sprintf(buf, "\033[%lu;%luR",
                        (unsigned long)terminal.y + 1,
                        (unsigned long)terminal.x + 1);
    term_cb->write_host(buf, strlen(buf));
}

void
term_set_originmode(bool origin)
{
    if (!origin) {
        terminal.page.top    = terminal.margin.top;
        terminal.page.bottom = terminal.margin.bottom;
        terminal.page.height = terminal.margin.height;
    }
    else {
        terminal.page.top    = TOP;
        terminal.page.bottom = BOTTOM;
        terminal.page.height = BOTTOM - TOP + 1;
    }
}

void
term_setscrollregion(size_t top, size_t bottom)
/* 1-indexed */
/* from=-1 means top, to=-1 means bottom */
{
    top    = (top    == (size_t)-1) ?             1 : top;
    bottom = (bottom == (size_t)-1) ? terminal.rows : bottom;

    if (bottom <= top)
        return;

    term_align(NULL, NULL, NULL);

    terminal.margin.top    = top - 1;
    terminal.margin.bottom = bottom - 1;
    terminal.margin.height = bottom - top + 1;

    term_set_originmode(terminal.origin_mode);
}


void
term_setcharattributes(int32_t arg[CSI_MAXARGS])
{
    /* TODO: Add from http://en.wikipedia.org/wiki/ANSI_escape_code */
    /* TODO: http://www.askapache.com/linux/zen-terminal-escape-codes.html#X-364_iBCS2 */
    size_t i;
    int c;

    for (i = 0; i < CSI_MAXARGS; i ++) {
        if (arg[i] == -1)
            break;

        c = arg[i];
        switch(c) {
            case 0:
                terminal.style.attr = CHAR_ATTR_NONE;
                terminal.style.foreground = config.foreground;
                terminal.style.background = config.background;
                continue;
            case 1:
                terminal.style.attr |= CHAR_ATTR_BOLD;
                continue;
            case 4:
                terminal.style.attr |= CHAR_ATTR_UNDERLINE;
                continue;
            case 5:
                terminal.style.attr |= CHAR_ATTR_BLINK;
                continue;
            case 7:
                terminal.style.attr |= CHAR_ATTR_INVERSE;
                continue;
            case 8:
                terminal.style.attr |= CHAR_ATTR_INVISIBLE;
                continue;
            case 21:
            case 22:
                terminal.style.attr &= ~CHAR_ATTR_BOLD;
                continue;
            case 24:
                terminal.style.attr &= ~CHAR_ATTR_UNDERLINE;
                continue;
            case 25:
                terminal.style.attr &= ~CHAR_ATTR_BLINK;
                continue;
            case 27:
                terminal.style.attr &= ~CHAR_ATTR_INVERSE;
                continue;
            case 28:
                terminal.style.attr &= ~CHAR_ATTR_INVISIBLE;
                continue;
            case 39:
                terminal.style.foreground = config.foreground;
                continue;
            case 49:
                terminal.style.background = config.background;
                continue;
        }

        if (between(c, 30, 37)) {
            terminal.style.foreground = config.color[c - 30];
            continue;
        }
        if (between(c, 40, 47)) {
            terminal.style.background = config.color[c - 40];
            continue;
        }

        if (between(c, 90, 97)) {
            terminal.style.foreground = config.color[(c - 90) + 8];
            continue;
        }
        if (between(c, 100, 107)) {
            terminal.style.background = config.color[(c - 100) + 8];
            continue;
        }

        if ((c == 38 || c == 48) && arg[i + 1] == 5) {
            if (i < sizeof(arg) - 2) {
                int color_id = arg[i + 2] % sizeof(config.color);

                if (c == 38) {
                    terminal.style.foreground = config.color[color_id];
                } else {
                    terminal.style.background = config.color[color_id];
                }
                i += 2;
                continue;
            }
            else {
                warning("Too few parameters left for %d", c);
            }
        }

        warning("Unknown style: %d", c);
    }

    debug("%d", terminal.style.attr);

}




#define CSI_DEFAULT(variable, value) (variable <= 0 ? (variable) = (value) : (variable))

#define CSI_DUMP "ESC[%c%d;%d;%d;%d;%c", privflag ? privflag: ' ', arg[0], arg[1], arg[2], arg[3], function
#define CSI_UNKNOWN warning("Unknown CSI: "CSI_DUMP);
#define CSI_TODO    warning("Not yet implemented CSI: "CSI_DUMP);
#define CSI_IGNORED  warning("Ignored CSI: "CSI_DUMP);

/* Reference: http://web.mit.edu/dosathena/doc/www/ek-vt520-rm.pdf */
void
csi_dispatch(char function, int32_t arg[CSI_MAXARGS], char privflag)
{
    debug(CSI_DUMP);

    switch (function) {
    /* CURSOR MOVEMENT */
    case 'A': /* CUU - Cursor Up */
        CSI_DEFAULT(arg[0], 1);
        term_cursor(X, Y - arg[0]);
        break;
    case 'B': /* CUD - Cursor Down */
    case 'e': /* VPR - Line position relative */
        CSI_DEFAULT(arg[0], 1);
        term_cursor(X, Y + arg[0]);
        break;
    case 'C': /* CUF - Cursor Forward */
    case 'a': /* HPR - Character Position Relative */
        CSI_DEFAULT(arg[0], 1);
        term_cursor(X + arg[0], Y);
        break;
    case 'D': /* CUB - Cursor Backward */
        CSI_DEFAULT(arg[0], 1);
        term_cursor(X - arg[0], Y);
        break;
    case 'E': /* CNL - Cursor Next Line */
        CSI_DEFAULT(arg[0], 1);
        term_cursor(BOL, Y + arg[0]);
        break;
    case 'F': /* CPL - Cursor Previous Line */
        CSI_DEFAULT(arg[0], 1);
        term_cursor(BOL, Y - arg[0]);
        break;
    case 'G': /* CHA - Cursor Character Absolute */
    case '`': /* HPA - Character position absolute */
        CSI_DEFAULT(arg[0], 1);
        term_cursor(arg[0] - 1, Y);
        break;
    case 'd': /* VPA - Line position absolute */
        CSI_DEFAULT(arg[0], 1);
        term_cursor(X, arg[0] - 1);
        break;
    case 'H': /* CUP - Cursor Position */
    case 'f': /* HVP - Horizontal and Vertical Position */
        CSI_DEFAULT(arg[0], 1);
        CSI_DEFAULT(arg[1], 1);
        term_cursor(arg[1] - 1, arg[0] - 1);
        break;
    case 'I': /* CHT - Cursor Forward Tabulation */
        CSI_DEFAULT(arg[0], 1);
        term_tab_move(arg[0]);
        break;
    /* Editing */
    case 'L': /* IL - Insert Lines */
        CSI_DEFAULT(arg[0], 1);
        term_insert(PAGE(BOL, Y), arg[0] * terminal.cols, PAGE(EOL, BOTTOM));
        break;
    case 'M': /* DL - Delete Lines */
        CSI_DEFAULT(arg[0], 1);
        term_delete(PAGE(BOL, Y), PAGE(EOL, Y + arg[0] - 1), PAGE(EOL, BOTTOM));
        break;
    case '@': /* ICH - Insert Character */
        CSI_DEFAULT(arg[0], 1);
        term_insert(PAGE(X, Y), arg[0], PAGE(EOL, Y));
        break;
    case 'P': /* DCH - Delete Character */
        CSI_DEFAULT(arg[0], 1);
        term_delete(PAGE(X, Y), PAGE(X + arg[0] - 1, Y), PAGE(EOL, Y));
        break;
    case 'b': /* REP - Repeat the preceding character */
        term_writechar_times(terminal.lastchar, CSI_DEFAULT(arg[0], 1));
        break;
    /* Settings */
    case 'h': /* SM, DECSET - Set mode */
    case 'l':/* RM, DECRST - Reset mode */
       {for (int i = 0; i < CSI_MAXARGS && arg[i] != -1; i ++) {
            int32_t a = arg[i];

            switch(privflag) {
            case '\0':
                switch(a) {
                case 4: /* IRM - Insert mode */
                    terminal.insert = (function == 'h');
                    break;
                case 20: /* LNM - Automatic formfeed */
                    terminal.crlf = (function == 'h');
                    continue;
                default:
                    CSI_UNKNOWN;
                    continue;
                }
                continue;
            case '?':
                switch(a) {
                case 3: /* DECCOLM - 132/80 columns */
                    if (terminal.allow_deccolm) {
                        terminal.col_mode = (function == 'h' ? COL_132 : COL_80);
                        term_resize(0, 0); /* Use values set in term_resize */
                        term_setscrollregion(-1, -1);
                        term_cursor(BOL, TOP);
                        if (!terminal.no_clear_on_col_mode_change) {
                            term_erase(SCREEN(BOL, TOP), SCREEN(EOL, BOTTOM));
                        }
                    }
                    continue;
                case 5: /* DECSCNM - Reverse video */
                    terminal.reverse_vid = (function == 'h');
                    term_invalidate();
                    continue;
                case 6: /* DECOM - Use scroll region */
                    term_set_originmode(function == 'l'); /* h means relative */
                    continue;
                case 7: /* DECAWM - Auto wrap mode */
                    terminal.autowrap = (function == 'h');
                    continue;
                case 12:/* Cursor blinking */
                    terminal.blink_cursor = (function == 'h');
                    terminal.cursor_dirty = true;
                    break;
                case 25:/* Show cursor */
                    terminal.show_cursor = (function == 'h');
                    terminal.cursor_dirty = true;
                    break;
                case 40:/* Allow DECCOLM 80 -> 132 mode */
                    terminal.allow_deccolm = (function == 'h');
                    continue;
                case 95: /* DECNCSM - No Clearing Screen On Column Change Mode */
                    terminal.no_clear_on_col_mode_change = (function == 'h');
                    continue;
                case 1: /* DECCKM - Cursor keys */
                case 9: /* Send mouse X & Y on button press */
                    CSI_TODO;
                    continue;
                case 2: /* DECANM - Ansi mode */
                case 4: /* DECSCLM - Smooth scrolling */
                case 8: /* DECARM - Auto repeat mode */
                case 10:/* Show toolbar */
                case 18:/* DECPFF - Print form feed */
                case 19:/* DECPEX - Print scroll region */
                case 30:/* Show scrollbar */
                case 35:/* Font shifting functions */
                case 38:/* Tektronic mode */
                case 41:/* more(1) fix */
                case 42:/* Enable nation replacement character sets */
                case 44:/* Margin bell */
                case 45:/* Reverse wrap-around mode */
                case 46:/* Start logging */
                /* TODO: There are many more codes */
                    CSI_IGNORED;
                    continue;
                default:
                    CSI_UNKNOWN;
                    continue;
                }
                continue;

            default:
                CSI_UNKNOWN;
                continue;
            }
            continue;
        }}
        break;
    case 'J': /* ED - Erase in terminal */
        switch(CSI_DEFAULT(arg[0], 0)) {
        case 0:
        default:
            term_erase(PAGE(X, Y), PAGE(EOL, BOTTOM));
            break;
        case 1:
            term_erase(PAGE(BOL, TOP), PAGE(X, Y));
            break;
        case 2:
            term_erase(PAGE(BOL, TOP), PAGE(EOL, BOTTOM));
            break;
        }
        break;
    case 'K': /* EL - Erase in line */
        switch(CSI_DEFAULT(arg[0], 0)) {
        case 0:
        default:
            term_erase(PAGE(X, Y), PAGE(EOL, Y));
            break;
        case 1:
            term_erase(PAGE(BOL, Y), PAGE(X, Y));
            break;
        case 2:
            term_erase(PAGE(BOL, Y), PAGE(EOL, Y));
            break;
        }
        break;
    case 'X': /* ECH - Erase Character */
        CSI_DEFAULT(arg[0], 1);
        term_erase(PAGE(X, Y), PAGE(X + arg[0] - 1, Y));
        break;
    case 'c': /* DA - Device Attributes */
        term_cb->write_host("\033[?1;0c", 7); /* VT100 */
        break;
    case 'g': /* TBC - Tabstop Clear */
        switch(CSI_DEFAULT(arg[0], 0)) {
        case 0:
            terminal.tabstop[X] = false;
            break;
        case 3:
            term_tabs_clear();
            break;
        }
        break;
    case 'm': /* SGR - Character Attributes */
        term_setcharattributes(arg);
        break;
    case 'n': /* DSR - Device Service Report */
        switch(arg[0]) {
        case 5: /* Status report */
            term_cb->write_host("\033[0n", 4); /* Report OK */
            break;
        case 6: /* Report cursor position */
            term_report_cursor_pos();
            break;
        case 15: /* Report printer */
            term_cb->write_host("\033[?11n", 6); /* Not ready */
            break;
        default:
            CSI_UNKNOWN;
            break;
        }
        break;
    case 'q': /* DECLL - Load LEDS */
        CSI_IGNORED;
        break;
    case 'r':
        switch(privflag) {
        case '\0': /* DECSTBM - Set Top and Bottom Margins */
            CSI_DEFAULT(arg[0], 1);
            CSI_DEFAULT(arg[1], terminal.rows);
            term_setscrollregion(arg[0], arg[1]);
            term_cursor(BOL, TOP);
            break;
        default:
            CSI_UNKNOWN;
            break;
        }
        break;
    case 'W':
        switch(privflag) {
        case '?':
            switch(arg[0]) {
            case 5:
                term_tabs_clear();
                term_tabs_every(8);
                break;
            default:
                CSI_UNKNOWN
            }
            break;
        default:
            CSI_UNKNOWN;
        }
        break;
    /* Ignored functions */
    case 'i': /* Printing */
    case 'y': /* Tests */
    case '3': /* DECDHL - Double height letters, top half */
    case '4': /* DECDHL - Double height letters, top half */
    case '5': /* DECSWL - Single width, single height letters */
    case '6': /* DECDWL - Double width, single height letters */
    case 'S': /* SU - Scroll up */
    case 'T': /* SD - Scroll down */
        CSI_IGNORED;
        break;
    default:
        CSI_UNKNOWN;
        break;
    }
}

void
term_designate_charset(char intermediate, char function)
/* Assign charset from ESC sequence */
/* http://invisible-island.net/xterm/ctlseqs/ctlseqs.html */
{
    /* TODO: Implement */
    enum charset_mode_t mode;
    switch(intermediate) {
        case '(':
            mode = G0;
            break;
        case ')':
        case '-':
            mode = G1;
            break;
        case '*':
        case '.':
            mode = G2;
            break;
        case '+':
        case '/':
            mode = G3;
            break;
        default:
            warning("Unknown mode: %c", intermediate);
            return;
    }

    switch(function) {
    case '0':
        terminal.charset[mode] = CHARSET_DEC;
        break;
    case 'A':
        terminal.charset[mode] = CHARSET_UK;
        break;
    case 'B':
        terminal.charset[mode] = CHARSET_US;
        break;
    case '4':
        terminal.charset[mode] = CHARSET_NL;
        break;
    case 'C':
    case '5':
        terminal.charset[mode] = CHARSET_FI;
        break;
    case 'R':
        terminal.charset[mode] = CHARSET_FR;
        break;
    case 'Q':
        terminal.charset[mode] = CHARSET_CA;
        break;
    case 'K':
        terminal.charset[mode] = CHARSET_GE;
        break;
    case 'Y':
        terminal.charset[mode] = CHARSET_IT;
        break;
    case 'E':
    case '6':
        terminal.charset[mode] = CHARSET_NO;
        break;
    case 'Z':
        terminal.charset[mode] = CHARSET_SP;
        break;
    case 'H':
    case '7':
        terminal.charset[mode] = CHARSET_SW;
        break;
    case '=':
        terminal.charset[mode] = CHARSET_CH;
        break;
    default:
        warning("Unknown charset: %c", function);
    }

}

void
esc_dispatch(char function, char intermediate)
{
    debug("%c/0x%02x %c", function, function, intermediate ? intermediate : ' ');
    switch (intermediate) {
    case '\0':
        switch (function) {
        /* Movement */
        case 'D': /* IND - Index */
            term_cursor(X, Y + 1);
            break;
        case 'E': /* NEL - Next Line */
            term_cursor(BOL, Y + 1);
            break;
        case 'H': /* HTS - Tab set */
            terminal.tabstop[X] = true;
            break;
        case 'M': /* RI - Reverse Index */
            term_cursor(X, Y - 1);
            break;
        case 'N': /* SS2 - Single Shift Select of G2 character set */
            warning("TODO: Implement SS2");
            break;
        case 'O': /* SS2 - Single Shift Select of G2 character set */
            warning("TODO: Implement SS2");
            break;
        case 'Z': /* DECID - Identify Terminal (deprecated) */
            term_write("\033[c"); /* Call DA */
            break;
        case 'c': /* RIS - full reset */
            term_reset();
            break;
        case '7': /* DECSC - Save Cursor */
            terminal.saved_cur.x = terminal.x;
            terminal.saved_cur.y = terminal.y;
            terminal.saved_cur.autowrap = terminal.autowrap;
            terminal.saved_cur.foreground = terminal.style.foreground;
            terminal.saved_cur.background = terminal.style.background;
            terminal.saved_cur.attr = terminal.style.attr;
            memcpy(terminal.saved_cur.charset, terminal.charset, sizeof(terminal.charset));
            terminal.saved_cur.charset_mode = terminal.charset_mode;
            break;
        case '8': /* DECRC - Restore Cursor*/
            terminal.x = terminal.saved_cur.x;
            terminal.y = terminal.saved_cur.y;
            terminal.autowrap = terminal.saved_cur.autowrap;
            terminal.style.foreground = terminal.saved_cur.foreground;
            terminal.style.background = terminal.saved_cur.background;
            terminal.style.attr = terminal.saved_cur.attr;
            memcpy(terminal.charset, terminal.saved_cur.charset, sizeof(terminal.charset));
            terminal.charset_mode = terminal.saved_cur.charset_mode;
            break;
        case '=': /* DECPAM - Set alternate keypad mode */
            warning("TODO: implement DECPAM");
            break;
        case '>': /* DECPNM - Set normal keypad mode */
            warning("TODO: implement DECPNM");
            break;
        }
        break;
    case '#':
        switch(function) {
        case '8': /* DECALN - Screen Alignment Display */
            term_fill(PAGE(BOL, TOP), PAGE(EOL, BOTTOM), L'E');
            break;
        }
        break;
    case '(':
    case ')':
    case '-':
    case '*':
    case '.':
    case '+':
    case '/':
        term_designate_charset(intermediate, function);
        break;
    default:
        warning("Unhandled: %c %c", function, intermediate);
    }
}

void
osc_dispatch(unused char *arg, unused size_t length)
{
    debug(arg);
    /* TODO: implement */
}

bool
term_handle_keypress(KeySym key, uint32_t mod)
/* Unfortunately, we depend somewhat on the X11 key symbols here. It would
 * be pretty to not have, but I guess that redefining the keys just to keep
 * this file indepened, makes little sense without an actual need to
 */
/* Returns true if handled */
{
    size_t i;

    if (mod & Mod1Mask) { /* Alt */
        term_cb->write_host("\033", 1);
    }


    if (key == XK_Return) {
        if (terminal.crlf) {
            term_cb->write_host("\r\n", 2);
        } else {
            term_cb->write_host("\r", 1);
        }
        return true;
    }

    for (i = 0; i < LENGTH(keymap); i ++) {
        if (keymap[i].key == key &&
                keymap[i].mod == (mod & ~Mod1Mask)) {
            term_cb->write_host(keymap[i].out, strlen(keymap[i].out));
            return true;
        }
    }

    return false;
}
