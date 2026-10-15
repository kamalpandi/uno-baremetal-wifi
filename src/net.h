/*
 * net.h — getting online, using the credentials stored in EEPROM.
 *
 * AT+CWJAP is unlike every other command in this project: it takes
 * seconds rather than milliseconds, it emits unsolicited status lines
 * while it works, and failing is a normal outcome rather than a bug. All
 * three are the module's design, so this layer exists to contain them
 * instead of scattering them through main().
 */

#ifndef NET_H
#define NET_H

/*
 * Put the module in station mode and associate with the stored network.
 *
 * Blocking. It loops on at_tick(), so every line the module sends is
 * echoed to the console and the whole negotiation is watchable. Returns
 * 1 on success, 0 on failure (having explained why).
 */
int net_join(void);

/* Print the module's current mode and IP address. */
void net_status(void);

#endif /* NET_H */
