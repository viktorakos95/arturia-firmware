# seqmute-08 = seqmute-07 + retrigger on every key (original KeyStep 37, 1.1.6.579)

Candidate: `keystep37_Firmware_Update_1_1_6_579_seqmute08.led`
SHA-256 `bd4c687d0fde1421221c89e15c48d6f874cf9177bf269682a89fce17f62fc5d2`
`ks37 verify --stock --plan`: 176/176 record sums, application residue 0. **Emulator-checked only.** Replaces seqmute-07.

## Changes from seqmute-07 (user report: gate sometimes silent, no retrigger while a note is held)
* **Every key press retriggers.** Before: only the first key sent a gate note, so with a percussive patch a key added while
  another was held changed the pitch of a decayed envelope and was silent. Now each key press sends Note Off of the previous
  gate note, then Note On (velocity of that key), after its pitch/level CCs. The gate note is the newest key's pitch; the last
  key up sends its Note Off. Releasing other keys sends no gate message. A held key's oscillator is still protected.
* **Repeated press of a key whose release was lost** reuses its oscillator and does not add to the held-key count (a stuck
  count would keep the gate from ever opening again).

## Memory / space
Unchanged layout. Code 732 bytes in the unreachable chord-page knob handlers (0x0800527c..), 428 bytes at 0x0801f180 (limit 0x0801f400).

## Emulation
`emu_rr_test.py` 127 PASS (adds retrigger expectations, repeated-press sessions in the 500 random sessions against the model).
Sustain test 26 PASS, seq test 0 FAIL, chord test only the two intended differences.
