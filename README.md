# HIDway

Forward Windows keyboard and mouse input through a Raspberry Pi and a Pico USB HID device.

## Overview

HIDway separates input capture from USB delivery. A Windows client captures selected input while armed, a Linux relay sends it over UART, and a Raspberry Pi Pico presents a standard keyboard and mouse to the target computer. Video and audio come from a separate application such as Moonlight or AnyDesk.

Physical, touchpad and virtual/remote input paths are implemented. The 2026-10-28 input preview adds source ownership, explicit routing, bounded click pacing and hold-repeat. Driver-specific and same-machine behavior still requires actual-device acceptance; see [input sources](docs/INPUT_SOURCES.md) and [validation](docs/VALIDATION.md).

## Features

- ARM/disarm controls, global toggle and panic hotkeys, floating badge and tray menu.
- Standalone input monitor with Windows device names, saved custom labels, Identify, per-device counters and optional injected-event observation; see [monitor](docs/INPUT_MONITOR.md).
- Monitor button, explicit key/button/motion recording with CSV logs, and guarded replay through the normal Pi/Pico output path.
- Dark Windows setup wizard bundles both apps and guides private networking, Pi relay setup and board-specific BOOTSEL flashing.
- Open HIDway from Monitor or Monitor from HIDway; manual **Check for updates** downloads and updates both Windows tools together while preserving settings. See [updates](docs/UPDATES.md).
- Connection feedback stays in the HUD, badge, tray status and recent history; Windows notification popups and taskbar flashing are disabled.
- Independent keyboard/mouse forwarding and Physical, Touchpad, Virtual / remote switches.
- Explicit Remote PC / Gaming PC routing, per-source held state and ordered keyboard/mouse transitions.
- Gaming PC native cursor placement with Pico keys/clicks/wheels; optional experimental relative Pico movement.
- **Pico on this PC** button beside ARM, also available in Input options, with local USB-device and relay-address checks; selects the same-PC preset without ARM.
- Configurable virtual click limit and optional hold-to-repeat by button/source/speed; normal drag when repeat is off.
- Coalesced waitable sender clock, stale-input cancellation and bounded receiver queues with release priority.
- Full keyboard/button state and cumulative mouse movement over UDP.
- Optional authenticated encryption with a shared key; intended deployment over a restricted private network such as Tailscale.
- Relay and Pico release-on-timeout, serial reconnect, target USB-state reporting and configured Wake-on-LAN.
- Linux service installation, release updates, health checks and rollback.

## Architecture

```text
Windows client → UDP → Raspberry Pi relay → UART → Pico → USB HID → target PC
```

For remote access, AnyDesk supplies input to the Windows client. In same-machine loopback, the first and last computer are the same; the Pi relay and UART link are still required. There is no direct Windows-to-Pico USB command interface.

### Components

| Component | Responsibility |
|---|---|
| `client/` | Win32 HUD, capture hooks, Raw Input, configuration and UDP |
| `common/` | Packet layouts, crypto, framing, key mapping and state helpers |
| `relay/` | Linux UDP-to-serial service and deployment tools |
| `firmware/` | Pico UART receiver, watchdog and TinyUSB HID reports |

See [architecture](docs/ARCHITECTURE.md), [relay operations](docs/RELAY.md) and [security](SECURITY.md).

Current test results and hardware limits are in [VALIDATION.md](docs/VALIDATION.md). Follow [INPUT_TESTING.md](docs/INPUT_TESTING.md) for actual-device acceptance.

## Tech Stack

C11, Win32/Winsock, Linux/POSIX, CMake/Ninja, Pico SDK 2.3.1, TinyUSB and vendored Monocypher 4.0.2. Deployment uses shell scripts and systemd; Windows setup uses PowerShell. Firmware builds target Pico, Pico W, Pico 2 and Pico 2 W.

## Installation

For normal use, extract the complete Windows release ZIP and run
`HIDway-Setup.exe`. Follow the six steps; both Windows tools install together.
See [the installation guide](docs/INSTALLATION.md) for Pi prerequisites, SSH,
UART wiring, firmware selection, updates and uninstall. The current preview is
unsigned. Existing compatible Pico firmware supports recording/replay unchanged.

For the Windows client and shared tests, open a Visual Studio developer terminal with the C++ workload, CMake and Ninja available. From the repository root:

```powershell
git clone https://github.com/wannabexaker/HIDway-.git
cd HIDway-
cmake -S . -B .scratch/2026-10-26-host -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_COMPILE_WARNING_AS_ERROR=ON
cmake --build .scratch/2026-10-26-host
ctest --test-dir .scratch/2026-10-26-host --output-on-failure
```

The executable is `client/hidway-client.exe` under that build directory. Use a fresh dated build directory and record it in `.scratch/README.md`. The release archive's `Setup.cmd` expects the packaged executable beside its setup scripts; running the source copy alone does not install a just-built client.

The local input-preview package is in `out/HIDway/`; use `Start-HIDway.cmd`
there so the executable reads its adjacent configuration. Generated packages
are ignored and are not guaranteed to exist in a fresh source checkout.

Firmware requires the Pico SDK and Arm GNU toolchain. Set `PICO_SDK_PATH` and `PICO_TOOLCHAIN_PATH` if they are outside the supported local SDK layout. Select the board explicitly:

```powershell
cmake -S firmware -B .scratch/2026-10-26-pico2 -G Ninja -DPICO_BOARD=pico2 -DHIDWAY_T0=OFF
cmake --build .scratch/2026-10-26-pico2
```

