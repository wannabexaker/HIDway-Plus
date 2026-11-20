# Input sources and routing

Updated 2026-10-30. The input changes are implemented and covered by host tests.
Actual AnyDesk drivers, target applications and USB hardware still have to be
checked on the real setup.

The route is Windows → UDP → Pi relay → UART → Pico → USB HID. Pico USB has
no direct Windows command channel.

## Source switches

| Source | Accepted feed |
|---|---|
| Physical | Non-injected keyboard hooks and device-bearing relative Raw Input |
| Touchpad | Absolute Raw Input, remembered absolute devices, and device-less Raw Input |
| Virtual / remote | Injected keyboard and mouse low-level hooks |

Keyboard and Mouse remain independent output switches. Source/output changes
disarm and release before taking effect. Physical remains enabled by default;
Touchpad and Virtual remain opt-in. Touchpad compatibility depends on the driver.

Classification is a compatibility policy, not authenticated device identity.
Precision Touchpads may have no device handle. Virtual HID drivers may appear
physical. Relative touchpads may follow the Physical path. The former 12 ms
mouse classification/deduplication guess has been removed. A driver emitting
both local Raw Input and injected copies needs specific validation; flags do
not identify applications or correlate every possible driver.

## The window: four tabs

The main window has four tabs. **ARM**, **Pico on this PC** and **PANIC** stay
at the bottom of every tab.

| Tab | What is on it |
|---|---|
| **Control** | Connection status (reachability, round-trip time, loss, rate, whether the target is on), whether the Pico is connected to this PC, the route in use and the live input readout |
| **Connection** | Relay address, port, send rate and auto-disarm time, with **Save** |
| **Input** | The route (Remote PC or Gaming PC), keyboard and mouse output, the sources, the Gaming PC pointer, hold-to-repeat and the virtual click limit, with **Apply and save** |
| **Tools** | Input Monitor, replay of a recording, updates and the recent-input list |

Remote PC and Gaming PC are two profiles. Each keeps its own relay address,
outputs, sources, pointer and repeat settings, so switching the route brings
back what was last saved for it. In `hidway.ini` the top-level keys describe
the route in use and `remote.*` / `gaming.*` keys hold the two profiles; a file
from an earlier version loads with every value kept.

Choices on the Input tab take effect with **Apply and save**, which disarms
first; edits that were not applied are dropped when you leave the tab. An
invalid value on the Connection or Input tab is refused with a message in the
window and nothing is changed. Button, Repeat source and Repeat clicks/sec are
greyed out, with their values kept, while **Hold to repeat** is not ticked. On
the Gaming PC route Physical and Touchpad are not available and Virtual /
remote is always on.

The Control tab's **Pico** line reads *On this PC*, *Partly detected* or *Not
on this PC*. It is a display only and never changes the route or arms.

**Pico on this PC**, beside ARM on the main window, is a one-click preset for
the Gaming PC with Pico USB
connected to it. It selects the Gaming PC profile and saves it. A new profile
starts with virtual input enabled, native pointer, keyboard/mouse output
enabled and hold-to-repeat off; after that it keeps what you saved for it.
The Remote PC profile keeps its own source choices. It never arms.
Selecting **Gaming PC** on the Input tab and applying does the same without
the check that the Pico is connected.
The main button is disabled while ARMED; a queued click cannot stop or change
an active capture/replay. Unsaved relay address/port edits must be saved first.
Success or a missing-device/address/save error appears in the Live Input card,
including when input history is disabled. After selecting the preset, press ARM
when ready. The button does not enable physical/touchpad recapture on this PC.
The configured Pi relay address, port and key stay unchanged: virtual keys,
buttons and wheels travel from this PC through Pi → UART → Pico → this PC's USB.
Pointer position stays native. The Pi must allow this PC as a client.

The preset requires both matching Pico keyboard and mouse Raw Input collections
on this PC and a configured UDP peer outside the loopback range. The USB check
uses `loopback_device` (default `VID_CAFE&PID_4877`); it does not authenticate the
device, verify UART wiring or prove relay reachability. Normal ARM checks still
apply. `127.0.0.1` / `localhost` points to this PC, not the Pi; the old localhost
test receiver did not produce USB HID. This preset needs no firmware update.
To return, select **Remote PC -> Gaming PC** and **Apply and save**.

**Game compatibility:** the user confirmed working AION2 input after starting
HIDway on the Gaming PC with matching elevated privileges and setting laptop
Moonlight to relative mouse mode. The earlier configuration failed.
Native-pointer mode does not convert movement into USB HID or isolate original
Raw Input. Running input capture below the target's integrity level can prevent
capture; no automatic elevation is performed.

| Setting | Behavior |
|---|---|
| Send input to: Remote PC | Exclusive badge capture, including virtual-only source selection |
| Send input to: Gaming PC through Pico | Capture virtual input only; native physical input remains local |
| Gaming PC pointer: Native position | Default for Gaming PC: remote cursor placement stays native; keys, buttons and wheels use Pico |
| Gaming PC pointer: Through Pico, relative | Experimental relative HID movement; pointer acceleration and DPI may change placement |
| Hold to repeat | OFF by default; generate clicks while the selected button is held |
| Button / Repeat source | Select button and physical, touchpad, virtual or combined sources |
| Repeat clicks/sec | Requested 1–25 CPS, default 10 |
| Virtual clicks/sec | Maximum requested rate per virtual button, 1–25 CPS, default 10 |

Virtual repeat also respects the virtual limit. Requested rates are not hard
real-time guarantees. Backpressure slows/rejects new pulses instead of building
click debt. Releases for admitted pulses are retained. A short rejected pair
is discarded; a still-held input can be admitted when capacity becomes available.

