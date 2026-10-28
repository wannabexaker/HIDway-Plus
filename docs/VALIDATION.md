# Validation

Validation snapshot: 2026-10-28. Automated checks, reference-hardware
observations and remaining acceptance work are listed separately. These
results do not certify every driver, remote-control application or target.

## Automated checks

Windows Release builds use MSVC with warnings treated as errors. All six
CTest groups pass:

| Group | Coverage |
|---|---|
| Common | Protocol, crypto, framing, key mapping and shared helpers |
| Input | Source ownership, pulse admission and ordered state transitions |
| Firmware host | Production USB-output logic with mocked time/endpoints |
| Windows client | Capture routing, stale generations, failures and UI options |
| Windows platform | Configuration migration, atomic saves and sender clock |
| Input monitor | 29 checks for counting, classification boundaries and persistent labels |

Linux validation includes common/input/serial-queue checks, 18 relay fail-safe
checks and 41 installer checks. Firmware builds target Pico, Pico W, Pico 2
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
| Another-PC physical keyboard/mouse and touchpad | Recorded press/release pairs, pointer movement and two-finger scrolling; all selected sources enabled |
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