Flash the resulting `hidway_fw.uf2` using the board's BOOTSEL drive. `HIDWAY_T0=ON` builds the separate BOOTSEL-driven input test, not the normal relay mode.

Install/configure the Pi using [RELAY.md](docs/RELAY.md). The input-source and click-queue test dependencies are included in the relay Makefile. A passing Windows CMake test does not validate the Linux build/deployment path; run those checks on Linux too.

## Windows download, update and rollback

Extract the complete Windows release ZIP, install it with `HIDway-Setup.exe`,
then open **Updates** in HIDway or **Check for updates** in Monitor/the Start
menu. The dark window shows **Installed**, **Latest** and the retained local
rollback version. Choose **Check**, **Download**, close Client/Probe/Monitor,
then **Install**. Each PC updates its own local copy; this never contacts another
PC, updates the Pi, flashes firmware, or enables ARM.

The standalone engine is `tools/setup/hidway-updater.psm1`, with the PowerShell
5.1 WPF front end `hidway-updates.ps1`. Both are ASCII and use only Windows/.NET.
The single immutable Owner/Repo pair pins requests to **wannabexaker/HIDway-**,
the repository this source is published from.
There is no repository/URL override, account, token, telemetry, background update
timer or retry loop. API redirects to another repository are refused. GitHub's
own release-storage redirects are allowed for the already validated asset only.

Stable `vX.Y.Z` releases must contain exactly one `HIDway-vX.Y.Z-windows.zip`.
The updater checks its exact project URL, uploaded state, 1 < size <= 128 MiB
and lowercase GitHub SHA-256 digest. It verifies the downloaded bytes again,
rejects unsafe ZIP paths and validates `PAYLOAD.sha256` plus `VERSION`. Installed
versions are read from the adjacent `VERSION`; equal versions are up to date,
older releases cannot be installed, and incomparable previews require an
explicit **Install stable release...** confirmation. A missing release, rate
limit or network failure appears as a non-fatal message.

Installation stages a complete local directory and uses two same-volume atomic
renames with a durable recovery journal. The previous tools remain available
through **Rollback**. `hidway.ini`, Monitor aliases, recordings and other local
files are retained, including settings changed after the update. Rollback is an
explicit local recovery operation; it never downloads an older GitHub release.
The fixed-source updater is retained when restoring legacy tools. Backups may
contain private configuration; keep the sibling hidden update-store folder private.

Maintainers publish an **annotated** `vX.Y.Z` tag on `main`. The release workflow
checks the repository against the module's same Owner/Repo pair, runs CI, builds
the ZIP and `SHA256SUMS.txt`, creates a draft, verifies GitHub's uploaded digest,
then publishes. It refuses to replace an existing release. Publication uses
GitHub Actions' short-lived built-in token; Windows users supply no credentials.
See [the full update guide](docs/UPDATES.md) for tests and interrupted-update recovery.

**One repository:** since 2026-11-20 the application, the updater, the setup
wizard's Pi step and the release workflow all use `wannabexaker/HIDway-`. The
earlier `wannabexaker/HIDway` repository holds the older code and its releases
up to `v0.3.0`; it is not consulted and the workflow refuses publication from
any repository other than the pinned one. No release exists here yet, so
**Check** reports that none is published. Version numbers continue from the
earlier line: the first release from this repository is `v0.4.0`. See
[versioning](docs/UPDATES.md#versioning).

## Usage

1. Connect Pico UART0 (GP0 TX, GP1 RX) to the Pi serial link with crossed TX/RX and common ground; use compatible 3.3 V signalling. Connect Pico USB to the target PC.
2. Configure the relay's private bind address, allowed client and serial device. Match encryption settings on both ends.
3. Start the client from its configuration directory. Set the relay address/port and verify status before arming. The default address is a placeholder.
4. Start with Physical enabled. ARM forwards input; `Ctrl+Shift+F12` toggles and `Ctrl+Alt+Shift+End` disarms immediately. Hotkeys are configurable.
5. Disarm before changing sources or configuration. See [input sources](docs/INPUT_SOURCES.md) for remote and loopback behaviour and limitations.

The client loads `hidway.ini` beside its executable, including Explorer and Monitor launches. An encryption key stored there is a plaintext local secret.

## Project Structure

```text
client/        Windows application
common/        Shared C modules and vendored crypto
firmware/      Pico project
relay/         Linux daemon and deployment
tests/         Shared, relay fail-safe and installer tests
tools/         Build, setup, probe and packaging tools
docs/          Architecture, operations, checklist and roadmap
.scratch/      Ignored local builds and working material
```

## Notes

- USB keyboard output is six non-modifier keys plus modifiers (6KRO), despite the larger wire bitmap.
- Requested send rate is not a measured delivery guarantee; a waitable-clock thread posts coalesced wakes to the GUI, which still owns capture state and transport.
- Development USB IDs are `CAFE:4877`; no compatibility with a particular application or security policy is guaranteed.
- Historical checklist results describe earlier runs, not validation of this snapshot. See [roadmap](docs/ROADMAP.md).
- Code is distributed under the [MIT license](LICENSE); vendored components retain their own notices.

## Future Improvements

- Validate touchpad/AnyDesk capture and loopback on physical hardware.
- Complete real AutoClicker/repeat, failure and extended soak acceptance.
- Improve silent-hook-loss detection where the Windows API permits it.
- Evaluate negotiated reliable edges if measured UDP loss justifies the change.
- Measure latency under stream load and migrate configuration to a stable per-user path.
