/* Public interface for the escape module */

#include <stdint.h>
#include <stdbool.h>

/* http://vt100.net/emu/dec_ansi_parser claims that 16 params is max */
#define CSI_MAXARGS 16

typedef void (*esc_dispatch_t)(char function, char intermediate);
typedef void (*csi_dispatch_t)(char function, int32_t params[], char privflag);
typedef void (*osc_dispatch_t)(char *arg, size_t lenght);

/* Return true if c was handled, false if not (e.g. c should print) */
bool esc_handle(char c); 
void esc_init(esc_dispatch_t, csi_dispatch_t, osc_dispatch_t);

/* http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
 * http://vt100.net/emu/dec_ansi_parser */
