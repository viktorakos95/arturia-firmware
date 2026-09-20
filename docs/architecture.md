# Selected architecture

Target: original KeyStep 37, application 1.1.6.579. All names below are
provisional. The [map](../map/index.md) records static evidence and open work;
it is not a complete MCU, memory or control-flow description.

## Address conventions

| Value | Role |
|---|---|
| `0x08013e8c` | Even instruction entry for step playback |
| `0x08013e8d` | Thumb function pointer for that entry |
| `0x20004ed4` | RAM consumer passed as an argument |
| `0x20002c4c` | Separate RAM state object referenced by the consumer |
| `0x0801e9a8` | Flash vtable of a different, MIDI transport object |

A code label inside a function is not a callable function. A global containing
a pointer is not the pointee. The vector's initial SP does not establish free
RAM or a safe stack budget.

## Input and execution contexts

USB packet decoding at `0x08010638` writes a byte ring. The main-loop parser at
`0x0800fa64` processes at most 50 bytes per invocation. Serial input can call
`0x0800fd5c` from IRQ `0x08018624`; not all note input is main-loop work.

At `0x08010310`, the parsed MIDI path still distinguishes USB/DIN before
channel filtering. Common note processing at `0x0801b750` receives `r2=0` from
the physical scanner and `r2=1` from either MIDI port. The tracker at
`0x20001ebc` merges pitches across ports and channels. Releasing pitch 60 from
one owner can clear its slot while another owner still holds pitch 60.

A mode transition therefore needs source/channel/pitch history before merging,
known startup/lifecycle state, and explicit loss handling. A selected pitch of
`-1`, parser return, or one quiet snapshot cannot prove global idle. USB and
serial copy rings and the clock queue have separate lifetimes. Running-status
parser state 2 can follow a completed message; resetting the parser to obtain
an artificial idle state is not a valid admission policy.

## Sequence data and publication

`S=0x20002c4c`: `S+0` is current-block pointer; `S+4` is pending-block pointer.
Native slots are `0x408` bytes each: 64 steps × 8 voices × 2 bytes, plus header.
The [sequencer document](sequencer.md) describes the interpreted fields.

`C=0x20002bec` schedules playback through `0x080129cc`.
`N=0x20004ed4` consumes a step through `0x08013e8c` and retains actual output
pitches for release. The BL at `0x08012ec0` is an observable emission site, not
an unconditional next-bar boundary.

Three competing paths matter:

| Path | Effect |
|---|---|
| `0x08012b62 → 0x08011a1c` | Rebuilds voice 0 in the current block in place |
| `0x08012bd6 → 0x08013028` | Replaces current pointer with pending pointer |
| `0x0801415c` | Reads current-block retention metadata; releases saved output pitches |

Swing is read before rebuild/pointer publication; length and gate are read
later. A pointer swap alone can expose mixed timing metadata. Pending requests
may apply immediately when their target already matches. Length publication
also involves cached length and timer period. Native Time Div reads absolute
phase at two points where IRQ can intervene; swing can leave a target alive
across an emitted step. Preserve these distinctions in boundary detection.

Stop/reset and Pause both disable emission, but differ in cleanup and paused
state. Prepare data outside a short critical section; recheck admission and
publish all dependent state under one explicit ownership protocol. Restoring
interrupts between cleanup and first publication breaks that transaction.

## Controls and feedback

Raw control boundary `0x08004930` precedes page routing. A raw change such as
100→101 is meaningful even when `raw >> 1` remains unchanged. Preserve the
native store/side effects and Scale pickup behavior when intercepting controls.

The native button router is reachable from a guarded external-transport IRQ
path. Suppression must cover both native press and release ownership, not only
a main-loop callback. The extension gesture used in the example work is Chord
held first, then Record, without Shift; rejected admission must not fall
through into native Record.

Key LED writer `0x0800d26e` edits the live DMA source. Refresh
`0x0800d1f8` can occur before later writers. A main-loop overlay or DMA-ready
flag does not establish frame atomicity or exclusive ownership. Preserve
unknown native writers until a complete reader/writer protocol is established.
