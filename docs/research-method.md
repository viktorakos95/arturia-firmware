# Reproduce and extend the research

Use a local copy of the exact supported firmware. The
repository contains selected facts, hashes, original tools and models; users
supply all vendor images. Keep originals read-only and generated artifacts in
an ignored local directory.

## Evidence levels

| Result | What it establishes |
|---|---|
| Container/hash check | Exact input identity and the checks implemented by the tool |
| Static trace | A field, branch, call or data relationship under stated conditions |
| Host model/test | Behavior of the model, including defined edge cases |
| Target/emulator test | Tested instruction/register behavior within its configured hardware model |
| Device observation | The observed scenario on that device, firmware and configuration |

No row substitutes for another. A short anchor hash proves byte identity, not
the meaning of surrounding code. A successful write dialog does not prove the
application booted. A working prototype does not establish full regression,
maximum stack usage or universal recovery.

## Minimal workflow

1. Record firmware version and whole-file SHA-256. Run the toolkit's profile
   and two-level integrity checks before analysis or modification.
2. Run `python3 map/check.py --firmware "$KS37_STOCK"` and
   `python3 map/render.py --check`. A mismatch stops version-specific analysis;
   do not substitute an approximate address from another release.
3. Choose one semantic question. Trace callers, arguments, conditions, field
   writers and cleanup paths. Separate instructions, literal pools, tables,
   RAM objects, globals and Thumb pointers.
4. Model the proposed behavior using synthetic input. Specify invalid values,
   endpoint policies, ownership and transport boundaries before patching.
5. Review every mutation against concurrent main/IRQ readers and writers.
   Check branch range, relocated literals, register preservation, stack
   alignment and interrupt return behavior for wrappers.
6. Build locally from an immutable input; record patch ranges and output hash.
   Independently compare actual saved output with the expected changes and
   verify local-record and overall integrity. Reparse/repack equality alone is
   insufficient for a modified image.
7. Report device results separately with the exact candidate hash and tested
   conditions. Include failures and limitations; do not publish personal MIDI
   captures, settings backups, generated vendor bytes or disassembly.

The map is a curated index, not a bulk export from an analysis database. Add
small evidence windows and narrow claims. Before extending a `confirmed_static`
claim, verify its complete conditions in the local image. Regenerate derived
map files after edits. Report unknown aliases, mode-dependent behavior and
uncovered clock/transport cases explicitly.

The separate-mode generator work provides integration lessons: independent
source/current state, heard-state Commit, Reset cancellation, guarded mode
entry and native note cleanup. The published [device implementation](../device/README.md) includes the V4
core and complete hardware adapters used for that build. Native sequence
direction and native arpeggiator changes remain separate research routes.
