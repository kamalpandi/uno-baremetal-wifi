/*
 * softuart.h — the bit-banged UART that talks to the ESP8266.
 *
 * There is no hardware behind this port. PD3 is a plain GPIO pin and we
 * produce the UART waveform ourselves, one bit at a time, from a timer
 * interrupt. That is the price of keeping the one real USART for the
 * debug console.
 *
 * Wiring (shield jumpers on D2/D3):
 *
 *   ESP TX  ->  D2 / PD2   our receive line  (Phase 3)
 *   ESP RX  <-  D3 / PD3   our transmit line (this phase)
 */

#ifndef SOFTUART_H
#define SOFTUART_H

#include <stdint.h>
#include <avr/pgmspace.h>

/* The shield's AT firmware defaults to 9600 8N1. */
#define SOFTUART_BAUD 9600UL

/*
 * One bit period in CPU cycles, rounded to the nearest whole cycle:
 * 16000000 / 9600 = 1666.7 -> 1667, which is 9598 baud (-0.02% error).
 *
 * Both directions share this number, and both derive their timing from
 * the same Timer1, so transmit and receive cannot drift apart.
 */
#define SOFTUART_BIT_CYCLES \
    ((uint16_t)((F_CPU + SOFTUART_BAUD / 2) / SOFTUART_BAUD))

/* ========================================================================= */
/* Transmitter — D3 / PD3                                                    */
/* ========================================================================= */

/* Configure PD3 as an output and set up Timer1 for the bit clock. */
void softuart_tx_init(void);

/*
 * Queue one byte for transmission. Returns immediately in the common
 * case; blocks only if the 128-byte queue is completely full.
 */
void softuart_tx_putc(char c);

/* Queue a NUL-terminated string. Newlines are NOT added for you. */
void softuart_tx_puts(const char *s);

/* Queue a string held in flash. Use with PSTR(). */
void softuart_tx_puts_P(const char *s);

/*
 * Non-zero when every queued byte has physically left the pin. Used to
 * know when a command has finished going out before we start listening
 * for a reply.
 */
int softuart_tx_idle(void);

#endif /* SOFTUART_H */
