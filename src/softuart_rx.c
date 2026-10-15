/*
 * softuart_rx.c — bit-banged UART receiver on Arduino D2 (PD2).
 *
 * WHY THIS IS HARDER THAN THE TRANSMITTER
 * ---------------------------------------
 * Transmitting is easy: we own the line and decide when every edge
 * happens. Receiving means a stranger starts talking at a moment of its
 * choosing, and we have to lock onto it and stay locked.
 *
 * HOW THE TIMING IS RECOVERED
 * ---------------------------
 * The line idles HIGH. A frame begins with a falling edge - the start
 * bit - and we use that edge to synchronise Timer1 to the sender's bit
 * grid. From then on we only have to sample once per bit, in the middle,
 * where the signal is guaranteed to be settled.
 *
 * Timer1 does the alignment for us, with no arithmetic in the hot path:
 *
 *   OCR1A = 1666   the counter is cleared on compare match, exactly at
 *                  the bit BOUNDARY. Crucially this clear happens in
 *                  hardware regardless of whether OCIE1A is enabled -
 *                  the interrupt enable gates the interrupt, not the
 *                  clear.
 *   OCR1B = 833    half a period later, i.e. the MIDDLE of the bit.
 *
 * So the usual "wait 1.5 bit periods for the first sample" special case
 * simply disappears. The OCR1A clear at the end of the start bit *is*
 * the half-bit offset. Ten COMPB events land exactly on:
 *
 *   t=833    middle of the START bit   -> verify still LOW
 *   t=2499   middle of data bit 0      -> sample
 *   t=4166   middle of data bit 1      -> sample
 *   ...
 *   t=14168  middle of data bit 7      -> sample
 *   t=15835  middle of the STOP bit    -> verify HIGH
 *
 * Note that this is TIMER1_COMPB_vect, a different vector from the
 * transmitter's TIMER1_COMPA_vect, so transmit and receive stay in
 * separate files with no shared interrupt and no mode flag.
 *
 * HALF-DUPLEX IS WHAT MAKES THE SHARING SAFE
 * ------------------------------------------
 * One timer serving both directions sounds like a conflict. It is not,
 * because AT traffic is request/response: the ESP cannot begin replying
 * until it has received our terminating newline, which is the last thing
 * we send. The two never need the timer at the same moment.
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "softuart.h"

/* --- Pin: D2 / PD2 is our receive line ----------------------------------- */
#define ESP_RX_PIN PD2

#define esp_rx_is_high() ((PIND & _BV(ESP_RX_PIN)) != 0)

/* --- Receive queue ------------------------------------------------------- */
/*
 * 256 bytes is a quarter of this chip's RAM, so it is a deliberate
 * choice rather than a default. AT responses are short, but a command
 * like AT+CWJAP can emit several lines, and later HTTP bodies will be
 * longer still.
 *
 * As with the transmitter this is a single-producer single-consumer ring
 * buffer - the ISR writes rx_head, main writes rx_tail - so no interrupt
 * masking is needed for the indices.
 *
 * The buffer is 256 entries but holds at most 255, because "full" and
 * "empty" would otherwise look identical when head == tail.
 */
#define RX_BUF_SIZE 256
#define RX_BUF_MASK (RX_BUF_SIZE - 1)

static volatile uint8_t rx_buf[RX_BUF_SIZE];
static volatile uint8_t rx_head;
static volatile uint8_t rx_tail;

/* Frame assembly. */
static volatile uint8_t  rx_shift;   /* bits collected so far */
static volatile uint8_t  rx_step;    /* which mid-bit sample we are on */
static volatile uint8_t  rx_overflow;
static volatile uint16_t rx_errors;
static volatile uint16_t rx_frames;
static volatile uint8_t  loopback;   /* see softuart_set_loopback() */

void softuart_set_loopback(int on)
{
    loopback = (on != 0) ? 1 : 0;
}

void softuart_rx_init(void)
{
    /*
     * PD2 as an input. Engage the internal pull-up.
     *
     * The ESP's TX pin drives this line, so the pull-up does nothing
     * while the shield is fitted - but with the shield removed a
     * floating input picks up mains hum and generates phantom start
     * bits forever. The pull-up costs nothing and removes that failure.
     */
    DDRD  &= (uint8_t)~_BV(ESP_RX_PIN);
    PORTD |= _BV(ESP_RX_PIN);

    /*
     * The mid-bit sample point. SOFTUART_BIT_CYCLES/2 is 833, and
     * softuart_tx_init() has already set OCR1A to 1666 - which is what
     * keeps clearing the counter and turns 833 into "half a bit after
     * the bit boundary".
     */
    OCR1B = SOFTUART_BIT_CYCLES / 2;

    /*
     * INT0 on the falling edge = start bit.
     *   ISC01:ISC00 = 10 selects falling edge.
     * INT0 lives on PD2 on the ATmega328P, which is why the shield's
     * RX jumper is on D2.
     */
    EICRA  = _BV(ISC01);
    EIMSK |= _BV(INT0);
}

