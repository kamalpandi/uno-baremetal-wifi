/*
 * main.c — Phase 6: an HTTP GET over a real TCP connection.
 *
 * CONTROLS (type into picocom; it has no local echo)
 *   c              enter WiFi credentials and save them to EEPROM
 *   u              show the stored credentials
 *   j              join the network
 *   n              network status (mode and IP address)
 *   g              fetch a page from a LAN server (TCP + HTTP)
 *   i              re-run module init (ATE0, single-connection mode)
 *   h              toggle hex dump of received bytes
 *   s              receiver counters
 *   f              flush the receive queue
 *   t              loopback self-test (needs D2 jumpered to D3)
 *   any other key  send AT
 *
 * The fetch is the point of the phase. It is blocking and watchable on
 * purpose: the AT handshake prints as it goes, then the response body
 * scrolls past until either the server closes the connection or the
 * budget of 1024 bytes runs out. Press 'q' to abandon a slow transfer.
 */

#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>

#include "at.h"
#include "console.h"
#include "http.h"
#include "net.h"
#include "settings.h"
#include "softuart.h"
#include "timer.h"
#include "uart0.h"

static uint8_t hex_mode;

static void set_credentials(void);
static void show_credentials(void);
static void fetch_demo(void);
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
    http_init();           /* registers the +IPD sink   */

    sei();                 /* the soft UART lives on interrupts */

    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("uno-baremetal-wifi"));
    uart0_putsln_P(PSTR("Phase 6: HTTP GET over TCP"));
    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("  c  set WiFi credentials       j  join network"));
    uart0_putsln_P(PSTR("  u  show stored credentials    n  network status"));
    uart0_putsln_P(PSTR("  g  fetch a page (HTTP)        i  re-init module"));
    uart0_putsln_P(PSTR("  h  hex dump   s  counters   f  flush   t  loopback"));
    uart0_putsln_P(PSTR("  any other key  send AT"));
    uart0_putsln_P(PSTR(""));

    if (settings_valid()) {
        uart0_puts_P(PSTR("Stored network: "));
        uart0_puts(settings_get()->ssid);
        uart0_putsln_P(PSTR(""));
    } else {
        uart0_putsln_P(PSTR("No WiFi credentials stored yet - press 'c'."));
    }

    /*
     * Silence the module's command echo before anything else. It is not
     * required - everything still works with echo on - but from here on
     * the console shows one copy of each command instead of two, and a
     * CWJAP line never contains the password.
     */
    net_init();

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
/* The fetch                                                               */
/* ---------------------------------------------------------------------- */

/* Parse a decimal field. Returns 0 for an empty or non-numeric string. */
static uint16_t parse_dec(const char *s)
{
    uint16_t value = 0;

    while (*s >= '0' && *s <= '9') {
        value = (uint16_t)((value * 10) + (*s - '0'));
        s++;
    }

    return value;
}

static void fetch_demo(void)
{
    /*
     * All three live in .bss rather than on the stack: the request buffer
     * is 112 bytes and the stack on this part is only a couple of hundred
     * deep at the best of times.
     */
    static char host[32];
    static char path[32];
    static char request[HTTP_REQ_MAX];

    char     port_text[6];
    uint16_t port = 8000;
    uint16_t len;
    uint16_t started;
    uint16_t quiet_at;
    uint16_t seen;
    uint8_t  finished;

    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("=== fetch a page over TCP ==="));
    uart0_putsln_P(PSTR("(server example: python3 -m http.server 8000)"));
    uart0_putsln_P(PSTR(""));

    uart0_puts_P(PSTR("Server IP or name: "));
    if (console_read_line(host, (uint8_t)(sizeof host - 1)) == 0) {
        uart0_putsln_P(PSTR("cancelled."));
        uart0_putsln_P(PSTR(""));
        return;
    }

    uart0_puts_P(PSTR("Port [8000]: "));
    if (console_read_line(port_text, (uint8_t)(sizeof port_text - 1)) > 0) {
        port = parse_dec(port_text);
        if (port == 0) {
            port = 8000;
        }
    }

    uart0_puts_P(PSTR("Path [/]: "));
    if (console_read_line(path, (uint8_t)(sizeof path - 1)) == 0) {
        path[0] = '/';
        path[1] = '\0';
    }

    /*
     * The request has to exist in memory before it is sent: AT+CIPSEND
     * demands the byte count up front, so there is no way to stream a
     * request whose length is not yet known.
     */
    len = http_build_request(request, (uint16_t)sizeof request, host, path);

    uart0_putsln_P(PSTR(""));
    uart0_puts_P(PSTR("=== GET "));
    uart0_puts(path);
    uart0_puts_P(PSTR(" from "));
    uart0_puts(host);
    uart0_puts_P(PSTR(":"));
    uart0_put_udec(port);
    uart0_putsln_P(PSTR(" ==="));

    /*
     * Turn the byte mirror off for the duration. The +IPD payload is
     * printed by the HTTP layer within its budget, so mirroring it as
     * well would show every byte twice and let a large body bury the
     * console. Press 'h' before a fetch to see the raw framing instead.
     */
    at_set_echo(0);
    http_begin();

    if (!net_tcp_open(host, port)) {
        at_set_echo(1);
        return;
    }

    if (!net_tcp_send((const uint8_t *)request, len)) {
        net_tcp_close();
        at_set_echo(1);
        return;
    }

    uart0_putsln_P(PSTR(""));
    uart0_putsln_P(PSTR("--- response ---"));

    /*
     * Drain until the peer hangs up. Two clocks run at once: a total
     * budget so a chatty server cannot hold us forever, and a shorter
     * idle budget so a silently stalled connection is noticed rather
     * than waited out. Both stay below 32.7 s, the range in which
     * timer_expired() is wrap-safe.
     */
    started  = millis();
    quiet_at = started;
    seen     = at_rx_bytes();

    while (!http_done()) {
        at_tick();

        if (at_rx_bytes() != seen) {
            seen     = at_rx_bytes();
            quiet_at = millis();
        }

        if (timer_expired(quiet_at, 4000)) {
            break;
        }

        if (timer_expired(started, 20000)) {
            break;
        }

        /* 'q' abandons a transfer without needing a reset. */
        if (uart0_has_data()) {
            if (uart0_getc() == 'q') {
                break;
            }
        }
    }

    finished = (uint8_t)http_done();

    net_tcp_close();
    at_set_echo(1);

    uart0_putsln_P(PSTR(""));
    uart0_puts_P(PSTR("--- HTTP "));
    uart0_put_udec(http_status());
    uart0_puts_P(PSTR(", body "));
    uart0_put_udec32(http_body_bytes());
    uart0_puts_P(PSTR(" bytes, response "));
    uart0_put_udec32(http_total_bytes());
    uart0_putsln_P(PSTR(" bytes ---"));

    if (http_total_bytes() > HTTP_PRINT_MAX) {
        uart0_puts_P(PSTR("--- printed the first "));
        uart0_put_udec((uint16_t)HTTP_PRINT_MAX);
        uart0_putsln_P(PSTR(" bytes only ---"));
    }

    if (!finished) {
        uart0_putsln_P(PSTR("--- connection did not close: transfer stopped early ---"));
    }

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

    case 'g':
        fetch_demo();
        return;

    case 'i':
        net_init();
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
