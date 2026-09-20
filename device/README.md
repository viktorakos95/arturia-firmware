# GEN device implementation

This directory contains the complete V4 generator and hardware integration
used for the GEN14 build on original KeyStep 37 firmware 1.1.6.579. It includes
startup/RAM initialization, physical controls and gestures, input history,
native playback publication, transport, display and key LEDs. The native
sequencer/arpeggiator remain the underlying firmware; this adds a separate GEN
mode.

`src/` contains 67 files copied without modification from the frozen build:
31 C files, 23 headers and 13 assembly files. [source-manifest.json](source-manifest.json)
records their SHA-256 values. Vendor firmware is supplied locally at build time.

The reference build was installed on one device. Normal keys and cold GEN
entry received a basic user acknowledgement. Complete control acceptance,
extended stability, latency, external-clock behavior and musical evaluation
remain incomplete. The source and reproducible build are included; those
remaining checks are not implied by compilation or host tests.

## Build and independently verify

Start with [tool installation and expected results](../docs/getting-started.md).
Use Python 3.10+, Clang with the `arm-none-eabi` target and LLVM LLD. The
compiled profile is Cortex-M3/Thumb with `-Oz`. A compiler
that can build the host tests may still lack the required ARM target. Pass
`--clang /path/to/clang --ld /path/to/ld.lld` when they are not on PATH.

From the repository root:

```sh
# Run without stock firmware or device access.
make -C device/tests test

# Compile/link the full device code without a vendor image.
python3 device/build.py --link-only

# Build against the local stock image; no candidate output requested.
python3 device/build.py --stock "$KS37_STOCK" --build-dir local/build-gen-01

# Explicitly create a NEW local output file.
python3 device/build.py --stock "$KS37_STOCK" \
  --build-dir local/build-gen-01 --output local/gen-01.led

# Read-only check of the actual output against stock and the compiled ELF.
python3 device/verify.py --stock "$KS37_STOCK" \
  --candidate local/gen-01.led --elf local/build-gen-01/generator.elf \
  --report local/build-gen-01/build-report.json
```

Keep the image, ELF and report from the same build together. Use a new output
name and build directory for each subsequent experiment. To make a first source
edit, follow the [startup note-pool example](../docs/first-gen-change.md).

The builder uses [profile.json](profile.json), [placement.ld](placement.ld) and
the exact source module order. Input identity, original hook locations,
relocations, code/RAM placement and both integrity levels are checked. The
verifier compares the actual candidate to the pinned stock image and compiled
ELF, including allowed changes, inserted code, padding and checksums.
The builder's `baseline_candidate_matches` and the independent verifier's
`matches_observed_baseline` distinguish exact reference reproduction from a
new local build. Passing either checker does not test music or physical controls.

Reference output SHA-256:
`45a4face4e130b304e87526e7b7e6c4bae64b88c48c6e4c4c9754fa5815fdd54`.
It contains 327,520 ASCII bytes, 46,063 inserted code bytes, 61 patch locations,
602 retained relocations and 176 records with local checksums. The whole-image
sum is zero under the supported profile. The runtime is 10,248 bytes; reserved
RAM is 10,328 bytes at `[0x20005eb0,0x20008708)`, including header/guards.
Reference toolchain: Apple Clang 17.0.0 and LLD 20.1.7. The builder records the
actual compiler/linker versions and flags in its local build report.
The 4,096-byte stack margin is a placement policy, not a measured maximum-stack
or free-RAM guarantee. Different compiler versions may produce different code;
an unexplained binary difference requires investigation before a device test.

Build outputs are local artifacts. Neither command uploads firmware. Keep
sequences/settings backed up and retain the exact stock image used as input.
A hardware test uses the resulting checked file through the normal updater,
followed by ordinary boot and the control checks below; selecting a file or
observing transfer completion alone is not runtime acceptance.

## First audible check

