/*
 * softuart.h — the bit-banged UART that talks to the ESP8266.
 *
 * There is no hardware behind this port. PD3 and PD2 are plain GPIO pins
 * and we produce and measure the UART waveform ourselves, from timer and
 * pin-change interrupts. That is the price of keeping the one real USART
 * for the debug console.
 *
 * Wiring (shield jumpers on D2/D3):
 *
 *   ESP TX  ->  D2 / PD2   our receive line  (softuart_rx.c)
 *   ESP RX  <-  D3 / PD3   our transmit line (softuart_tx.c)
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
 * Queue an unsigned 16-bit value in decimal. Used to build commands whose
 * arguments are numbers: AT+CIPSEND=<len> and the port in AT+CIPSTART.
 */
void softuart_tx_put_udec(uint16_t value);

/*
 * Non-zero when every queued byte has physically left the pin. Used to
 * know when a command has finished going out before we start listening
 * for a reply.
 */
int softuart_tx_idle(void);

/* ========================================================================= */
/* Receiver — D2 / PD2                                                       */
/* ========================================================================= */

/*
 * Configure PD2 as an input and arm INT0 for start-bit detection.
 *
 * MUST be called after softuart_tx_init(), because it relies on the
 * Timer1 settings (in particular OCR1A) that softuart_tx_init installs.
 */
void softuart_rx_init(void);

/* Non-zero when at least one received byte is waiting. */
int softuart_rx_has_data(void);

/* Number of bytes waiting (0..255). */
uint8_t softuart_rx_count(void);

/* Remove and return one byte. Only call when softuart_rx_has_data(). */
uint8_t softuart_rx_getc(void);

/* Discard everything currently queued. */
void softuart_rx_flush(void);

/* Non-zero at least once if a byte was ever dropped because the queue
 * was full. Sticky; read it to detect a slow consumer. */
int softuart_rx_overflowed(void);

/* Count of frames rejected by the start/stop bit checks. At power-up the
 * ESP's 74880-baud boot log inflates this considerably, which is fine. */
uint16_t softuart_rx_errors(void);

/* Count of frames accepted. */
uint16_t softuart_rx_frames(void);

/*
 * Loopback self-test support.
 *
 * Normally the receiver ignores anything arriving while we are still
 * transmitting, because a real ESP cannot answer until it has seen the
 * terminating newline. With D2 jumpered to D3 that rule would make the
 * test impossible, so this turns the check off and lets the receiver
 * decode our own transmission.
 *
 * This is the only way to verify the transmit timing without a scope:
 * a mis-shaped start bit is invisible to an activity LED, but it cannot
 * survive a round trip through our own receiver.
 */
void softuart_set_loopback(int on);

#endif /* SOFTUART_H */
