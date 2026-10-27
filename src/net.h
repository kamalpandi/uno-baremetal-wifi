/*
 * net.h — getting online, and opening a TCP connection.
 *
 * AT+CWJAP is unlike every other command in this project: it takes
 * seconds rather than milliseconds, it emits unsolicited status lines
 * while it works, and failing is a normal outcome rather than a bug. All
 * three are the module's design, so this layer exists to contain them
 * instead of scattering them through main().
 *
 * Phase 6 adds the transport half: one TCP connection, opened to a host
 * and port given at runtime, fed a pre-built buffer, then closed. It
 * knows nothing about HTTP - that lives in http.c.
 */

#ifndef NET_H
#define NET_H

#include <stdint.h>

/*
 * Prepare the module: check it answers, turn command echo off, and
 * select single-connection mode.
 *
 * Blocking and short (well under a second when the module is present).
 * Safe to call again if the shield was reset, which is why `i` on the
 * console does exactly that: a module reset puts its echo back on.
 */
void net_init(void);

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

/*
 * Open a TCP connection. `host` may be a dotted quad or a name, because
 * the module resolves names itself - no DNS code here.
 *
 * Blocking, up to 10 seconds. Returns 1 on success. Clears any stale
 * "CLOSED" flag first, so the end-of-response test in http.c starts
 * from a clean slate.
 */
int net_tcp_open(const char *host, uint16_t port);

/*
 * Send `len` bytes with the AT+CIPSEND two-stage handshake: ask, wait
 * for '>', push the bytes, wait for "SEND OK".
 *
 * Blocking. Returns 1 only if the module confirmed the payload arrived.
 */
int net_tcp_send(const uint8_t *data, uint16_t len);

/*
 * Close the connection.
 *
 * The module answers "CLOSED" and/or "OK" depending on version, and a
 * connection the peer already dropped answers ERROR, so the result is
 * deliberately ignored - there is nothing useful to do with it.
 */
void net_tcp_close(void);

#endif /* NET_H */
