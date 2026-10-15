/*
 * main.c — Phase 1: get a debug console working on D0/D1.
 *
 * Success looks like this:
 *
 *   1. a banner appears in your serial terminal at 115200 baud,
 *   2. every character you type comes back reported in hex.
 *
 * Point 2 matters more than it looks. A working transmitter is easy to
 * fake by accident; a working *receiver* proves the baud rate, frame
 * format and wiring are all genuinely correct in both directions.
 *
 * Notice that every fixed message below is wrapped in PSTR() and sent
 * with the _P variant. These strings therefore cost flash only, and the
 * whole program uses no RAM beyond what the C runtime needs.
 */

#include <avr/io.h>
#include <avr/pgmspace.h>

#include "uart0.h"

int main(void)
{
    uart0_init();

    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("uno-baremetal-wifi"));
    uart0_putsln_P(PSTR("Phase 1: USART0 up at 115200 8N1"));
    uart0_putsln_P(PSTR("Type something and watch it come back as hex."));
    uart0_putsln_P(PSTR(""));

    /*
     * The whole loop is non-blocking.
     *
     * Nothing here ever waits for input. If there is no byte, we just
     * go around again. This is the shape every later phase will take:
     * check what has arrived, do a little work, return immediately.
     * It is the difference between a program that can also service a
     * WiFi module and one that cannot.
     */
    for (;;) {
        if (uart0_has_data()) {
            char c = uart0_getc();

            uart0_puts_P(PSTR("received "));
            uart0_put_hex8((uint8_t)c);

            if (c == '\r' || c == '\n') {
                uart0_putsln_P(PSTR("   <- line ending"));
            } else {
                uart0_puts_P(PSTR("   '"));
                uart0_putc(c);
                uart0_putsln_P(PSTR("'"));
            }
        }
    }

    return 0; /* unreachable */
}
