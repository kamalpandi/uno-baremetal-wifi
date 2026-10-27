/*
 * net.c — the join sequence, and one TCP connection.
 *
 * The join is three commands:
 *
 *   AT+CWMODE=1                 station mode (not an access point)
 *   AT+CWJAP="ssid","pass"      associate + DHCP, up to ~15 seconds
 *   AT+CIPSTA?                  report the address we ended up with
 *
 * The middle one is the interesting one. While it runs the module sends
 * "WIFI CONNECTED" and "WIFI GOT IP" as unsolicited lines *before* the
 * final OK. at.c already ignores anything that is not a final result
 * code, so those arrive on the console as progress and change nothing.
 *
 * Phase 6 adds the connection itself, which is the first thing in this
 * project that is not a single request/response:
 *
 *   AT+CIPSEND=<n>   -> '>'          (asking)
 *   <n raw bytes>    -> "SEND OK"    (payload)
 *
 * That is why at.c grew at_wait_t: "OK" is not the answer to either
 * half, and the '>' arrives without the newline the parser normally
 * waits for.
 */

#include <avr/pgmspace.h>

#include "net.h"
#include "at.h"
#include "settings.h"
#include "softuart.h"
#include "uart0.h"

/* Wait for the outstanding command to finish. Returns 1 if it said OK. */
static int wait_for_result(void)
{
    while (!at_ready()) {
        at_tick();
    }

    return at_result() == AT_RESULT_OK;
}

/* Run a flash-string command, printing it first. Returns 1 on OK. */
static int run(const char *cmd_p, uint16_t timeout_ms)
{
    uart0_putsln_P(PSTR(""));
    uart0_puts_P(PSTR("> "));
    uart0_puts_P(cmd_p);
    uart0_putsln_P(PSTR(""));

    at_command_P(cmd_p, timeout_ms);

    return wait_for_result();
}

void net_init(void)
{
    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("[module init]"));

    /*
     * Ask once before changing anything. If the shield is not seated,
     * this is the only place that says so clearly, instead of every
     * later command timing out for an unexplained reason.
     */
    if (!run(PSTR("AT"), 2000)) {
        uart0_putsln_P(PSTR("  no answer - check that the shield is seated"));
        return;
    }

    /*
     * ATE0 stops the module echoing commands back at us from now on.
     * That is the module's echo, not this project's console mirror
     * (at_set_echo); the two are unrelated and both exist.
     *
     * It matters most at AT+CWJAP, where the echoed line contains the
     * WiFi password.
     */
    run(PSTR("ATE0"), 2000);

    /*
     * One connection at a time. This is also the default, but saying it
     * out loud keeps the +IPD format unambiguous: with mux off it is
     * "+IPD,<len>:" with no link id in the middle.
     */
    run(PSTR("AT+CIPMUX=0"), 2000);
}

/*
 * The numeric reason the module gives for a failed join. It is the
 * entire diagnosis, so it is worth turning into words.
 */
static void print_cwjap_reason(int16_t code)
{
    switch (code) {
    case 1:  uart0_puts_P(PSTR("connection timeout"));     break;
    case 2:  uart0_puts_P(PSTR("wrong password"));         break;
    case 3:  uart0_puts_P(PSTR("access point not found")); break;
    case 4:  uart0_puts_P(PSTR("connection failed"));      break;
    default: uart0_puts_P(PSTR("unrecognised reason"));    break;
    }
}

