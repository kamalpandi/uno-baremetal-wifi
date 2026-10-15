/*
 * eeprom.h — the ATmega328P's 1024 bytes of on-chip EEPROM.
 *
 * EEPROM keeps its contents across power cycles and chip resets, which
 * is what makes it the right place for WiFi credentials.
 *
 * The hardware is fussy in two ways worth knowing before you use it:
 *
 *   1. A write takes about 3.4 milliseconds, during which EEPE stays
 *      set and the control register must not be touched. So writes are
 *      slow and blocking.
 *   2. Each cell survives roughly 100,000 writes. This driver therefore
 *      skips any write whose value is already correct, which is what
 *      turns "save on every boot" from a lifespan problem into a no-op.
 */

#ifndef EEPROM_H
#define EEPROM_H

#include <stdint.h>

/* Read one byte. Addresses 0..1023 on this chip. */
uint8_t eeprom_read(uint16_t addr);

/* Write one byte, doing nothing if it already holds that value. */
void eeprom_write(uint16_t addr, uint8_t value);

void eeprom_read_block(uint16_t addr, void *dst, uint16_t len);
void eeprom_write_block(uint16_t addr, const void *src, uint16_t len);

#endif /* EEPROM_H */
