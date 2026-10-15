/*
 * main.c — Phase 3: bit-banged receive on D2.
 *
 * Now that we can both transmit and receive, we can ask the module a
 * question and look at the answer. There is no line parser yet, so the
 * reply is dumped as raw hex - which is the only honest way to look at a
 * byte stream, because a terminal cannot show the difference between
 * 0x00, 0xFF and a stray control character.
 *
 * Expected result:
 *
 *   tx: AT   rx: 41 54 0D 0A 0D 0A 4F 4B 0D 0A
 *
 * which is the module's echo of "AT" followed by its "OK".
 *
 * Press the shield's ESP RESET button to watch the module boot: its log
 * comes out at 74880 baud, roughly eight times faster than we listen, so
 * it arrives as garbage. The stop-bit check discards most of it - the
 * "rejected" count is that check doing its job, not a fault.
 */

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include "softuart.h"
#include "timer.h"
#include "uart0.h"

int main(void)
{
    timer_init();

    uart0_init();          /* debug console, D0/D1, 115200 */
    softuart_tx_init();    /* sets up Timer1            */
    softuart_rx_init();    /* needs Timer1 - call second */

    sei();                 /* the soft UART lives on interrupts */

    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("uno-baremetal-wifi"));
    uart0_putsln_P(PSTR("Phase 3: bit-banged RX on D2 @ 9600 8N1"));
    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("Sending AT every 2 s and dumping the reply as hex."));
    uart0_putsln_P(PSTR(""));

    for (;;) {
        uint16_t started;

        uart0_puts_P(PSTR("tx: AT   rx: "));

        softuart_tx_puts_P(PSTR("AT\r\n"));

        /* Wait for the four bytes to physically leave the pin. */
        while (!softuart_tx_idle()) {
        }

        /* Collect and print the reply for two seconds. */
        started = millis();
        while (!timer_expired(started, 2000)) {
            while (softuart_rx_has_data()) {
                uart0_put_hex8(softuart_rx_getc());
                uart0_putc(' ');
            }
        }

        uart0_putsln_P(PSTR(""));
        uart0_puts_P(PSTR("     frames ok "));
        uart0_put_udec(softuart_rx_frames());
        uart0_puts_P(PSTR(", rejected "));
        uart0_put_udec(softuart_rx_errors());
        uart0_putsln_P(PSTR(""));
        uart0_putsln_P(PSTR(""));
    }

    return 0; /* unreachable */
}
