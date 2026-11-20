# Validation

## First release and real updates (2026-11-20)

`v0.4.0` is the first release published from this repository. The release
workflow had never run here and could not have published: GitHub answers 404
for a draft asked for by tag and gives draft assets provisional `untagged-...`
URLs, while the publish step expected the final ones. This was confirmed with
a private draft that was deleted again, the step was corrected and dry-run
against that draft's real metadata, and the corrected workflow then built,
verified and published `v0.4.0` on its first run.

Both PCs of the reference setup were first brought to the same preview with
the package installer (a backup was taken, no program was running or stopped,
`hidway.ini` unchanged). The installed updater was then exercised against the
real release:

| Step | Laptop | Gaming PC |
|---|---|---|
| Check | preview offered `v0.4.0` | preview offered `v0.4.0` |
| Download and verify | size, GitHub digest, archive paths and manifest passed | passed |
| Install | refused: the folder was in use by a coding-assistant session started in it; installation left intact | folder swap completed, `hidway.ini` unchanged, rollback to the preview retained |
| Check again | up to date (after applying the same verified package with the file-by-file installer) | up to date |

Two defects found by this run were corrected afterwards: the updater did not
update the version shown in Windows Installed apps, and a folder in use was
reported as a raw rename error with a recovery journal left behind. Neither PC
ran ARM during these steps; the client stayed closed on the Gaming PC.

## Installer fixes and window layout (2026-11-20)

Before any change the working tree was built and all 11 CTest groups passed;
that state is commit `f452667`. After the changes the Windows Release build
passes with warnings treated as errors and all **11 CTest groups** pass again.
The Linux installer suite `tests/deploy/test_install.sh` passes **80 checks**
under WSL with stubbed git/make/systemctl. That WSL has no compiler, so the
relay build, `make check` and the relay fail-safe tests could not run locally;
GitHub CI ran them on the pushed branch: Linux unit tests, relay build and
`make check`, fail-safe 18/18, installer tests and shellcheck, the Windows job
and firmware for all four boards passed. `relay/hidwayd.c` and `common/` were
not modified.

A code review and a security review were run before merging. The code review
found one defect introduced by this work: the remote install command was
concatenated on the `ssh` line, where PowerShell does not treat `+` as an
operator, so `install.sh` received a stray argument and the Pi step would have
failed every time. The earlier test only looked for text in the script. The
command is now built in one function, tested for its exact text and for
reaching a native program as a single argument, and the installer test refuses
a stray trailing argument. The review also led to: an explicit `--serial`
winning over `--serial-default`, the setup guide recognising a copy installed
by `Setup.cmd`, Sunshine backups that are never overwritten, and a shorter
Tailscale lookup. The security review reported no high or medium findings.

Each reported installer defect was reproduced before the fix and re-run after:

| Defect | Before | After |
|---|---|---|
| Relay installer copied alone to the Pi | stopped at the missing example file, or rewrote the configuration and then failed | completes using the files of its checkout; refuses before touching `/etc/hidway` when they are missing |
| Re-running the Pi step | serial device overwritten with `/dev/ttyUSB0` | existing device kept; only the new sender is added |
| Sunshine input step | `sunshine.conf` emptied, empty backup | other lines kept, backup equals the original |
| Setup guide from the installed folder | blocked at the Windows step | continues to the network, Pi and Pico steps |
| OneDrive folder | refused as "junction or symbolic link" | refused with a OneDrive notice and the remedy, also when detected only by its reparse point |
| Host, user, IPv4 with a trailing newline | accepted | rejected |

The new client was started on the development PC in preview mode and once in
normal mode, disarmed, with a copy of a real configuration: it reached the
relay, showed every setting, switched between the full and compact layout,
opened and cancelled Input options and closed normally. The saved file kept
all 25 existing keys unchanged and gained `compact = 0`. All six wizard pages
render; the network page now fits without scrolling.

Not exercised: ARM and real input through the Pi and Pico, the SSH relay
installation against a real Pi, the elevated Sunshine step, a human clicking
through the wizard, tooltips and the monitor's Record tint on screen, a real
1366x768 or mixed-DPI display, and the Linux relay build. The off-screen
`--shot` capture gives blank images for the compact and options previews; the
windows themselves render correctly on screen. No firmware, relay or capture
code changed, and no package was installed over the working installation.

## Main-window same-PC button (2026-10-30)

