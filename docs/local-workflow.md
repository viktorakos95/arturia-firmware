# Local analysis and modification

Install tools using [setup](getting-started.md). This route covers inspection
and small data patches; the [GEN route](../device/README.md) builds added code.

Target: original KeyStep 37, official **1.1.6.579**, ASCII `.led` SHA-256:

```text
464e2ca2fc5318e6026f15cf3c3a61f6417a80171260543a18e87df116319a8a
```

The tools intentionally reject another image as a patch base. Keep the original
read-only and retain an independent backup outside this repository.

## Read-only checks

```sh
mkdir -p local
export KS37_STOCK=local/keystep37_Firmware_Update_1_1_6_579.led
python3 -B -m ks37 inspect "$KS37_STOCK"
python3 -B -m ks37 verify "$KS37_STOCK"
python3 -B map/check.py --firmware "$KS37_STOCK"
```

The map check compares address-window hashes with the local image. Matching
bytes do not prove an interpretation of those bytes. Follow the conditions and
unresolved questions in the map before selecting a hook.

For local Thumb disassembly, optionally install
[Capstone](https://www.capstone-engine.org/lang_python.html) in a virtual environment:

```sh
python3 -m venv .venv
.venv/bin/python -m pip install 'capstone>=5,<6'
.venv/bin/python tools/disassemble.py "$KS37_STOCK" --start 0x08013e8c --size 64
```

This linear window may contain data or start inside another code region. It
does not recover function boundaries, literal pools, jump tables or indirect
calls automatically. Do not submit the output as a repository artifact.

## Minimal worked patch

The one-byte data change at flash `0x0801e0e8`, `10` to `00`, changes one channel
of the first CC bank color. It is a compact example of the complete integrity
path, not a general RGB API. The device experiment observed white to turquoise.
The recipe is [examples/color02.json](../examples/color02.json): `02` identifies
the second color experiment, not CC bank 2. Its target is CC bank 0.

```sh
python3 -B -m ks37 patch "$KS37_STOCK" examples/color02.json
# Explicit local output; existing files are not overwritten:
python3 -B -m ks37 patch "$KS37_STOCK" examples/color02.json --output local/color-example.led
python3 -B -m ks37 verify local/color-example.led --stock "$KS37_STOCK" --plan examples/color02.json
```

The tool updates the changed record's local sum, the application sum and the
last record's local sum. The expected result differs in exactly four decoded
bytes. `patch-color` is a shortcut for this same fixed change. Generating this
file does not send it to hardware. This is separate from GEN; do not use the
color-patched file as input for the GEN builder.

## A custom patch plan

Copy the recipe and replace its edit only after identifying the target bytes.
Keep the pinned source hash; specify the target `flash_address` and equal-length
`expected` and `replacement` bytes.

```sh
cp examples/color02.json local/plan.json
# Edit local/plan.json, then inspect the in-memory result first.
python3 -B -m ks37 patch "$KS37_STOCK" local/plan.json
python3 -B -m ks37 patch "$KS37_STOCK" local/plan.json --output local/modified.led
python3 -B -m ks37 verify local/modified.led --stock "$KS37_STOCK" --plan local/plan.json
```

Unknown versions, mismatched anchors, overlapping changes, absent payloads and
direct writes to the application checksum are rejected. This small patcher does
not allocate code, grow records, relocate functions or assemble branch veneers.
A general firmware extension needs a linker and a separate placement verifier.
The [complete generator example](../device/README.md) supplies these for an
additional hardware mode, including RAM reservation and native hook checks.

## Integrity model

Each decoded record stores a local negative byte sum in a little-endian 16-bit
field. The known application sum covers address fields `[0x004000,0x02fffe)`;
its LE16 value is at `[0x02fffe,0x030000)`. Summation represents records without
payload as `FF`. This calculation agrees with the examined images and the
successful minimal experiment; it is not a physical readback of absent records.

An earlier color experiment omitted the application sum: its local sums passed,
but normal boot failed. A corrected experiment booted and the official image
was restored. This supports checking both layers; the bootloader reader itself
was not recovered. Do not interpret these sums as cryptographic signatures.

## Device experiment boundary

Before a device experiment, prepare recovery, back up settings and sequences with
the official tool, stop playback and disconnect other MIDI clients.
Independently verify the actual output file and its complete diff.
Use the official MIDI Control Center update workflow documented by
[Arturia](https://support.arturia.com/hc/en-us/articles/4405748057618-KeyStep-37-General-Questions).
Follow its physical reconnection prompts; do not infer success from file selection.

Record transfer result, normal boot, ordinary keys and the specific modified
behavior separately. Test Stop/Pause/restart, note release, original modes and
mode exit. If testing a minimal probe, return to the verified official image
and confirm normal operation. A previous successful recovery does not cover
every failure. No recovery mechanism or automatic flasher is supplied here.

Publish only a concise observation with hashes, conditions and expected/actual
behavior. Keep generated images, listings and captures local.
