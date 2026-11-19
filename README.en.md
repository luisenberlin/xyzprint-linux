# xyzprint — da Vinci 1.0 Pro on Linux

[![CI](https://github.com/luisenberlin/xyzprint-linux/actions/workflows/build.yml/badge.svg)](https://github.com/luisenberlin/xyzprint-linux/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)

🇪🇸 [Español](README.md) · 🇬🇧 English

Print on the **XYZprinting da Vinci 1.0 Pro** from Linux with no Wine, no virtual machine and none of the official Windows software: slice with CuraEngine, pack the `.3w` file and send it straight to the printer over USB.

> **Status**: tested on a real printer (model `dvF1W0A000`, firmware 1.4.2 / UI 5.1.5). The whole chain — slice → `.3w` → USB → print — has been validated. Documentation is mostly in Spanish; issues and pull requests in English are welcome.

## What it does

- **`slice`**: slices an STL/OBJ/3MF with CuraEngine using the 1.0 Pro profile (200×200×200 bed, 0.4 mm nozzle, PLA) and produces the `.3w`.
- **`wrap` / `encode` / `decode` / `inspect`**: build, extract and validate `.3w` files without slicing (for example, G-code coming from Cura, PrusaSlicer or Slic3r).
- **`send` / `upload`**: upload the `.3w` through `/dev/ttyACM0` and **start the print** when the upload finishes.
- **`status`**, **`pause`**, **`resume`**, **`cancel`**, **`complete`**: query and control the job.
- **`xyzsend`**: one command, `xyzsend model.gcode` (packs if needed, uploads and shows how to follow the job).
- **Caja context menu**: right-click a `.gcode` or `.3w` file → *Print on the da Vinci*.

## Status

| Area | Status |
| --- | --- |
| USB serial protocol | rebuilt and verified against the real printer |
| `.3w` format | validated byte for byte against real files |
| Slicing (`slice`) | working with CuraEngine 4.13 |
| Upload and print start | **verified by printing a real cube** (2026-11-18) |
| Time and filament estimate | computed from the G-code (±15–25 % on time) |
| Caja integration | working |
| Graphical interface | not yet (CLI + Caja for now) |
| Cura plugin | not yet |

## Tested on

| Printer | Firmware | System | Result |
| ------- | -------- | ------ | ------ |
| da Vinci 1.0 Pro (`dvF1W0A000`) | 1.4.2 / UI 5.1.5 | Debian/Ubuntu, CuraEngine 4.13 | ✅ full print |

Tried it with another model, firmware or distribution? Please open an [issue](https://github.com/luisenberlin/xyzprint-linux/issues) with the output of `xyzprint-cli status`.

## Requirements

- Linux x86_64 (tested on Debian/Ubuntu with Qt 6 and CuraEngine 4.13).
- Qt 6 (`Core` and `SerialPort`) and OpenSSL.
- `cura-engine` only if you want to slice (`slice`).
- Serial port access: install the udev rule (`packaging/udev/99-xyzprinting.rules`) or add your user to the `dialout` group.
- The printer connected over USB (VID:PID `11f1:2520`).

```
sudo apt install build-essential cmake ninja-build \
                 qt6-base-dev qt6-serialport-dev libssl-dev \
                 cura-engine zenity
```

`zenity` is optional: the Caja menu uses it to ask for confirmation before printing.

## Quick install

### From the release package

Download the `.tar.gz` (and its `.sha256`) from [Releases](https://github.com/luisenberlin/xyzprint-linux/releases):

```
tar xf xyzprint-0.1.0-linux-x86_64.tar.gz
cd xyzprint-0.1.0-linux-x86_64
./tools/install-local.sh
```

### From source

```
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./tools/install-local.sh
```

Both routes install into `~/.local/opt/xyzprint/` and link `xyzprint-cli` and `xyzsend` in `~/.local/bin`. The `--bundle` option also copies the required Qt6/OpenSSL libraries so you can move it to another machine without installing Qt.

### udev rule (no `sudo` needed)

```
sudo install -m 0644 packaging/udev/99-xyzprinting.rules /etc/udev/rules.d/
sudo udevadm control --reload-rules && sudo udevadm trigger
```

## Usage

```
# Printer status
xyzprint-cli status --port /dev/ttyACM0

# Slice and produce a .3w (object centered on the bed)
xyzprint-cli slice model.stl model.3w --set center_object=true

# Print an existing G-code or .3w (asks for confirmation)
xyzsend model.gcode

# No questions asked (scripts, Caja)
xyzsend -y model.gcode

# Only pack, don't send
xyzsend --dry-run model.gcode

# Pause, resume and cancel
xyzprint-cli pause  --port /dev/ttyACM0
xyzprint-cli resume --port /dev/ttyACM0
xyzprint-cli cancel --port /dev/ttyACM0
```

Notes:

- The print **starts as soon as the upload finishes**; there is no separate "start" command.
- The printer must be idle (`state: 9511`) before uploading. After a job it stays at `9509` until you press **OK** on the LCD.
- Without an interactive terminal (e.g. from Caja) `xyzsend` assumes `-y`.

## Using G-code from Cura

Save the G-code from Cura's interface and send it with `xyzsend`. The Start G-code must include `M108`, `M191` and `G21 ;` so the printer starts properly. The full guide, with ready-to-copy Start/End G-code, is in [docs/imprimir-desde-cura.md](docs/imprimir-desde-cura.md) (Spanish).

## Repository layout

```
src/                  C++/Qt6 CLI (serial protocol, .3w codec, slicing)
src/tests/            tests for the .3w codec and the profile
tools/                xyzsend, installers, packager, .3w reference in Node
profiles/             CuraEngine profile for the 1.0 Pro (PLA, 0.4 mm)
packaging/caja/       Python extension for the Caja context menu
packaging/udev/       optional udev rule
docs/                 technical documentation and guides
.github/workflows/    continuous integration and release publishing
```

## Documentation (Spanish)

- [docs/how-to.md](docs/how-to.md) — install, use, Caja, Cura, distribution and troubleshooting.
- [docs/linux-build.md](docs/linux-build.md) — build and CLI reference.
- [docs/imprimir-desde-cura.md](docs/imprimir-desde-cura.md) — Cura + `xyzsend`.
- [docs/formato-3w.md](docs/formato-3w.md) — the `.3w` format (header and encryption).
- [docs/protocolo-serie.md](docs/protocolo-serie.md) — USB serial protocol and states.
- [docs/perfiles-y-maquina.md](docs/perfiles-y-maquina.md) — profile, machine parameters and calibration.
- [docs/plan.md](docs/plan.md) — roadmap (GUI, AppImage, Cura plugin).

## Build and test

```sh
cmake -S src -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
```

The tests do not need a printer. To check the profile without Qt:

```sh
node tools/check-profile.mjs     # requires Node 14+
```

## Build a package for another machine

```sh
./tools/make-dist.sh                 # binary + profiles + scripts + docs
./tools/make-dist.sh --with-libs     # also bundle the Qt6/OpenSSL libraries
./tools/make-dist.sh --with-source   # also include the source code
```

This produces `dist/xyzprint-<version>-linux-x86_64.tar.gz` and its `.sha256`.

## Contributing

Tests with other models of the da Vinci family, bug reports and improvements are welcome; see [CONTRIBUTING.md](CONTRIBUTING.md). Changes between releases are listed in [CHANGELOG.md](CHANGELOG.md) and the roadmap is in [docs/plan.md](docs/plan.md).

## Legal notice

This is an **independent** project, unaffiliated with XYZprinting or its manufacturers. It does not contain or redistribute binaries, DLLs, installers or source code from XYZprinting: the protocol and the `.3w` format were reconstructed for interoperability from the observable behavior of the software and of the printer itself, and are documented here so others can print with hardware they already own.

The software is provided **as is**, without warranty. Printing involves physical risks (heat, moving parts, filament): use it at your own risk.

## License

MIT — see [LICENSE](LICENSE).
