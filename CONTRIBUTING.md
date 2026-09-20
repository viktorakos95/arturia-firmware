# Contributing

Keep each change limited to one behavior or one research claim. Run the checks
in [README](README.md) before submitting a pull request.

For a map change, supply the firmware version and SHA-256, instruction address,
control-flow conditions, evidence window hash, and a way to reproduce the check
from a local image. Label static evidence, inference, and device observation
separately. Function names are provisional. Regenerate the map index.

For a behavior change, specify input events and expected output. Test transport
Stop/Pause/restart, length changes, note release, ties, clocks, pending commands,
and mode exit where relevant. A passing host test does not establish device
compatibility. State which device checks were actually performed.

Contributions use MIT. Preserve source attributions and document external
references in [provenance](docs/provenance.md).

Do not upload firmware, patched images, extracted machine code, vendor tools,
manuals, disassembly dumps, device backups, serial numbers, personal paths, or
private captures. Use synthetic test fixtures. Attach a short textual failure
description with hashes and addresses instead of a firmware attachment.
