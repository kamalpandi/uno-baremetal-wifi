# uno-baremetal-wifi

Bare-metal C for the **ATmega328P** (Arduino Uno R3) driving a
**Cytron ESP8266 WiFi Shield** over a bit-banged UART.

No Arduino core. No libraries. `avr-gcc` writes directly to registers, and the
ESP8266 is spoken to with raw Hayes AT commands over a serial line we time by
hand.

---

## Why there are two serial ports

The ATmega328P has exactly **one** hardware USART, wired to Arduino D0/D1.
Those same two pins are also the CH340's link to your PC. One peripheral cannot
serve both, so the two links are split:

```
     PC ──USB── CH340 ──┬── D0/D1 (hardware USART0) ──► debug console
                        │
   ESP8266 ─────────────┴── D2/D3 (bit-banged)  ──────► AT command link
```

This is also what Cytron's own shield manual recommends for the Uno.

## Pin map

| Arduino | AVR | Dir | Use |
|---------|-----|-----|-----|
| D0 / RXD | PD0 | in  | USART0 RX from CH340 (unused) |
| D1 / TXD | PD1 | out | USART0 TX -> PC debug console |
| D2 | PD2 | **in**  | <- ESP TX — shield **RX** jumper, `INT0` / `PCINT18` |
| D3 | PD3 | **out** | -> ESP RX — shield **TX** jumper |
| D4 | PD4 | —   | microSD card CS — **do not use** |
| D13 | PB5 | out | onboard LED (heartbeat) |

Shield settings: jumpers on **D2/D3**, slide switch to **RUN**.

## Timing

| Purpose | Peripheral | Setting | Result |
|---------|-----------|---------|--------|
| millisecond clock | Timer0 CTC | prescaler 64, `OCR0A=249` | exactly 1 ms |
| bit-bang UART | Timer1 CTC | prescaler 1, `OCR1A=1666` | 9598 baud (0.02% error) |

Timer0 is free precisely *because* there is no Arduino core — `millis()` would
have claimed it otherwise.

Baud rate is **9600**: the shield's AT firmware default, and the fastest rate
that is reliably sampleable by a bit-banged receiver on a 16 MHz AVR.

## Build and flash

```sh
make                # compile + report flash/RAM usage
make flash          # upload over the bootloader
make flash monitor  # upload, then attach the console
make monitor        # just attach the console
make disasm         # disassembly interleaved with C source
make compdb         # regenerate compile_commands.json for the editor (clangd)
make clean
```

There is no F_CPU or fuse handling here — we use the Uno bootloader as-is.

## Watching the debug console

### The port is exclusive

Your serial terminal and the AVR bootloader need the **same wire**, so only one
of them can have it. `make flash` now refuses to run while something else holds
the port and names the culprit — because avrdude's own error for this
(`programmer is not responding`, `try -B 125kHz`) sends you off debugging
wiring that is perfectly fine.

```sh
sudo apt install picocom
```

The workflow is **detach, flash, reattach**:

```sh
Ctrl-A Ctrl-X      # in picocom: quit
make flash         # upload
make monitor       # reattach
```

### You will miss the banner

The board prints its banner once, immediately on reset. If you flash first and
attach afterwards, that output is already gone — serial has no replay. Press
the **RESET button on the Uno** after attaching `picocom` to see it again.

Zero-install fallback:

```sh
stty -F /dev/ttyUSB0 115200 cs8 -cstopb -parenb -echo raw
cat /dev/ttyUSB0
```

## The PROGMEM rule

On AVR, a plain string literal is **copied into RAM at startup**. With only
2 KB of RAM that is fatal, so every fixed message in this project is wrapped
in `PSTR()` and sent through a `_P` function that reads it from flash with
`pgm_read_byte()`.

Measured on the Phase 1 console, before and after:

| | Program (flash) | Data (RAM) |
|---|---|---|
| plain literals | 580 B | **152 B** |
| `PSTR()` + `_P` | 552 B | **0 B** |

Saving 152 bytes of RAM also made the code slightly *smaller*. Use the RAM
(`uart0_puts`) variants only for buffers that genuinely live in RAM.

### Editor support (clangd)

`clangd` does not read the Makefile, so without help it treats these files as
host x86 C and cannot find `<avr/io.h>`. Run `make compdb` after adding sources
or changing `CFLAGS`: it writes `compile_commands.json` with the exact `avr-gcc`
command per file. Together with the checked-in `.clangd` hints this gives full
AVR-aware completion and diagnostics. Both generated files
(`compile_commands.json`, `.cache/`) are git-ignored.

---

## Source layout

| File | Responsibility |
|---|---|
| `main.c` | Banner, the console key map, credential entry UI |
| `uart0.c` | Hardware USART0 — the debug console (D0/D1, 115200) |
| `console.c` | Line editor for reading input from the console |
| `softuart_tx.c` | Bit-banged transmit to the ESP (D3, 9600) |
| `softuart_rx.c` | Bit-banged receive from the ESP (D2, 9600) |
| `timer.c` | 1 ms tick from Timer0, `millis()`, wrap-safe timeouts |
| `at_parse.c` | Bytes → lines, and classification of AT response codes |
| `at.c` | The non-blocking AT command state machine |
| `eeprom.c` | Register-level access to the 1024-byte on-chip EEPROM |
| `settings.c` | Credential layout, validity marker, load/save |
| `net.c` | The join sequence: `CWMODE` → `CWJAP` → `CIPSTA` |

## How the soft UART works

