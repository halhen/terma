#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stropts.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <X11/Xlib.h>

#include <linux/kd.h> /* Writing LED */

#include "util.h"

#include "shell.h"
#include "terminal.h"

#include "config.h"

const char *WINDOW_TITLE  = TERM_NAME;
static int shell_fd;


void init();
void run();


/* X functions */
void x_clearline(size_t col, size_t row, size_t length, color_t bg);
void x_destroy();
void x_draw();
void x_drawline(size_t col, size_t row, wchar_t *text, size_t length, color_t fg, color_t bg, bool bold, bool underline);
void x_init();
void x_init_gc();
void x_init_input();
void x_init_window();
void x_resize(size_t width, size_t height);
void x_show();

/* X event callbacks */
void x_on_configure(XEvent *event);
void x_on_expose(XEvent *event);
void x_on_keypress(XEvent *event);
void on_reschange(size_t cols, size_t rows);

static void (*x_handler[])(XEvent *) = {
    [KeyPress]         = x_on_keypress,
    [Expose]           = x_on_expose,
    [ConfigureNotify]  = x_on_configure,
};

static struct term_push_callbacks callbacks = {
    .write_host         = sh_write,
    .write_screen       = x_drawline,
    .write_finished     = x_show,
    .clear_line         = x_clearline,
    .res_change         = on_reschange,
};

static struct {
    Display          *dpy;
    int               screen;
    Window            window;
    GC                gc;
    Pixmap            pixmap;

    size_t            glyph_ascent;
    size_t            glyph_descent;
    size_t            glyph_width;
    size_t            glyph_height;

    size_t            win_width;
    size_t            win_height;

    XFontSet          font;
    XFontSet          bold_font;
    XIM               xim;
    XIC               xic;

    struct timeval    last_draw;
} X;



void
x_init_font()
{
    int i, n;
    char *def, **missing;
    XFontStruct **xfonts;
    char **font_names;

    X.font = XCreateFontSet(X.dpy,
                            config.font,
                            &missing,
                            &n,
                            &def);
    if (missing) {
        while (n--) {
            debug("Missing font: %s", missing[n]);
        }
        XFreeStringList(missing);
    }

    X.bold_font = XCreateFontSet(X.dpy,
                                 config.bold_font,
                                 &missing,
                                 &n,
                                 &def);
    if (missing) {
        while (n--) {
            debug("Missing font: %s", missing[n]);
        }
        XFreeStringList(missing);
    }

    n = XFontsOfFontSet(X.font, &xfonts, &font_names);
    for (i = 0; i < n; i++) {
        X.glyph_ascent  = max(X.glyph_ascent,  xfonts[i]->ascent);
        X.glyph_descent = max(X.glyph_descent, xfonts[i]->descent);
        X.glyph_height  = X.glyph_ascent + X.glyph_descent;
        X.glyph_width   = max(X.glyph_width,   xfonts[i]->max_bounds.width);
    }
}

void
x_init_gc()
{
    uint32_t mask   = GCForeground | GCBackground;

    XGCValues values;
    values.foreground   = config.foreground;
    values.background   = config.background;

    X.gc = XCreateGC(X.dpy, X.window, mask, &values);
}


void
x_init_window()
{
    uint32_t mask     = CWBackPixel | CWEventMask;
    XSetWindowAttributes values;

    values.background_pixel = config.background;
    values.event_mask       = KeyPressMask |
                              ExposureMask |
                              StructureNotifyMask;

    size_t width  = X.glyph_width  * config.cols;
    size_t height = X.glyph_height * config.rows;

    Window parent = XRootWindow(X.dpy, X.screen);

    X.window = XCreateWindow(X.dpy,
                             parent,
                             0, 0,             /* x, y */
                             width, height,
                             0,                /* border width */
                             XDefaultDepth(X.dpy, X.screen),
                             InputOutput,
                             XDefaultVisual(X.dpy, X.screen),
                             mask, &values);
}

void
x_init_input()
{
    X.xim = XOpenIM(X.dpy, NULL, NULL, NULL);
    X.xic = XCreateIC(X.xim, XNInputStyle, XIMPreeditNothing
                       | XIMStatusNothing, XNClientWindow, X.window,
                          XNFocusWindow, X.window, NULL);

}


void
x_init()
{
    atexit(x_destroy);
    X.dpy    = XOpenDisplay(NULL);
    X.screen = XDefaultScreen(X.dpy);

    x_init_font();
    x_init_window();
    x_init_gc();
    x_init_input();
    /* We don't initialize the pixmap; we'll get a resize soon enough */

    XMapWindow(X.dpy, X.window);
    XSync(X.dpy, 0);
}

void
x_destroy()
{
    debug(".");
    XFreeFontSet(X.dpy, X.font);
    XFreeFontSet(X.dpy, X.bold_font);
    XDestroyIC(X.xic);
    XCloseIM(X.xim);
    XFreePixmap(X.dpy, X.pixmap);
    XFreeGC(X.dpy, X.gc);
    XCloseDisplay(X.dpy);
}

void
x_resize(size_t width, size_t height)
{
    if (X.win_width == width && X.win_height == height) {
        return;
    }

    debug("%lux%lu", (long unsigned)width, (long unsigned)height);

    /* Update pixmap */
    if (X.pixmap)
        XFreePixmap(X.dpy, X.pixmap);
    X.pixmap = XCreatePixmap(X.dpy,
                             X.window,
                             width, height,
                             XDefaultDepth(X.dpy, X.screen));
    XSetForeground(X.dpy,
                   X.gc,
                   config.background);
    XFillRectangle(X.dpy,
                   X.pixmap,
                   X.gc,
                   0, 0,
                   width, height);

    X.win_width  = width;
    X.win_height = height;

    size_t glyphs_w = X.win_width  / X.glyph_width;
    size_t glyphs_h = X.win_height / X.glyph_height;

    term_resize(glyphs_w, glyphs_h);
}


