# First change: GEN startup note pool

This changes the additional GEN mode's initial pitch set from C major to
C minor. It leaves native modes and the existing hook/ownership design intact.
Complete the [unchanged build](getting-started.md) first so a baseline exists.

## Edit one initializer

In [`device/src/device_runtime.c`](../device/src/device_runtime.c), function
`ks37_runtime_init`, change:

```c
const uint8_t notes[]={60,64,67};
```

to:

```c
const uint8_t notes[]={60,63,67};
```

Leave the note count `3`, parameters, seed and other code unchanged. These are
allowed MIDI pitches, not a fixed three-note melody. With the default E00/O00,
GEN draws notes from C/E-flat/G. A later Chord capture replaces this set.
The change becomes visible after initialization following a power cycle;
leaving and re-entering GEN can retain the existing RAM state.

## Check and rebuild

```sh
python3 -B -m unittest discover -s tests -v
make -C device/tests test
python3 -B device/build.py --stock "$KS37_STOCK" --build-dir local/build-minor-01
```

Expect `PASS-in-memory-candidate` and `baseline_candidate_matches: false`.
The local report's `baseline_source_hashes_match` is also false. Those identity
flags are not errors for this intentional edit. Keep the reference hashes in
`device/source-manifest.json` and `device/profile.json` unchanged.

The normal musical host tests cover the core/controller but do not compile
`device_runtime.c`; they cannot prove that this initializer is correct. The
ARM build compiles it and the independent verifier checks the resulting image
structure. Actual startup behavior still needs an initializer test or the
device check below.

To save the candidate, repeat the build with `--output local/minor-01.led`, then
verify it against `local/build-minor-01/generator.elf` and that directory's
`build-report.json`, as shown in [setup](getting-started.md#build-gen-and-keep-matching-artifacts).
Do not pair it with an ELF from the unchanged build.

## Expected device result

After the verified candidate has been installed, power-cycle, enter GEN in
stopped SEQ/Internal, and press Play **without capturing new notes first**.
At the initial E00/O00 settings, generated pitches belong to `{60,63,67}`;
64 should not occur. Check MIDI output as well as listening. Ordinary keyboard
notes are a separate path. Test Stop and exit back to native behavior.

Then capture a different Chord note set and confirm it replaces the default.
Reverting the initializer and rebuilding restores the original startup pool.

## Continue with a larger change

| Desired change | First source to inspect |
|---|---|
| Rhythm, pitch development or accent rules | [generator_core.c](../device/src/generator_core.c) and its [host tests](../device/tests/test_core.c) |
| Knob ranges, pages or command mapping | [generator_controller.c](../device/src/generator_controller.c) and [control tests](../device/tests/test_controls.c) |
| Display or key LED projection | [generator_panel.c](../device/src/generator_panel.c), [generator_keyled.c](../device/src/generator_keyled.c) |
| Native sequencer or arpeggiator behavior | [Sequencer route](sequencer.md), [arpeggiator route](arpeggiator.md) |

The device builder relocates edits within the existing integration. Its 61
native patch points are independently pinned. Adding another hook requires a
reviewed wrapper, profile/linker changes where necessary, and matching independent
rules in `device/verify.py`, plus new behavior/ABI checks. Editing `profile.json`
alone does not turn an uninvestigated native function into a supported hook.
