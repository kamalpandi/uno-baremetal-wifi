/*
 * test_parse.c — host-side tests for the two pieces that cannot be
 * checked by looking at a terminal: the +IPD length framing and the HTTP
 * response parser.
 *
 * Both modules are pure logic - no registers, no timers, no interrupts -
 * so they compile for the PC. That is far faster and more repeatable than
 * flashing and reading a terminal, and it lets awkward cases be typed out
 * deliberately: a body containing a literal "+IPD,", a zero-length block,
 * a 404, a response with no headers at all.
 *
 * Run with `make test`.
 */

#include <stdio.h>
#include <string.h>

#include "at.h"
#include "at_parse.h"
#include "http.h"

/* --------------------------------------------------------------------- */
/* Stubs for the two things http.c borrows from the rest of the firmware. */
/* --------------------------------------------------------------------- */

static char console[4096];
static int  console_len;
static int  closed;

void uart0_putc(char c)
{
    if (console_len < (int)sizeof console - 1) {
        console[console_len++] = c;
    }
}

void at_set_ipd_sink(at_ipd_fn fn)
{
    (void)fn;
}

int at_link_closed(void)
{
    return closed;
}

/* --------------------------------------------------------------------- */
/* Helpers                                                               */
/* --------------------------------------------------------------------- */

static int failures;

static void check(const char *what, int ok)
{
    printf("%-58s %s\n", what, ok ? "PASS" : "FAIL");
    if (!ok) {
        failures++;
    }
}

/*
 * Feed a whole string one byte at a time, exactly as at_tick() would.
 * Returns the payload bytes seen, in `out`.
 */
static int run_ipd(const char *s, char *out, int cap)
{
    int n = 0;

    at_parse_reset();

    while (*s != '\0') {
        uint8_t        c = (uint8_t)*s;
        at_parse_evt_t e = at_parse_byte(c);

        if (e == AT_PARSE_DATA) {
            if (n < cap - 1) {
                out[n++] = (char)c;
            }
        } else if (e == AT_PARSE_LINE) {
            at_parse_consume();
        }
        s++;
    }

    out[n] = '\0';
    return n;
}

/* Feed a string and collect the completed lines. */
static int run_lines(const char *s, char lines[][16], int cap)
{
    int n = 0;

    at_parse_reset();

    while (*s != '\0') {
        uint8_t        c = (uint8_t)*s;
        at_parse_evt_t e = at_parse_byte(c);

        if (e == AT_PARSE_LINE) {
            if (n < cap) {
                snprintf(lines[n], 16, "%s", at_parse_line());
                n++;
            }
            at_parse_consume();
        }
        s++;
    }

    return n;
}

/* Feed a whole HTTP response and reset the counters around it. */
static void run_http(const char *response)
{
    int i;

    console_len = 0;
    console[0]  = '\0';
    closed      = 0;

    http_begin();
    for (i = 0; response[i] != '\0'; i++) {
        http_rx_byte((uint8_t)response[i]);
    }
}

/* --------------------------------------------------------------------- */

int main(void)
{
    char out[256];
    char lines[8][16];
    int  n;

    printf("--- +IPD framing -------------------------------------------------\n");

    n = run_ipd("+IPD,5:hello", out, sizeof out);
    check("plain payload: 5 bytes, no trailing line", n == 5 && strcmp(out, "hello") == 0);

    n = run_ipd("\r\n+IPD,5:hello\r\nCLOSED\r\n", out, sizeof out);
    check("payload framed by CRLF and CLOSED", n == 5 && strcmp(out, "hello") == 0);

    n = run_ipd("+IPD,2:ab+IPD,3:cde", out, sizeof out);
    check("two consecutive blocks, no separator", n == 5 && strcmp(out, "abcde") == 0);

    n = run_ipd("+IPD,10:+IPD,3:xyz", out, sizeof out);
    check("payload containing a literal +IPD,", n == 10 && strcmp(out, "+IPD,3:xyz") == 0);

    n = run_ipd("+IPD,6:\r\nOK\r\n", out, sizeof out);
    check("payload containing CRLF OK CRLF", n == 6 && strcmp(out, "\r\nOK\r\n") == 0);

    n = run_ipd("+IPD,0:", out, sizeof out);
    check("zero-length payload does not enter data mode", n == 0 && !at_parse_in_data());

    n = run_ipd("+IPD,4:abcd", out, sizeof out);
    check("data mode ends exactly after len bytes", n == 4 && !at_parse_in_data());

    printf("\n--- line mode ----------------------------------------------------\n");

    n = run_lines("\r\nOK\r\n> ", lines, 8);
    check("prompt '>' is emitted without a newline", n == 3 && strcmp(lines[2], ">") == 0);

    n = run_lines("\r\nOK\r\n> SEND OK\r\n", lines, 8);
    check("space after '>' does not glue onto next line",
          n == 4 && strcmp(lines[3], "SEND OK") == 0);

    check("'>' classifies as SEND_PROMPT", at_classify(">") == AT_RESP_SEND_PROMPT);
    check("\"SEND OK\" classifies as SEND_OK", at_classify("SEND OK") == AT_RESP_SEND_OK);
    check("\"CLOSED\" classifies as CLOSED", at_classify("CLOSED") == AT_RESP_CLOSED);
    check("\"+CWJAP:2\" is not a final code", at_classify("+CWJAP:2") == AT_RESP_OTHER);

    printf("\n--- HTTP response ------------------------------------------------\n");

    {
        const char *rsp =
            "HTTP/1.0 200 OK\r\n"
            "Server: SimpleHTTP/0.6\r\n"
            "Content-Length: 5\r\n"
            "\r\n"
            "hello";

        run_http(rsp);

        check("status code read by position", http_status() == 200);
        check("body counted, headers excluded", http_body_bytes() == 5);
        check("total counts every response byte", http_total_bytes() == strlen(rsp));
        check("body printed verbatim",
              console_len >= 5 && strcmp(console + console_len - 5, "hello") == 0);
    }

    {
        const char *rsp =
            "HTTP/1.0 404 Not Found\r\n"
            "\r\n"
            "nope";

        run_http(rsp);

        check("404 parsed as 404, not accumulated", http_status() == 404);
        check("empty-header response still finds the body", http_body_bytes() == 4);
    }

    {
        /* A body whose own bytes contain the end-of-header marker. */
        const char *rsp =
            "HTTP/1.0 200 OK\r\n"
            "\r\n"
            "a\r\n\r\nb";

        run_http(rsp);

        check("CRLFCRLF inside the body is just body", http_body_bytes() == 6);
    }

    printf("\n%s (%d failure%s)\n",
           failures ? "FAILED" : "all passed",
           failures, failures == 1 ? "" : "s");

    return failures ? 1 : 0;
}
