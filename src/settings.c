/*
 * settings.c — EEPROM layout and access for the WiFi credentials.
 *
 * Layout (100 bytes of the 1024 available):
 *
 *   offset  size  contents
 *   ------  ----  --------------------------------------------------
 *     0      1    magic       - 0xA5 means "this was written"
 *     1      1    version     - lets a future layout change be detected
 *     2      1    ssid length
 *     3      1    pass length
 *     4     32    ssid bytes
 *    36     64    pass bytes
 *
 * The magic is written LAST during a save. If power is lost part-way
 * through, the magic is missing or stale and the whole record reads as
 * empty - which is much better than a half-written SSID that looks
 * real.
 */

#include <string.h>

#include "settings.h"
#include "eeprom.h"

#define SETTINGS_MAGIC   0xA5
#define SETTINGS_VERSION 1

#define EE_MAGIC    0
#define EE_VERSION  1
#define EE_SSIDLEN  2
#define EE_PASSLEN  3
#define EE_SSID     4
#define EE_PASS     (EE_SSID + WIFI_SSID_MAX)

static wifi_creds_t creds;
static uint8_t      loaded;

void settings_save(const wifi_creds_t *in)
{
    uint8_t ssid_len = (uint8_t)strlen(in->ssid);
    uint8_t pass_len = (uint8_t)strlen(in->pass);

    if (ssid_len > WIFI_SSID_MAX) {
        ssid_len = WIFI_SSID_MAX;
    }
    if (pass_len > WIFI_PASS_MAX) {
        pass_len = WIFI_PASS_MAX;
    }

    eeprom_write_block(EE_SSID, in->ssid, ssid_len);
    eeprom_write_block(EE_PASS, in->pass, pass_len);

    eeprom_write(EE_SSIDLEN, ssid_len);
    eeprom_write(EE_PASSLEN, pass_len);
    eeprom_write(EE_VERSION, SETTINGS_VERSION);
    eeprom_write(EE_MAGIC, SETTINGS_MAGIC);   /* commit marker, last */

    /* Keep the cached copy in step so the next join uses what we saved. */
    creds  = *in;
    loaded = 1;
}

void settings_erase(void)
{
    /* Removing the magic is enough to invalidate the whole record,
     * and is one EEPROM cell instead of a hundred. */
    eeprom_write(EE_MAGIC, 0x00);

    creds.ssid[0] = '\0';
    creds.pass[0] = '\0';
    loaded        = 1;
}

const wifi_creds_t *settings_get(void)
{
    if (loaded) {
        return &creds;
    }

    creds.ssid[0] = '\0';
    creds.pass[0] = '\0';

    if (eeprom_read(EE_MAGIC) != SETTINGS_MAGIC ||
        eeprom_read(EE_VERSION) != SETTINGS_VERSION) {
        loaded = 1;
        return &creds;
    }

    uint8_t ssid_len = eeprom_read(EE_SSIDLEN);
    uint8_t pass_len = eeprom_read(EE_PASSLEN);

    /* Guard against nonsense from a corrupted or foreign record. */
    if (ssid_len > WIFI_SSID_MAX) {
        ssid_len = WIFI_SSID_MAX;
    }
    if (pass_len > WIFI_PASS_MAX) {
        pass_len = WIFI_PASS_MAX;
    }

    eeprom_read_block(EE_SSID, creds.ssid, ssid_len);
    creds.ssid[ssid_len] = '\0';

    eeprom_read_block(EE_PASS, creds.pass, pass_len);
    creds.pass[pass_len] = '\0';

    loaded = 1;
    return &creds;
}

int settings_valid(void)
{
    return settings_get()->ssid[0] != '\0';
}
