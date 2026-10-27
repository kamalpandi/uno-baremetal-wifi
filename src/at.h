/*
 * at.h — a non-blocking AT command interface to the ESP8266.
 *
 * The point of this layer is that it never blocks. You start a command,
 * then call at_tick() from your main loop; the state machine feeds
 * itself from the receive queue, decides when the response is complete,
 * and enforces a timeout. Your loop stays free to do other things.
 *
 * Usage:
 *
 *     at_command_P(PSTR("AT"), 2000);      // queue it
 *     while (!at_ready()) {                // or just keep calling
 *         at_tick();                       // at_tick() from your loop
 *     }
 *     if (at_result() == AT_RESULT_OK) { ... }
 *
 * The blocking while-loop above is the caller's choice, not a property
 * of the library. A program controlling motors would simply call
 * at_tick() once per loop pass and check at_ready() when convenient.
 *
 * Phase 6 adds two things that are not "send a line, wait for OK":
 *
 *   - AT+CIPSEND answers in two stages, first '>' and only then
 *     "SEND OK", so the layer needs to be told what to expect (at_wait_t).
 *   - received TCP data arrives as +IPD blocks and is handed to a sink
 *     callback instead of being parsed as lines.
 */

#ifndef AT_H
#define AT_H

#include <stdint.h>
#include <avr/pgmspace.h>

typedef enum {
    AT_RESULT_NONE = 0,     /* no command has been run yet        */
    AT_RESULT_IN_PROGRESS,  /* queued or waiting for the response */
    AT_RESULT_OK,           /* saw the answer we were waiting for */
    AT_RESULT_ERROR,        /* saw "ERROR" or "FAIL"              */
    AT_RESULT_TIMEOUT,      /* nothing definitive within the time */
} at_result_t;

/*
 * What ends the command that is currently outstanding.
 *
 * AT_WAIT_FINAL is the ordinary case and the default. The other two
 * exist because AT+CIPSEND replies "OK" *before* the '>' prompt: if that
 * OK were allowed to finish the command, the payload would be sent to a
 * module that is not yet listening for it.
 */
typedef enum {
    AT_WAIT_FINAL = 0,      /* OK / ERROR / FAIL                   */
    AT_WAIT_PROMPT,         /* '>' - payload may now be sent       */
    AT_WAIT_SEND_OK,        /* "SEND OK" - payload was accepted    */
} at_wait_t;

/* Called once per received TCP payload byte (a +IPD block). */
typedef void (*at_ipd_fn)(uint8_t c);

void at_init(void);

/*
 * Queue a command and begin waiting for its response. The CR LF that
 * every AT command needs is appended for you; do not include it.
 *
 * `timeout_ms` is measured from the moment the last byte leaves the
 * wire, not from this call, so a slow baud rate cannot eat the budget.
 * Only valid when at_ready() is true.
 *
 * Resets the expectation to AT_WAIT_FINAL, so call at_set_wait() after
 * this if the command is not an ordinary one.
 */
void at_command(const char *cmd, uint16_t timeout_ms);
void at_command_P(const char *cmd, uint16_t timeout_ms);

/*
 * Build a command from several pieces, for when part of it lives in RAM
 * (an SSID or password, say) and part in flash:
 *
 *     at_command_begin(20000);
 *     softuart_tx_puts_P(PSTR("AT+CWJAP=\""));
 *     softuart_tx_puts(creds->ssid);
 *     softuart_tx_puts_P(PSTR("\",\""));
 *     softuart_tx_puts(creds->pass);
 *     softuart_tx_puts_P(PSTR("\""));
 *     at_command_end();
 *
 * at_command_end() supplies the CR LF and begins the response clock, so
 * every path through must reach it exactly once.
 */
void at_command_begin(uint16_t timeout_ms);
void at_command_end(void);

/* Choose what will end the outstanding command. See at_wait_t. */
void at_set_wait(at_wait_t w);

/*
 * Queue raw bytes *without* a trailing newline and wait for "SEND OK".
 *
 * This is stage two of AT+CIPSEND, and it deliberately does not call
 * at_command_begin(): that flushes the receiver and resets the parser,
 * and by this point the module may already be pushing +IPD payload at
 * us. Only the expectation and the clock change here.
 *
 * Blocks only if `len` exceeds the 128-byte transmit queue, which cannot
 * happen for a request built by http_build_request().
 */
void at_send_data(const uint8_t *data, uint16_t len, uint16_t timeout_ms);

/* Non-zero if the last completed command ended on the '>' prompt. */
int at_prompt_seen(void);

/*
 * Non-zero once the module has reported "CLOSED". Sticky: it stays set
 * until at_clear_closed(), because the peer can hang up at any moment,
 * with no command outstanding at all. For an HTTP/1.0 request this is
 * the end-of-response signal.
 */
int at_link_closed(void);
void at_clear_closed(void);

/* Register the handler for +IPD payload bytes. Pass 0 to discard them. */
void at_set_ipd_sink(at_ipd_fn fn);

/*
 * The numeric code from the most recent "+CMD:<n>" response line, or -1
 * if there was none.
 *
 * The ESP8266 reports a failed AT+CWJAP this way, ahead of its "FAIL",
 * and the number is the whole diagnosis: 1 = timeout, 2 = wrong
 * password, 3 = access point not found, 4 = connect failed. Without it a
 * failed join is just "not OK".
 */
int16_t at_last_code(void);

/* Advance the state machine. Call frequently from the main loop. */
void at_tick(void);

/* Non-zero when no command is outstanding. */
int at_ready(void);

/* Outcome of the most recent command. */
at_result_t at_result(void);

/*
 * Mirror received bytes to the debug console as they arrive. On by
 * default, because watching the raw module output is how you find out
 * what it is really doing.
 *
 * Payload bytes are never mirrored regardless of this setting: they go
 * to the +IPD sink, which decides for itself what to show. That is what
 * keeps a multi-kilobyte HTTP body from burying the console.
 */
void at_set_echo(int on);

/*
 * Print each received byte as two hex digits instead of as a character.
 *
 * This exists because a terminal cannot show you the difference between
 * 0x00, 0xFF and a stray control byte - they all render as nothing or
 * as a replacement glyph. When a link is misbehaving, hex is the only
 * honest view. Takes precedence over at_set_echo() when both are on.
 */
void at_set_hexdump(int on);

/* Total bytes received from the module since at_init(). */
uint16_t at_rx_bytes(void);

#endif /* AT_H */
