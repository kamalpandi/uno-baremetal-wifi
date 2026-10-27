/*
 * uart0.h — hardware USART0 on D0/D1, our debug console to the PC.
 *
 * This is deliberately small. It is a *debugging* aid, not the AT link:
 * the ESP8266 lives on a separate, bit-banged port (see softuart).
 *
 * Everything here is blocking on transmit and polling on receive. That
 * is fine for a console, and it keeps the code short enough to read.
 */

#ifndef UART0_H
#define UART0_H

#include <stdint.h>
#include <avr/pgmspace.h>

/* Configure the USART for 115200 8N1 and turn on TX + RX. */
void uart0_init(void);

/* Send one byte, blocking until the hardware is ready for it. */
void uart0_putc(char c);

/*
 * Send a NUL-terminated string from RAM. No newline is added.
 *
 * On AVR, a plain C string literal like "hello" is NOT kept in flash: it
 * is copied into RAM at startup. So the non-_P variants are for strings
 * that genuinely live in a RAM buffer (an AT response we received, say).
 */
void uart0_puts(const char *s);
void uart0_putsln(const char *s);

/*
 * Send a string from FLASH. Wrap the literal in PSTR() at the call site:
 *
 *   uart0_putsln_P(PSTR("hello"));
 *
 * This costs no RAM at all, so it is what you use for every fixed
 * message in the program. The trade-off is one pgm_read_byte per
 * character instead of a direct load, which is slower but irrelevant
 * for console output.
 */
void uart0_puts_P(const char *s);
void uart0_putsln_P(const char *s);

/* Send one byte as "0xAB" (always two uppercase hex digits). */
void uart0_put_hex8(uint8_t value);

/* Send an unsigned 16-bit value in decimal, e.g. 1234. */
void uart0_put_udec(uint16_t value);

/*
 * Send an unsigned 32-bit value in decimal.
 *
 * Only needed because an HTTP response size is not bounded by anything
 * we control: a directory listing fits in 16 bits, a file need not. It
 * costs a software 32-bit divide, which is why the 16-bit version still
 * exists for everything else.
 */
void uart0_put_udec32(uint32_t value);

/*
 * Non-blocking receive.
 *
 * uart0_has_data() returns non-zero if a byte is waiting. uart0_getc()
 * BLOCKS until one arrives, so it is only safe to call after
 * uart0_has_data() said yes.
 */
int  uart0_has_data(void);
char uart0_getc(void);

#endif /* UART0_H */
