# KeyStep 37 (original) 1.1.6.579 — "SEQ position = sequencer notes off" candidate `seqmute-01`

Status: **built and statically/emulator verified. NOT flashed, NOT tested on hardware.**

## 1. Firmware identification

| Item | Value |
|---|---|
| Source file | `keystep37_Firmware_Update_1_1_6_579.led` (from `~/Downloads`, read-only copy in `local/`) |
| Version | 1.1.6.579, original KeyStep 37 (repo profile `official-1.1.6.579`; not Mk2) |
| Stock SHA-256 | `464e2ca2fc5318e6026f15cf3c3a61f6417a80171260543a18e87df116319a8a` |
| Stock checks | 176 records / 176 local sums valid, application residue 0, 154 map identity windows + 32 BL targets PASS |
| Candidate | `local/seqmute-01/ks37-seqmute-01.led` |
| Candidate SHA-256 | `5086cd506cade413adaf543d991ab2d1452077bd41efefb1964ceb42ff5ff7de` |
| Candidate checks | 176/176 local sums valid, application sum `0xc1b3`, residue 0, exact plan diff verified by `ks37 verify --stock --plan` |

## 2. Selector analysis

```
GPIO port 0x40011800 (STM32F1 GPIOE), mask 0x10 (pin 4)      object built at 0x0801525a (r1=port, r2=0x10)
  -> 0x08008804   raw read: (IDR & mask) != 0 -> 0/1
  -> 0x08019064   debounce (7 equal samples), latched at obj+6
  -> 0x08018fe4   main-loop poll (BL at 0x080157d6); on change:
  -> 0x08016968   mode handler(U, state)        (boot: 0x080168e0 stores state directly)
  -> U+0x0f       U = 0x20002ddc, reached through pointer 0x20001124
```

Evidence for the meaning of the two states (functional, from the handler itself):

* state **1** -> `U+0x0f = 1`: current block := one of the recorded slot blocks (table `0x200010fc`,
  indexed by the Mode selector), transposition root loaded, note messages tagged source 2. This is **SEQ**.
* state **0** -> `U+0x0f = 0`: current block := working phrase block (`0x200010f4`), arpeggiator builder
  (`0x20001094`) re-initialised and a phrase rebuild requested, messages tagged source 3. This is **ARP**.

The board silk-screen was not inspected; "SEQ" here means "the position in which stock firmware plays
recorded sequences", which is the definition that matters. If keys are held while transport is active the
handler defers the change (`U+6/U+7`) until all keys are up (`0x08016e96`). On a change while playing it
issues transport action 4 (a re-phase request, not a stop).

## 3. Sequence analysis (ARP and SEQ share all of this)

| Stage | Address | Notes |
|---|---|---|
| Scheduler | `0x080129cc` | called 3x per main loop; `C = 0x20002bec`; `C+4` = running |
| Tick / MIDI clock | `0x080120ec` -> `0x08014350` | sends 0xF8; depends on `C+4` and clock source only; runs *before* any note decision |
| Emission site | BL `0x08012ec0` (and `0x08012028` for external step advance) | -> consumer |
| Step consumer | `0x08013e8c` | `N = 0x20004ed4`; reads block, releases previous notes, emits Note On via `0x0801b6c4`, records pitches in `N+5..N+0xc`, sets `N+0x25` |
| Gate-end release | `0x0801415c` | conditional (tie checks against block data) |
| Unconditional release | `0x08014258` | Note Off for every pitch in `N+5..`, clears `N+0x25/0x26`; used by Stop and Pause |
| Output stage | `0x0801ad20` | per-pitch owner byte; seq and arp tags are treated alike (`& 0x0c`); a Note Off for a pitch with no owner is dropped |
| Transport | `0x08012334` | 0 Stop (0xFC), 1 Start (0xFA), 2 Pause (0xFC), 3 Continue (0xFB), 4 re-phase |

Keyboard path `0x0801b750`: in SEQ with transport playing, stock does **not** send keyboard notes; it
writes the transposition root `0x200000c8` (`0x0801be40`, and `0x0801b8d6` for the chord/mono route),
unless `U+0x11 == 1` (the alternate-channel keyboard mode, left untouched).

## 4. Patch (17 code bytes in 4 edits + recomputed application sum)

```
address:               0x08013eb8
original instruction:  cmp r3,#0 ; beq 0x08013f3e
original bytes:        00 2b 40 d0
replacement:           cbnz r3,0x08013ed0 ; b 0x08013f3e
replacement bytes:     53 b9 40 e0
reason:                r3 = U+0x0f. ARP (0) takes the same path as before. SEQ (1) leaves the consumer
                       before any block read.

address:               0x08013ece
original instruction:  ldr r3,[pc,#0x268] ; ldr r3,[r3] ; ldrb r3,[r3,#0xf] ; cmp r3,#0 ; bne 0x08013f56
original bytes:        9a 4b 1b 68 db 7b 00 2b 3e d1
replacement:           b 0x08013ed8 ; mov r0,r4 ; bl 0x08014258 ; b 0x08013f38
replacement bytes:     03 e0 20 46 00 f0 c1 f9 2f e0
reason:                The second U+0x0f test is redundant once SEQ exits earlier (ARP always took the
                       fall-through to 0x08013ed8, tag 3). Its 8 freed bytes hold the SEQ path: call the
                       native unconditional release for anything still held, then the normal epilogue.

address:               0x0801be40
original instruction:  cmp sl,r7
original bytes:        ba 45
replacement:           b 0x0801be30
replacement bytes:     f6 e7
reason:                SEQ + playing + no record: instead of transposing, use the stopped-state "send note" path.

address:               0x0801b8d6
original instruction:  cmp sl,r7
original bytes:        ba 45
replacement:           b 0x0801b92a
replacement bytes:     28 e0
reason:                Same change for the chord/mono keyboard route.

address:               0x0802fffe   (application sum, recomputed by the repo patcher)
original bytes:        95 c4      replacement bytes: b3 c1
```

