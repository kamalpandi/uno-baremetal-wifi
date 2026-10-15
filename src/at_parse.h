/*
 * at_parse.h — turn a raw byte stream into AT response lines.
 *
 * The ESP8266 speaks in lines terminated by CR LF. This module
 * accumulates them one byte at a time and classifies the finished lines,
 * which is all the AT layer needs to know whether a command succeeded.
 *
 * Lines longer than AT_LINE_MAX are truncated rather than allowed to
 * overflow, and at_line_truncated() reports that it happened.
 */

#ifndef AT_PARSE_H
#define AT_PARSE_H

#include <stdint.h>

/* What a response line turned out to be. */
typedef enum {
    AT_RESP_OTHER = 0,          /* anything unrecognised, e.g. data    */
    AT_RESP_OK,                 /* "OK"       - command succeeded      */
    AT_RESP_ERROR,              /* "ERROR"    - command rejected       */
    AT_RESP_FAIL,               /* "FAIL"     - e.g. join failed       */
    AT_RESP_READY,              /* "ready"    - firmware booted        */
    AT_RESP_BUSY,               /* "busy p..." - module is occupied    */
    AT_RESP_WIFI_CONNECTED,     /* unsolicited, during AT+CWJAP        */
    AT_RESP_WIFI_GOT_IP,        /* unsolicited, DHCP finished          */
    AT_RESP_WIFI_DISCONNECT,    /* unsolicited, link dropped           */
    AT_RESP_SEND_PROMPT,        /* ">" - AT+CIPSEND is asking for data */
} at_resp_t;

/* Clear the accumulator and start a fresh line. */
void at_parse_reset(void);

/*
 * Feed one received byte.
 *
 * Returns 1 when that byte completed a line. The line is then available
 * from at_parse_line() until at_parse_consume() is called.
 */
int at_parse_byte(uint8_t c);

/* The most recently completed line (always NUL-terminated, trailing CR
 * already removed). Valid only after at_parse_byte() returned 1. */
const char *at_parse_line(void);

/* Mark the current line as handled and begin the next one. */
void at_parse_consume(void);

/* Non-zero if the last line was too long and got cut off. */
int at_line_truncated(void);

/* Turn a line into a response code. */
at_resp_t at_classify(const char *line);

#endif /* AT_PARSE_H */
