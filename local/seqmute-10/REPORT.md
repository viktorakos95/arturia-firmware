# seqmute-10 = seqmute-09 + 8 voices (4 tracks x 2 oscillators) + lower note osc 1 / higher note osc 2 (original KeyStep 37, 1.1.6.579)

Candidate: `keystep37_Firmware_Update_1_1_6_579_seqmute10.led`
SHA-256 `b94043b6487276255b2a47453ef0bacfa6c0330ea3722c13dba7ab4808a844e6`
`ks37 verify --stock --plan`: 176/176 record sums, application residue 0. **Emulator-checked only.** Replaces seqmute-09.
Build: `clang --target=thumbv7m-none-eabi -c rr.S`, `ld.lld -Ttext=0x08004000 --no-rosegment -e 0`, `llvm-objcopy -O binary` to rr.bin, then
`build_plan.py` (reads /tmp/s10.elf) and `ks37 patch`.

## Behaviour (Shift+Hold on/off; knob 4 on the chord page = tracks used 1..4, default 1)
* Voice v = 2 * track + oscillator; track t uses keyboard channel + t (A4 default: tracks 1-4 on channels 1-4 when the keyboard sends on 1).
  The same sound must be on every track in use, with tracking off on both oscillators.
* A key takes the first free voice (T1 osc1, T1 osc2, T2 osc1, ...); with every voice busy it takes the newest key's voice, so older
  held keys keep sounding. A repeated press of a key that is still counted reuses its voice.
* Within a track the lower note is always oscillator 1 and the higher one oscillator 2 (pitch CCs of both are re-sent when a second
  key joins). A key alone in its track mutes that track's other oscillator.
* Per track one gate note, retriggered (Note Off, Note On) by every key that goes into that track, closed when the track's last key is
  up. Other tracks are not retriggered. With Hold on, a track stays open until Hold is released.
* Held-key state is the voice slots themselves (no separate counter, so no drift when a release is lost).
* Shift+Hold off: open gates closed, tune 0 (MSB 64, LSB 0) and level 127 sent to every voice in use.
* Knobs: 1 scale (100..354 units per semitone), 2 base offset (-12..+12), 3 fine (-99..+99), 4 tracks (1..4). Bytes 0 = default.

## Memory / space
Chord object 0x20001e04: +9 (bit7 on, bits0-1 = 3 - (tracks - 1)), +0x48/+0xb2/+0xb3 tuning, +0x82..0x89 voice slots, +0x8a..0x8d gates,
+0x8e Hold flags, +0x8f newest voice. The stock constructor fills 0x82..0x91 with 0x10 (only the unreachable chord engine uses it);
turning on clears it, and the first turn-on closes no gates (they would be the stock fill). Code: 828 bytes at 0x08005334.. (after
the literal pool the CC pages read; check_pools.py, 18 windows scanned) and 492 bytes at 0x0801f180 (limit 0x0801f400).

## Emulation
`emu_rr_test.py` 121 PASS: off == seqmute-03; hand-checked 1-track and 4-track sequences; pitch maths; 600 random sessions with 1-4 tracks,
Hold and repeated presses against an independent model, message for message; Shift+Hold on/off through the router table (including
the stock 0x10 fill); knobs 1-4; formatters; pool checks. Sustain 26 PASS, seq 0 FAIL, chord test only the two intended differences.
Found and fixed by the harness: the first turn-on sent four Note Offs for the stock fill.

## Unverified on hardware
How the Analog Four treats Note Off + Note On back to back, the CC to pitch scale/centre, and the poly/voice behaviour with four tracks.
