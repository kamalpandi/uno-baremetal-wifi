/*
 * eeprom.c — register-level EEPROM access.
 *
 * Registers (all in <avr/io.h>):
 *
 *   EEARH / EEARL   the address, 10 bits on this chip
 *   EEDR            the data byte
 *   EECR            control:
 *                     EEPE  (bit 1)  write enable - set to start a write
 *                     EEMPE (bit 2)  master write enable
 *                     EERE  (bit 0)  read enable
 *
 * The one genuinely fiddly part is the write handshake. EEMPE clears
 * itself again four CPU cycles after it is set, and EEPE must be set
 * *within* that window or the write never starts. Four cycles is far too
 * short to risk an interrupt landing in the middle, so those two
 * instructions run with interrupts masked.
 */

#include <avr/io.h>
#include <avr/interrupt.h>

#include "eeprom.h"

uint8_t eeprom_read(uint16_t addr)
{
    /* If a write is in progress, EEAR and EEDR are in use. Wait it out. */
    while (EECR & _BV(EEPE)) {
    }

    EEAR = addr;
    EECR |= _BV(EERE);

    /* The data appears in EEDR on the following cycle. Reading the
     * variable is enough to wait for it, because a load from EEDR
     * cannot complete any earlier. */
    return EEDR;
}

void eeprom_write(uint16_t addr, uint8_t value)
{
    /* EEPROM cells are rated for ~100,000 writes. Skipping writes that
     * change nothing is what lets us save freely. */
    if (eeprom_read(addr) == value) {
        return;
    }

    /* eeprom_read() above already waited for any write in progress. */
    EEAR = addr;
    EEDR = value;

    /*
     * The EEMPE/EEPE handshake. Interrupts must be off, and the two
     * writes must follow each other immediately.
     *
     * SREG is saved and restored rather than calling sei() afterwards,
     * so this stays correct if it is ever called with interrupts
     * already disabled.
     */
    uint8_t sreg = SREG;

    cli();
    EECR |= _BV(EEMPE);
    EECR |= _BV(EEPE);
    SREG = sreg;

    /*
     * The write now runs on its own for about 3.4 ms with EEPE held
     * high. Nothing may modify EECR until it clears - in particular
     * another write or a read, which is why both start by checking it.
     */
    while (EECR & _BV(EEPE)) {
    }
}

void eeprom_read_block(uint16_t addr, void *dst, uint16_t len)
{
    uint8_t *out = (uint8_t *)dst;

    for (uint16_t i = 0; i < len; i++) {
        out[i] = eeprom_read((uint16_t)(addr + i));
    }
}

void eeprom_write_block(uint16_t addr, const void *src, uint16_t len)
{
    const uint8_t *in = (const uint8_t *)src;

    for (uint16_t i = 0; i < len; i++) {
        eeprom_write((uint16_t)(addr + i), in[i]);
    }
}
