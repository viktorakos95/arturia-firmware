# seqmute-04 = seqmute-03 + paraphonic keyboard split on the chord knob page (original KeyStep 37, 1.1.6.579)

Candidate: `keystep37_Firmware_Update_1_1_6_579_seqmute04.led`
SHA-256 `5d36339f12d9351bcadbdf6ac48e0042bdef167cc7f009bc4af53c493f53d2bb`
Built from pinned stock with `ks37 patch` (plan.json from `build_plan.py`, source `split.S`); `ks37 verify --stock --plan`:
176/176 record sums, application residue 0. **Emulator-checked only; not yet run on the device.**

## Behaviour
Switch: the chord knob page (Shift+Chord toggles it, as in seqmute-02). On that page, physical keys only:

* Knob 1 = split point, shown as a note name (C0..C7, middle C = C3, range 24..108). Never touched = note 60.
* Knob 2 = gate side: `1` keys at/above the split send CC 17 (value = note number) and no note, so the lower
  keys own the gate; `2` keys below the split send CC 16 and the upper keys play notes; `ALL` upper keys send
  CC 17 and also play, unless a lower key is already sounding.
* Knobs 3 and 4 do nothing on this page (chord parameters are unused).
* Releases of keys that sent only a CC are swallowed.
* The split is **off after power-up** until knob 1 or 2 is turned on the chord page (the page constructor puts
  the keyboard on the chord page at boot, so a page-only switch would have split the keyboard at power-up).
  Leaving the page (CC page) turns it off; returning re-enables it with the remembered settings.
* MIDI-in notes are never split. Hold sustain from seqmute-03 still works for notes that are played.

Also changed: Chord held no longer arms chord capture, and Shift+Hold no longer toggles chord mode
(chord generation is unreachable, so the chord object's bytes can be reused).

## Memory used
No new RAM. Chord object `0x20001e04`: `+0x48` split point (stock: chord knob 4 value, zeroed by the constructor
at `0x0801b04a`), `+9` mode (stock: chord knob 2 value, core `+5`). Only the replaced knob handlers and the
chord engine use them. Added code: 0x0801f180..0x0801f3c0 (576 bytes, padding).

## Hook sites (changes vs seqmute-03)
`0x0800496c` chord-page knob table (knobs 1,2 -> new handlers, 3,4 -> exit); `0x08017856` Chord press
(`movs r3,#1`->`#0`); `0x0801786c` Shift+Hold jump-table entry -> exit; note entry and Note Off hooks and the four
Hold hooks as in seqmute-03 (retargeted to the new block).

## Emulation
`emu_split_test.py`: all PASS (split inactive == seqmute-03 over 72 states x 6 events; modes 1/2/ALL; default 60;
MIDI in; Hold; knobs run through the real table; both display formatters). seqmute-03 sustain test adapted to
this image: 26 PASS. `emu_chord_test.py`: every check passes except "all other button events identical to
stock", whose only differences are the two intended ones (Chord press, Shift+Hold press). `emu_seq_test.py`:
all behaviour checks pass; the keyboard-path RAM comparison differs only in 3 dead stack bytes below SP.

## Unverified
Pitch scale of CC 16/17 on the Analog Four (CC 16/17 = OSC1/OSC2 pitch MSB, with 48/49 as LSB, per the MKII
list; MKI not confirmed here) and how note numbers map to semitones; the on-device display of "Db"-style names.
