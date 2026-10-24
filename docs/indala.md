# Experimental Indala reader

Use the matching firmware and CLI, connect an Ultra, then run:

```text
hw mode r
lf indala read
```

The command reports a raw 64-bit frame, for example
`Indala raw (64-bit): A0000000A1B2C3D4`. The example is synthetic.
Only PSK1, fc/2, RF/32, 64-bit frames with the 33-bit Indala preamble
are handled. Two adjacent identical frames must be observed. There is no
Indala emulation, writing, 224-bit decoding or credential-number extraction.
The explicit command is not added to automatic LF discovery.

## Implementation and validation limits

The Ultra samples the LF envelope using SAADC at 16 MHz / 96 (166.67 kHz),
with a 3 us acquisition time. TIMER2 temporarily supplies the sample clock
instead of counting carrier periods. Every exit restores counter mode,
the PWM sample route, battery ADC configuration and the original LF carrier.
A fixed 16 KiB capture buffer avoids the firmware's 8 KiB heap limit.

The decoder correlates the fc/2 subcarrier in quadrature, searches symbol
alignment, checks either phase polarity and requires repeated raw frames.
This approach was informed by the public
[nieldk implementation](https://github.com/nieldk/ChameleonUltra/blob/d77d39f09179d6133b0967bbebc49afa6c5bd216/firmware/application/src/rfid/nfctag/lf/protocols/indala.c).
Our decoder and capture path are independent implementations; this is not a
merge of that fork. Synthetic waveforms do not establish that real Indala
tags produce a sufficiently strong envelope on every Ultra hardware revision.

Host regression test (from the repository root):

```sh
cc -O2 -Wall -Wextra -Werror -Ifirmware/application/src/rfid/reader/lf \
  firmware/application/tests/test_indala_decode.c \
  firmware/application/src/rfid/reader/lf/indala_decode.c -lm -o /tmp/test_indala
/tmp/test_indala
```

Before calling this hardware-validated:

1. Compare a known 64-bit Indala tag with a reference reader. Repeat at several
   distances and orientations, including a T55xx test tag if available.
2. Check no-tag, EM410x and HID Prox inputs do not produce Indala results.
3. After success and timeout, read EM410x/HID/Pyramid tags and check battery
   reporting, BLE and the configured LF tuning still work.
4. Test reboot with existing fork v13 settings; confirm bindings and tuning
   remain intact and the long-press threshold defaults to 1000 ms.

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
