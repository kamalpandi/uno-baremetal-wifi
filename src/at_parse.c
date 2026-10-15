/*
 * at_parse.c — line accumulation and response classification.
 *
 * There is exactly one line buffer, owned by this file, and callers work
 * on it in place through at_parse_line(). That avoids a second copy and
 * the RAM it would cost on a chip with 2 KB of it.
 */

#include <string.h>
#include <avr/pgmspace.h>

#include "at_parse.h"

/*
 * 64 bytes. Real AT response lines are short ("+CWJAP:...", version
 * strings); the long ones live in HTTP bodies, which Phase 6 will
 * stream instead of buffering. Callers that care can check
 * at_line_truncated().
 */
#define AT_LINE_MAX 64

static char    line[AT_LINE_MAX];
static uint8_t len;
static uint8_t truncated;

void at_parse_reset(void)
{
    len = 0;
    truncated = 0;
    line[0] = '\0';
}

int at_parse_byte(uint8_t c)
{
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
        return 1;
    }

    if (len < AT_LINE_MAX - 1) {
        line[len] = (char)c;
        len++;
    } else {
        truncated = 1;
    }

    return 0;
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

    return AT_RESP_OTHER;
}
