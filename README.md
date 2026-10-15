# uno-baremetal-wifi

Bare-metal C for the **ATmega328P** (Arduino Uno R3) driving a
**Cytron ESP8266 WiFi Shield** over a bit-banged UART.

No Arduino core. No libraries. `avr-gcc` writes directly to registers.

## Status

**Phase 2** — the bit-banged transmitter works: a Timer1 compare ISR
produces a 9600 8N1 waveform on D3, confirmed by the shield's RX LED.

## Hardware

| Arduino | AVR | Use |
|---------|-----|-----|
| D13 | PB5 | onboard LED |

## Build and flash

```sh
make          # compile + report flash/RAM usage
make flash    # upload over the bootloader
make clean
```

This board carries a **CH340** USB-serial bridge rather than the official
Arduino ATmega16U2, so it enumerates as `/dev/ttyUSB0` instead of
`/dev/ttyACM0`. Set `PORT` in the Makefile if yours differs.

## Roadmap

- [x] **Phase 0 — toolchain.** Blink PB5, prove `avrdude` works on this clone.
- [x] **Phase 1 — `uart0.c`.** Register-level USART0 debug console.
- [x] **Phase 2 — `softuart_tx.c`.** Timer1-driven bit-banged transmit.
- [ ] **Phase 3 — `softuart_rx.c`.** `INT0` start-bit detect, sampling ISR.
- [ ] **Phase 4 — `at.c` + `at_parse.c`.** Non-blocking AT command layer.
- [ ] **Phase 5 — network.** Credentials in EEPROM, `AT+CWJAP`.