The main HUD exposes **Pico on this PC** beside ARM. It shares the existing
Input options preset handler; capture, relay and firmware behavior are unchanged.
Production window-command tests cover successful save without ARM or packets,
missing/partial Pico USB collections, loopback peers, unsaved address/port,
configuration save failure, preserved physical/touchpad preferences and relay
settings, plus refusal during active capture/replay. Hidden-control tests check
that the button disables while ARMED and re-enables after disarm. These checks
do not replace yesterday's user-confirmed game acceptance with a new hardware
test. Native pointer movement and original Raw Input overlap remain as documented.

## Paired Windows updates (2026-10-29)

The isolated Windows Release build passes with warnings treated as errors and
all **11 CTest groups** pass. Companion routing tests mock process/window calls
to check sibling paths, existing-instance restore, different-copy refusal,
missing/reparse targets and launch failures without opening input applications.
The Windows setup suite now passes **49 checks**, including registered-folder
discovery and the update shortcut. The new update suite passes **27 checks**
covering version states, fixed release metadata, digest/manifest tampering,
ZIP traversal/duplicates, network failure and active-process refusal using
isolated archives and mock HTTP. No real release is installed by these tests.

The live public latest-release request returns no published release. This is
shown explicitly instead of claiming the preview is up to date. End-to-end
download/install of a real published GitHub release remains untested until a
release exists. Existing running installations, the relay and Pico were left
untouched. See [updates](UPDATES.md).

## Same-PC game acceptance (2026-10-29)

**The user confirmed working in-game input after the configuration corrections.**
HIDway runs on the Gaming PC with ARM enabled and matching elevated privileges;
the laptop Moonlight client uses relative mouse mode. The initial configuration
had unresponsive keyboard and erratic mouse movement through laptop AnyDesk and
Moonlight. Monitor counts alone did not establish game compatibility.

Separate transport observations showed five controlled Moonlight clicks producing
five Pico DOWN/UP pairs, slow AnyDesk keys producing five pairs, and balanced
phone-originated Pico clicks. Some manual cases contained additional actions and
are not exact five-click acceptance results. One disarmed run was invalidated
and replaced by an agent-controlled armed run.

Read-only diagnosis found the main game process at High integrity while HIDway
was at Medium integrity, and the laptop Moonlight absolute desktop-mouse option
enabled. With explicit user approval and manual UAC acceptance, HIDway was
restarted at High integrity and Moonlight was reconnected with relative mouse
mode. The user subsequently confirmed working input in the actual game. This
is acceptance of that setup, not a certification of every remote driver, mobile
gesture or game. The HIDway preset also retains native motion and cannot
guarantee isolation of the original Raw Input feed. No firmware change, game
modification, persistent elevation or security-policy change was performed.

Keys, mouse buttons and wheels are forwarded as Pico USB HID. Pointer movement
remains native in the selected preset; this result does not mean every input
passes through USB or that original Raw Input reports disappear. Elevated startup
was explicitly approved for the current session; ordinary launches still start
without elevation and without ARM.

## Same-PC Pico preset (2026-10-29)

The Windows Release build passes with warnings treated as errors; all nine CTest
groups pass. Production-handler fixtures cover missing/partial local Pico USB
collections, loopback/unavailable peers, failed saves, successful save, retained
Pi target and physical/touchpad preferences, virtual-only capture and no automatic
ARM. Normal Apply also retains the previous runtime config if saving fails.
Read-only connected-socket checks cover IPv4 and IPv6 loopback plus an external
documentation address; these checks send no input packets. IPv4-mapped IPv6 is
checked when the host permits that connection.

The preset selects the existing Gaming PC capture path; it does not alter firmware
or solve the documented Raw Input isolation limitation. Automated fixtures do not
establish a new end-to-end physical acceptance result.

Validation snapshot: 2026-10-28. Automated checks, reference-hardware
observations and remaining acceptance work are listed separately. These
results do not certify every driver, remote-control application or target.

## Automated checks

Windows Release builds use MSVC with warnings treated as errors. All nine
CTest groups pass:

| Group | Coverage |
|---|---|
| Common | Protocol, crypto, framing, key mapping and shared helpers |
| Input | Source ownership, pulse admission and ordered state transitions |
| Firmware host | Production USB-output logic with mocked time/endpoints |
| Windows client | Capture routing, stale generations, failures and UI options |
| Windows platform | Configuration migration, atomic saves and sender clock |
| Input monitor | 257 checks for counters, labels, raw/injected identity, Unicode diagnostics, wheel/button distinction, writer/export and output collisions |
| Recording | Versioned round-trip, truncation, count, bounds, ordering, numeric overflow and trailing-data rejection |
| Setup bootstrap | Native launcher opens a visible, inactive WPF wizard and closes without installing |
| Windows setup | 45 isolated clean-install/update/configuration/shortcut/registration/uninstall checks |

