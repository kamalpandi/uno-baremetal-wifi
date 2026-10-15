/*
 * main.c — Phase 2: bit-banged transmit on D3 to the ESP8266.
 *
 * Success looks like this:
 *
 *   1. the debug console prints "tx #N: AT" a couple of times a second,
 *   2. the shield's RX LED flickers in time with it.
 *
 * Point 2 is the real result. The LED sits on the ESP module's receive
 * line, so if it blinks then our hand-timed waveform physically reached
 * the shield. We still cannot read the ESP's reply - that is Phase 3 -
 * so we cannot yet confirm the module *understood* us, only that we are
 * putting a signal on the wire.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <util/delay.h>

#include "softuart.h"
#include "uart0.h"

int main(void)
{
    uart0_init();
    softuart_tx_init();

    /*
     * Global interrupt enable.
     *
     * The bit-banged transmitter runs entirely from TIMER1_COMPA_vect.
     * Without sei() that ISR never fires, D3 just sits idle, and the
     * program looks perfectly correct while doing nothing at all. If
     * the LED ever stays dark, check this line first.
     */
    sei();

    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("uno-baremetal-wifi"));
    uart0_putsln_P(PSTR("Phase 2: bit-banged TX on D3 @ 9600 8N1"));
    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("Sending \"AT\" every 500 ms. The shield's RX LED"));
    uart0_putsln_P(PSTR("should flicker. We cannot read the reply yet."));
    uart0_putsln_P(PSTR(""));

    uint16_t n = 0;

    for (;;) {
        n++;

        uart0_puts_P(PSTR("tx #"));
        uart0_put_udec(n);
        uart0_putsln_P(PSTR(": AT"));

        /* Queue "AT" plus the CR LF that every AT command needs. */
        softuart_tx_puts_P(PSTR("AT\r\n"));

        /*
         * Wait for those four bytes to actually leave the pin before
         * spacing out the next command. softuart_tx_idle() is the hook
         * Phase 4 will use to know when to start listening for a reply.
         */
        while (!softuart_tx_idle()) {
        }

        _delay_ms(500);
    }

    return 0; /* unreachable */
}
