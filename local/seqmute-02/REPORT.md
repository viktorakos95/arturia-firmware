# seqmute-02 = seqmute-01 + Chord button swap (original KeyStep 37, 1.1.6.579)

Candidate: `keystep37_Firmware_Update_1_1_6_579_seqmute02.led`
SHA-256 `bb526ad9c056497a2ba5fe9afb1591e4f0b3f86fc2348cc086c486bade6f11fc`
Built from pinned stock with `ks37 patch` (plan.json, 7 edits); `ks37 verify --stock --plan` PASS
(176/176 record sums, application residue 0). seqmute-01 was confirmed working on the device by the user;
seqmute-02 is emulator-checked only until flashed.

The four seqmute-01 edits are unchanged (see ../seqmute-01/REPORT.md). Added edits, all inside the Chord
release handler of the button router (`0x08017260`, button id 8, release case `0x08017d98`):

| Address | Original bytes | Replacement bytes | Replacement instructions |
|---|---|---|---|
| `0x08017d9c` | `46 4b 1b 78 33 b3` | `28 e0 20 46 00 bf` | `b 0x08017df0 ; mov r0,r4 ; nop` |
| `0x08017df0` | `35 4b 1d 78 03 7b 02 2b 21 d0 00 2d 7f f4 87 ae 63 7d 02 2b 1e d0 01 2b 37 d0 00 2b e4 d1` | `31 4b 1b 78 3b b9 34 4b 1d 78 03 7b 02 2b 1e d0 00 2d cc d0 83 e6 63 7d 01 2b 36 d0 e4 d8` | Shift test first; Shift -> page dispatch; no Shift -> stock capture check, then CC-bank path |
| `0x08017e42` | `da e7` | `dd e7` | `b 0x08017e00` (return from chord finalize to the moved capture test) |

Stock: plain release toggles chord page <-> CC page (`U+0x15` 0/1); Shift release steps the CC bank
(`U+0x16`, 0..3) or, from the chord page, goes to the CC page.
New: plain release takes the bank path; Shift release takes the page toggle. Hold Chord + play notes
(chord capture, flag `0x200010e4`) is still finalized on plain release and does not change the bank.
Chord *press* handling is untouched, so holding Shift+Chord for 2 s still enters the third knob page
(`U+0x15 = 2`, timer `U+0x18`); Shift+Chord press leaves it as in stock. One difference: a plain tap on
that third page now does nothing (stock returned to the CC page).

Emulation (`emu_chord_test.py`, stock vs candidate, external calls stubbed): 10 checks PASS, including
684 other button events identical to stock. `emu_seq_test.py` (the seqmute-01 checks rerun on this
image): 55 PASS.
