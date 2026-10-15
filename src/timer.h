/*
 * timer.h — a millisecond clock, for timeouts.
 *
 * Deliberately 16-bit rather than 32-bit. At one tick per millisecond
 * that wraps every 65.5 seconds, which is far longer than any AT command
 * needs, and comparisons can still be written to survive the wrap (see
 * timer_expired). The 32-bit version would cost RAM and pull in 32-bit
 * arithmetic for no benefit here.
 */

#ifndef TIMER_H
#define TIMER_H

#include <stdint.h>

/* Start the 1 ms tick. Must be called before millis() is used. */
void timer_init(void);

/* Milliseconds since timer_init(), wrapping every 65536 ms. */
uint16_t millis(void);

/*
 * Non-zero once `timeout` ms have passed since `start`.
 *
 * Written as a subtraction so it stays correct across the wrap: when
 * millis() rolls over from 65535 to 0 the unsigned difference is still
 * the true elapsed time. Comparing timestamps directly ("is now > start
 * + timeout?") breaks at the wrap; this does not.
 */
int timer_expired(uint16_t start, uint16_t timeout);

#endif /* TIMER_H */
