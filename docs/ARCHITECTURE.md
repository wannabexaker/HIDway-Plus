# HIDway architecture

Source implementation updated 2026-10-28. Mechanisms described here are
implemented; host tests, deployed versions and real-device checks are tracked
separately in [VALIDATION.md](VALIDATION.md).

## System boundary

~~~text
Physical / touchpad / remote software
                 ↓
Windows capture → source ownership → ordered state journal
                 ↓
UDP → Pi relay → ordered UART states → Pico state/motion FIFO
                                           ↓
                              USB keyboard + mouse → target
~~~

The target needs no HIDway agent. Video/audio, remote desktop transport,
Tailscale provisioning and target application behavior are separate systems.
Client and target may be the same Windows PC; traffic still crosses the Pi,
UART and Pico. Firmware provides no USB CDC command interface.

## Modules and ownership

| Component | Responsibility |
|---|---|
| client/src/main.c | Win32 HUD/badge/options, ARM policy, capture handlers, journal publication, UDP state/status |
| client/src/capture_queue.h | Bounded hook-to-GUI queue, wake coalescing, generation and age |
| client/src/send_clock.* | Waitable-timer worker that posts one pending sender wake |
| client/src/config.c / net.c | INI persistence, connected nonblocking UDP, clocks and entropy |
| common/hidway_input_state.* | Source-held bindings, button owners, pulse admission and ordered event journal |
| common/hidway_input_source.* | Source classification helpers and absolute-position conversion |
| common/protocol.h / hidway_crypto.* | UDP layouts and optional authenticated encryption |
| common/serial.* / cobs.* / crc16.* | UART frames and integrity checks |
| common/hidway_kbd.* / hidway_motion.* / keymap_sc2hid.* | HID conversion, six-key output and cumulative motion |
| relay/hidwayd.c | Exact peer allow list, session/sequence handling, bounded serial output, status/WOL |
| firmware/src/link.c | UART receive, cumulative-motion baselines, timeout release and reports |
| firmware/src/hid_out.c | Ordered state/motion FIFO, endpoint readiness, release priority and overflow/age fault |
| firmware/src/usb_descriptors.c | Keyboard/mouse USB interfaces |
| relay/deploy / tools/setup | Managed Linux deployment and Windows setup |

The Windows GUI thread owns input state, queues and transport. The hook
thread only classifies/copies bounded events; it does not touch widgets or
perform network I/O. The timer thread posts coalesced wakes and does not access
input/socket/crypto state. This avoids introducing concurrent owners of the
existing globals.

## Windows lifecycle

Startup loads hidway.ini from the working directory, creates the HUD/badge
and registers hotkeys. While disarmed it probes about every 400 ms and updates
the UI every 50 ms. Disarmed operation installs no capture hooks.

ARM creates a fresh relay session, starts capture with a fresh local generation,
clears output state, sends an initial neutral state and starts the waitable
sender clock. The requested rate defaults to 250 Hz; actual cadence depends
on Windows and GUI scheduling. A pending wake is coalesced, never replayed as a
burst after a stall.

Another-PC mode uses exclusive foreground badge capture. This-PC mode consumes
selected virtual events without taking target-app focus and excludes physical
recapture. Routing is explicit; source checkbox changes no longer silently
select another topology. Legacy virtual-only settings migrate to This-PC once.

The input options window is a separate compact settings surface. Opening it
disarms; ARM is blocked while it is open. Source/output/route changes begin
from released state. Saves atomically replace the existing INI; changing the
relay address releases the old peer before opening the new one.

Disarm disables capture, advances the local generation, stops the sender clock,
clears source/journal/pulse state and sends three RELEASE packets. Focus loss
in exclusive mode, session lock, sleep, display changes, detected hook/queue
failure, receiver fault and expired local backlog provide additional release
paths. Notices stay in the app, badge or tray; no Windows notification balloons
or taskbar flashing are used.

## Input lifecycle

Keyboard hooks preserve origin and scan/E0 identity. Source-held DOWN bindings
prevent one source's UP from clearing another's hold and retain release
identity across layout changes. Mouse owners and keyboard-to-mouse aliases
merge by source. Auto-repeat does not increase hold ownership.

Physical/local mouse data uses Raw Input; injected mouse input uses the
low-level hook. Device-less Raw Input is an opted-in touchpad compatibility
path, not proof of injection. A matching Pico device-path token is excluded
from Raw Input. Neither this token nor injection flags authenticate an app.

Supported scan-less virtual keys are normalized with layout-aware mapping and
explicit navigation semantics. Unicode packets are rejected before scan
conversion. [INPUT_SOURCES.md](INPUT_SOURCES.md) details driver limitations.

Virtual click admission and optional hold-repeat produce bounded pulses.
Repeat is per selected source/button; other sources' holds remain owners.
Backpressure prevents new pulses before accepted releases are endangered.
One full-state journal orders keyboard and mouse changes together, including
modifier/click combinations. Event-time cumulative motion accompanies state
edges; ordinary motion remains live when no older state edge is pending.

