#ifndef FAKE_AVR_PGMSPACE_H
#define FAKE_AVR_PGMSPACE_H

/*
 * Host-build stand-in for <avr/pgmspace.h>.
 *
 * On the AVR, flash and RAM are separate address spaces, so PROGMEM
 * strings need LPM-based accessors. On a PC there is no split, so every
 * one of them collapses to the ordinary libc call. That is what lets
 * at_parse.c and http.c - which are pure logic - be compiled with gcc and
 * tested directly instead of flashed and read off a terminal.
 */

#include <string.h>

#define PROGMEM
#define PSTR(s) (s)
#define pgm_read_byte(p) (*(const unsigned char *)(p))

#define strcmp_P  strcmp
#define strncmp_P strncmp

#endif /* FAKE_AVR_PGMSPACE_H */
