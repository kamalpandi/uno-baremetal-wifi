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
 */

#ifndef AT_H
#define AT_H

#include <stdint.h>
#include <avr/pgmspace.h>

typedef enum {
    AT_RESULT_NONE = 0,     /* no command has been run yet        */
    AT_RESULT_IN_PROGRESS,  /* queued or waiting for the response */
    AT_RESULT_OK,           /* saw "OK"                           */
    AT_RESULT_ERROR,        /* saw "ERROR" or "FAIL"              */
    AT_RESULT_TIMEOUT,      /* nothing definitive within the time */
} at_result_t;

void at_init(void);

/*
 * Queue a command and begin waiting for its response. The CR LF that
 * every AT command needs is appended for you; do not include it.
 *
 * `timeout_ms` is measured from the moment the last byte leaves the
 * wire, not from this call, so a slow baud rate cannot eat the budget.
 * Only valid when at_ready() is true.
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
