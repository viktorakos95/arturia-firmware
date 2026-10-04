# seqmute-09 = seqmute-08 with the CC-page freeze fixed (original KeyStep 37, 1.1.6.579)

Candidate: `keystep37_Firmware_Update_1_1_6_579_seqmute09.led`
SHA-256 `155135269095c439b27eb02213bbf0d93490603eda7d7aba83252ce925a6e614`
`ks37 verify --stock --plan`: 176/176 record sums, application residue 0. **Emulator-checked only.** Replaces seqmute-06/07/08 (all affected).

## Bug (user report: going to CC banks 1-4 froze the KeyStep, with or without moving a knob)
seqmute-06..08 put the round-robin code over the stock chord-page knob handlers (0x0800527c..). Those handlers are unreachable,
but the literal pool behind the first one (0x0800531c..0x08005333, six words: 0x20001124, 0x20001204, 0x200051cc, 0x20001180,
0x0801cab9, 0xffff8000) is also read by the CC-page code before it (0x08005052, 0x0800506a, 0x08005078, 0x0800509c, 0x080050a6,
0x080050b8, 0x08005250, 0x0800525a). The new code overwrote those words, so the CC pages loaded garbage pointers.
The earlier "nothing else references this area" check scanned branches only, not data reads. That was wrong.

## Fix
The block now starts after the pool, at 0x08005334 (..0x08005610; stock handlers' unreachable head 0x0800527c..0x08005333 stays).
New checks: `check_pools.py` and a test that the six pool words equal stock; a scan over all 18 patched windows finds no
outside pc-relative read into any of them. Behaviour is otherwise identical to seqmute-08.

## Emulation
`emu_rr_test.py` 129 PASS, sustain 26 PASS, seq test 0 FAIL, chord test only the two intended differences.
