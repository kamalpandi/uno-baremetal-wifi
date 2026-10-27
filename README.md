# uno-baremetal-wifi

Bare-metal C for the **ATmega328P** (Arduino Uno R3) driving a
**Cytron ESP8266 WiFi Shield** over a bit-banged UART.

No Arduino core. No libraries. `avr-gcc` writes directly to registers, and the
ESP8266 is spoken to with raw Hayes AT commands over a serial line we time by
hand.

## What it does

Joins a WiFi network using credentials typed at the serial console and kept in
the chip's own EEPROM, then fetches a page over TCP and prints it:

```
> AT+CIPSTART="TCP","192.168.29.91",8000
  [OK]
> AT+CIPSEND=44
  prompt received, sending 44 bytes
  SEND OK

--- response ---
HTTP/1.0 200 OK
Server: SimpleHTTP/0.6 Python/3.11
Content-Type: text/html; charset=utf-8
Content-Length: 1420
...
--- HTTP 200, body 1420 bytes, response 1596 bytes ---
```

That is **9534 bytes of flash and 784 bytes of RAM** — no libraries, no
allocator, no buffering of the response.

### Console keys

Type into `picocom`; it has no local echo, so the firmware echoes for you.

| Key | Action |
|---|---|
| `c` | enter WiFi credentials and save them to EEPROM |
| `u` | show the stored credentials |
| `j` | join the network |
| `n` | network status (mode and IP address) |
| `g` | fetch a page over TCP — then host, port, path |
| `i` | re-run module init (`ATE0`, single-connection mode) |
| `h` | toggle hex dump of received bytes |
| `s` | receiver counters |
| `f` | flush the receive queue |
| `t` | loopback self-test (D2 jumpered to D3) |
| any other | send `AT` |

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
make test           # host-side tests for the +IPD framing and HTTP parser
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
| `net.c` | The join sequence, then one TCP connection: `CIPSTART` / `CIPSEND` / `CIPCLOSE` |
| `http.c` | Builds one `GET`, parses the status line and headers, streams the body |

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

Measured on the final build — 784 of 2048 bytes, or 38%:

| Region | Bytes |
|---|---|
| `rx_buf` (soft UART receive queue) | 256 |
| `tx_buf` (soft UART transmit queue) | 128 |
| HTTP request buffer | 112 |
| WiFi credentials (SSID + password) | 97 |
| AT line accumulator | 64 |
| host and path typed at the console | 64 |
| all state variables, timers, counters | 63 |

The remaining ~1.2 KB is stack. The receive queue alone is an eighth of the
chip, and that single number is what decides the whole design of Phase 6: an
HTTP response **cannot** be buffered, so it is parsed and printed as it
arrives.

Note what the HTTP request buffer is *not* for. It is not a response buffer —
it holds the ~90-byte request we are about to send. It exists because
`AT+CIPSEND=<n>` demands the byte count **before** any byte goes out, so there
is no way to stream a request whose length is not yet known.

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

## Phase 6: a TCP connection and an HTTP response

Everything up to Phase 5 was request/response: send a line, wait for a line.
A TCP connection is not, and it breaks three assumptions at once.

### 1. Received data is not line-shaped

When the module receives TCP data it pushes it as:

```
+IPD,<len>:<exactly len raw bytes>
```

There is no terminator. **The length is the only boundary** — and that is a
much sharper edge than it looks. Get it wrong by one and nothing fails
loudly: the stream shifts by a byte, the HTML still looks roughly like HTML,
and every subsequent block is misread.

So `at_parse.c` has two modes. Up to `+IPD,<len>:` it accumulates lines
exactly as before. The instant the colon closes that header it switches to
data mode, counts payload bytes, and hands each one straight to a sink
callback. Nothing is stored, so a response of any size passes through in
constant memory.

Because the parser is fed one byte at a time, a header split across two
ring-buffer reads is harmless. A block-based parser would need real care
there.

### 2. `AT+CIPSEND` is a two-stage handshake

```
AT+CIPSEND=44
OK            <- the module accepted the command
>             <- now it is listening for exactly 44 bytes
<44 bytes>
SEND OK       <- the payload was taken
```

The `OK` is **not** the answer. If the state machine treated it as the
result it would start pushing the payload into a module that had not yet
opened its input, and lose the first bytes.

That is why `at.c` grew `at_wait_t`: the layer is told what a *successful*
ending looks like (`OK`, the `>` prompt, or `SEND OK`) instead of assuming it
is always `OK`.

The `>` is also the only thing the module sends **without a newline**, so
line accumulation can never complete on it — it would sit in the buffer until
the AT command timed out. It is special-cased to emit a line immediately.
Some firmware versions send it as `> ` with a trailing space, which would
glue itself onto the front of the next line and stop `SEND OK` from matching,
so a space at the start of a line is swallowed.

### 3. There is no end-of-body marker except the connection closing

HTTP/1.0 makes this trivial: the server closes when it is finished, so
`CLOSED` *is* the end of the response. No `Content-Length` parsing, no header
line buffer.

That one choice removes the last thing that would have needed RAM. The status
code is read by **position** instead — it lives at columns 9-11:

