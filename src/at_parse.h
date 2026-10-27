/*
 * at_parse.h — turn a raw byte stream into AT lines and +IPD payload.
 *
 * The ESP8266 speaks in lines terminated by CR LF. This module
 * accumulates them one byte at a time and classifies the finished lines,
 * which is all the AT layer needs to know whether a command succeeded.
 *
 * Phase 6 breaks that tidy picture. When a TCP connection receives data,
 * the module pushes it as:
 *
 *     +IPD,<len>:<exactly len raw bytes>
 *
 * There is no terminator: the length is the only boundary. So the parser
 * has two modes, and the interesting part is that the switch has to
 * happen *inside* the byte stream, not between reads. A header split
 * across two ring-buffer reads is therefore harmless, which is one of the
 * reasons this is fed one byte at a time rather than in blocks.
 *
 * Lines longer than AT_LINE_MAX are truncated rather than allowed to
 * overflow, and at_line_truncated() reports that it happened. The same
 * buffer holds a +IPD header, which is at most "+IPD,4294967295:" - far
 * shorter than the limit in practice.
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
    AT_RESP_SEND_OK,            /* "SEND OK" - payload accepted        */
    AT_RESP_CLOSED,             /* "CLOSED" - the peer hung up         */
} at_resp_t;

/*
 * What one call to at_parse_byte() produced.
 *
 * AT_PARSE_DATA means "the byte you just handed me was payload". There is
 * deliberately no accessor to fetch it: the caller already has it, so the
 * data path costs no copy and no buffer.
 */
typedef enum {
    AT_PARSE_NONE = 0,          /* absorbed as part of a line          */
    AT_PARSE_LINE,              /* a line is ready via at_parse_line() */
    AT_PARSE_DATA,              /* that byte was +IPD payload          */
} at_parse_evt_t;

/* Clear the accumulator and start a fresh line. */
void at_parse_reset(void);

/*
 * Feed one received byte.
 *
 * Returns AT_PARSE_LINE when that byte completed a line. The line is then
 * available from at_parse_line() until at_parse_consume() is called.
 * Returns AT_PARSE_DATA when the byte was part of a +IPD payload.
 */
at_parse_evt_t at_parse_byte(uint8_t c);

/* The most recently completed line (always NUL-terminated, trailing CR
 * already removed). Valid only after at_parse_byte() returned
 * AT_PARSE_LINE. */
const char *at_parse_line(void);

/* Mark the current line as handled and begin the next one. */
void at_parse_consume(void);

/* Non-zero if the last line was too long and got cut off. */
int at_line_truncated(void);

/* Non-zero while a +IPD payload is being consumed. */
int at_parse_in_data(void);

/* Payload bytes still owed by the current +IPD header. */
uint16_t at_parse_data_left(void);

/* Turn a line into a response code. */
at_resp_t at_classify(const char *line);

#endif /* AT_PARSE_H */
