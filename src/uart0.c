/*
 * uart0.c — hardware USART0 on Arduino D0 (RX) / D1 (TX).
 *
 * Registers involved, all from <avr/io.h>:
 *
 *   UBRR0H / UBRR0L   baud rate divisor (high and low byte)
 *   UCSR0A            status register A — flags, and U2X0
 *   UCSR0B            control register B — enables
 *   UCSR0C            control register C — frame format
 *   UDR0              the actual data register (write to send, read to receive)
 */

#include <avr/io.h>

#include "uart0.h"

/*
 * ---------------------------------------------------------------------------
 * Baud rate maths
 * ---------------------------------------------------------------------------
 *
 * The divisor we load into UBRR0 depends on whether double-speed mode
 * (U2X0 in UCSR0A) is on:
 *
 *   normal speed :  UBRR = F_CPU / (16 * baud) - 1
 *   double speed :  UBRR = F_CPU / ( 8 * baud) - 1
 *
 * At 16 MHz and 115200 there is no exact integer solution, so pick the
 * closest one and know the error:
 *
 *   double speed: 16000000 / (8 * 115200) - 1 = 16.36  ->  16
 *   actual baud : 16000000 / (8 * (16 + 1))     = 117647
 *   error       : +2.1%
 *
 * 2.1% is right at the edge of what a UART tolerates, and it is what
 * every Arduino does at 115200. It survives because the sampling point
 * only drifts about 0.19 of a bit period across a whole 10-bit frame.
 *
 * If you ever see garbled output, drop BAUD_DEBUG to 57600UL: that gives
 * a divisor of 34 and a much safer 0.8% error.
 *
 * The compiler folds all of this into a constant — no division at run time.
 */
#define BAUD_DEBUG 115200UL
#define UBRR_VALUE ((F_CPU / (8UL * BAUD_DEBUG)) - 1)

void uart0_init(void)
{
    /* Load the 12-bit divisor. UBRR0H must be written before UBRR0L. */
    UBRR0H = (uint8_t)(UBRR_VALUE >> 8);
    UBRR0L = (uint8_t)(UBRR_VALUE & 0xFF);

    /*
     * Status register A.
     *
     * U2X0 = 1 selects double-speed mode, which halves the divisor and
     * doubles the tolerable error margin here. We assign rather than OR:
     * on reset UCSR0A is all zero, and the other bits (MPCM0 and the
     * RXC0/TXC0/UDRE0 flags) are read-only or cleared by writing 1 to
     * them — so |= would be actively wrong here.
     */
    UCSR0A = _BV(U2X0);

    /*
     * Control register B: turn on the hardware.
     *
     *   RXEN0 = receiver enable
     *   TXEN0 = transmitter enable
     *
     * Note there is no RXCIE0 here: we are polling, not interrupting.
     * (The bit-banged ESP port will need interrupts, because a 9600
     * start bit is too narrow to catch reliably by polling.)
     */
    UCSR0B = _BV(RXEN0) | _BV(TXEN0);

    /*
     * Control register C: frame format, 8N1.
     *
     *   UMSEL01:00 = 00   asynchronous USART
     *   UPM01:00   = 00   parity disabled
     *   USBS0      = 0    one stop bit
     *   UCSZ01:00  = 11   character size: 8 bits
     *
     * 8-bit mode needs UCSZ02 (in UCSR0B) to be 0, which it is.
     */
    UCSR0C = _BV(UCSZ01) | _BV(UCSZ00);
}

void uart0_putc(char c)
{
    /*
     * UDRE0 (USART Data Register Empty) is set by the hardware whenever
     * UDR0 is free to accept another byte. Waiting on it is what paces
     * us to the real baud rate; without it we would overwrite bytes
     * before they were shifted out.
     *
     * Writing UDR0 clears UDRE0 automatically, so we do not clear it.
     */
    while (!(UCSR0A & _BV(UDRE0))) {
        /* spin */
    }

    UDR0 = (uint8_t)c;
}

void uart0_puts(const char *s)
{
    while (*s != '\0') {
        uart0_putc(*s);
        s++;
    }
}

void uart0_putsln(const char *s)
{
    uart0_puts(s);
    uart0_putc('\r');
    uart0_putc('\n');
}

/*
 * pgm_read_byte() reads one byte out of the flash address space.
 *
 * The AVR Harvard architecture means flash and RAM are addressed through
 * *different* instructions. A normal C pointer dereference always reads
 * RAM, so pointing one at flash silently returns garbage. pgm_read_byte
 * emits the special LPM instruction instead. That is the whole reason
 * PROGMEM data needs its own accessor functions.
 */
void uart0_puts_P(const char *s)
{
    char c;

    while ((c = (char)pgm_read_byte(s)) != '\0') {
        uart0_putc(c);
        s++;
    }
}

void uart0_putsln_P(const char *s)
{
    uart0_puts_P(s);
    uart0_putc('\r');
    uart0_putc('\n');
}

/* Convert a value 0..15 into its hex digit, '0'..'F'. */
static char hex_digit(uint8_t v)
{
    v &= 0x0F;

    if (v < 10) {
        return (char)('0' + v);
    }
    return (char)('A' + (v - 10));
}

void uart0_put_hex8(uint8_t value)
{
    /* "0x" comes from flash; the two digits are computed, not looked up
     * in a table, so this function needs no RAM at all. */
    uart0_puts_P(PSTR("0x"));

    uart0_putc(hex_digit(value >> 4));  /* high nibble */
    uart0_putc(hex_digit(value));       /* low nibble  */
}

void uart0_put_udec(uint16_t value)
{
    /*
     * Digits come out least-significant first, so collect them into a
     * small stack buffer and print it backwards.
     *
     * 65535 is the widest a uint16_t can be, hence 5 bytes. Note that
     * the AVR has no divide instruction, so each % 10 and / 10 becomes
     * a call into avr-libc's software division. That costs a little
     * flash, but this is a debugging aid and clarity wins.
     */
    char    buf[5];
    uint8_t i = 0;

    if (value == 0) {
        uart0_putc('0');
        return;
    }

    while (value > 0) {
        buf[i] = (char)('0' + (value % 10));
        i++;
        value = (uint16_t)(value / 10);
    }

    while (i > 0) {
        i--;
        uart0_putc(buf[i]);
    }
}

int uart0_has_data(void)
{
    /* RXC0 is set by the hardware when an unread byte sits in UDR0. */
    return (UCSR0A & _BV(RXC0)) != 0;
}

char uart0_getc(void)
{
    while (!uart0_has_data()) {
        /* spin */
    }

    return (char)UDR0;
}
