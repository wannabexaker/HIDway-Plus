# Validation

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
