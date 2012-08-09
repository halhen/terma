#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>

/* A read callback returns the number of chars read */
typedef size_t (*cb_read_t)(char *);

int sh_init(); /* return fd to shell */
void sh_read(cb_read_t callback);
void sh_write(const char *str, size_t n);
