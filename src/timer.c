/*
 * timer.c — a 1 ms tick from Timer0.
 *
 * Timer0 is available precisely because there is no Arduino core. The
 * Arduino runtime claims Timer0 for its own millis(), and reclaiming it
 * is one of the quiet advantages of going bare-metal.
 *
 * Timer1 belongs to the bit-banged UART, so Timer0 is the only sensible
 * choice; Timer2 would also work but has nothing to offer here.
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "timer.h"

static volatile uint16_t tick_count;

void timer_init(void)
{
    /*
     * Timer0 in CTC mode, ticking once per millisecond.
     *
     *   prescaler 64:  16000000 / 64 = 250000 Hz
     *   250000 Hz / 1000           = 250 ticks per millisecond
     *   OCR0A = 249 makes the period 250 ticks
     *
     * In CTC mode the counter is cleared on a compare match with OCR0A,
     * so the interrupt fires once per millisecond with no drift.
     */
    TCCR0A = _BV(WGM01);                  /* CTC mode           */
    TCCR0B = _BV(CS01) | _BV(CS00);       /* prescaler 64       */
    OCR0A  = 249;                         /* 250 ticks = 1 ms    */
    TCNT0  = 0;
    TIMSK0 = _BV(OCIE0A);
}

ISR(TIMER0_COMPA_vect)
{
    tick_count++;
}

uint16_t millis(void)
{
    /*
     * A 16-bit read from SRAM takes two instructions, and an interrupt
     * can land between them - a "torn read" that yields half of each
     * value. Mask interrupts around it.
     *
     * Note the save/restore of SREG rather than calling cli()/sei()
     * outright: sei() would *enable* interrupts unconditionally, which
     * would be wrong if millis() were ever called from inside an ISR.
     */
    uint8_t  sreg = SREG;
    uint16_t value;

    cli();
    value = tick_count;
    SREG = sreg;

    return value;
}

int timer_expired(uint16_t start, uint16_t timeout)
{
    /*
     * (millis() - start) is unsigned arithmetic, so it wraps with the
     * clock instead of breaking. Cast to int16_t and test the sign: we
     * have waited long enough once the elapsed time has reached the
     * timeout. This stays correct across the 65.5 s rollover, and for
     * any timeout below 32768 ms.
     */
    return (int16_t)(millis() - (uint16_t)(start + timeout)) >= 0;
}
