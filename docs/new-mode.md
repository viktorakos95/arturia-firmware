# Example: adding an independent mode

The [device example](../device/README.md) includes the complete C/Thumb source
of the additional generator mode, its native adapters and a local image builder.
The following boundaries explain how that port is structured and what a different
mode must preserve. No prebuilt firmware is included.

To change the existing mode, start with the [first GEN edit](first-gen-change.md).
Core, control and display changes can reuse the supplied integration. A new
native hook needs its own wrapper, pinned original bytes and independent
verification rules; adding an address to the profile alone is insufficient.

## State and publication

The core keeps saved SEED, applied CURRENT and a private pending projection.
Commit saves CURRENT as it existed at the command; it must not accidentally save
an unheard pending variation. Reset cancels previous queued changes. Rhythm,
motif and accents have separate cycles; changing density preserves the motif
but changes its distribution across grid steps. The Scale control keeps the
reverse-motion plateau until pickup; replacing this with a relative encoder
changes the interaction.

The caller serializes all state access. The musical core has no allocator or
peripheral access. The device adapter owns native pointers and publication.

## Hardware adapter contract

| Boundary | Required behavior |
|---|---|
| Entry gesture | Hold Chord, then press Record, without Shift. Keep separate Record, short Chord and Chord with notes. |
| Rejected entry | Consume the rejected combination consistently; it must not fall through into native Record. |
| Admission | Establish real input history and stopped playback. Unknown state is not idle. |
| Prepare | Validate the native block, clock and note-release state; build an inactive block. |
| Publish | Revalidate, exchange ownership and publish while serialized against all relevant interrupts. |
| Runtime | Pair accepted press/release events, preserve native acknowledgements, and keep native transport ordering. |
| Exit | Release owned notes, restore native pointers and display ownership, then return accepted controls. |

The experimental entry was restricted to stopped SEQ/internal operation.
An adapter must explicitly define whether future versions admit other clock
sources or entry during playback; silence is not evidence that this is safe.

Critical sections must preserve the previous PRIMASK. A helper that requires
entry with interrupts enabled cannot be called from an already masked button
callback. Preparing outside the critical section and publishing inside it needs
revalidation; no interrupt may observe a half-published state. Thumb wrappers
must preserve registers, stack alignment and the live return slot across IRQs.

The native release path can read the current block. Pointer exchange alone does
not retire old notes safely. Stop, Pause, retiming, ties and queued publication
must be tested together. Native Mode/Div changes are not arbitrary safe boundaries.

## Resource and evidence limits

Empty container records are not evidence of free flash. Heap usage estimates do
not prove a free RAM budget, and a current stack-pointer sample is not a stack
high-water measurement. A new device port needs an explicit linker reservation,
startup initialization, branch/literal checks and independent final-file checks.

Display and key LEDs have multiple writers, including IRQ paths. DMA reads a
live source in the investigated implementation. Main-loop ordering does not
prove exclusive ownership or frame atomicity. The supplied LED adapter retains
the native DMA source and does not establish frame atomicity.

Experimental builds have executed a separate generator mode on one device.
That establishes feasibility, not full-device stability. The public host checks
and ARM compilation can run without vendor software. Constructing a complete
modified image requires the exact locally supplied stock file.

For changing native behavior directly, start with the
[sequencer route](sequencer.md) or [arpeggiator route](arpeggiator.md).
