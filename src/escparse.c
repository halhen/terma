#include <ctype.h>
#include <stdlib.h>
#include <string.h>

#include "escparse.h"
#include "util.h"

/* Reference: http://www.vt100.net/emu/dec_ansi_parser */

const char BEL=0x07;
const char CAN=0x18;
const char SUB=0x1A;
const char ESC=0x1B;

/*
 * There seems to be no specified limit of how long an escape sequence
 * may be, but it varies between implementations.
 */
static struct {
    bool      (*state)(char c);
    void      (*onexit)();

    char        buf[1024];   /* collected characters */
    size_t      nbuf;        /* number of collected characters */
} esc_seq;


static struct {
    csi_dispatch_t  csi;
    esc_dispatch_t  esc;
    osc_dispatch_t  osc;
} dispatch;



void
esc_collect(char c)
{
    if (esc_seq.nbuf >= LENGTH(esc_seq.buf)) {
        debug("Buffer full");
        return; /* silently ignore. TODO: how to handle */
    }

    esc_seq.buf[esc_seq.nbuf] = c;
    esc_seq.nbuf += 1;
}


void
esc_clear()
{
    if (esc_seq.onexit) {
        (*esc_seq.onexit)();
    }
    esc_seq.state = NULL;
    esc_seq.onexit = NULL;
    memset(esc_seq.buf, '\0', LENGTH(esc_seq.buf));
    esc_seq.nbuf = 0;
}


void
esc_csi_dispatch(char c)
{
    int current = 0;
    int params[CSI_MAXARGS];
    char private = '\0';
    char intermediate = '\0';
    bool error = false; /* if true, read sequence to function marker, then ignore */
    size_t i, nparams = 0;

    for (i = 0; i < CSI_MAXARGS; i ++) {
        params[i] = -1;
    }

    for (i = 0; i < esc_seq.nbuf; i ++) {
        if (isdigit(esc_seq.buf[i])) {
            current = current * 10 + esc_seq.buf[i] - '0';
        }
        else if (esc_seq.buf[i] == ';') {
            if (nparams < LENGTH(params)) {
                params[nparams++] = current;
            } else {
                debug("Too many parameters: %s", esc_seq.buf);
                error = true;
            }
            current = 0;
        }
        else if (between(esc_seq.buf[i], 0x3c, 0x3f)) {
            if (private == '\0') {
                private = esc_seq.buf[i];
            } else {
                debug("Private marker already set %s", esc_seq.buf);
                error = true;
            }
        }
        else if (between(esc_seq.buf[i], 0x20, 0x2f)) {
            if (intermediate == '\0') {
                intermediate = esc_seq.buf[i];
            } else {
                debug("Intermediate character already set %s", esc_seq.buf);
                error = true;
            }
        }
        else if (esc_seq.buf[i] == ':') {
            debug(": in code %s", esc_seq.buf);
            error = true;
        }
        else {
            debug("Error parsing params: %s, ignoring 0x%02x", esc_seq.buf, esc_seq.buf[i]);
        }
    }
    if (nparams < LENGTH(params)) {
        params[nparams++] = current;
    } else {
        debug("Too many parameters: %s", esc_seq.buf);
        error = true;
    }

    if (!error && dispatch.csi)
        (*dispatch.csi)(c, params, private);
}

void
esc_esc_dispatch(char c)
{
    size_t i;
    bool error = false;
    char intermediate = '\0';

    for (i = 0; i < esc_seq.nbuf; i++) {
        if (between(esc_seq.buf[i], 0x20, 0x2f)) {
            if (intermediate == '\0') {
                intermediate = esc_seq.buf[i];
                continue;
            } else {
                error = true;
            }
        }
    }

    if (!error && dispatch.esc)
        (*dispatch.esc)(c, intermediate);
}

/*
 * states csi_entry, csi_ignore, csi_param, csi_intermediate
 */
bool
esc_state_csi(char c)
{
    if (between(c, 0x40, 0x7e)) {
        esc_csi_dispatch(c);
        esc_clear();
        return true;
    }

    if (c == 0x7f) {
        return true; /* ignore */
    }

    if (c >= 0x20) {
        esc_collect(c);
        return true;
    }

    return false;
}

void
esc_osc_end()
{
    if (dispatch.osc) {
        esc_seq.buf[esc_seq.nbuf + 1] = '\0';
        (*dispatch.osc)(esc_seq.buf, esc_seq.nbuf);
    }
}

bool
esc_state_osc(char c)
{
    if (c == BEL) {
        esc_clear(); 
        return true;
    }

    if (between(c, 0x00, 0x1f)) {
        /* ignore */
        return true; 
    }

    else {
        esc_collect(c);
        return true;
    }
}

bool
esc_state_wait_for_ST(unused char c)
{
    return true;
}


bool
esc_state_escape(char c)
{
    switch (c) {
    case '[': /* CSI */
        esc_clear();
        esc_seq.state = esc_state_csi;
        return true;
    case ']': /* OSC */
        esc_clear();
        esc_seq.state = esc_state_osc;
        esc_seq.onexit = esc_osc_end;
        return true;
    case 'P': /* DCS */
        debug("Ignoring DCS sequence");
        esc_seq.state = esc_state_wait_for_ST;
        return true;
    case '\\': /* ST */
        /* No-op, since ST terminated states handle their business on exit */
        esc_clear();
        return true;
    case 'X': /* SOS */
    case '^': /* PM */
    case '_': /* APC */
        esc_seq.state = esc_state_wait_for_ST;
        return true;
    }

    if (between(c, 0x20, 0x2f)) {
        esc_collect(c);
        return true;
    }

    if (between(c, 0x30, 0x7e)) {
        esc_esc_dispatch(c);
        esc_clear();
        return true;
    }

    return false;
}




/* Public API */

void
esc_init(esc_dispatch_t esc, csi_dispatch_t csi, osc_dispatch_t osc)
{
    dispatch.esc = esc;
    dispatch.csi = csi;
    dispatch.osc = osc;
    esc_clear();
}


bool
esc_handle(char c)
/* return true if c was handled as an esc, else false */
/* "print" and "execute" are performed outside */
{
    if (c == ESC || c == SUB || c == CAN) {
        esc_clear();
        
        if (c == ESC) { /* Restart sequence */
            esc_seq.state = esc_state_escape;
        }

        return true;
    }

    if (esc_seq.state == NULL) {
        return false; /* Not escape mode */
    }

    return (*esc_seq.state)(c);
}
