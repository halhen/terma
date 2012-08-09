#include <pty.h>
#include <errno.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include "shell.h"
#include "util.h"

#define INPUT_BUFFER_SIZE 4096

static struct {
    pid_t            pid;
    int              fd;
} shell;


static void
sigchld(unused int a)
{
    int code = 0;
    if (waitpid(shell.pid, &code, 0) < 0) {
        die("Waiting for child failed: %d", errno);
    }
    if (WIFEXITED(code)) {
        exit(WEXITSTATUS(code));
    }
    else {
        exit(1);
    }
}



static void
sh_destroy()
{
    debug("killing %d", shell.pid);
    kill(shell.pid, SIGKILL);
}

static void
sh_exec()
{
    char **args;
    char *sh = getenv("SHELL");

    if (sh == NULL) {
        sh = "/bin/sh";
    }

    putenv("TERM="TERM_NAME);
    args = (char *[]) {sh, "-i", NULL};
    execvp(args[0], args);
    die("exec of %s failed", sh);
}

int
sh_init()
{
    int amaster, aslave;
    if (openpty(&amaster, &aslave, NULL, NULL, NULL) != 0) {
        die("openpty failed: %d", errno);
    }

    switch (shell.pid = fork()) {
        case -1:
            die("fork failed: %d", errno);
            break;
        case 0:
            setsid();
            dup2(aslave, 0);
            dup2(aslave, 1);
            dup2(aslave, 2);
            if (ioctl(aslave, TIOCSCTTY, NULL) < 0) {
                die ("ioctl TIOCSCTTY failed: %d", errno);
            }
            close(amaster);
            close(aslave);
            sh_exec();
            break;
        default:
            close(aslave);
            shell.fd = amaster;
            signal(SIGCHLD, sigchld);
    }

    atexit(sh_destroy);
    return shell.fd;
}

void
sh_read(cb_read_t callback)
{
    /* read and process one cycle before sharing cycles with the other parts of the system */
    static char buffer[INPUT_BUFFER_SIZE];
    static size_t  buflen = 0;

    size_t length;

    length = read(shell.fd, buffer + buflen, LENGTH(buffer) - buflen - 1);
    if (length == (size_t)-1) {
        die("read failed: %d", errno);
    }

    buflen += length;
    buffer[buflen] = '\0';

    length = (*callback)(buffer);
    debug("%lu", (long unsigned)buflen);
    buflen -= length; /* Everything may not have been consumed; save that 'til next time */
    memmove(buffer, buffer + length, buflen);
}

void
sh_write(const char *str, size_t n) 
{
    if (write(shell.fd, str, n) < 0) {
        die("write failed: %d", errno);
    }
}