Use this after the [device experiment procedure](../docs/local-workflow.md#device-experiment-boundary)
with an unchanged GEN build. Connect MIDI output to a synthesizer or a DAW
instrument; the keyboard does not generate audio itself.

1. Power-cycle, confirm ordinary keys work, select SEQ and Internal clock,
   set Time Div to 1/16 and leave playback stopped.
2. Hold Chord first, press Record, then release both. Expect the brief `GEn`
   indication followed by the default operator label `rPt`. Record any `E##`
   refusal instead of treating it as successful entry.
3. Hold Chord, play MIDI notes 60/64/67, release the keys, then release Chord.
   This replaces the allowed note set. Do not turn the parameter knobs yet.
4. Press Play. The defaults produce five attacks per 32-step rhythm cycle,
   using pitches from that set. At E00/O00 no development or octave expansion
   is applied. The seven-event motif and three-event accent cycle continue
   between rhythm cycles, so the complete note/velocity pattern need not repeat
   every 32 steps.
5. Press ordinary Stop. Check that all generated notes are released. While
   stopped, use Chord then Record again to leave GEN; confirm native controls
   and ordinary keys work.

For knob checks, use the table below. Each Scale page first establishes a
physical baseline, then scales further movement; reversing direction can
produce a plateau before the value changes. Record the observed MIDI events
and control results; this short check does not cover the full acceptance list
at the end of this document.

## Controls

Enter or leave GEN while stopped in SEQ/Internal: hold **Chord first**, press
Record, then release both. No Shift and no timed hold gesture. Separate Record,
short Chord and Chord note entry keep distinct ownership. A refused entry does
not fall through into native Record.

| Control | GEN | Shift held before the action |
|---|---|---|
| Knob 1 | `L`: rhythm length 1–64 steps | `n`: motif length 1–32 events |
| Knob 2 | `d`: attack count 0–L | `A`: velocity-accent depth 0–99 |
| Knob 3 | `r`: rhythm rotation 0–L−1 | `b`: accent period 1–16 events |
| Knob 4 | `E`: development depth 0–99 | `t`: auto interval 1/2/4/8 cycles; 0 disables auto |
| Mode | Select one of eight development operators | Same operator selection |
| Rate / Div | Native tempo / time division | Native handling |
| Octave −/+ | Native keyboard octave | `O`: permitted extra upper octaves 0–3 |
| Record | Attempt one development operation | Commit heard CURRENT to SEED |
| Stop | Native Stop | Reset to SEED and cancel prior queued work |

Record, Commit and Reset execute on release of the command button. Their
press-time assignment survives releasing Shift first. Shift+Stop is Reset;
use ordinary Stop to stop playback. Shift+Octave is rejected during Chord note
capture and retains ownership of the entire rejected press/release pair.

Hold Chord, play the source notes, then release Chord to capture up to 32
unique pitches. They form an allowed pitch set, not a recording of played
rhythm or note order. Capture replaces SEED. `O` extends this set with upper
octaves; it does not transpose the complete phrase. No harmonizer infers a key.

Both knob pages retain independent Scale pickup values, including the native
reverse-movement plateau. Turning Mode selects subsequent development without
recreating the material. The display returns to the Mode label after a brief
parameter display. A dot indicates actual queued work. Sixteen key LEDs show
the current rhythm page and playback cursor.

## Musical state and operators

Defaults: MIDI pitches 60/64/67, `L32 d05 n07 b03 E00 A55 O00 t01`, Mode `rPt`.
Rhythm, pitch motif and accents use separate cycles. Each attack consumes the
next motif and accent event. At E00 the material and RNG remain unchanged,
but event phases continue across complete rhythm cycles. With five attacks,
n=7 and b=3, both phases align again after 21 cycles.

Changing density preserves motif material while changing where its events
land on the grid. Editing n/b preserves hidden motif/accent entries. Rotation
moves the current developed rhythm, including changes made by SYn. Exactly d
attacks remain within L steps. d00 emits no attacks and consumes no RNG.

| Mode label | Development operation |
|---|---|
| `rPt` | Move a short neighboring motif fragment through allowed pitch steps |
| `CYC` | Change start phases; at greater depth may reverse event reading |
| `ArC` | Reverse a phrase fragment or reflect its pitches within range |
| `ACC` | Accumulate a bounded pitch-step offset with cyclic return |
| `AnS` | Preserve the beginning and transform the continuation |
| `brn` | Revisit four related variants of a remembered base |
| `SYn` | Move attacks/reorder accents while preserving exact attack count |
| `rnd` | Select among the development operators |

Automatic development uses E both as depth and an E/99 probability of acting
at each t-cycle opportunity. Manual Record with E>0 bypasses that probability
filter but may still have no available change. E00 preserves material, branch
and accumulator. Commit does not turn auto-development off; set E00 or t00
separately. Wide octave ranges can produce large pitch jumps.

SEED and CURRENT contain parameters, material, event phases, RNG and operator
state. Commit captures the applied, heard CURRENT at command time, even if a
new proposal is pending. Reset discards the previous queue and restores the
saved snapshot. Changes are normally published at a subsequent rhythm boundary
while playing and immediately through the stopped transaction while stopped.
All memory is volatile; power-off loses the snapshot.

A proposed block is built privately. If publication cannot proceed, the prior
heard block repeats and the unplayed proposal does not advance musical state.
Native time-division rephasing can reposition within the current block; it is
not itself a completed-cycle event.

## Integration points

| Modules | Responsibility |
|---|---|
| `generator_core`, `generator_controller`, `generator_gesture` | Material, queue, controls, gesture ownership |
| `boot_ram*`, `runtime`, `device_runtime*` | Early initialization, owned RAM, guards and shared runtime |
| `button_*`, `control_*`, `native_admission*` | Native button tickets, paired releases, raw controls, coherent admission |
| `input_history*`, `input_device*` | USB/DIN/channel/pitch history, physical contacts, cold/loss lifecycle |
| `stock_sequence`, `playback_*`, `generator_time_division*` | Native block encoding, preparation, serialized publication and timing |
| `generator_device_*`, `generator_writer_guards` | Main/IRQ integration, ownership, native writer quarantine |
| `generator_panel*`, `generator_keyled*`, `generator_rate12*` | Display, Chord state, rhythm/cursor projection and native tempo notice |
| `generator_pitch*`, `generator_mode12_hooks` | Owned-pitch routing and native mode side-effect guards |

Preparation runs with interrupts enabled; admission, native Chord cleanup and
first publication share the masked transaction. Hardware callbacks do not
invent an idle/known context. Native cleanup retains the saved output-note
list until releases have been sent. Block pointer, cached length and timer
state must agree before publication. Plain aligned stores are insufficient.

LED integration intercepts the native pixel writer and keeps a native shadow
for restoration. The DMA source remains the native live buffer; this does not
claim frame atomicity or complete alias coverage. Writer quarantine is a
bounded inventory of known native paths. An entry `E##` or runtime `F##` is a
diagnostic refusal/fault; record the code and preceding action, then inspect
the corresponding predicate. Do not remove a guard to make entry succeed.

## Host tests and device acceptance

`make -C device/tests test` compiles the actual V4 sources with ASan/UBSan.
`CC=clang` may be overridden; `SANITIZERS=` disables sanitizers where unsupported.
The tests require no vendor firmware or device. They cover:

- All valid L/d/Mode base geometries, pitch/velocity bounds and exact density.
- Independent event phases, heard-state Commit, Reset cancellation, deterministic
  replay, alias-safe capture and invalid-input rejection.
- Eight knob targets and Scale pages, command release ownership, Mode selection,
  Shift+Octave ownership, capture, display and LED projection.
- Mixed parameter/development/Commit/Reset stress and operator invariants.

The extracted semantic suites pass 31,852,707 core assertions and 73,027
controller/panel/LED assertions. They do not emulate full native playback,
peripheral timing, IRQ stacks or hardware. For a new build, test cold entry,
ordinary keys, every control/page, capture, queued/applied Commit and Reset,
Mode/Div while playing, Pause/Continue, Stop with balanced MIDI releases,
exit to native operation and repeated reboot. Record exact output hash and
conditions separately from host results.
