/*
 * main.c — Phase 0: prove the toolchain and the upload path.
 *
 * There is deliberately no UART in this file. Before writing a single
 * line of serial code we want to know, with certainty, that:
 *
 *   1. avr-gcc can produce a .hex for this chip,
 *   2. avrdude can push that .hex through the CH340 clone's bootloader,
 *   3. the chip actually runs and we can see it.
 *
 * The onboard LED on Arduino pin D13 is the cheapest possible "hello".
 */

#include <avr/io.h>
#include <util/delay.h>

/*
 * _BV(n) is defined by avr-libc as (1 << n).
 *
 * The name means "bit value". Writing _BV(DDB5) is identical to writing
 * (1 << 5) == 0b00100000. It is spelled out separately so the reader knows
 * we mean "bit 5", not "the number 32".
 *
 * DDB5 is the Data Direction bit for port B pin 5, which is Arduino D13.
 *   DDRB  (Data Direction Register B): 1 = output, 0 = input
 *   PORTB (output register): when the pin is an output, 1 = high, 0 = low
 */
static void led_init(void)
{
    DDRB |= _BV(DDB5);   /* configure PB5/D13 as an output */
}

int main(void)
{
    led_init();

    for (;;) {
        PORTB |= _BV(PORTB5);    /* drive the pin high -> LED on  */
        _delay_ms(500);

        PORTB &= ~_BV(PORTB5);   /* drive the pin low  -> LED off */
        _delay_ms(500);
    }

    /* Unreachable. An embedded program never returns; there is nothing
     * to return to. If main() ever fell off the end the chip would
     * re-run the reset vector and start over, which would be a bug. */
    return 0;
}
