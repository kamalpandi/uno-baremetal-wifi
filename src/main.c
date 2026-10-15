/*
 * main.c — Phase 5: credentials in EEPROM, and joining a network.
 *
 * CONTROLS (type into picocom; it has no local echo)
 *   any other key  send AT
 *   c              enter WiFi credentials and save them to EEPROM
 *   u              show the stored credentials
 *   j              join the network
 *   n              network status (mode and IP address)
 *   h              toggle hex dump of received bytes
 *   s              receiver counters
 *   f              flush the receive queue
 *   t              loopback self-test (needs D2 jumpered to D3)
 */

#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include "at.h"
#include "console.h"
#include "net.h"
#include "settings.h"
#include "softuart.h"
#include "timer.h"
#include "uart0.h"

static uint8_t hex_mode;

static void set_credentials(void);
static void show_credentials(void);
static void report_stats(void);
static void loopback_test(void);
static void run(const char *cmd_p, uint16_t timeout_ms);
static void handle_key(char c);

int main(void)
{
    timer_init();

    uart0_init();          /* debug console, D0/D1, 115200 */
    softuart_tx_init();    /* sets up Timer1            */
    softuart_rx_init();    /* needs Timer1 - call second */
    at_init();

    sei();                 /* the soft UART lives on interrupts */

    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("uno-baremetal-wifi"));
    uart0_putsln_P(PSTR("Phase 5: EEPROM credentials + network join"));
    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("  c  set WiFi credentials       j  join network"));
    uart0_putsln_P(PSTR("  u  show stored credentials    n  network status"));
    uart0_putsln_P(PSTR("  h  hex dump      s  counters   f  flush   t  loopback"));
    uart0_putsln_P(PSTR("  any other key  send AT"));
    uart0_putsln_P(PSTR(""));

    if (settings_valid()) {
        uart0_puts_P(PSTR("Stored network: "));
        uart0_puts(settings_get()->ssid);
        uart0_putsln_P(PSTR(""));
    } else {
        uart0_putsln_P(PSTR("No WiFi credentials stored yet - press 'c'."));
    }
    uart0_putsln_P(PSTR(""));

    for (;;) {
        at_tick();

        if (uart0_has_data()) {
            handle_key(uart0_getc());
        }
    }

    return 0; /* unreachable */
}

/* ---------------------------------------------------------------------- */
/* Credential entry                                                        */
/* ---------------------------------------------------------------------- */

static void set_credentials(void)
{
    wifi_creds_t fresh;

    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("=== WiFi credentials ==="));
    uart0_puts_P(PSTR("SSID (up to 32 chars, Enter now to cancel): "));

    if (console_read_line(fresh.ssid, WIFI_SSID_MAX) == 0) {
        uart0_putsln_P(PSTR("cancelled, nothing changed."));
        uart0_putsln_P(PSTR(""));
        return;
    }

    uart0_puts_P(PSTR("Password (up to 63 chars): "));
    console_read_line(fresh.pass, WIFI_PASS_MAX);

    uart0_putsln_P(PSTR(""));
    uart0_puts_P(PSTR("Writing to EEPROM... "));

    /*
     * ~3.4 ms per byte that actually changes, so this is a visible
     * pause on a first save. Interrupts stay enabled throughout, so
     * only the console stops responding, not the radio.
     */
    settings_save(&fresh);

    uart0_putsln_P(PSTR("saved."));
    uart0_putsln_P(PSTR("Press 'j' to join."));
    uart0_putsln_P(PSTR(""));
}

static void show_credentials(void)
{
    const wifi_creds_t *creds = settings_get();

    uart0_putsln_P(PSTR(""));

    if (!settings_valid()) {
        uart0_putsln_P(PSTR("No credentials stored. Press 'c'."));
        uart0_putsln_P(PSTR(""));
        return;
    }

    uart0_puts_P(PSTR("SSID     : "));
    uart0_puts(creds->ssid);
    uart0_putsln_P(PSTR(""));

    uart0_puts_P(PSTR("Password : "));
    uart0_puts(creds->pass);
    uart0_putsln_P(PSTR(""));

    uart0_puts_P(PSTR("Lengths  : SSID "));
    uart0_put_udec((uint16_t)strlen(creds->ssid));
    uart0_puts_P(PSTR(", password "));
    uart0_put_udec((uint16_t)strlen(creds->pass));
    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR(""));
}

/* ---------------------------------------------------------------------- */
/* Key handling                                                            */
/* ---------------------------------------------------------------------- */