Record local sums are recomputed by `ks37 patch`. No code is added, no record is populated, no RAM is used.
`0x08013f56..0x08013f5b` becomes unreachable and is left untouched.

## 5. Safety analysis

* **ARP** — every changed instruction sits on a path that is only taken with `U+0x0f != 0`, except
  `0x08013eb8/0x08013ece`, where the ARP flow is re-encoded to reach the same targets. Emulation: 3000
  randomised consumer/release/cleanup runs with `U+0x0f = 0`, stock vs candidate, identical call traces
  and identical full RAM.
* **Play / Stop / Continue, MIDI Start/Stop/Continue, MIDI Clock** — scheduler, tick/clock helper,
  transport actions, clock emitter, button router, MIDI in/out and IRQ handlers are byte-identical
  (hashes in `patched-windows.txt`). The scheduler still runs every step in SEQ; only the consumer's
  note side effect is gone. Incoming external Start/Stop paths were not traced individually; they are
  covered by byte identity, not by a separate trace.
* **USB / DIN** — transport and output code untouched; the note router is called exactly as in the
  stopped state.
* **Keyboard** — in SEQ while playing it now takes the same code path as SEQ while stopped
  (emulation: identical MIDI Note On/Off). Other states compared equal to stock: SEQ stopped, paused,
  alternate-channel mode, step record, real-time record, chord route, and all ARP cases.
* **Sequence notes only in SEQ suppressed** — the SEQ exit is before the block is read, so no Note On
  and no pitch bookkeeping (`N+5..`, `N+0x25`) can occur; with nothing recorded, later release calls
  have nothing to send.
* **Stuck notes** — anything still held when SEQ is entered (e.g. an arp note at the moment of the
  switch) is released by `0x08014258` on the first SEQ step, regardless of tie flags in the slot data
  (emulated for normal/tie/rest steps, both retention modes). Stop and Pause still call the same
  release. The output stage drops Note Offs for pitches nobody owns, so no stray Note Offs.

## 6. Verification performed

| Check | Result | Evidence level |
|---|---|---|
| `python3 -B -m unittest discover -s tests -v` | 49 run, OK (2 skipped) | host |
| `make -C device/tests test` | PASS (31,852,707 + 73,027 assertions) | host |
| `python3 -B map/render.py --check` | PASS | host |
| `python3 -B tools/check_publication.py` | PASS | host |
| `python3 -B map/check.py --firmware <stock>` | PASS | static identity |
| `ks37 patch` dry run + `--output`, `ks37 verify --stock --plan` | PASS | container + exact diff |
| Disassembly of patched windows, branch targets, no inbound branches/literals | PASS (linear disassembly) | static |
| `emu_test.py` (Unicorn, stock vs candidate) | 55 checks, ALL PASS | emulator, synthetic RAM, stubbed outputs |

Not performed: any device test; full scheduler/IRQ emulation; `device/build.py` (that builds the unrelated
GEN mode and needs `ld.lld`, which is not installed). There is no ELF: the change is an in-place edit of
existing instructions, so nothing is compiled or linked. `ks37-seqmute-01.app.bin` is the flat patched image.

## 7. Known limits

* **Requires physical KeyStep validation.** The only device-observed use of this patch-and-checksum
  route in the repository is a one-byte colour change.
* In SEQ the keyboard no longer transposes while playing (it plays notes instead).
* CV/Gate pitch output still does not follow the keyboard while transport runs in SEQ (stock tail at
  `0x0801c16a` unchanged); MIDI does.
* Recording still works in SEQ, but recorded sequences are silent there.
* "No other branch lands in the patched windows" rests on a linear disassembly.

## 8. Hardware acceptance test (MIDI monitor on USB and DIN)

* **A — ARP:** switch to ARP, hold a chord, Play: arp notes appear; Stop: notes end, 0xFC seen; Play
  again: 0xFA and 0xF8 stream; pause/continue: 0xFC/0xFB.
* **B — SEQ:** select a slot that contains a recorded sequence. Play: 0xFA and continuous 0xF8, **no**
  note messages. Play keys: normal Note On/Off on the user channel. Pause/continue and Stop: 0xFC/0xFB/0xFC
  as stock.
* **C — running transition:** start in SEQ (no notes), flip to ARP with keys released, hold a chord:
  normal arp. Flip back to SEQ while the arp is sounding (Hold on): the sounding note ends within one step
  and nothing further is generated.
* **D — cleanup:** repeat C several times at different tempos, including a slot full of tied notes; hold
  keys across Play/Stop; Stop, restart; confirm Note On count equals Note Off count in the monitor.

Rollback: flash the stock image above with MIDI Control Center.