The recording/setup preview also passes 45 isolated Windows installer checks,
including preserving settings/labels, locked-file refusal, payload tamper/path
rejection, SSH argument validation and Pico-family checks. All six WPF wizard
pages load/render without installation. The setup launcher passes single-config
and Release-subfolder multi-config builds. Replay production-handler tests cover
per-device overlap, wheel accumulation, absolute baselines, live-input exclusion,
load-only IPC and PANIC release. The portable recording tests pass under Linux
with warnings treated as errors. Windows source/theme previews were inspected.

These new tests do not claim a fresh real Pico replay run, a complete interactive
Pi installation, a firmware flash, or a long recording soak. Existing hardware
and live connections were preserved during development. Follow
[the one-operator matrix](INPUT_TESTING.md#recordingreplay-acceptance-one-operator).

Linux validation includes common/input/serial-queue checks, 18 relay fail-safe
checks and 50 installer checks, including additive Remote PC/Gaming PC permission
preservation, idempotence and capacity refusal. Firmware builds target Pico, Pico W, Pico 2
and Pico 2 W. Host firmware tests do not replace endpoint tests on a real board.

The Windows package is checked for the client, probe, monitor, launchers,
documentation and board-labelled firmware. Private configuration, numeric
reports and working files are excluded from release packaging.

## Reference-hardware observations

The reference topology uses Windows, a Linux relay and Pico 2 USB HID.
A board-matched image was verified before flashing; keyboard and mouse
interfaces re-enumerated and delivered input. Recovery firmware was rebuilt
from retained pre-change source, not read back from device flash.

| Scenario | Observation and boundary |
|---|---|
| Remote PC physical keyboard/mouse and touchpad | Recorded press/release pairs, pointer movement and two-finger scrolling; all selected sources enabled |
| Same-PC virtual clicks | Ten clicks produced ten Pico press/release pairs and ten ordinary window click pairs |
| Same-PC Raw Input | Original remote reports remained visible alongside Pico; universal source isolation did not pass |
| External AutoClicker | Baseline 100 pairs; sustained runs settled at 1021 and 900 pairs with ARM retained and no post-stop click debt |
| Hold-repeat | Approximately 5.03 and 9.76 CPS at the 5/10 CPS settings; buttons released after stopping |
| PANIC during a hold | Pico stopped at 159 press/release pairs while the original source remained held |
| Wheel forwarding | Vertical and horizontal units reached Pico |
| Native-pointer short drag | Button pairs arrived, but a short automated title-bar drag did not move the window |
| Phone remote input | Practical AnyDesk-to-capture-PC-to-Pico control confirmed; individual layouts/IME not exhaustively tested |
| Input monitor | Device names, saved Unicode aliases, Identify, manual labels and dark controls exercised; both Pico interfaces displayed on the USB host |

An AutoClicker interval setting is not proof of the achieved injection rate.
Requested 1 ms did not establish 1000 actual clicks per second. Counter totals
prove observed events, not that a particular game accepted their semantics.

## Latency

An extended 30-minute transport probe received 3598/3600 replies: median
3.9 ms, p95 7.5 ms and maximum 1027.3 ms. The session mixed idle/active periods
and stream changes; it was not 30 minutes of uninterrupted input. Latency
spikes remain an open issue. These software RTTs alone cannot distinguish
network delay, local scheduling and driver effects.

Regression checks cover rejection of stale/duplicate status timestamps and
health leases based on request time. Those fixes do not remove real jitter.
The coalesced sender clock avoids accumulated timer replay; a real GUI stall
can still cause release/disarm when the local age budget expires.

## Remaining acceptance work

- Per-driver source filtering and simultaneous physical/virtual ownership.
- User-paced dragging, coordinate accuracy, DPI and multiple monitors.
- Keyboard layouts, AltGr, remote-driver flags and Unicode/IME boundaries.
- Actual target-application native/Pico overlap, including Raw Input consumers.
- Secure-desktop transitions, silent hook removal and live failure injection.
- A continuous active-input hardware soak.

Follow [INPUT_TESTING.md](INPUT_TESTING.md) and record the actual topology,
board, source switches, driver and application. See [INPUT_SOURCES.md](INPUT_SOURCES.md)
and [INPUT_MONITOR.md](INPUT_MONITOR.md) for implementation limits.
