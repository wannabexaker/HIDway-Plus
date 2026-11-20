# Input monitor

The standalone Windows `hidway-input-monitor.exe` replaces the temporary click
counter as a maintained debugging tool. It observes input, never injects or
forwards it, and has no network component. F9 is reserved for Stop only while
recording. HIDway, relay and Pico
operation are independent of the monitor.

The native interface uses HIDway's dark palette for the window, device table,
selection, fields, dropdowns and buttons; it resizes and scales with Windows DPI.
The header's **HIDway** button opens the installed sibling client or brings its
existing window forward. HIDway's **Monitor** button provides the reverse path.
**Check for updates** opens a separate update window for the paired Windows
package; it does not start ARM, stop a recording or replace running tools.
See [updates](UPDATES.md).

The device list uses Windows Raw Input identities and device-tree metadata.
Select a row to see its Windows name, connection, driver, identity and counters.
**Identify** selects the next device that sends a key/button press. Give it a
custom name and an optional source label; **Save label** retains those settings
in a Unicode INI beside the executable. Reset clears counts, not device names.
Changing USB ports can change Windows identity and require a new label.
Composite hardware may expose several mouse/keyboard collections with the
same Windows product name. Their identifiers and counters remain separate;
Identify helps label the collection actually producing an event.

`USB device`, `Bluetooth device` and `Built-in device` describe the Windows
device path, not authenticated physical origin. `Virtual driver` requires
Windows metadata reporting a virtual/remote device. An unknown or zero handle
is **Unknown**, not automatically virtual: Precision Touchpads may have zero
handles. A manually chosen Physical/Virtual/Touchpad label is explicitly marked
as a user label and does not override the underlying evidence.
The no-handle mouse and keyboard rows are shared unidentified feeds; naming
one does not establish which application generated later events in that feed.

Optional **Observe injected events** adds read-only low-level hooks on a
dedicated thread. Its separate counters show Windows injection flags, not an
application name or a link to a particular Raw Input row. Do not add these
totals to device totals; they are different views of potentially the same
actions. Missing injection flags do not prove physical input. Hook removal,
integrity levels and secure desktops limit observation.

Background counting is explicit and enabled by default. Pause/resume or a
capture-mode change begins a fresh count epoch because events can be missed.
Disconnect clears displayed held state without inventing release events.
Reset only after releasing inputs. Counts do not prove target-app acceptance,
absolute pointer accuracy or network latency.

Names/source labels persist locally; pressed-state bookkeeping and numeric
counts stay in memory until explicit recording is started. Optional
`--aggregate-report` overwrites a numeric JSON summary beside the executable
for scripted diagnostics, preserving the earlier counter's aggregate fields.

## Key/button details and recording

The **Last event** line above the device table shows the device, source label and actual event:
key name, scan code, E0/E1 flags and virtual key; Left/Right/Middle/X1/X2;
signed vertical/horizontal wheel units; relative/absolute coordinates.
**Wheel rotation** and **Middle / wheel press** are different event types.
Key names use this Windows keyboard layout; scan/HID identity is what matters
when the target uses a different layout. This is not text or IME translation.

Choose All Raw Input devices, Selected device only, or Pico devices only.
The fourth scope, **Injected flags only**, records the separate low-level Windows
injected-flag perspective, with distinct keyboard/mouse feed identities. It
never adds those events to a Raw Input recording. Selecting this scope starts
the visible observer when needed and stops it afterward if it was previously
off. The hook-to-UI queue is also bounded to 4,096 events and overflow invalidates
the recording. The callbacks still pass ordinary input onward.
Flags do not prove which app produced an event; virtual drivers without these
flags require the Raw Input view instead. Windows hook removal/integrity-level
limits can omit events without a reliable API notification.

On Gaming PC, choose **Pico devices only** to capture delivered HID actions.
All Raw Input devices may include both original remote input and returned Pico
input; recording both legs can duplicate the intended action in a later replay.
Release held inputs before **Record** (the red-tinted button while idle) and choose a new `.hidrec` filename.
The title displays RECORDING, elapsed time and event count. **Stop / Save**
finishes the file and a UTF-8 `.hidrec.csv` beside it. CSV includes identities,
names, manual source labels, Windows names/evidence and numeric event fields.
The default folder is Documents/HIDway/Recordings; choose another folder if
Documents is cloud-synced and you need private local storage. No recording starts
automatically. Pause/background changes are disabled while recording.
Background counting must be enabled before Record. A Windows device-removal
notification writes a device-state reset so replay releases that device's holds
without fabricating key-up events in its diagnostic counters.

Use **F9** to stop without moving/clicking the monitor toolbar: otherwise that
movement and Stop click are ordinary Raw Input and become part of the sequence.
F9 is reserved for Stop while recording and is omitted from that recording.
The hotkey is unregistered when recording stops. If another app owns F9, the
Raw Input F9 fallback still stops when Windows delivers that event to the monitor.

Each segment is bounded to one hour or 500,000 events; start another segment
after the limit. High-rate movement can reach the event limit much sooner.
The disk writer has a 4,096-event queue; overflow/disk errors leave `.partial`
files and an explicit failure rather than a silently incomplete replay.
The last-event display is transient and does not retain a full session in RAM.
CSV logs contain key identities and can reveal typed information: do not record
passwords or publish logs. Files are not encrypted by the monitor.

## Replay through HIDway

**Replay in HIDway** selects a completed recording and loads it into the client.
Existing-instance IPC loads only the path and is refused while ARM is active.
Click **Replay** in the client to validate and confirm the destination. This
requires no second operator: have a blank test window ready on the target.
After a three-second countdown, HIDway sends the sequence through its usual
Pi/Pico path. No local SendInput or monitor injection is used. Live capture is
disabled until playback finishes. **Stop**, ARM toggle and PANIC cancel and
release; link/watchdog/queue/session failures also retain normal disarm behavior.

Playback runs once, combines per-device held ownership and ignores configured
hold-repeat/key-to-mouse aliases so recorded actions are not transformed twice.
It uses recorded physical scan identity, with deliberate VK fallback where
available. Unsupported Unicode/E1 keys are rejected before ARM. Recordings have
strict size/value/order bounds and must have a matching completion footer.

Replay is a diagnostic sequence, not an exact timing/coordinate macro. Existing
minimum HID dwell and backpressure may stretch rapid input; overload cancels.
Relative movement is subject to target acceleration; absolute input becomes
relative displacement after its first baseline. It does not place a pointer at
an exact recorded screen position or restore target focus. Do not add injection
hook totals to Raw Input. Injected motion is observed screen-pixel displacement;
replaying it through relative HID may apply pointer acceleration again. The
target controls its own typematic repeat and keyboard layout; replay is not a
typed-text macro. Unicode packets are logged numerically but rejected for HID
replay. Keep other controllers idle during playback.

Microsoft references: [Raw Input device model](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input),
[zero-handle touchpad caveat](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputheader),
[mouse injection flags](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-msllhookstruct).
