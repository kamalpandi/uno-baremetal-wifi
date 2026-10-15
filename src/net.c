/*
 * net.c — the join sequence.
 *
 * Three commands:
 *
 *   AT+CWMODE=1                 station mode (not an access point)
 *   AT+CWJAP="ssid","pass"      associate + DHCP, up to ~15 seconds
 *   AT+CIPSTA?                  report the address we ended up with
 *
 * The middle one is the interesting one. While it runs the module sends
 * "WIFI CONNECTED" and "WIFI GOT IP" as unsolicited lines *before* the
 * final OK. at.c already ignores anything that is not a final result
 * code, so those arrive on the console as progress and change nothing.
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
