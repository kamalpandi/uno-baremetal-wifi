/*
 * http.c — request building, and a response parser with no buffer.
 *
 * The parser has three states and never holds a line:
 *
 *   STATUS    "HTTP/1.0 200 OK" - capture columns 9..11, print the rest
 *   HEADERS   everything up to the first blank line
 *   BODY      count it, print within budget
 *
 * The status code is read by *position* rather than by matching a
 * prefix, which is what makes a line buffer unnecessary:
 *
 *     H T T P / 1 . 0   2 0 0   O K
 *     0 1 2 3 4 5 6 7 8 9 ...     <- columns
 *
 * End of headers is one byte of state: "has anything non-CRLF appeared
 * since the last newline?". When a newline arrives and the answer is no,
 * the blank line has been found and the body starts.
 */

#include <string.h>
#include <avr/pgmspace.h>

#include "http.h"
#include "at.h"
#include "uart0.h"

typedef enum {
    HTTP_STATUS = 0,    /* the "HTTP/1.x NNN ..." line       */
    HTTP_HEADERS,       /* header lines until a blank one    */
    HTTP_BODY,          /* everything after that             */
} http_state_t;

static http_state_t state;
static uint8_t      col;      /* byte offset within the current line */
static uint8_t      empty;    /* current line has no content yet     */
static uint16_t     status;
static uint32_t     body;
static uint32_t     total;

void http_init(void)
{
    /*
     * at.c knows about framing, not about HTTP, so payload has to be
     * handed over rather than interpreted there. Registering the sink is
     * what makes "+IPD,<len>:<bytes>" turn into calls to http_rx_byte().
     */
    at_set_ipd_sink(http_rx_byte);
}

void http_begin(void)
{
    state  = HTTP_STATUS;
    col    = 0;
    empty  = 1;
    status = 0;
    body   = 0;
    total  = 0;
}

/* ---------------------------------------------------------------------- */
/* Request                                                                 */
/* ---------------------------------------------------------------------- */

/* Append a RAM string, never writing past cap-1 plus the terminator. */
static uint16_t append(char *buf, uint16_t cap, uint16_t n, const char *s)
{
    while (*s != '\0' && n < (uint16_t)(cap - 1)) {
        buf[n] = *s;
        n++;
        s++;
    }

    buf[n] = '\0';
    return n;
}

/* The same, for a string held in flash. */
static uint16_t append_P(char *buf, uint16_t cap, uint16_t n, const char *s)
{
    char c;

    while ((c = (char)pgm_read_byte(s)) != '\0' && n < (uint16_t)(cap - 1)) {
        buf[n] = c;
        n++;
        s++;
    }

    buf[n] = '\0';
    return n;
}

uint16_t http_build_request(char *buf, uint16_t cap, const char *host, const char *path)
{
    uint16_t n = 0;

    n = append_P(buf, cap, n, PSTR("GET "));
    n = append  (buf, cap, n, path);
    n = append_P(buf, cap, n, PSTR(" HTTP/1.0\r\n"));
    n = append_P(buf, cap, n, PSTR("Host: "));
    n = append  (buf, cap, n, host);
    n = append_P(buf, cap, n, PSTR("\r\n\r\n"));

    /*
     * No "Connection: close" header: with HTTP/1.0 that is already the
     * default, and the server will close the connection when it is done.
     * That default is what lets the parser treat CLOSED as end-of-body.
     */
    return n;
}

/* ---------------------------------------------------------------------- */
/* Response                                                                */
/* ---------------------------------------------------------------------- */

/* Print one byte if the console budget has not run out, and count it. */
static void emit(uint8_t c)
{
    total++;

    if (total <= HTTP_PRINT_MAX) {
        uart0_putc((char)c);
    }
}

void http_rx_byte(uint8_t c)
{
    if (state == HTTP_BODY) {
        body++;
        emit(c);
        return;
    }

    /*
     * "HTTP/1.0 200 OK" puts the code at fixed columns 9, 10 and 11.
     * Reading it there avoids keeping the status line anywhere.
     */
    if (state == HTTP_STATUS && col >= 9 && col <= 11 && c >= '0' && c <= '9') {
        status = (uint16_t)((status * 10) + (c - '0'));
    }

    emit(c);

    if (c == '\r') {
        return;                 /* the LF that follows does the work */
    }

    if (c == '\n') {
        if (empty) {
            state = HTTP_BODY;  /* blank line: headers are over */
        } else {
            empty = 1;
            col   = 0;
            if (state == HTTP_STATUS) {
                state = HTTP_HEADERS;
            }
        }
        return;
    }

    empty = 0;
    col++;
}

int http_done(void)
{
    /*
     * The server hangs up when it is finished, and at.c latches that as
     * a sticky flag. Nothing else needs to be parsed to know the
     * response is complete.
     */
    return at_link_closed();
}

uint16_t http_status(void)
{
    return status;
}

uint32_t http_body_bytes(void)
{
    return body;
}

uint32_t http_total_bytes(void)
{
    return total;
}