static void handle_key(char c)
{
    switch (c) {
    case 'c':
        set_credentials();
        return;

    case 'u':
        show_credentials();
        return;

    case 'j':
        net_join();
        return;

    case 'n':
        net_status();
        return;

    case 'h':
        hex_mode = (uint8_t)(hex_mode ? 0 : 1);
        at_set_hexdump(hex_mode);
        uart0_putsln_P(PSTR(""));
        uart0_puts_P(PSTR("[hex dump "));
        uart0_puts_P(hex_mode ? PSTR("ON") : PSTR("OFF"));
        uart0_putsln_P(PSTR("]"));
        uart0_putsln_P(PSTR(""));
        return;

    case 's':
        report_stats();
        return;

    case 'f':
        softuart_rx_flush();
        uart0_putsln_P(PSTR(""));
        uart0_putsln_P(PSTR("[receive queue flushed]"));
        uart0_putsln_P(PSTR(""));
        return;

    case 't':
        loopback_test();
        return;

    default:
        /* Anything else is a cue to try the link again. */
        run(PSTR("AT"), 2000);
        return;
    }
}

/* ---------------------------------------------------------------------- */
/* Diagnostics                                                             */
/* ---------------------------------------------------------------------- */

/*
 * Transmit a known string and decode it back through our own receiver.
 *
 * This is the test that would have caught the truncated start bit
 * immediately. An activity LED only proves the pin moves; it cannot tell
 * a 5 microsecond start bit from a 104 microsecond one. A round trip
 * through the receiver cannot be fooled that way.
 *
 * Requires a jumper between D2 and D3, with the shield removed (or its
 * clips pulled) so the ESP8266 is not also driving those pins.
 */
static void loopback_test(void)
{
    static const char pattern[] PROGMEM = "AT\r\n";

    uint8_t  got[16];
    uint8_t  count = 0;
    uint8_t  i;
    uint8_t  ok;
    uint16_t t0;

    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("[loopback self-test]"));
    uart0_putsln_P(PSTR("needs a jumper from D2 to D3; pull the shield"));
    uart0_putsln_P(PSTR(""));

    softuart_rx_flush();
    softuart_set_loopback(1);

    softuart_tx_puts_P(pattern);

    /* Wait for every byte to physically leave the pin. */
    while (!softuart_tx_idle()) {
    }

    /*
     * The final frame is still being sampled. Interrupts are enabled, so
     * the receiver keeps working during this pause.
     */
    t0 = millis();
    while (!timer_expired(t0, 10)) {
    }

    softuart_set_loopback(0);

    while (softuart_rx_has_data() && count < (uint8_t)sizeof got) {
        got[count] = softuart_rx_getc();
        count++;
    }

    uart0_puts_P(PSTR("sent     : "));
    for (i = 0; i < (uint8_t)(sizeof pattern - 1); i++) {
        uart0_put_hex8((uint8_t)pgm_read_byte(&pattern[i]));
        uart0_putc(' ');
    }
    uart0_putsln_P(PSTR(""));

    uart0_puts_P(PSTR("received : "));
    for (i = 0; i < count; i++) {
        uart0_put_hex8(got[i]);
        uart0_putc(' ');
    }
    uart0_putsln_P(PSTR(""));

    ok = 1;
    if (count != (uint8_t)(sizeof pattern - 1)) {
        ok = 0;
    }
    for (i = 0; i < count; i++) {
        if (got[i] != (uint8_t)pgm_read_byte(&pattern[i])) {
            ok = 0;
        }
    }

    uart0_puts_P(PSTR("result   : "));
    uart0_putsln_P(ok ? PSTR("PASS") : PSTR("FAIL"));
    uart0_putsln_P(PSTR(""));
}

static void report_stats(void)
{
    uart0_putsln_P(PSTR(""));
    uart0_puts_P(PSTR("frames accepted : "));
    uart0_put_udec(softuart_rx_frames());
    uart0_putsln_P(PSTR(""));

    uart0_puts_P(PSTR("frames rejected : "));
    uart0_put_udec(softuart_rx_errors());
    uart0_putsln_P(PSTR(""));

    uart0_puts_P(PSTR("bytes consumed  : "));
    uart0_put_udec(at_rx_bytes());
    uart0_putsln_P(PSTR(""));

    uart0_puts_P(PSTR("queue overflowed: "));
    uart0_puts_P(softuart_rx_overflowed() ? PSTR("yes") : PSTR("no"));
    uart0_putsln_P(PSTR(""));

    uart0_puts_P(PSTR("still queued    : "));
    uart0_put_udec(softuart_rx_count());
    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR(""));
}

static void run(const char *cmd_p, uint16_t timeout_ms)
{
    uart0_putsln_P(PSTR(""));
    uart0_puts_P(PSTR("> "));
    uart0_puts_P(cmd_p);
    uart0_putsln_P(PSTR(""));

    at_command_P(cmd_p, timeout_ms);

    while (!at_ready()) {
        at_tick();
    }

    switch (at_result()) {
    case AT_RESULT_OK:
        uart0_putsln_P(PSTR("  [OK]"));
        break;

    case AT_RESULT_ERROR:
        uart0_putsln_P(PSTR("  [ERROR]"));
        break;

    case AT_RESULT_TIMEOUT:
        uart0_putsln_P(PSTR("  [TIMEOUT - no reply]"));
        break;

    default:
        uart0_putsln_P(PSTR("  [?]"));
        break;
    }
}