Both directions share **Timer1**, which sounds like a conflict but is not:
AT traffic is request/response, so transmit and receive never need the
timer at the same moment. The ESP cannot begin replying until it has
received our terminating newline — the last thing we send.

Transmit and receive use *different compare units* on that one timer, so
they live in separate files with no shared interrupt and no mode flag:

| | Compare unit | Vector | Role |
|---|---|---|---|
| TX | `OCR1A = 1666` | `TIMER1_COMPA_vect` | change the pin at the **bit boundary** |
| RX | `OCR1B = 833` | `TIMER1_COMPB_vect` | sample the pin at the **bit middle** |

The trick that makes receive simple: in CTC mode the counter is cleared
on an `OCR1A` compare match **regardless of whether `OCIE1A` is
enabled**. The interrupt enable gates the interrupt, not the hardware
clear. So during receive we leave `OCIE1A` off and `OCR1A` quietly keeps
the counter locked to the sender's bit grid — which is what turns
`OCR1B = 833` into "half a bit after the bit boundary".

The usual "wait 1.5 bit periods for the first sample" special case
therefore disappears entirely. The clear at the end of the start bit *is*
the half-bit offset:

```
t=0      falling edge -> INT0: TCNT1 = 0, arm OCIE1B
t=833    middle of START bit   -> must still be LOW, else it was a glitch
t=1666   OCR1A clears counter  -> hardware, no interrupt
t=2499   middle of data bit 0  -> sample
...
t=15835  middle of STOP bit    -> must be HIGH, else framing error
```

The start-bit and stop-bit checks are what let the receiver survive the
ESP8266's boot log, which it emits at 74880 baud — roughly eight times
faster than we listen. Those bytes fail the stop-bit check and are
discarded instead of reaching the parser. The firmware then prints
`ready` at the normal AT baud rate, which we *can* read.

## The stale compare-flag trap

Both directions arm their interrupt from ordinary code and both have to
clear Timer1's compare-match flag **first**:

```c
TIFR1 = _BV(OCF1A);     /* or OCF1B - writing 1 clears it */
TIMSK1 |= _BV(OCIE1A);
```

Skipping that line produced the most misleading bug in this project. The
counter free-runs, so it crosses `OCR1A` and `OCR1B` once per bit period
and sets both flags *whether or not the interrupts are enabled* — and
with the interrupts off, nothing clears them.

So the moment `OCIE1A` was armed, `COMPA` fired **immediately** instead
of one bit period later, and the ISR emitted data bit 0 the instant the
start bit was asserted. The start bit lasted **85 CPU cycles (5 µs)
instead of 1667 (104 µs)** and the ESP decoded `AT\r\n` as
`b'\xaaC\xe1'` — garbage.

What made it expensive is that **nothing looked broken**. The pin moved,
the shield's RX LED blinked, and the receiver worked perfectly in the
other direction (`\r\nready\r\n` decoded byte-for-byte). An activity LED
proves a wire is connected; it cannot tell a 5 µs pulse from a 104 µs
one. Only two things caught it: disassembling `tx_kick` and checking
which registers were written, and a loopback round trip (`t` in the
diagnostic build) that cannot be fooled by a moving pin.

## RAM budget

Measured on the Phase 5 build — 580 of 2048 bytes, or 28%:

| Region | Bytes |
|---|---|
| `rx_buf` (soft UART receive queue) | 256 |
| `tx_buf` (soft UART transmit queue) | 128 |
| WiFi credentials (SSID + password) | 97 |
| AT line accumulator | 64 |
| all state variables, timers, counters | 35 |

The remaining ~1.4 KB is stack. Note that the receive queue alone is an
eighth of the chip, which is what forces the AT parser to be
line-bounded and (from Phase 6) forces HTTP bodies to be stream-parsed
rather than buffered.

## EEPROM credentials

Stored at a fixed layout in the 1024-byte on-chip EEPROM, with a magic
byte and version written **last** so an interrupted save reads back as
"nothing stored" rather than as a plausible-looking corrupt SSID.

Two hardware properties shape the driver. A write takes about **3.4 ms**,
and each cell is rated for **~100,000 writes** — so `eeprom_write()`
returns immediately if the cell already holds the value, which turns a
repeated save into a no-op instead of a countdown to failure.

The fussy part is the write handshake: `EEMPE` self-clears after four CPU
cycles, and `EEPE` must be set inside that window. The assembly is two
`sbi` instructions two cycles apart, with interrupts masked around them:

```
cli
sbi EECR, EEMPE
sbi EECR, EEPE
```

Credentials are entered over the serial console and never appear in the
source tree. They are, however, stored and transmitted in plaintext.

---

## Roadmap

- [x] **Phase 0 — toolchain.** Blink PB5, prove `avrdude` works on this clone.
- [x] **Phase 1 — `uart0.c`.** Register-level USART0 console, 115200 8N1, non-blocking RX.
- [x] **Phase 2 — `softuart_tx.c`.** Timer1-driven bit-bang TX on PD3.
- [x] **Phase 3 — `softuart_rx.c`.** `INT0` start-bit detect, `OCR1B` mid-bit sampling, ring buffer.
- [x] **Phase 4 — `at.c` + `at_parse.c`.** Non-blocking command/response state machine with timeouts.
- [x] **Phase 5 — network.** Credentials in EEPROM, entered over the console; `AT+CWMODE=1` + `AT+CWJAP`.
- [ ] **Phase 6 — TCP.** `AT+CIPSTART` / `AT+CIPSEND`, stream parsing.