Repeat replaces dragging for that button/source. Disable it for normal drag.
Different sources merge with OR: one source cannot release another source's
hold. A continuous hold from another source can therefore mask repeat pulses
on the same button. Input history describes paced output intent, not proof
of target USB delivery. The HUD shows repeat/limiting status; no Windows balloon
notifications are used.

Gaming PC native pointer mode forwards no motion to Pico, so the same movement
cannot also be applied through USB. It preserves the remote application's
cursor coordinates and avoids relative HID acceleration changing click targets.
This is a mixed route by design, not an all-input-through-USB claim. Remote PC
mode always translates motion through Pico; its capture policy is unchanged.

Native pointer motion and Pico button delivery have different timing. A very
short drag can move the native pointer before the delayed USB DOWN reaches
the target; the live automated title-bar trial did not move the window.
User-paced dragging remains an acceptance case. Button counts alone do not
prove successful dragging or target placement.

Live Sunshine loopback testing showed one legacy window click per Pico click,
but Raw Input also exposed the original remote mouse events. Low-level hook
suppression does not establish isolation for Raw-Input-consuming applications.
Use Remote PC capture for those applications unless their own input filtering
can select Pico specifically. Universal same-host raw-event isolation would
need a different interception/driver architecture; none is installed here.

## State and ordering

Held keys, modifiers, buttons and mouse-key aliases belong to their source.
Typematic DOWN events do not create extra owners. A captured key keeps its DOWN
binding for release, using scan/E0 identity when available so layout changes do
not retarget the UP. This does not distinguish two physical keyboards or two
remote applications inside the same source category.

A bounded shared keyboard/mouse journal preserves short taps and modifier/click
order, including Ctrl+Alt+click. Keyboard changes have an 8 ms minimum dwell;
button changes have a 16 ms minimum dwell. The sender uses a coalesced waitable
timer rather than GUI SetTimer. The GUI still owns state and transport; a stalled
GUI is not presented as a hard real-time sender.

Capture and output backlog older than 100 ms is cancelled with release/disarm.
Overflow and enqueue failures also fail closed. This deliberately trades
continuity during a real stall for avoiding delayed input replay. New ARM
generations discard old local events, and each ARM creates a new relay session.

## Keyboard normalization

Valid scan codes keep scan-to-HID behavior. Supported scan-less virtual keys
use the current source layout, with explicit navigation/keypad distinction and
left/right modifiers. Physical layout helper events without a scan code are
not blindly converted.

Unicode VK_PACKET input is explicitly unsupported and is never interpreted as
a hardware scan code. IME, composed text and arbitrary Unicode cannot generally
be represented by one physical HID key. Greek/English layout and AltGr require
actual remote-driver acceptance tests.

## Same-machine loopback

In Gaming PC mode, Physical and Touchpad forwarding are disabled. Selected
injected events are consumed, and returning physical Pico HID is passed to
Windows. Native physical input stays direct. The time-based keyboard echo
heuristic has been removed.

This mode cannot transform native physical mouse holds into Pico repeat:
that requires per-device interception beyond the current low-level hooks.
Identical native/Pico holds must also be checked against the destination
application's input model. Remote drivers presenting virtual HID as physical
are not automatically safe for this route.

A virtual mouse UP whose DOWN happened before ARM passes locally; it is not
swallowed and replaced by an unrelated Pico UP. Relative USB HID cannot
universally reproduce absolute remote cursor coordinates, DPI scaling or every
multi-monitor layout.

The relay must permit the gaming PC's source address in addition to the laptop
for this route. Its allow list accepts up to eight comma-separated exact IPv4
addresses. Use the private addresses of the intended controllers, not a broad
public bind or unrestricted peer policy. Only one active controller session
owns the relay; a new ARM takes ownership and retires the previous session.
For a rollback-compatible managed installation, HIDWAY_ALLOW_EXTRA can add
exact peers while leaving the original base HIDWAY_ALLOW unchanged. Older
relays ignore that extra environment field and retain the original peer policy.

## Configuration

The client selects its executable directory before loading the adjacent INI,
including launches from Explorer or the Monitor. Keep each installed pair and
its configuration together.
Configuration saves use an atomic replacement. Existing source-only settings
migrate once: a legacy virtual-only selection becomes explicit Gaming PC mode;
other combinations become Remote PC mode.

Example defaults:

~~~ini
loopback_mode = 0
virtual_click_cps = 10
repeat_enabled = 0
repeat_button = 0
repeat_cps = 10
repeat_sources = 1
~~~

Button values are left=0, right=1, middle=2, back=3, forward=4.
Repeat source bits are physical=1, touchpad=2, virtual=4. Combinations are ORed.
The default Raw Input exclusion token remains VID_CAFE&PID_4877.

## Receiver limits

The new relay retains ordered held-state transitions during UART backpressure,
coalescing only motion updates with identical held state. New Pico firmware
retains one ordered state/motion FIFO and waits for keyboard completion before
dependent mouse action. Pending receiver work expires after 100 ms; overflow
or expiry releases and latches a fault until RELEASE/rearm.

Optional serial fault report 0x82 and UDP STATUS flag 0x0040 report this to the
new client, which disarms with an in-app reason. Updating only the client gives
client-side fixes; receiver protections require the corresponding relay/Pico
updates. Legacy normal state/target frames retain their layouts. Old relays
ignore the optional fault report without providing fault visibility.

UDP remains state-based and best effort. Queues preserve transitions that
arrived; they cannot reconstruct packets lost before arrival. STATUS is not an
acknowledgment of each USB transition. Reliable edge acknowledgments/retries
would require a separately negotiated protocol extension.

See [SECURITY.md](../SECURITY.md) for hook, desktop and network boundaries.
