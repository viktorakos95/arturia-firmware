# Native arpeggiator research route

The native builder already has forward/backward and two bidirectional orders.
The user-visible arpeggiator has a bidirectional mode; exact correspondence of
all internal mode indices to every panel label is not established here. Native
sequencer bidirectionality remains a [separate unimplemented route](sequencer.md).

## First inspection

After the [read-only setup and optional Capstone installation](local-workflow.md#read-only-checks):

```sh
.venv/bin/python tools/disassemble.py "$KS37_STOCK" --start 0x08011a1c --size 32
.venv/bin/python tools/disassemble.py "$KS37_STOCK" --start 0x08011878 --size 32
```

The first window shows pool-size checks, the mode byte at builder offset
`0x10`, and a TBH dispatch. It ends before the branch-table data, which must not
be interpreted as instructions. The second is the entry to the mode-5 walk
helper. Use the [map](../map/index.md) and the behavior below to choose one mode
to model before changing native code.

## Recovered builder behavior

Builder `0x2000063c` uses primary pool `0x20002dfc` and deferred-release pool
`0x20002d0c`. The primary pool stores up to 32 unique pitches with velocity.
Insertion order and ascending-pitch index order are distinct. Duplicate pitch
does not update velocity; the key does not include channel/source.

Let A be ascending-pitch order, P insertion order, N the pool size.
`0x08011a1c` dispatches these modes for 2≤N≤32:

| Internal mode | Prepared voice-0 data | Length |
|---|---|---|
| 0 | A forward | N |
| 1 | A backward | N |
| 2 | A forward, then all of A backward | 2N |
| 3 | A forward, then backward excluding endpoints | 2N−2 |
| 4 | One pseudorandom P element; requests another rebuild | 1 |
| 5 | P in insertion order | N |
| 6 | Random batch with optional octave movement | Requested K, valid range 1..64 |
| 7 | P in insertion order | N |

For A=`60 64 67`, mode 2 builds `60 64 67 67 64 60`; mode 3 builds
`60 64 67 64`. Mode 2 repeats the top within the block and the bottom across
its wrap. Empty input resets to a one-step no-attack phrase. With one note,
length first becomes one; mode 6 additionally builds its requested batch.

Prepared data alone does not determine mode 5 playback. In the reached
`U+0x0f==0` branch, `0x08013e8c` calls `0x08011878` to substitute a storage-order
walk index: back, stay, or advance. Mode 7 does not use that substitution.
The stay branch does not normalize a saved index after pool shrink. PRNG state
is separate from an immutable musical seed; random selection permits repeats.

Deferred-release behavior at `0x080116e0` can retain released notes in the
primary pool. The secondary pool holds removals until a later condition; it
is neither a physical-key ledger nor a saved phrase. Repeated Note On and
multiple ports require careful ownership semantics.

## Candidate changes

These are research proposals, not installed patches:

| Change | Initial investigation | Required checks |
|---|---|---|
| Endpoint/repetition policy | Mode 2/3 builder branches | N=0,1,2,32; cycle seam; hold/release |
| Preserve/alter played order | Modes 5/7, `0x08011878` | Pool removal, stale walk index, repeated pitch |
| Random without immediate repeats or without replacement | `0x08011854`, `0x0801196c` | N=1, bias, deterministic replay, bounded writes |
| Independent pattern length or octave policy | Mode 6 length/octave setters and batch writer | Clamp before writing; pitch bounds; live updates |
| Rhythmic or polyphonic arp | Builder and step consumer together | Voice terminators, rest/tie behavior, gate and Note Off |

Start with a host model of one selected mode and a fixed note pool. Next trace
parameter setters and rebuild request/target lifetimes. Then test mutation of
the pool while holding, releasing and retriggering notes. A rebuild writes
voice 0 into the current block; it does not allocate a staging buffer or clear
all other voices. Publish a hardware adapter only after proving writer
ownership and an emission boundary for that specific change.
