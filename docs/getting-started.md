# Setup and first build

Run commands from the repository root after cloning or extracting it. The
examples use a POSIX shell. Native Windows builds have not been validated.
Reading the map and running host tests needs no keyboard or firmware. Hearing
the result requires the original KeyStep 37 and a MIDI synthesizer or DAW.

## Tools

Python 3.10+, Make and Clang are used for the local checks. Building device code
also requires Clang's ARM target and `ld.lld`; a host C compiler alone is not
enough. ASan/UBSan are enabled in the C tests by default.

On Ubuntu/Debian, install missing tools from the distribution packages:

```sh
sudo apt-get update
sudo apt-get install python3 python3-venv make clang lld
```

[LLVM's package site](https://apt.llvm.org/) documents versioned packages if
your distribution does not provide the required tools.

On macOS, install Xcode Command Line Tools if absent (`xcode-select --install`).
With Homebrew already installed:

```sh
brew install python llvm lld
export PATH="$(brew --prefix llvm)/bin:$(brew --prefix lld)/bin:$PATH"
```

Homebrew supplies [Clang in llvm](https://formulae.brew.sh/formula/llvm) and
[LLD separately](https://formulae.brew.sh/formula/lld). These commands obtain
tools, not the exact historical reference toolchain. Record their versions:

```sh
python3 --version
clang --version
ld.lld --version
make --version
```

Alternatively pass `--clang /path/to/clang --ld /path/to/ld.lld` on **each**
`device/build.py` invocation. Host tests accept
`make -C device/tests test CC=/path/to/clang`. These settings are independent.

## Check without firmware

```sh
python3 -B -m unittest discover -s tests -v
make -C device/tests test
python3 -B map/render.py --check
python3 -B tools/check_publication.py
python3 -B device/build.py --link-only
```

Expect Python `OK`, both C programs reporting `PASS`, and the builder reporting
`PASS-link-only` with `firmware_file_written: false`. Tests needing external
firmware files are explicitly skipped in this run. The link-only build checks
the full added code and its RAM/branch layout; it does not construct a `.led`.

If host sanitizers are unavailable, a plain host check is possible with
`make -C device/tests clean` followed by `make -C device/tests test SANITIZERS=`.
That run provides no sanitizer coverage. An ARM-target or linker error still
has to be resolved before building device code.

## Add the stock image and inspect it

Create `local/`, obtain official **1.1.6.579** from the original
[KeyStep 37 downloads](https://www.arturia.com/support/downloads-manuals/product/keystep-37),
and place the unmodified file there:

```sh
mkdir -p local
export KS37_STOCK=local/keystep37_Firmware_Update_1_1_6_579.led
python3 -B -m ks37 verify "$KS37_STOCK"
python3 -B map/check.py --firmware "$KS37_STOCK"
```

Expect profile `official-1.1.6.579`, 176 valid local sums, application residue 0,
and a map check of 154 identity windows and 32 direct BL targets. A different
hash is a different input: do not change the pin to make it pass.

Both the color example and GEN start from this original file. They are separate
experiments. Neither patcher accepts a previously modified image as its base.

## Build GEN and keep matching artifacts

First run entirely in memory:

```sh
python3 -B device/build.py --stock "$KS37_STOCK" --build-dir local/build-gen-01
```

Expect `PASS-in-memory-candidate` and `firmware_file_written: false`. If the
result needs to be saved for an experiment, use an output filename that does
not already exist:

```sh
python3 -B device/build.py --stock "$KS37_STOCK" \
  --build-dir local/build-gen-01 --output local/gen-01.led
python3 -B device/verify.py --stock "$KS37_STOCK" \
  --candidate local/gen-01.led --elf local/build-gen-01/generator.elf \
  --report local/build-gen-01/build-report.json
```

Expect `PASS-independent-generator-device` from the verifier. Keep the `.led`,
`generator.elf` and `build-report.json` from the **same build** together. Use a
new build directory and output name for the next experiment; rebuilding in an
old directory replaces its ELF/report, not an earlier `.led` elsewhere.

The builder reports `baseline_candidate_matches`; the independent verifier
reports `matches_observed_baseline`. True means exact image identity with the
reference GEN14. False with otherwise passing checks is expected after a source
change. On untouched sources, investigate compiler/linker differences. Passing
structural checks does not establish the behavior of a different binary.

The tools do not flash. Follow the [device experiment procedure](local-workflow.md#device-experiment-boundary)
for a hardware test, then the [first audible check](../device/README.md#first-audible-check).
For a code change, continue with [first GEN change](first-gen-change.md).

## Common failures

| Result | Action |
|---|---|
| `Cannot find ld.lld` | Install LLD, add it to PATH, or pass `--ld` on the build command. |
| Clang lacks `arm-none-eabi` support | Use a Clang build with the ARM backend; do not substitute a host-only compiler. |
| Source SHA mismatch | Check model, version and untouched ASCII file against the vendor download. |
| Output already exists | Choose a new output name; do not overwrite a prior experiment. |
| ELF/relocation/anchor verification fails | Inspect the reported condition and toolchain/change; no force mode is provided. |
| Device shows `E##` or `F##` | Record the exact code and preceding action; inspect the predicate, not just the display. |
