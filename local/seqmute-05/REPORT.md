# seqmute-05 = seqmute-03 + paraphonic keyboard split, Shift+Hold on/off (original KeyStep 37, 1.1.6.579)

Candidate: `keystep37_Firmware_Update_1_1_6_579_seqmute05.led`
SHA-256 `603bd64d4f8403ea9bddaad2aace27f8e378c4aad9faabde804b56ed3fcc01a5`
`ks37 verify --stock --plan`: 176/176 record sums, application residue 0. **Emulator-checked only.**

## Changes from seqmute-04 (which was flashed and tested by the user)
* seqmute-04 reported: with a single note both oscillators sound; leaving the chord page for a CC bank stops the split.
  Cause: the split was tied to the knob page (U+0x15 == 0), and the chord object's constructor leaves the mode byte at 3
  ("ALL"), so 04 was armed in ALL at power-up. (04's report said it was off after power-up; that was wrong.)
* Split on/off is now **Shift+Hold** (the stock chord on/off gesture). It no longer depends on the knob page or CC bank.
  The display shows `ON ` / `OFF`. Chord mode itself stays unreachable.
* Chord object `+9`: bit7 = split on (clear at power-up), bits0-1 = gate side (stock boot value 3 is read as ALL).
  `+0x48` = split point (0 = 60). Knob 1 (split point) and knob 2 (gate side `1`/`2`/`ALL`) on the chord page set them;
  knob 2 preserves the on/off bit, knob 1 does not touch it.

## Not done
Lone note sounding only one oscillator needs the Analog Four's OSC1/OSC2 level CC numbers (unconfirmed), and the
14-bit pitch (CC 17 + CC 49) with a calibrated scale. Reference: coarse tune 0 = C#, 1 semitone per step (user, on device).

## Emulation
`emu_split_test.py` 36 PASS (adds: page independence, Shift+Hold through the real router table entry, on-bit
preservation by the knobs, ON/OFF names). Sustain test 26 PASS on this image. Chord/seq regression tests differ from
stock only in the intended Chord-press and Shift+Hold events, and 3 dead stack bytes below SP.