Journal capacity is 64 frames. Minimum dwell is 8 ms for key changes and 16 ms
for button changes. Local captured/journal work and sender wakeups older than
100 ms cause release instead of delayed replay. The dedicated timer is not an
independent network owner: a GUI stall can still pause sending, and is treated
as a capture/sender fault.

## UDP and session contract

UDP input packets contain complete keyboard/button state and cumulative
X/Y/wheel/pan counters. RELEASE is authoritative neutral state. PROBE and WAKE
do not own or refresh the input session. STATUS echoes client time for RTT and
includes relay/USB state and optional receiver-fault status.

Sequence comparisons accept 32-bit wrap within the ordinary half-range.
The relay retires the previous 16 session identifiers to stop delayed packets
from taking ownership back. Each new ARM creates a new identifier, so a
previously used controller can intentionally take ownership again.

STATUS handling validates session, age and echoed-time order, rejects
duplicates/older replies, drains with a finite budget and derives health from
the request timestamp rather than delayed arrival time. Initial no-reply
timeout starts at ARM. Correct RTT presentation does not remove real network
jitter or prove USB completion.

Optional XChaCha20-Poly1305 seals packets with a shared 32-byte key and random
nonces. Command replay controls remain separate from input session IDs.
Plaintext mode relies on the restricted network and exact peer allow list.

## Relay and firmware

The relay accepts up to eight exact comma-separated IPv4 peers. One controller
session owns input at a time. It keeps a current UART frame and up to 32
subsequent held-state transitions. Motion-only updates with identical held
state may coalesce. RELEASE clears queued work and is sent first. UART errors
reconnect from neutral state; new sessions reset Pico motion baselines through
RELEASE. Silence releases after 250 ms.

Pico UART framing remains COBS + CRC16. Link receive converts cumulative motion
to deltas and resynchronizes after release/first state/large receive gaps.
The USB stage keeps one 64-frame state/motion FIFO. It confirms keyboard
endpoint progress before dependent mouse action, preventing independent
endpoint queues from reordering modifiers and clicks. Motion belongs to its
state segment.

Receiver queues/motion expire after 100 ms. Overflow/expiry releases, discards
old work and latches a fault until RELEASE/link reset. USB disconnect and
suspend discard stale history; suspend keeps current intent for wake behavior,
without replaying old click bursts. Pico also releases after 250 ms input
silence and has a 100 ms hardware watchdog.

An optional compact serial FAULT report (0x82, value 0/1) lets the new relay set
STATUS_INPUT_FAULT (0x0040). New clients release/disarm with an in-app reason.
Normal state/target packet layouts remain compatible with older peers; old
relays cannot expose this new fault indication.

USB output remains six normal keys plus modifiers, five mouse buttons,
relative 16-bit X/Y and both wheel axes. Boot mouse protocol has fewer buttons
and no wheels. One-millisecond USB polling is not end-to-end latency. LED
output is stored on Pico but not yet propagated to the source keyboard.

## Build, change and release workflow

Root CMake builds common/input/firmware-host tests and Windows client/platform
tests. Firmware has a separate Pico SDK 2.3.1 ARM cross build for Pico, Pico W,
Pico 2 and Pico 2 W. The relay Makefile runs common/input/serial queue tests;
separate Linux fail-safe and installer checks validate operational paths.
CI definitions are not evidence that remote GitHub CI ran on this workspace.

Shared codec changes need tests for both ends and legacy behavior. Capture
changes require production-handler tests, not only helper tests. Endpoint
changes require mocked readiness/backpressure and actual-device acceptance.
Keep source/config backups, diagnostics and builds in indexed ignored scratch
bundles. Preserve user settings when packaging; launch with the correct cwd.

Managed Pi deployment keeps old releases and switches current/previous
atomically, defers active sessions and checks health. Runtime remains
unprivileged and hardened. Update authenticity is not a signed release chain;
local preview builds must identify their source/hash honestly and may later
be replaced by the configured upstream release policy.

## Practical boundaries

In This-PC mode the default pointer policy lets remote movement pass natively
and does not enqueue it for Pico. Keys, buttons and wheels remain intercepted
and forwarded. An explicit experimental option routes relative movement through
Pico; host acceleration/DPI can change absolute remote placement. Another-PC
exclusive capture still forwards movement through Pico.

Queues protect arrived transitions, not UDP packets lost before arrival.
There are no per-edge USB acknowledgments/retries. Relative HID cannot
universally preserve absolute remote cursor coordinates. Hooks cannot reliably
identify every physical device/application or detect every silent OS removal.
Same-PC native/Pico overlap, secure-desktop behavior and actual remote drivers
remain acceptance-test boundaries. See [SECURITY.md](../SECURITY.md).
