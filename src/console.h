/*
 * console.h — read a line of text from the debug console.
 *
 * Up to now the console has been output-only. Entering an SSID and a
 * password needs the other direction, with enough editing to fix a
 * typo without starting over.
 *
 * picocom reports `emap : crcrlf,delbs`, so the Enter key arrives as a
 * bare CR and Backspace arrives as 0x08. Both Backspace codes are
 * handled anyway, because terminals disagree on this.
 */

#ifndef CONSOLE_H
#define CONSOLE_H

#include <stdint.h>

/*
 * Read one line, blocking until Enter. Characters are echoed as they
 * are typed, Backspace removes the last one, and anything that is not
 * printable ASCII is ignored.
 *
 * Writes at most max_len characters into dst plus a NUL terminator, so
 * dst must have room for max_len + 1 bytes. Returns the length.
 *
 * Blocks. That is fine for an interactive prompt, and is why this is
 * not how the AT layer reads the ESP.
 */
uint8_t console_read_line(char *dst, uint8_t max_len);

#endif /* CONSOLE_H */