int softuart_rx_has_data(void)
{
    return (rx_head != rx_tail) ? 1 : 0;
}

uint8_t softuart_rx_count(void)
{
    return (uint8_t)((rx_head - rx_tail) & RX_BUF_MASK);
}

uint8_t softuart_rx_getc(void)
{
    uint8_t byte = rx_buf[rx_tail];

    rx_tail = (uint8_t)((rx_tail + 1) & RX_BUF_MASK);
    return byte;
}

void softuart_rx_flush(void)
{
    /*
     * rx_head is written by the ISR, so read/assign it with interrupts
     * briefly off. A frame arriving mid-flush would otherwise be
     * silently un-queued. The previous interrupt state is restored
     * rather than blindly calling sei(), so this is safe to call from
     * inside an ISR too.
     */
    uint8_t sreg = SREG;

    cli();
    rx_tail = rx_head;
    SREG = sreg;
}

int softuart_rx_overflowed(void)
{
    return rx_overflow != 0;
}

uint16_t softuart_rx_errors(void)
{
    return rx_errors;
}

uint16_t softuart_rx_frames(void)
{
    return rx_frames;
}

/* Append a received byte. Drops it if the queue is full. */
static void rx_push(uint8_t byte)
{
    uint8_t next = (uint8_t)((rx_head + 1) & RX_BUF_MASK);

    if (next == rx_tail) {
        rx_overflow = 1;
        return;
    }

    rx_buf[rx_head] = byte;
    rx_head = next;
}

/* Stop sampling and go back to watching for the next start bit. */
static void rx_finish(void)
{
    TIMSK1 &= (uint8_t)~_BV(OCIE1B);
    EIMSK  |= _BV(INT0);
}

/*
 * A falling edge on the receive line.
 */
ISR(INT0_vect)
{
    /*
     * Stop listening immediately.
     *
     * A frame contains plenty of low bits, and every one of them is a
     * falling edge. If we left INT0 armed we would keep restarting the
     * bit clock part-way through a frame and never finish one. INT0 is
     * re-armed by rx_finish() when the frame is done.
     */
    EIMSK &= (uint8_t)~_BV(INT0);

    /*
     * If we are still transmitting, this cannot be a reply - the ESP
     * cannot answer until it has seen our terminating newline. Abandon
     * and stay listening. (In loopback mode we are deliberately
     * listening to ourselves, so the check is skipped.)
     */
    if (!loopback && !softuart_tx_idle()) {
        EIMSK |= _BV(INT0);
        return;
    }

    TCNT1 = 0;      /* synchronise the bit clock to this edge */
    rx_shift = 0;
    rx_step  = 0;

    /*
     * Discard any compare-match B flag left over from the idle period.
     *
     * The counter free-runs and crosses OCR1B once per bit period, so
     * OCF1B gets set whether or not the interrupt is enabled - and with
     * OCIE1B off, nothing clears it. Enabling the interrupt on a stale
     * flag fires COMPB immediately, which steals the rx_step == 0 slot
     * at the wrong time and shifts every subsequent sample by a whole
     * bit. Writing 1 to the flag clears it.
     */
    TIFR1 = _BV(OCF1B);

    TIMSK1 |= _BV(OCIE1B);
}

/*
 * One mid-bit sample point has arrived.
 */
ISR(TIMER1_COMPB_vect)
{
    if (rx_step == 0) {
        /*
         * Middle of the START bit. A real start bit is still LOW here.
         * If it has already returned HIGH this was a glitch, not a
         * frame, and we bail out now rather than wasting ten bit
         * periods discovering the same thing at the end.
         */
        if (esp_rx_is_high()) {
            rx_errors++;
            rx_finish();
            return;
        }

        rx_step = 1;
        return;
    }

    if (rx_step <= 8) {
        /*
         * Data bits, least significant first.
         *
         * Shifting right and inserting at the top is the standard
         * receive idiom: the first bit received gets pushed down one
         * place for every bit that follows, so after eight samples it
         * has landed in bit 0 - exactly where an LSB-first byte wants
         * it. As with the transmitter, this avoids a variable shift,
         * which the AVR would have to compile into a loop.
         */
        rx_shift >>= 1;
        if (esp_rx_is_high()) {
            rx_shift |= 0x80;
        }

        rx_step++;
        return;
    }

    /*
     * rx_step == 9: middle of the STOP bit, which must be HIGH.
     *
     * This check is what makes the receiver usable at all in practice.
     * At power-up the ESP8266 dumps its boot log at 74880 baud, roughly
     * eight times faster than we are listening. Those bytes fail the
     * stop-bit check and are discarded here, instead of being handed to
     * the parser as garbage. (The firmware then prints "ready" at the
     * normal AT baud rate, which we *can* read.)
     */
    if (esp_rx_is_high()) {
        rx_push(rx_shift);
        rx_frames++;
    } else {
        rx_errors++;
    }

    rx_finish();
}
