/*
 * http.h — build one GET request, parse one response, buffer nothing.
 *
 * HTTP/1.0 is doing most of the work here. It means the server closes
 * the connection when the response is finished, so "CLOSED" is the
 * end-of-body marker and Content-Length never has to be parsed. That
 * removes the only reason this module would have needed a line buffer
 * for the header block, and with it 64 bytes of RAM.
 *
 * The response is parsed as it arrives and printed within a fixed byte
 * budget. Bytes past that budget are still counted but not shown, so a
 * large response reports its true size while the console stays readable.
 * Nothing is stored: an HTTP body of any size passes through in
 * constant memory.
 *
 * What this deliberately does not do: chunked transfer encoding,
 * keep-alive, redirects, gzip, TLS. One request, one response, one
 * connection, closed at the end.
 */

#ifndef HTTP_H
#define HTTP_H

#include <stdint.h>

/*
 * Console budget for one response, headers and body together.
 *
 * This costs no RAM - it is only a stopping point for printing. Raised
 * from an originally planned 800 because a directory listing of this
 * project is around 1 KB, and seeing all of it is more convincing than
 * seeing most of it.
 */
#define HTTP_PRINT_MAX 1024

/*
 * Largest request we will build. Worst case, with a 31-character name
 * and a 31-character path, is:
 *
 *     GET <31> HTTP/1.0 CRLF  Host: <31> CRLF CRLF   =  89 bytes
 *
 * so 112 leaves room without being wasteful.
 */
#define HTTP_REQ_MAX 112

/*
 * Register the +IPD sink with at.c. Call once, after at_init().
 *
 * Body bytes reach this module through at.c, which owns the framing but
 * knows nothing about HTTP. This function is the join between them.
 */
void http_init(void);

/* Reset the parser for a new response. Call before opening the connection. */
void http_begin(void);

/*
 * Build "GET <path> HTTP/1.0" plus a Host header into `buf`.
 *
 * Returns the number of bytes written, which is the number AT+CIPSEND
 * must be told in advance - the reason this buffer exists at all. The
 * result is always NUL-terminated, but the NUL is not counted.
 */
uint16_t http_build_request(char *buf, uint16_t cap, const char *host, const char *path);

/* Feed one payload byte. Registered as the +IPD sink; not called directly. */
void http_rx_byte(uint8_t c);

/* Non-zero when the peer has closed the connection. */
int http_done(void);

/* The three-digit status code, or 0 if the status line never completed. */
uint16_t http_status(void);

/* Body bytes received (everything after the blank line). */
uint32_t http_body_bytes(void);

/* Bytes of the whole response, headers included. */
uint32_t http_total_bytes(void);

#endif /* HTTP_H */
