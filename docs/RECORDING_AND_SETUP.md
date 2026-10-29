# Recording, replay and guided setup

The Windows package contains the HIDway client, Input Monitor and a graphical
setup wizard. The Monitor button opens the companion beside the client.

Input Monitor observes either Raw Input or a separately selected Windows
injected-flag stream. These recording perspectives are never combined.
Device names and manual labels
remain separate from Windows evidence. Live event details show scan code, E0/E1,
virtual key, mouse button, signed wheel units and relative/absolute motion.
Wheel rotation is not a middle-button press.

Recording is explicit and visible, with a chosen output file and source scope.
Recordings contain key identities and can reveal typed information: keep them
local and never record passwords. The numeric versioned recording is accompanied
by a readable CSV. A bounded writer queue protects the input thread from disk
latency; write failures invalidate the recording rather than silently lose events.

Replay is loaded and confirmed in HIDway and uses its normal network/HID output,
without local Windows injection. It runs once, disables live capture, uses a fresh
ARM session, and releases on completion, Stop, PANIC, link loss or queue failure.
Recorded device ownership is merged so overlapping holds cannot release each
other. Files have strict bounds and a completion footer. They cannot execute code.
Absolute pointer positions are converted into relative displacement; playback
does not reproduce desktop coordinates or bypass target mouse acceleration.

The setup wizard installs Windows components per user and explains the separate
Pi relay and Pico firmware steps. Authentication, SSH host verification, privilege
prompts and physical BOOTSEL remain visible user actions. Existing configuration
must be preserved. Windows-only recording/replay does not change the wire protocol.
