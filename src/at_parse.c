/*
 * at_parse.c — line accumulation, response classification, +IPD framing.
 *
 * There is exactly one line buffer, owned by this file, and callers work
 * on it in place through at_parse_line(). That avoids a second copy and
 * the RAM it would cost on a chip with 2 KB of it.
 *
 * The payload path never touches that buffer at all. Once a +IPD header
 * says how many bytes are coming, each one is handed straight back to the
 * caller of at_parse_byte(). Nothing is stored, so an HTTP response of
 * any size passes through in constant memory.
 */

#include <string.h>
#include <avr/pgmspace.h>

#include "at_parse.h"

/*
 * 64 bytes. Real AT response lines are short ("+CWJAP:...", version
 * strings) and so is a +IPD header; the long lines live in HTTP bodies,
 * which never enter this buffer. Callers that care can check
 * at_line_truncated().
 */
#define AT_LINE_MAX 64

/* Prefix that introduces a payload block. */
#define IPD_PREFIX "+IPD,"

static char     line[AT_LINE_MAX];
static uint8_t  len;
static uint8_t  truncated;

/*
 * Payload bytes still owed. Non-zero means "data mode": every incoming
 * byte belongs to the body, no matter what it looks like.
 */
static uint16_t ipd_left;

void at_parse_reset(void)
{
    len = 0;
    truncated = 0;
    ipd_left = 0;
    line[0] = '\0';
}

int at_parse_in_data(void)
{
    return ipd_left != 0;
}

uint16_t at_parse_data_left(void)
{
    return ipd_left;
}

/*
 * Read a decimal number out of the accumulated header.
 *
 * The digits start at offset 5 ("+IPD," is five characters) and stop at
 * the colon we just appended, which fails the digit test below.
 */
static uint16_t ipd_length(void)
{
    uint16_t value = 0;
    uint8_t  i;

    for (i = (uint8_t)(sizeof IPD_PREFIX - 1); i < len; i++) {
        if (line[i] < '0' || line[i] > '9') {
            break;
        }
        value = (uint16_t)((value * 10) + (line[i] - '0'));
    }

    return value;
}

at_parse_evt_t at_parse_byte(uint8_t c)
{
    /*
     * Data mode comes first and takes priority over everything else.
     * While the module owes us N bytes, none of them may be interpreted -
     * including bytes that happen to look like newlines or like "+IPD,".
     * That is exactly what protects a body served from this project's
     * own source.
     */
    if (ipd_left > 0) {
        ipd_left--;
        return AT_PARSE_DATA;
    }

    /*
     * The '>' prompt is the one thing the module sends that is not
     * followed by a newline, so waiting for '\n' would hang forever and
     * AT+CIPSEND would always time out.
     */
    if (c == '>' && len == 0) {
        line[0] = '>';
        line[1] = '\0';
        return AT_PARSE_LINE;
    }

    /*
     * A space at the very start of a line is never meaningful, and some
     * firmware versions send the prompt as "> " with a trailing space.
     * Swallowing it keeps that space from being glued onto the front of
     * the next line, which would stop "SEND OK" from matching.
     */
    if (c == ' ' && len == 0) {
        return AT_PARSE_NONE;
    }

    if (c == '\n') {
        /*
         * Strip trailing CR. The ESP terminates lines with CR LF, but
         * echoes and boot messages are inconsistent about it, so trim
         * whatever is there rather than assuming exactly one CR.
         */
        while (len > 0 && line[len - 1] == '\r') {
            len--;
        }

        line[len] = '\0';
        return AT_PARSE_LINE;
    }

    if (len < AT_LINE_MAX - 1) {
        line[len] = (char)c;
        len++;
    } else {
        truncated = 1;
    }

    /*
     * The colon closes a "+IPD,<len>:" header. Checking the prefix as
     * well matters because plenty of ordinary lines contain a colon
     * ("+CIPSTA:ip:...", "Host: ...").
     */
    if (c == ':' && strncmp(line, IPD_PREFIX, sizeof IPD_PREFIX - 1) == 0) {
        ipd_left = ipd_length();

        /* The header is consumed here, so the caller never sees it as a
         * line and the next byte starts a fresh one. */
        len = 0;
        truncated = 0;
        line[0] = '\0';
    }

    return AT_PARSE_NONE;
}

const char *at_parse_line(void)
{
    return line;
}

void at_parse_consume(void)
{
    len = 0;
    truncated = 0;
    line[0] = '\0';
}

int at_line_truncated(void)
{
    return truncated != 0;
}

/*
 * Compare the line against a literal held in flash.
 *
 * strcmp_P is avr-libc's PROGMEM-aware strcmp: it reads the flash
 * operand with LPM instead of a normal load, which is required because
 * a plain pointer dereference can only ever see RAM on this chip.
 */
at_resp_t at_classify(const char *s)
{
    if (strcmp_P(s, PSTR("OK")) == 0)              return AT_RESP_OK;
    if (strcmp_P(s, PSTR("ERROR")) == 0)           return AT_RESP_ERROR;
    if (strcmp_P(s, PSTR("FAIL")) == 0)            return AT_RESP_FAIL;
    if (strcmp_P(s, PSTR("ready")) == 0)           return AT_RESP_READY;
    if (strcmp_P(s, PSTR("busy p...")) == 0)       return AT_RESP_BUSY;
    if (strcmp_P(s, PSTR("WIFI CONNECTED")) == 0)  return AT_RESP_WIFI_CONNECTED;
    if (strcmp_P(s, PSTR("WIFI GOT IP")) == 0)     return AT_RESP_WIFI_GOT_IP;
    if (strcmp_P(s, PSTR("WIFI DISCONNECT")) == 0) return AT_RESP_WIFI_DISCONNECT;
    if (strcmp_P(s, PSTR(">")) == 0)               return AT_RESP_SEND_PROMPT;
    if (strcmp_P(s, PSTR("SEND OK")) == 0)         return AT_RESP_SEND_OK;
    if (strcmp_P(s, PSTR("CLOSED")) == 0)          return AT_RESP_CLOSED;

    return AT_RESP_OTHER;
}
