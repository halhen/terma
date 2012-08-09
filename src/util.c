#include <errno.h>
#include <iconv.h>
#include <stdlib.h>
#include <string.h>

#include "util.h"

static iconv_t cd;

static void
util_destroy()
{
    iconv_close(cd);
}

void
util_init()
{
    cd = iconv_open("WCHAR_T", "UTF-8");
    if (cd == (iconv_t)-1) {
        die("Failed to open iconv: %d", errno);
    }

    atexit(util_destroy);
}

/* convert utf-8 encoded character to one ucs-2 encoded dest
 * Returns then number of bytes consumed from source, or 0 if no encoding
 * was successful;
 */
size_t
utf8towchar(char *source, wchar_t *dest)
{
    const size_t max_source = 6;
    size_t inbytes, outbytes;

    inbytes = max_source;
    outbytes = sizeof(wchar_t);

    if (iconv(cd, &source, &inbytes, (char**)&dest, &outbytes) == (size_t)-1) {
        if (errno != EINVAL && errno != E2BIG ) {
            /* Incomplete sequence is fine; here something worse happened */
            debug("iconv error: %d", errno);

            if (max_source - inbytes == 0) {
                /* TODO: What to do? We made no progress, but unless we move forward,
                   we'll probably end up in an ifinite loop */
                return 1;
            }
        }
    }

    return max_source - inbytes;
}

void*
emalloc(size_t size)
{
    assert(size > 0);

    void *p = malloc(size);
    if (!p) {
        die("Out of memory");
    }
    return p;
}

void*
erealloc(void *ptr, size_t size)
{
    assert(size > 0); /* Don't accept free-like usage */
    void *p = realloc(ptr, size);
    if (!p) {
        die("Out of memory");
    }
    return p;
}
