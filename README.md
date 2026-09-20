# KeyStep 37 Research

Independent firmware research for the **original Arturia KeyStep 37**, version
**1.1.6.579**. An address map, local analysis and patching tools, and the complete
source of an additional hardware generator mode. Other KeyStep models and firmware revisions are
not compatible targets.

No firmware images, vendor binaries, device backups or full disassemblies are
distributed. Obtain the original firmware from
[Arturia's product downloads](https://www.arturia.com/support/downloads-manuals/product/keystep-37).

## What you can do

| Goal | Available result | Start here |
|---|---|---|
| Understand the native firmware | Checked addresses, call relationships, sequence format and input/output paths | [Architecture](docs/architecture.md), [map](map/index.md) |
| Make a small data change | A guarded patch recipe, both checksum layers and exact diff verification | [Color example](docs/local-workflow.md#minimal-worked-patch) |
| Build the additional GEN mode | Full C/Thumb sources and local image construction; basic operation observed on one device | [Setup](docs/getting-started.md), [device build](device/README.md) |
| Change GEN behavior | Edit its C algorithms, defaults, controls or display and rebuild the existing integration | [First GEN change](docs/first-gen-change.md) |
| Add reverse/ping-pong to the native sequencer | Research route and traversal/release contracts; implementation still required | [Sequencer](docs/sequencer.md) |
| Change the native arpeggiator | Recovered mode behavior and concrete investigation targets; proposed changes are not implemented | [Arpeggiator](docs/arpeggiator.md) |

GEN provides independent rhythm, motif and accent cycles, eight development
operators, Chord note capture, Scale controls, Commit/Reset, native transport,
and display/key LED integration. Its algorithms can be edited without creating
another set of native hooks. New native hooks require a separate integration
review; this is not a general firmware plugin loader.

## First run

Follow [setup and expected results](docs/getting-started.md) for the compiler,
linker and local image. Python tools use only the standard library. From the
repository root, these checks need no firmware or connected keyboard:

```sh
python3 -B -m unittest discover -s tests -v
make -C device/tests test
python3 -B map/render.py --check
python3 -B tools/check_publication.py
```

Then create `local/` and put your own unmodified 1.1.6.579 image there:

```sh
mkdir -p local
export KS37_STOCK=local/keystep37_Firmware_Update_1_1_6_579.led
python3 -B -m ks37 inspect "$KS37_STOCK"
python3 -B -m ks37 verify "$KS37_STOCK"
python3 -B map/check.py --firmware "$KS37_STOCK"
python3 -B device/build.py --stock "$KS37_STOCK"
```

The last command needs Clang with the ARM target and `ld.lld`. It compiles the
additional mode, checks placement and hooks, and constructs the modified image
**in memory**. Writing a new local image requires `--output`. See the
[device build and controls](device/README.md). The tools do not download firmware,
open MIDI/USB ports or flash the device.

For a first independent change, follow the
[major-to-minor startup pool example](docs/first-gen-change.md), then choose a
specific algorithm or native behavior to investigate.

## Contents

| Path | Purpose |
|---|---|
| [map/index.md](map/index.md) | Curated address index, evidence and conditional call relationships |
| [map/firmware.json](map/firmware.json) | Machine-readable research facts and evidence hashes |
| [docs/architecture.md](docs/architecture.md) | Startup, input, sequence, arpeggiator and output boundaries |
| [docs/sequencer.md](docs/sequencer.md) | Route for adding native reverse/ping-pong playback; not implemented |
| [docs/arpeggiator.md](docs/arpeggiator.md) | Existing traversal modes and routes for changing native behavior |
| [device/](device/README.md) | Full additional mode: C/Thumb sources, placement, hooks, build and independent verification |
| [docs/new-mode.md](docs/new-mode.md) | Integration boundaries for adding another mode |
| [ks37/](ks37/) | Container inspection, validation, guarded local patch planning and packing |
| [docs/research-method.md](docs/research-method.md) | Evidence levels, address conventions and validation procedure |

## Limits

The map is partial; recovered names are provisional. Static findings, host tests
and hardware observations are distinct evidence. The color modification was
observed on one device with a successful return to official firmware; this is
not a general recovery guarantee. The generator build has run on a device and
basic operation was acknowledged; extended controls, stress testing and musical
acceptance remain incomplete. A checksum match does not establish boot acceptance
or stability of a changed build.

See [contribution rules](CONTRIBUTING.md) and [provenance](docs/provenance.md).
Original repository code and documentation use the [MIT license](LICENSE).
Vendor firmware is not part of this repository.
