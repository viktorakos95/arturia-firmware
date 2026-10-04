# seqmute-11 = seqmute-10 + Hold sustain per voice + simultaneous-key fix (original KeyStep 37, 1.1.6.579)

Candidate: `keystep37_Firmware_Update_1_1_6_579_seqmute11.led`
SHA-256 `76c37c39af23f8a27164bef62b8e6dd0563dc7e956ac78d1abe6e88e35228a5a`
`ks37 verify --stock --plan`: 176/176 record sums, application residue 0. **Emulator-checked only.** Replaces seqmute-10.

## User report on seqmute-10
Hold works only for some notes; gate unreliable, especially for keys pressed exactly together (one sounds, or not all).

## Causes found
* **Pair path unmuted only the new key's oscillator.** A key alone in a track leaves the other oscillator muted (level 0). When a second
  key was LOWER, the lower-note-first ordering moved the resident key to oscillator 2, which was still muted, so it went silent. Pressing
  two keys together does this about half the time.
* **Hold muted released keys.** With Hold on, releasing one key of a pair muted its oscillator (the other key still held the gate), so only
  the last key of each track sustained.

## Changes
* In a pair both oscillators get level 127 every time a key joins.
* Hold is now per voice (+0x90, bit v = voice sustained): with Hold on a released key changes nothing and keeps sounding (slot, gate and level
  untouched). A new key takes a free voice first, then a sustained one, then the newest key's voice. A repeated press of a sustained key reuses
  it. When Hold is released: sustained voices are freed; a track left empty gets its Note Off, otherwise the freed oscillators are muted.
  The old per-track "gate held open" flags are gone.

## Space / memory
Code 992 bytes at 0x08005334.. (limit 0x080059ec) and 480 bytes at 0x0801f180 (limit 0x0801f400). State is still inside the stock 0x82..0x91
fill (cleared by Shift+Hold). Pool and 18-window scans: no outside reads of replaced words.

## Emulation
`emu_rr_test.py` 126 PASS (adds the lower-second-key case and Hold scenarios; 600 random sessions with 1-4 tracks, Hold and repeated presses
against an updated independent model). Sustain test 26 PASS, seq test 0 FAIL, chord test only the two intended differences.

## Unverified on hardware
Whether the Note Off immediately followed by Note On (the retrigger) is handled cleanly by the Analog Four for two keys within a millisecond.