```
H T T P / 1 . 0   2 0 0   O K
0 1 2 3 4 5 6 7 8 9 ...
```

and end-of-headers is a single byte of state: "has anything non-CRLF appeared
since the last newline?". When a newline arrives and the answer is no, the
blank line has been found.

The request is deliberately minimal, because with HTTP/1.0 `Connection:
close` is already the default:

```
GET /path HTTP/1.0\r\n
Host: 192.168.29.91\r\n
\r\n
```

### Printing without flooding

The body is printed as it is parsed, within a **1024-byte budget** shared by
headers and body. Bytes past the budget are still counted, just not shown:

```
--- HTTP 200, body 4210 bytes, response 4386 bytes ---
--- printed the first 1024 bytes only ---
```

A large response therefore reports its true size while the console stays
readable. The budget costs two bytes of state, not 1024 — it is a stopping
point for printing, not a buffer. Press `q` during a transfer to abandon it.

### Trying it

Serve something from your laptop:

```sh
mkdir -p /tmp/www && cd /tmp/www
python3 -m http.server 8000 --bind 0.0.0.0
hostname -I          # the address to type at the Uno's prompt
```

Then on the Uno: `j` to join, then `g`:

```
Server IP or name: 192.168.29.91
Port [8000]:
Path [/]:
```

Python's handler speaks HTTP/1.0 by default, which is exactly the case this
parser is built for.

If the fetch reports `connection failed`, the server is fine but something
between the two is not. On a Linux desktop the usual culprit is `ufw`:

```sh
sudo ufw allow 8000/tcp     # or scope it to your LAN
```

Worth knowing how to tell the failure modes apart, because `AT+CIPSTART`
answers `ERROR` for all of them, including "the packet was silently dropped".
Two clues:

- **When** the ERROR arrives. Our 10-second budget expires first and prints
  the failure, then the module's `ERROR` turns up afterwards. That is a
  dropped packet — firewall or wrong address. A command the module actually
  rejects comes back immediately instead.
- **Whether the server logs a connection at all.** If the listener saw
  nothing, the SYN never arrived, so nothing about the request can be wrong.

A public HTTP server is a useful control experiment, because no local
firewall can break it: if `example.com:80` works and a LAN address does not,
the firmware is fine and the network is the problem.

A better test than a directory listing: copy `at_parse.c` into the served
directory and fetch `/at_parse.c`. The body is then full of `\r\n`, `:`, and
the literal string `+IPD,` — a line-based parser would corrupt it
immediately, while length framing carries it through intact.

### How it was verified

The framing and the HTTP state machine are pure logic, so they compile for
the host with a stand-in `<avr/pgmspace.h>`:

```sh
make test
```

Twenty assertions, covering:

- a payload containing a literal `+IPD,`, and one containing `\r\nOK\r\n`
- two consecutive blocks with no separator between them
- a zero-length `+IPD,0:` block
- data mode ending exactly after `len` bytes
- the `>` prompt, including the `> ` trailing-space form
- 200 and 404 status lines, and a response with no headers at all
- `\r\n\r\n` occurring **inside** the body

That is cheaper and far more repeatable than flashing and reading a
terminal, and it lets awkward cases be asserted on purpose rather than
hoped for. Hardware is then used for the things a host cannot know: that
the module really sends `+IPD` in this shape, and that it accepts our
handshake.

Then on real hardware: `AT+CIPSTART` to a **name** (so the module's own DNS
was exercised), the `>` handshake, and a complete `HTTP/1.1 200 OK` response
with `Connection: close` and no `Content-Length`. The summary read:

```
--- HTTP 200, body 577 bytes, response 892 bytes ---
```

892 − 577 = 315 bytes of headers, and the body printed in full. That
arithmetic is the real proof: the header block and the body account for
exactly the bytes that arrived, which is only possible if the `+IPD` length
was honoured precisely.

## Scope

Phase 6 is the end of this project. What it deliberately does **not** do:
chunked transfer encoding, keep-alive, redirects, gzip, TLS, or
`AT+CIPRECVMODE` passive receive (the module's firmware, AT 1.7.4.0, predates
it). One request, one response, one connection, closed at the end.

---

## Roadmap

- [x] **Phase 0 — toolchain.** Blink PB5, prove `avrdude` works on this clone.
- [x] **Phase 1 — `uart0.c`.** Register-level USART0 console, 115200 8N1, non-blocking RX.
- [x] **Phase 2 — `softuart_tx.c`.** Timer1-driven bit-bang TX on PD3.
- [x] **Phase 3 — `softuart_rx.c`.** `INT0` start-bit detect, `OCR1B` mid-bit sampling, ring buffer.
- [x] **Phase 4 — `at.c` + `at_parse.c`.** Non-blocking command/response state machine with timeouts.
- [x] **Phase 5 — network.** Credentials in EEPROM, entered over the console; `AT+CWMODE=1` + `AT+CWJAP`.
- [x] **Phase 6 — TCP.** `AT+CIPSTART` / `AT+CIPSEND`, `+IPD` framing, streamed HTTP GET.

Each phase is a tagged commit, and every one of them builds and runs:

```sh
git checkout phase-3      # or phase-0 … phase-6
make size
git checkout main
```
