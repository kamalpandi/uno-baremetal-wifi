/*
 * settings.h — WiFi credentials, persisted in EEPROM.
 *
 * A magic byte plus a version number are written last, so a chip that
 * was never programmed - or was programmed by an older layout - reads
 * back as "no credentials" rather than as a plausible-looking SSID made
 * of erased bytes (0xFF).
 *
 * The password is stored and transmitted in plaintext. WPA2 protects it
 * over the air; nothing here protects it on the chip. That is an
 * acceptable trade for a learning project and worth knowing.
 */

#ifndef SETTINGS_H
#define SETTINGS_H

#include <stdint.h>

/* WPA2 limits: 32-byte SSID, 63-byte passphrase. */
#define WIFI_SSID_MAX 32
#define WIFI_PASS_MAX 63

typedef struct {
    char ssid[WIFI_SSID_MAX + 1];   /* NUL-terminated */
    char pass[WIFI_PASS_MAX + 1];   /* NUL-terminated */
} wifi_creds_t;

/* Fetch the stored credentials, loading them from EEPROM on first use. */
const wifi_creds_t *settings_get(void);

/* Non-zero if an SSID is actually stored. */
int settings_valid(void);

/* Persist these credentials to EEPROM (slow: ~3.4 ms per changed byte). */
void settings_save(const wifi_creds_t *in);

/* Invalidate the stored credentials without erasing every byte. */
void settings_erase(void);

#endif /* SETTINGS_H */