int net_join(void)
{
    const wifi_creds_t *creds = settings_get();

    if (!settings_valid()) {
        uart0_putsln_P(PSTR(""));
        uart0_putsln_P(PSTR("No credentials stored."));
        uart0_putsln_P(PSTR("Press 'c' to enter an SSID and password first."));
        return 0;
    }

    /* 1. Station mode. */
    if (!run(PSTR("AT+CWMODE=1"), 2000)) {
        uart0_putsln_P(PSTR("  could not set station mode"));
        return 0;
    }

    /*
     * 2. Associate. This is the slow one, hence the 20 second budget -
     * timer_expired() is wrap-safe anywhere below 32 seconds, so this
     * is comfortably inside it.
     *
     * The command is assembled from several pieces because the SSID and
     * password live in RAM while the surrounding syntax lives in flash.
     */
    uart0_putsln_P(PSTR(""));
    uart0_puts_P(PSTR("> AT+CWJAP=\""));
    uart0_puts(creds->ssid);
    uart0_putsln_P(PSTR("\",\"********\""));
    uart0_putsln_P(PSTR("  associating and requesting an IP (up to 20s)..."));

    at_command_begin(20000);
    softuart_tx_puts_P(PSTR("AT+CWJAP=\""));
    softuart_tx_puts(creds->ssid);
    softuart_tx_puts_P(PSTR("\",\""));
    softuart_tx_puts(creds->pass);
    softuart_tx_puts_P(PSTR("\""));
    at_command_end();

    if (!wait_for_result()) {
        int16_t code = at_last_code();

        uart0_putsln_P(PSTR(""));
        uart0_puts_P(PSTR("  join failed"));

        if (code >= 0) {
            uart0_puts_P(PSTR(" (code "));
            uart0_put_udec((uint16_t)code);
            uart0_puts_P(PSTR("): "));
            print_cwjap_reason(code);
        } else {
            uart0_puts_P(PSTR(": no reason given - did the module answer?"));
        }

        uart0_putsln_P(PSTR(""));
        return 0;
    }

    /* 3. Report what we actually got. */
    uart0_putsln_P(PSTR("  connected."));
    run(PSTR("AT+CIPSTA?"), 3000);

    return 1;
}

void net_status(void)
{
    run(PSTR("AT+CWMODE?"), 2000);
    run(PSTR("AT+CIPSTA?"), 3000);
}

/* ---------------------------------------------------------------------- */
/* TCP                                                                     */
/* ---------------------------------------------------------------------- */

int net_tcp_open(const char *host, uint16_t port)
{
    /*
     * A previous connection's "CLOSED" may still be latched. Clearing it
     * here, and not in http.c, keeps the ordering right: this is the last
     * moment before a new connection can report anything.
     */
    at_clear_closed();

    uart0_putsln_P(PSTR(""));
    uart0_puts_P(PSTR("> AT+CIPSTART=\"TCP\",\""));
    uart0_puts(host);
    uart0_puts_P(PSTR("\","));
    uart0_put_udec(port);
    uart0_putsln_P(PSTR(""));

    at_command_begin(10000);
    softuart_tx_puts_P(PSTR("AT+CIPSTART=\"TCP\",\""));
    softuart_tx_puts(host);
    softuart_tx_puts_P(PSTR("\","));
    softuart_tx_put_udec(port);
    at_command_end();

    if (!wait_for_result()) {
        uart0_putsln_P(PSTR("  connection failed (refused, unreachable, or no DNS)"));
        return 0;
    }

    return 1;
}

int net_tcp_send(const uint8_t *data, uint16_t len)
{
    uart0_putsln_P(PSTR(""));
    uart0_puts_P(PSTR("> AT+CIPSEND="));
    uart0_put_udec(len);
    uart0_putsln_P(PSTR(""));

    /* --- stage one: ask for permission to send --- */
    at_command_begin(2000);
    at_set_wait(AT_WAIT_PROMPT);   /* must follow begin(), which resets it */
    softuart_tx_puts_P(PSTR("AT+CIPSEND="));
    softuart_tx_put_udec(len);
    at_command_end();

    if (!wait_for_result() || !at_prompt_seen()) {
        uart0_putsln_P(PSTR("  no '>' prompt - the module refused the send"));
        return 0;
    }

    uart0_puts_P(PSTR("  prompt received, sending "));
    uart0_put_udec(len);
    uart0_putsln_P(PSTR(" bytes"));

    /* --- stage two: the payload itself, no trailing CR LF --- */
    at_send_data(data, len, 5000);

    if (!wait_for_result()) {
        uart0_putsln_P(PSTR("  no SEND OK - the payload may not have arrived"));
        return 0;
    }

    uart0_putsln_P(PSTR("  SEND OK"));
    return 1;
}

void net_tcp_close(void)
{
    uart0_putsln_P(PSTR(""));
    uart0_puts_P(PSTR("> AT+CIPCLOSE"));
    uart0_putsln_P(PSTR(""));

    at_command_P(PSTR("AT+CIPCLOSE"), 2000);
    wait_for_result();
}
