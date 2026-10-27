/*
 * softuart_tx.c — bit-banged UART transmitter on Arduino D3 (PD3).
 *
 * WHAT A UART FRAME LOOKS LIKE
 * ----------------------------
 * The line sits HIGH when nothing is happening ("idle"). A frame is ten
 * bit periods long, and every bit lasts the same amount of time:
 *
 *        idle  start  d0   d1   d2   d3   d4   d5   d6   d7   stop  idle
 *        ────┐       ┌────┐    ┌────┐    ┌────┐    ┌────┐         ┌──────
 *            │       │    │    │    │    │    │    │    │         │
 *            └───────┘    └────┘    └────┘    └────┘    └─────────┘
 *        HIGH   LOW  ...                    ...                 HIGH
 *
 *   - One START bit, always LOW. The receiver watches for this edge.
 *   - Eight DATA bits, sent LEAST significant bit first.
 *   - One STOP bit, always HIGH, which guarantees a LOW->HIGH edge so the
 *     receiver is ready to see the next start bit.
 *
 * Both sides must agree on how long a bit lasts, and there is no clock
 * wire, so each side counts from its own crystal. That is why baud
 * accuracy matters: ~2% is the usual tolerance before bits are misread.
 *
 * HOW WE TIME IT
 * --------------
 * Timer1 in CTC mode. The counter is cleared by hardware every time it
 * matches OCR1A, so a compare-match interrupt fires once per bit period
 * with no accumulated drift.
 *
 *   bit period = F_CPU / baud = 16000000 / 9600 = 1666.7 cycles
 *   OCR1A      = 1666  ->  period is 1667 cycles  ->  9598 baud
 *   error      = -0.02%
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "softuart.h"

/* --- Pin: D3 / PD3 is our transmit line ---------------------------------- */
#define ESP_TX_PIN   PD3

#define esp_tx_high()  (PORTD |= _BV(ESP_TX_PIN))
#define esp_tx_low()   (PORTD &= (uint8_t)~_BV(ESP_TX_PIN))

/* Rounded to the nearest whole number of CPU cycles per bit: 1667. */
#define BIT_CYCLES SOFTUART_BIT_CYCLES

/* --- Transmit queue ------------------------------------------------------ */
/*
 * A power of two so the wrap-around is a cheap AND instead of a modulo.
 * 128 bytes is 6% of the chip's entire RAM, which is the sort of trade
 * you think about constantly on an ATmega328P.
 *
 * This is a textbook single-producer single-consumer ring buffer:
 *   - main writes tx_head, the ISR reads it
 *   - the ISR writes tx_tail, main reads it
 * Neither side writes the other's index, and an 8-bit load/store is
 * atomic on AVR, so no interrupt masking is needed.
 */
#define TX_BUF_SIZE 128
#define TX_BUF_MASK (TX_BUF_SIZE - 1)

static volatile uint8_t tx_buf[TX_BUF_SIZE];
static volatile uint8_t tx_head;
static volatile uint8_t tx_tail;

static volatile uint8_t tx_byte;  /* the byte currently being shifted out */
static volatile uint8_t tx_bit;   /* 0..7 data bits, 8 stop, 9 frame done  */
static volatile uint8_t tx_busy;

/* Pull one byte out of the queue. Returns 0 when the queue is empty. */
static uint8_t tx_load_next(void)
{
    if (tx_tail == tx_head) {
        return 0;
    }

    tx_byte = tx_buf[tx_tail];
    tx_tail = (uint8_t)((tx_tail + 1) & TX_BUF_MASK);
    return 1;
}

/*
 * Begin a frame immediately: emit the start bit now and start the clock.
 * Only ever called when we are idle, so it cannot disturb a frame in
 * progress.
 */
static void tx_kick(void)
{
    if (!tx_load_next()) {
        return;
    }

    tx_busy = 1;
    tx_bit  = 0;

    esp_tx_low();   /* start bit, right now */
    TCNT1 = 0;      /* begin this bit period from zero so the first  */
                    /* compare match lands a full period from now    */

    /*
     * Clear any compare-match A flag left over from the idle period.
     *
     * The counter free-runs and crosses OCR1A once per bit period, so
     * OCF1A is set whether or not the interrupt is enabled - and with
     * OCIE1A off, nothing clears it. Enabling the interrupt on a stale
     * flag makes COMPA fire immediately, which emits data bit 0 the
     * instant the start bit is asserted. The start bit then lasts a
     * handful of CPU cycles instead of a whole bit period, and the
     * receiver has no chance of framing the byte.
     *
     * This is easy to miss because everything still *looks* right: the
     * pin toggles, the activity LED blinks, and only the timing is
     * wrong. Writing 1 to the flag clears it.
     */
    TIFR1 = _BV(OCF1A);

    TIMSK1 |= _BV(OCIE1A);
}

