/*
 * console.c — a minimal line editor on the debug console.
 */

#include <avr/pgmspace.h>

#include "console.h"
#include "uart0.h"

#define CHAR_BS  0x08   /* control-H, "backspace"  */
#define CHAR_DEL 0x7F   /* DEL, which many terminals send instead */

uint8_t console_read_line(char *dst, uint8_t max_len)
{
    uint8_t len = 0;

    for (;;) {
        char c = uart0_getc();   /* blocks until a byte arrives */

        if (c == '\r' || c == '\n') {
            uart0_putsln_P(PSTR(""));
            break;
        }

        if (c == CHAR_BS || c == CHAR_DEL) {
            if (len > 0) {
                len--;
                /*
                 * Back up, overwrite the old character with a space,
                 * then back up again - the only way to erase on a
                 * terminal that has no concept of deletion.
                 */
                uart0_puts_P(PSTR("\b \b"));
            }
            continue;
        }

        /* Ignore control characters and anything non-ASCII. */
        if (c < 0x20 || c > 0x7E) {
            continue;
        }

        /* Silently drop anything past the limit rather than letting the
         * buffer overflow - the field cannot hold it anyway. */
        if (len < max_len) {
            dst[len] = c;
            len++;
            uart0_putc(c);
        }
    }

    dst[len] = '\0';
    return len;
}
