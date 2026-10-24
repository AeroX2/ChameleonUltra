# Indala support

Use matching firmware, CLI and companion GUI builds. The Ultra can read and
write; Ultra and Lite can store and emulate Indala frames. Supported modulation
is PSK1 or PSK2, fc/2, RF/32, with 64-bit or 224-bit raw frames. Raw data and
encoding are preserved through files, slots and writes. Facility/card-number
interpretation and other carrier/bit rates are outside this implementation.

## CLI

```text
hw mode r
lf indala read --output indala.json
lf indala econfig -s 1 --file indala.json
lf indala econfig -s 1 --output exported.json
hw mode e
```

Select slot 1 to emulate the stored credential. Slot configuration saves to flash.
To write a T55xx test tag, put it on the Ultra, switch to reader mode and run:

```text
hw mode r
lf indala write --file indala.json
```

The command only reports success after reading back the same bit length,
encoding and raw bytes. A failed verification is reported as failure even if a
write command was sent. 224-bit data occupies all seven T55xx data blocks, so
password protection is disabled; block 7 holds credential data. 64-bit writes
retain the standard password configuration.

Raw entry is also supported with `--raw HEX --psk 1` or `--psk 2` on `write` and
`econfig`. Use 16 hex digits for 64 bits or 56 for 224 bits, including the preamble.
Without an explicit encoding, raw entry defaults to PSK1 for 64 bits and PSK2
for 224 bits. JSON files preserve the encoding explicitly.

Automatic reading prioritizes 64-bit PSK1 and then PSK2. For unusual 224-bit
PSK1 tags, use `lf indala read --psk 1`; `--psk 2` forces PSK2. The long preamble
can be ambiguous between absolute phases and phase transitions, so the reader
must not silently label an unusual 224-bit PSK1 tag as a reliable automatic result.

## GUI

The LF Read button scans Indala as well as existing LF formats. Its Indala
selector offers automatic, PSK1 and PSK2. Save a read as a card, load it into a
slot, edit/export the slot, or use Write to program a T55xx tag. Saved cards
retain the bit length and encoding through their tag type. Read errors display
an error and release the busy state; continuous scans cannot overlap LF reads.
Older firmware without the extended read capability is skipped by the Indala
scanner. Install matching firmware for slot and write operations.

## Implementation and validation

A versioned record contains `[1, encoding, 0, bit_count, raw_bytes...]` and is
12 or 32 bytes. Flash stores a padded 32-byte record. Tag types 300/301 represent
64-bit PSK1/224-bit PSK2; 302/303 represent 64-bit PSK2/224-bit PSK1.
Commands 3802/3803 read/write records; 5800/5801 set/get the active slot record.
Legacy command 3801 retains its eight-byte 64-bit PSK1 response.

The Ultra captures 28,800 signed ADC samples at 166.67 kHz in one EasyDMA
transaction into static memory,
uses a tapered 24-sample carrier filter and estimates the phase axis from IQ
covariance, searches alignment and requires two identical
consecutive frames. Capture restores the LF timer, ADC and configured carrier.
The former streaming path restarted the ADC in an interrupt every 2,048 samples;
interrupt delays could lose sample periods and invalidate phase/timing alignment.
Indala now bypasses that streaming path. A regression fixture demonstrates
224-bit PSK2 rejection with periodic dropped samples and success without gaps.
This fixes a capture defect; physical read reliability still needs retesting.
PWM emulation repeats each half-carrier cycle 16 times per bit and doubles odd
PSK2 transition-parity frames to maintain phase continuity on looping.

Host decoder tests cover 2,304 noisy alignment/phase cases across all four
formats, automatic detection, rejection, T55xx block generation and emulation
phase continuity. CLI and GUI integration tests cover read, file/card persistence,
slot operations and exact write verification. Synthetic samples and mocked
communication do **not** establish physical read range or interoperability.
Physical tag/reader validation remains pending.

```sh
cc -O2 -Wall -Wextra -Werror \
  -Ifirmware/application/src/rfid/reader/lf \
  -Ifirmware/application/src/rfid/nfctag/lf \
  firmware/application/tests/test_indala_decode.c \
  firmware/application/src/rfid/reader/lf/indala_decode.c \
  firmware/application/src/rfid/nfctag/lf/protocols/indala.c -lm -o /tmp/test_indala
/tmp/test_indala
```

Before treating this as hardware-validated, compare 64/224-bit tags against a
reference reader, verify slot save/reboot/emulation on a reader, and verify
T55xx reads after power cycling. Also check no-tag/other LF formats, scan timeout
recovery, battery reporting, BLE and existing slot/settings migration.

## Protocol compatibility

Upstream IDs remain unchanged. Fork-only commands moved out of collisions:

| Command | Old ID | New ID |
| --- | ---: | ---: |
| Get/set double-click binding | 1041/1042 | 1800/1801 |
| Get/set chord binding | 1043/1044 | 1802/1803 |
| Set/get HF RX gain | 2018/2019 | 2800/2801 |
| HID Prox candidate scan | 3021 | 3800 |
| Indala scan | — | 3801 |

Old companion GUI versions must be updated before using those fork-only
controls. Settings storage version 14 retains the fork v13 field offsets and
adds the long-press threshold in its trailing padding. Migration from an
upstream v7 settings record is not supported because upstream and older fork
settings versions overlap; reset settings when moving from upstream firmware.

## Physical read investigation (2026-10-25)

Testing one stationary 224-bit PSK2 tag over USB gave 1/10 successful reads on
c3ebbc8 and 0/20 on d8a3a3b. Three raw captures exposed strong low-frequency
envelope interference leaking into the rectangular carrier filter. A Hann
window and covariance-derived phase axis decoded all three captured signals;
the candidate firmware then returned the same frame on 29/30 physical reads
(19/20 forced PSK2, 10/10 automatic). The final reader allows at least 1.5 seconds
for another capture when necessary. These measurements cover this tag and
position, not all variants, read distances, emulation or T55xx interoperability.
Raw credential captures are kept locally and are not checked into the repository.
