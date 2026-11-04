# Controller protocol notes

Facts only, for interoperability with the controller the app supports (vendor `0x054C`, product `0x0CE6`, over Bluetooth).
Every fact comes from one of: **[D]** the controller itself (HID descriptor, its own reports, black-box tests on this
machine), **[M]** this project's measurements, **[S]** SDL's HIDAPI driver for this controller (zlib licence), **[N]** the
trigger notes by John Klein (MIT licence), **[P]** the Python driver by flok (MIT licence), **[G]** the protocol document of
a Godot plugin's native driver by Lelisvaldo (MIT licence) for the general layout of stream reports; the waveform and speaker lanes below come from
this project's black-box tests [D, M].

## Reports the controller declares [D]

| Direction | ID | Bytes, ID included |
|---|---|---|
| Input | `0x31` | 78 |
| Output | `0x31` / `0x32` / `0x33` / `0x34` / `0x35` / `0x36` / `0x37` / `0x38` / `0x39` | 78 / 142 / 206 / 270 / 334 / 398 / 462 / 526 / 547 |
| Feature | `0x05` | 41 |
| Feature | `0x09`, `0x20` | 20, 64 |

Output reports end in a little-endian CRC-32 over the byte `0xA2` followed by the report without its last four bytes [S].

## Input report `0x31` [S, D]

Offsets from the start of the report, ID at 0. Byte 1 is a counter.

| Offset | Field |
|---|---|
| 2, 3, 4, 5 | Left X, left Y, right X, right Y, 0 to 255, 128 centred |
| 6, 7 | Left and right trigger, 0 to 255 |
| 9 | Low nibble: hat, 0 up clockwise to 7, 8 released. High nibble: bits west, south, east, north |
| 10 | Bits: left shoulder, right shoulder, left trigger, right trigger, back, start, left stick, right stick |
| 11 | Bits: home, touch surface, mute |
| 17, 19, 21 | Gyro, signed 16-bit |
| 23, 25, 27 | Accelerometer, signed 16-bit |
| 29 | Sensor clock, 32-bit, in thirds of a microsecond [M] |
| 34, 38 | Touch points: high bit set means no touch, then 12-bit X and 12-bit Y in three bytes |
| 54 | Power: low nibble level, high nibble state [D] |

## Motion calibration, feature report `0x05` [S, D]

Signed 16-bit values at: 1, 3, 5 gyro zero; 7 to 17 gyro plus and minus per axis; 19, 21 rated speed plus and minus;
23 to 33 accelerometer plus and minus per axis. Scale per axis is `(speed+ + speed-) / (plus - minus)`
degrees per second per count, about 1/16.4 [M].

## Control report, `0x31`, 78 bytes [S, D]

Byte 1: high nibble is a sequence shared by all output reports. Byte 2: `0x10`.

| Byte | Meaning |
|---|---|
| 3 | Valid flags: `0x02` rumble style, `0x04` right trigger, `0x08` left trigger, `0x20` speaker volume, `0x40` microphone volume, `0x80` audio control |
| 4 | Valid flags: `0x01` microphone light, `0x02` power save, `0x04` light bar, `0x10` player lights, `0x40` motor power, `0x80` audio control 2 |
| 5, 6 | Rumble small, big |
| 8 | Speaker volume |
| 10 | Audio path: `0x30` the speaker [D] |
| 12 | Power save: `0x10` mutes the microphone |
| 13 to 23, 24 to 34 | Right and left trigger effect, 11 bytes each |
| 39 | Motor power reduction, 0 means none |
| 40 | Preamplifier gain |
| 41 | `0x02` light bar setup valid, `0x04` improved rumble |
| 11 | Microphone light: 0 off, 1 on [S] |
| 44 | Light bar setup: `0x02` light out |
| 46 | Player lights: five bits, `0x20` for no fade [S] |
| 47 to 49 | Light bar red, green, blue [S] |

