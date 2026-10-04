# seqmute-03 = seqmute-02 + Hold button as internal note sustain (original KeyStep 37, 1.1.6.579)

Candidate: `keystep37_Firmware_Update_1_1_6_579_seqmute03.led`
SHA-256 `1c2812872d87173fc3a16a1e52d2ac22bce29c7026aa6a5884e6f330973c6fb1`
Built from pinned stock with `ks37 patch` (plan.json, 14 edits); `ks37 verify --stock --plan` PASS
(176/176 record sums, application residue 0). Emulator-checked only; not yet run on the device.
`BROKEN-attempt1-do-not-flash.bin` is a first build with four hook sites 2 bytes early; never flash it.

## Stock behaviour found
Hold object `*0x2000115c`: `+5` = Hold state, `+6` = arp latch flag. Button press `0x08014a8e` toggles;
depending on the MCC mode byte (settings `+0xab`: bit0 arp latch, bit1 CC64) it latches the arp and/or
sends CC64. Stock has no internal note sustain. Keyboard notes sent directly carry flag bit 0x02 in the
per-pitch array at note object `0x20001eb8 + 0x496`; arp-fed notes carry 0x08 instead.

## Change
Added 236 bytes of code at `0x0801f180..0x0801f26b`, inside the 0xFF padding after `.data` in the last
payload record (record layout unchanged), source `sustain.S`. Hook sites (each 4 bytes):

| Address | Original | Replacement | Purpose |
|---|---|---|---|
| `0x0801bfa8` | `13 f0 02 0f` (`tst.w r3,#2`) | `03 f0 ea b8` (`b.w 0x0801f180`) | keyboard Note Off: if Hold is on and the note came from the physical keys, withhold it and mark flag bit 0x04 |
| `0x0801b75e` | `05 1d 28 46` (`adds r5,r0,#4 ; mov r0,r5`) | `03 f0 25 fd` (`bl 0x0801f1ac`) | note entry: a new press of a withheld pitch first sends its Note Off (retrigger) |
| `0x08014afa` | `ff f7 56 ff` (`bl 0x080149aa`) | `0a f0 a4 fb` (`bl 0x0801f246`) | Hold button toggled off: stock clear, then release all withheld notes |
| `0x08014b52` | `ff f7 2a ff` | `0a f0 78 fb` (`bl 0x0801f246`) | Hold button momentary release: same |
| `0x08014a2c` | `ff f7 bd ff` | `0a f0 0d fc` (`bl 0x0801f24a`) | pedal off: stock clear, release only if Hold state is now 0 |
| `0x08014a84` | `ff f7 91 ff` | `0a f0 e1 fb` (`bl 0x0801f24a`) | pedal momentary release: same |

"Arp engaged" needs no extra test: while the arp runs, keys feed the arp (flag 0x08) and never reach the
withheld path, so Hold latches the arp exactly as stock.

## Limits
* Sustain covers notes from the physical keys sent as ordinary notes. Not covered: chord-mode/mono-route
  notes, notes arriving on MIDI in, CV/Gate (gate still closes on key release).
* If the Hold button is set in MCC to also send sustain CC64, that still happens as in stock.
* Withheld notes are released on the keyboard channel current at release time.

## Emulation
`emu_sustain_test.py` 26 PASS (withhold, release on Hold off, key still down kept, retrigger, arp
unchanged vs stock, MIDI-in unaffected, alternate channel, pedal, real Hold button function in all four
mode settings). `emu_seq_test.py` 55 PASS and `emu_chord_test.py` 10 PASS on this image.
Comparisons record only real call arguments; two earlier mismatches were dead scratch registers.
