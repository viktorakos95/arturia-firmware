# seqmute-07 = seqmute-06 + protected held keys + fine tuning (original KeyStep 37, 1.1.6.579)

Candidate: `keystep37_Firmware_Update_1_1_6_579_seqmute07.led`
SHA-256 `d72e3bc293923de9c5cc2ddacb2181b6661310c35084b012c2bbab47ba8dcbd8`
`ks37 verify --stock --plan`: 176/176 record sums, application residue 0. **Emulator-checked only.**
Build as in seqmute-06 (`rr.S`, `build_plan.py` reads `/tmp/s07.elf`). Replaces seqmute-06.

## Changes from seqmute-06 (user report: "kind of works", needs fine tuning, held notes get cut off)
* **Held keys keep their oscillator.** With both oscillators taken, a new key now takes the NEWEST oscillator instead of the
  oldest, so the longest-held key is never cut off (a drone on one oscillator, melody on the other). A key that lost its
  oscillator this way stays silent until released.
* **Fine tuning**, all numeric on the chord-page knobs (value 0 = default, so power-up behaviour is unchanged):
  knob 1 = units per semitone, 100..354 (default 128); knob 2 = base offset -12..+12 semitones; knob 3 = fine offset
  -99..+99 units added to the 14-bit value. Knob 4: nothing. Pitch = 8192 + (note - 64 + offset) * scale + fine, clamped.
* Shift+Hold display is now `ON ` / `OFF`.

## Memory
Chord object 0x20001e04: +9 bit7 = on; +0x48 fine, +0xb2 scale, +0xb3 offset (raw knob byte, 0 = default); +0x49/+0x4a
oscillator keys; +0x4b count / newest oscillator / Hold flag; +0xb1 gate note. Code: 704 bytes in the unreachable stock
chord-page knob handlers (0x0800527c..), 428 bytes at 0x0801f180 (limit 0x0801f400).

## Emulation
`emu_rr_test.py` 127 PASS: off == seqmute-03, protected-key sequences, pitch maths for 100 parameter combinations, 500 random
key/Hold sessions against an independent model, Shift+Hold through the router table, knobs 1-3 (values and displays),
formatters over their whole ranges. Sustain test 26 PASS, seq test 0 FAIL, chord test only the two intended differences.