void softuart_tx_init(void)
{
    DDRD |= _BV(ESP_TX_PIN);   /* D3 is an output */
    esp_tx_high();             /* idle level is HIGH */

    /*
     * Timer1, CTC mode, no prescaler.
     *
     *   TCCR1A = 0        no PWM, no compare-output pin, WGM11:10 = 00
     *   WGM12  = 1        CTC: clear TCNT1 on compare match with OCR1A
     *   CS10   = 1        1:1 prescaler, i.e. one tick per CPU cycle
     *
     * The interrupt is left disabled until there is a byte to send.
     */
    TCCR1A = 0;
    TCCR1B = _BV(WGM12) | _BV(CS10);
    TCCR1C = 0;
    OCR1A  = BIT_CYCLES - 1;
    TCNT1  = 0;
}

void softuart_tx_putc(char c)
{
    uint8_t next = (uint8_t)((tx_head + 1) & TX_BUF_MASK);

    /*
     * Block only if the queue is genuinely full. At 9600 baud a byte
     * takes about 1 ms, so a 128-byte queue is over 100 ms of slack -
     * far more than any AT command needs. This is the one blocking call
     * in the driver, and it is safe precisely because AT traffic is
     * request/response: we send a short command and then wait.
     */
    while (next == tx_tail) {
        /* queue full: wait for the ISR to drain it */
    }

    tx_buf[tx_head] = (uint8_t)c;
    tx_head = next;

    /*
     * If the transmitter was idle, start it. This is race-free without
     * masking interrupts: tx_busy is only ever 0 when OCIE1A is
     * disabled, so no ISR can be running or can start until we enable
     * it ourselves inside tx_kick().
     */
    if (!tx_busy) {
        tx_kick();
    }
}

void softuart_tx_puts(const char *s)
{
    while (*s != '\0') {
        softuart_tx_putc(*s);
        s++;
    }
}

void softuart_tx_puts_P(const char *s)
{
    char c;

    while ((c = (char)pgm_read_byte(s)) != '\0') {
        softuart_tx_putc(c);
        s++;
    }
}

void softuart_tx_put_udec(uint16_t value)
{
    /*
     * Digits come out least-significant first, so collect them and
     * queue them backwards. 65535 is the widest a uint16_t can be, so
     * five bytes is enough.
     */
    char    buf[5];
    uint8_t n = 0;

    do {
        buf[n] = (char)('0' + (value % 10));
        n++;
        value = (uint16_t)(value / 10);
    } while (value > 0);

    while (n > 0) {
        n--;
        softuart_tx_putc(buf[n]);
    }
}

int softuart_tx_idle(void)
{
    return (!tx_busy && (tx_head == tx_tail)) ? 1 : 0;
}

/*
 * One bit period has elapsed. Emit the next bit of the frame.
 *
 * tx_bit is where we are within the ten-bit frame. Note that the start
 * bit is not handled here at all: it is emitted by tx_kick() at the
 * moment the frame begins, so by the time this first runs we owe the
 * receiver data bit 0.
 */
ISR(TIMER1_COMPA_vect)
{
    if (tx_bit < 8) {
        /*
         * Data, least significant bit first.
         *
         * Shifting the byte right as we go is the natural way to express
         * this: the bit that has to go out next is always bit 0. It also
         * avoids a variable shift, which the AVR would have to compile
         * into a loop because it has no barrel shifter.
         */
        if (tx_byte & 1u) {
            esp_tx_high();
        } else {
            esp_tx_low();
        }
        tx_byte >>= 1u;
        tx_bit++;

    } else if (tx_bit == 8) {
        /* Stop bit: always HIGH. */
        esp_tx_high();
        tx_bit++;

    } else {
        /* tx_bit == 9: the stop bit just finished. */
        if (tx_load_next()) {
            tx_bit = 0;
            esp_tx_low();      /* start bit of the next frame */
        } else {
            /*
             * Nothing left to send. Go idle and switch the timer
             * interrupt off - no point burning an interrupt every
             * 1667 cycles while we have nothing to say.
             */
            tx_busy = 0;
            TIMSK1 &= (uint8_t)~_BV(OCIE1A);
        }
    }
}
