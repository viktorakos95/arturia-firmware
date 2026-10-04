# seqmute-06 = seqmute-03 + round-robin oscillator allocation (original KeyStep 37, 1.1.6.579)

Candidate: `keystep37_Firmware_Update_1_1_6_579_seqmute06.led`
SHA-256 `37183c69a76ed09bedd0dbbafaf9ad2cac02cc6322f6fb552be6eecb99e4de2a` (see `sha256sum`; rebuilt after the last source edit)
`ks37 verify --stock --plan`: 176/176 record sums, application residue 0. **Emulator-checked only.**
Build: `clang --target=thumbv7m-none-eabi -c rr.S`, `ld.lld -Ttext=0x08004000 --no-rosegment -e 0`, `llvm-objcopy -O binary`, then
`build_plan.py` (reads `/tmp/s06.elf`) and `ks37 patch`. Replaces seqmute-05 (split), which was never flashed.

## Behaviour (Shift+Hold = on/off, shown ON / OFF; off at power-up)
* Physical keys only; MIDI-in notes are untouched. Chord mode stays unreachable.
* Each held key takes one oscillator of the track on the keyboard channel: first key osc 1, second osc 2, a third key
  takes the oscillator of the oldest key. A lone key mutes the other oscillator (level 0); levels used are 127 / 0.
* Pitch: OSC1 CC 16 (MSB) + CC 48 (LSB), OSC2 CC 17 + CC 49, 14-bit = 8192 + (note - 64 + offset) * units-per-semitone,
  clamped to 0..16383. Base note 64 = "E-3" (assumption; coarse tune 0 = E-3 per the user). Levels: OSC1 CC 69, OSC2 CC 78.
* One gate note per chord (the first key's pitch, first key's velocity): opened by the first key, closed by the last key
  up. With Hold on, the gate stays open until Hold is released; a new key then retriggers.
* Turning it off sends tune 0 (MSB 64, LSB 0) and level 127 to both oscillators, and closes an open gate.
* Chord-page knobs: knob 1 = units per semitone (names 128 / 171 / 341; 128 = one semitone per MSB step), knob 2 = base
  offset in semitones (0 / -1 / +1 / -12 / +12). Knobs 3, 4: nothing. Settings are kept while off.
* The Analog Four sound must have both oscillators' tracking off (pitch comes from the CCs only).

## Not covered
Notes beyond two keys steal the oldest oscillator (that key loses its sound). Keys do not reach the sequencer, arp or CV
output while on. One track only (the four-track allocator is the next step). The CC to semitone scale is unverified.

## Memory
No new RAM. Chord object `0x20001e04` (unused with chord mode unreachable): +9 flags, +0x49/+0x4a oscillator keys, +0x4b
count/oldest/Hold flag, +0xb1 gate note. Code: 540 bytes in the now-unreachable stock chord-page knob handlers
(0x0800527c..0x08005498; the only entry was the knob jump table, checked over the whole disassembly) and 460 bytes in
the padding at 0x0801f180 (limit 0x0801f400).

## Emulation
`emu_rr_test.py` 51 PASS: off == seqmute-03 (72 states x 6 events), hand-checked sequences, pitch/clamp maths for all
scale/offset settings, 400 random key/Hold sessions (message for message against an independent model), Shift+Hold
through the real router table, knobs, names. Sustain test 26 PASS. Seq test 0 FAIL. Chord test: only the two
intended differences (Chord press, Shift+Hold press).
