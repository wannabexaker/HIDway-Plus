# Input monitor

The standalone Windows `hidway-input-monitor.exe` replaces the temporary click
counter as a maintained debugging tool. It observes input; it never injects,
blocks or forwards it and has no network component. HIDway, relay and Pico
operation are independent of the monitor.

The native interface uses HIDway's dark palette for the window, device table,
selection, fields, dropdowns and buttons; it resizes and scales with Windows DPI.

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
counts stay in memory. No typed text or key-name history is saved. Optional
`--aggregate-report` overwrites a numeric JSON summary beside the executable
for scripted diagnostics, preserving the earlier counter's aggregate fields.

Microsoft references: [Raw Input device model](https://learn.microsoft.com/en-us/windows/win32/inputdev/about-raw-input),
[zero-handle touchpad caveat](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-rawinputheader),
[mouse injection flags](https://learn.microsoft.com/en-us/windows/win32/api/winuser/ns-winuser-msllhookstruct).