void
x_drawline(size_t col, size_t row, wchar_t *text, size_t length, color_t fg, color_t bg, bool bold, bool underline)
{
	XSetForeground(X.dpy,
                   X.gc,
                   fg);
	XSetBackground(X.dpy,
                   X.gc,
                   bg);

    size_t xpix  = col    * X.glyph_width;
    size_t width = length * X.glyph_width;
    size_t ypix  = row * X.glyph_height + X.glyph_ascent;

    XwcDrawImageString(X.dpy,
                       X.pixmap,
                       bold ? X.bold_font : X.font,
                       X.gc,
                       xpix,
                       ypix,
                       text,
                       length);
    if (underline) {
        XDrawLine(X.dpy,
                  X.pixmap,
                  X.gc,
                  xpix,
                  ypix + 1,
                  xpix + width - 1,
                  ypix + 1);
    }
}

void
x_clearline(size_t col, size_t row, size_t length, color_t bg)
{
	XSetForeground(X.dpy,
                   X.gc,
                   bg);
	XFillRectangle(X.dpy,
                   X.pixmap,
                   X.gc,
                   col * X.glyph_width,
                   row * X.glyph_height,
                   length * X.glyph_width,
                   X.glyph_height);
}

void
x_show()
{
    XCopyArea(X.dpy,
              X.pixmap,
              X.window,
              X.gc,
              0, 0,
              X.win_width, X.win_height,
              0, 0);
}

void
x_draw()
{
    term_flush();
    gettimeofday(&X.last_draw, NULL);
}


void
x_on_configure(XEvent *event)
{
    size_t w = event->xconfigure.width;
    size_t h = event->xconfigure.height;
    x_resize(w, h);

}

void
x_on_expose(unused XEvent *event)
{
    term_invalidate();
    x_draw();
}

void
x_on_keypress(XEvent *event)
{
    XKeyEvent *e = &event->xkey;
    KeySym ksym;
    char buf[32];
    int len;
    Status status;

    len = XmbLookupString(X.xic, e, buf, sizeof(buf)-1, &ksym, &status);

    if (!term_handle_keypress(ksym, e->state) && len > 0) {
        sh_write(buf, len);
    }
}

void
on_reschange(size_t cols, size_t rows)
{
    /* Clear window */
	XSetForeground(X.dpy,
                   X.gc,
                   config.background);
	XFillRectangle(X.dpy,
                   X.pixmap,
                   X.gc,
                   0,
                   0,
                   X.win_width,
                   X.win_height);

    /* Report new size */
    struct winsize w;
    w.ws_row = rows;
    w.ws_col = cols;
    w.ws_xpixel = w.ws_ypixel = 0;
    if (ioctl(shell_fd, TIOCSWINSZ, &w) < 0) {
        debug("ioctl failed: %d", errno);
    }

    /* Order a full repaint */
    term_invalidate();
}


void
run()
{
    /* Using Xlib event handling to use the keyboard helpers */
    XEvent          event;
    fd_set          fds;
    struct timeval  timeout;
    struct timeval  now;
    uint32_t        usec_sleep = 1000000 / config.HZ;
    uint32_t        usec_sleep_passive = 1000000 / config.HZ_passive;

    int Xfd = XConnectionNumber(X.dpy);

    /* If there is no user activity, we can spend more time between screen
     * updates without the user feeling less responsiveness. Passive mode
     * means that there are no X events to respond to, and we could slow down
     * rendering a bit, offering more throughput
     */
    struct timeval last_event;
    last_event.tv_sec = 0;
    last_event.tv_usec = 0;
    bool passive = false;

    for (;;) {
        FD_ZERO(&fds);
        FD_SET(Xfd, &fds);
        FD_SET(shell_fd, &fds);


        timeout.tv_sec  = 0;
        timeout.tv_usec = usec_sleep; /* TODO: set to blinking time? */

        if (select(max(Xfd, shell_fd) + 1, &fds, NULL, NULL, &timeout) < 0) {
            if (errno == EINTR)
                continue;
            die("select failed");
        }

        if (FD_ISSET(shell_fd, &fds)) {
            sh_read(term_write); /* short circuit shell output and term input */
        }

        while (XPending(X.dpy)) {
            XNextEvent(X.dpy, &event);
            if (XFilterEvent(&event, X.window))
                continue;

            passive = false;
            if (event.type < (int)LENGTH(x_handler) && x_handler[event.type])
                (x_handler[event.type])(&event);
        }

        gettimeofday(&now, NULL);
        if (timediff_usec(now, X.last_draw) > (passive ? usec_sleep_passive : usec_sleep)) {
            x_draw();
        }

        if (FD_ISSET(Xfd, &fds)) {
            gettimeofday(&last_event, NULL);
            passive = false;
        }
        else {
            passive = timediff_usec(now, last_event) > usec_sleep_passive;
        }

        if (!FD_ISSET(shell_fd, &fds) && passive) {
            term_gc(); /* Clean up term if we have time to spare */
        }
    }
}

void
init()
{
    setlocale(LC_CTYPE, "");
    util_init();
    x_init();
    term_init(&callbacks);
    shell_fd = sh_init();
}

int main()
{
    init();
    run();
    return 0;
}