Only fields marked valid change, so a report can leave everything else alone [D].

## Trigger effects, 11 bytes [N, D]

| First byte | Meaning |
|---|---|
| `0x05` | Off |
| `0x01` | Raw force from the first millimetre: byte 2 is force, 255 the firmware's ceiling |
| `0x21` | Ten zones: bytes 1, 2 mask of active zones; bytes 3 to 6 strengths, three bits each, value minus one |
| `0x25` | Wall and break: bytes 1, 2 mask with start and end zones; byte 3 strength minus one |
| `0x26` | Vibration: as `0x21` with amplitude; byte 9 is frequency in hertz |

## Stream report, `0x32`, 142 bytes [D, M]

Byte 1 sequence as above. Byte 2 `0x91`. Then one of:

**Waveform.** Byte 3 `4`, byte 4 `0x66`, bytes 5, 6 buffer depth (`64`), byte 7 counter (adds 2 per report), byte 8 `0xD2`,
byte 9 `64`, bytes 10 to 137 two blocks of 32 stereo frames, signed 8-bit, 3 kHz. One report lasts 21.33 ms.

**Speaker.** Byte 3 `6`, byte 4 `0x7E`, bytes 5 to 8 queue depth (`48`), byte 9 counter (adds 2 per report), byte 10 `0xD3`,
byte 11 `63`, bytes 12 to 137 two Opus packets of 63 bytes. The controller plays 480 samples every 512/48000 s, so the stream
is clocked at 45 kHz, one report every 21.33 ms. Before audio the route is set with a control report: bytes 3 `0xA0`, 4 `0x80`,
8 volume, 10 path `0x30`, 40 preamplifier `2`; about 12 silent reports let the amplifier and link settle. Pure CELT from
libopus (restricted low delay, constant 50.4 kbit/s, complexity 0) decodes correctly; other encoders may not [M].

## Measured limits [M]

| Fact | Value |
|---|---|
| Command to body actuator | about 110 ms, depth does not change it |
| Waveform and rumble bytes, waveform and speaker | cannot run together |
| Link without Game Mode | 66 reports/s at 78 bytes, 33.5 at 142, 17 at 270, 13 at 398, 10 at 547 |
| Link with Game Mode | 133 reports/s at 78 bytes, 66 at 142, 20 at 547 |
| Input rate | 65/s without Game Mode, 132/s with it |
| In-flight reports | above 4 the system queue only adds latency |

## Similarity review

Done by a reviewer separate from the implementer, on 2026-11-03, in three rounds. It compared the code in `src/link`, `src/guardian` and `src/listen` with the copyleft and unlicensed implementations this project's earliest notes had credited, and with this project's own earlier code.

- **Round 1** found no wholesale copying, and small parallels: a checksum helper, codec setup and a few names. It also found that parts of the code still followed the project's earlier code, whose origin could not be shown.
- **Round 2 and 3,** after each rewrite: the checksum's form was the only remaining parallel to a copyleft source, and it has since been rewritten. The earlier code's remaining traces were reduced in a second pass. What the review did not re-read after that second pass: `limiter.h`, the calibration parser, the output part of `feel.c`, and small parts of `capture.m`.

- **Round 4,** on what round 3 had not re-read: no copied code. The calibration formula's form followed a copyleft driver and now uses the form in [S]. The speaker report's four queue-depth bytes (`48`) came to this project through byte recordings of its own earlier build, whose origin could not be shown; they also appear in one GPL project. They are wire-format values the controller needs. `limiter.h` and `feel.c` keep a few constants from the earlier code, with no external source found. Coverage was partial: selected files only of the projects compared.

Limits of this review, stated plainly: the reviewer read quote-limited extracts rather than whole copyleft files, and could not obtain every file. The implementer had seen the earlier code, so this is a rewrite and not a clean-room process. Protocol facts match every implementation of this protocol, as they must. This is an engineering review, not legal advice. Have a lawyer look before commercial use.
