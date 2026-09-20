# Native sequencer modification route

Backward/ping-pong playback of native recorded sequences is **unimplemented in
this repository**. Existing bidirectional arpeggiator builders are described
[separately](arpeggiator.md); they do not establish a native sequence-direction
option. The [GEN device implementation](../device/README.md) adds a separate mode; it
does not implement this native sequence-direction change.

## First inspection

Complete the [read-only setup and optional Capstone installation](local-workflow.md#read-only-checks),
then inspect two small windows from the local stock image:

```sh
.venv/bin/python tools/disassemble.py "$KS37_STOCK" --start 0x08012eb6 --size 14
.venv/bin/python tools/disassemble.py "$KS37_STOCK" --start 0x0801415c --size 64
```

The first window loads the step argument into `r1` and shows the direct BL at
`0x08012ec0` to the step consumer `0x08013e8c`. The second begins the release
helper and its early conditions. Follow both in the [map](../map/index.md):
changing emission order requires tracing note release as well. These windows
are starting points, not complete function listings or a ready patch.

## Data consumed by native playback

| Block offset | Meaning |
|---|---|
| `16*step + 2*voice` | Pitch or marker; 64 steps, up to 8 voices |
| Following byte | Velocity bits 0..6; voice 0 bit 7 participates in retention |
| `0x400` | Length, maximum 64; extension must reject or handle zero explicitly |
| `0x401 / 0x402` | Swing / gate |
| `0x403` | Partially understood flags; preserve unknown bits |
| `0x404` | Retention-source selector |
| `0x405..0x407` | Unknown; preserve |

Pitch `0xff` terminates voices. First pitch `0x81` retains preceding notes;
`0x82` starts no new notes but is not an unconditional Note Off. The consumer
compares transformed pitches for common-note retention and saves output
pitches/channel for later Note Off.

`0x0801415c` checks the passed step when `block[0x404]==1`; otherwise it checks
the following step with wrap. A direction patch to Note On alone leaves this
look-ahead referring to the wrong musical neighbor. Cleanup helper
`0x08014258` sends saved releases without tie checks, but does not clear the
saved retain request. Do not discard active-pitch state instead of releasing it.

## Proposed traversal contract

Choose endpoint behavior explicitly. For storage indices 0..3:

| Policy | Repeating traversal | Period for length L≥2 |
|---|---|---|
| Forward | `0 1 2 3` | L |
| Backward | `3 2 1 0` | L |
| Ping-pong, endpoints once | `0 1 2 3 2 1` | 2L−2 |
| Ping-pong, endpoints twice | `0 1 2 3 3 2 1 0` | 2L |

For L=1, every valid direction resolves to step 0 with a one-step period. For
L=0, never index the block; define stopped/error handling and complete any
owned releases. Reject L>64. These are proposed extension rules, not claims
that malformed native blocks are safe.

Keep the monotonically advancing timing slot separate from the selected
storage step. Reversing pitches does not require reversing the timer. Define
how direction, length, Start, Continue, Pause, Stop and slot changes reset or
preserve the traversal phase. Direction changes should occur at a documented
emission boundary, with the same next-step rule used by tie/gate decisions.

## Investigation and implementation order

1. Verify the supplied firmware and [map](../map/index.md). Trace effective-step
   calculation in `0x080129cc`, BL `0x08012ec0`, and release at `0x0801415c`.
   Record every place that consumes the logical index, storage index or length.
2. Implement a host traversal state machine. Check L=0,1,2,64, both endpoint
   policies, reversal at endpoints, shrink/grow and direction changes. Do not
   infer a new cycle from seeing index 0 in repeated polling calls.
3. Model Note On/Off with repeated pitches, chords, rest/tie markers and both
   retention sources. Track already emitted notes independently of mutable
   sequence data. Decide whether a tie describes arrival at a step or a storage
   relationship when traversing backward.
4. Add an isolated fixed-length experiment: internal clock, swing 50, no live
   retiming, no pending slot change. Intercept a proven boundary and preserve
   native registers, stack alignment and displaced instruction side effects.
   This restricted profile is a development stage, not a final acceptance test.
5. Extend to swing, live Time Div, queued slot changes, recording/append,
   transport/resync and external clock. Check IRQ arrival before/after publication
   and ensure Stop releases each owned note once.

Expanding a phrase into a copied forward/backward block is another design,
but a 64-step source cannot fit a 126/128-step cycle into one native block.
Short-source expansion must still preserve all voices, header semantics and
release look-ahead. An index-mapping adapter avoids that size limit but needs
an explicit direction/phase state and a complete audit of native readers.

Accept a device change only after matching an expected MIDI trace, balanced
Note On/Off ownership, live controls, Pause/Continue/Stop, slot changes, reboot,
and the supported clock profiles. Host/emulator success is a separate result.
