/*
 * at.c — the non-blocking AT command state machine.
 *
 * Three states:
 *
 *   IDLE     nothing outstanding
 *   SENDING  bytes queued, still going out at 9600
 *   WAITING  on the wire; listening for a final response code
 *
 * The SENDING state exists for a reason that is easy to miss. At 9600
 * baud a short command takes a few milliseconds to leave the pin, and
 * the ESP will not reply until it has received the terminating newline.
 * Starting the timeout when at_command() is called would therefore
 * charge the command's own transmission time against its budget, which
 * matters for slow links and long commands. Instead the clock starts
 * once softuart_tx_idle() says the line is quiet.
 */

#include <string.h>
#include <avr/pgmspace.h>

#include "at.h"
#include "at_parse.h"
#include "softuart.h"
#include "timer.h"
#include "uart0.h"

typedef enum {
    AT_ST_IDLE = 0,
    AT_ST_SENDING,
    AT_ST_WAITING,
} at_state_t;

static at_state_t  state;
static at_result_t result;
static uint16_t    started;   /* millis() when transmission finished */
static uint16_t    timeout;   /* this command's budget, in ms */
static uint8_t     echo;
static uint8_t     hexdump;
static uint16_t    rx_bytes;  /* total bytes seen from the module */
static int16_t     last_code; /* from a "+CMD:<n>" line, or -1 */

void at_init(void)
{
    at_parse_reset();

    state     = AT_ST_IDLE;
    result    = AT_RESULT_NONE;
    echo      = 1;
    hexdump   = 0;
    rx_bytes  = 0;
    last_code = -1;
}

int at_ready(void)
{
    return state == AT_ST_IDLE;
}

at_result_t at_result(void)
{
    return result;
}

void at_set_echo(int on)
{
    echo = (on != 0) ? 1 : 0;
}

void at_set_hexdump(int on)
{
    hexdump = (on != 0) ? 1 : 0;
}

uint16_t at_rx_bytes(void)
{
    return rx_bytes;
}

int16_t at_last_code(void)
{
    return last_code;
}

void at_command_begin(uint16_t timeout_ms)
{
    /*
     * Drop anything left in the receive queue. A completed response can
     * leave a trailing CR LF behind, and the previous command's echo may
     * still be in flight; carrying that into the next command risks
     * matching a stale "OK" that was never a reply to *this* command.
     */
    softuart_rx_flush();
    at_parse_reset();

    last_code = -1;
    result    = AT_RESULT_IN_PROGRESS;
    timeout   = timeout_ms;
    state     = AT_ST_SENDING;
}

void at_command_end(void)
{
    /* Every AT command ends with CR LF, and the module will not act
     * until it sees one. */
    softuart_tx_puts_P(PSTR("\r\n"));
}

void at_command(const char *cmd, uint16_t timeout_ms)
{
    at_command_begin(timeout_ms);
    softuart_tx_puts(cmd);
    at_command_end();
}

void at_command_P(const char *cmd, uint16_t timeout_ms)
{
    at_command_begin(timeout_ms);
    softuart_tx_puts_P(cmd);
    at_command_end();
}

/* Read a decimal number at the start of s, skipping leading spaces. */
static int16_t parse_code(const char *s)
{
    int16_t value = 0;
    uint8_t digits = 0;

    while (*s == ' ') {
        s++;
    }

    while (*s >= '0' && *s <= '9') {
        value = (int16_t)((value * 10) + (*s - '0'));
        digits++;
        s++;
    }

    return digits ? value : -1;
}

/* A complete line has arrived from the ESP. Decide what it means. */
static void at_handle_line(const char *line)
{
    at_resp_t r = at_classify(line);

    /*
     * Remember "+CWJAP:2" style codes. They arrive *before* the final
     * OK or FAIL and carry the actual reason, so they are worth keeping
     * even though they never terminate a command themselves.
     */
    if (strncmp_P(line, PSTR("+CWJAP:"), 7) == 0) {
        last_code = parse_code(line + 7);
    }

    if (state == AT_ST_IDLE) {
        /*
         * Unsolicited message with no command outstanding - "WIFI
         * DISCONNECT", for instance. It has already been echoed to the
         * console, which is all we can usefully do with it for now.
         */
        return;
    }

    /*
     * Until a final code arrives, everything else is noise we simply
     * ignore: the echo of our own command, "+CWMODE:1" style
     * information, "(3,6)" boot text, blank lines.
     */
    if (r == AT_RESP_OK) {
        result = AT_RESULT_OK;
        state  = AT_ST_IDLE;
    } else if (r == AT_RESP_ERROR || r == AT_RESP_FAIL) {
        result = AT_RESULT_ERROR;
        state  = AT_ST_IDLE;
    }
}

void at_tick(void)
{
    /*
     * 1. Drain the receiver, taking each completed line as it forms.
     *
     * Consuming inside the loop rather than draining first and parsing
     * afterwards matters: there is a single line buffer, and a burst
     * arriving in one pass (the ESP can emit several lines back to
     * back) would otherwise overwrite lines before they were read.
     */
    while (softuart_rx_has_data()) {
        uint8_t c = softuart_rx_getc();

        rx_bytes++;

        if (hexdump) {
            /* Two hex digits and a space per byte: lossless. */
            uart0_put_hex8(c);
            uart0_putc(' ');
        } else if (echo) {
            uart0_putc((char)c);
        }

        if (at_parse_byte(c)) {
            at_handle_line(at_parse_line());
            at_parse_consume();
        }
    }

    /* 2. Once the command has left the pin, start the response clock. */
    if (state == AT_ST_SENDING && softuart_tx_idle()) {
        started = millis();
        state   = AT_ST_WAITING;
    }

    /* 3. Give up if nothing definitive arrived in time. */
    if (state == AT_ST_WAITING && timer_expired(started, timeout)) {
        result = AT_RESULT_TIMEOUT;
        state  = AT_ST_IDLE;
    }
}
