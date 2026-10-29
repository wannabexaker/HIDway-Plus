# Input acceptance tests

Updated 2026-10-28. Automated tests and hardware observations are separate.
Record client/relay/firmware versions, route, enabled sources and actual driver.

## Safe target

Use Input Monitor on the USB target PC. Keep it foreground for clear comparison;
background counting is explicitly available. Its optional observer hooks are
off by default, and it has no network or input injection. Raw Input
rows separate devices and label the default Pico VID/PID. Keyboard repeats
are separate from unique DOWNs; mouse Held is a bitmask.

Reset with all inputs released and record the baseline after the reset click.
A focus change makes the observation incomplete; restart that case. The counter
reports aggregate relative motion and both wheel axes; it does not measure
latency, absolute cursor coordinates or whether a game accepts a report.
Those need separate observations. Optional numeric reporting overwrites a
single file and records no key names or typed text. The separate explicit Record
feature does save key identities and motion; keep test recordings private.
If background counting is disabled, a focus change starts a fresh count epoch.

## Recording/replay acceptance (one operator)

1. Open Monitor on the input-source PC, release held inputs, select a scope and
   Record to a new file. Press A, right Ctrl, keypad Enter, all five mouse buttons,
   wheel rotation and horizontal wheel; move/drag slowly. Stop/Save.
2. Verify key names plus scan/E0/E1/VK in CSV, balanced down/up pairs, and separate
   Middle press versus wheel units. Confirm Windows evidence versus manual labels.
3. Put the target Monitor/test window in front. Replay in HIDway, review the
   destination and confirm. Compare target Pico events after the countdown.
   Keyboard layout and relative pointer acceleration are separate observations.
4. Replay again and press PANIC mid-hold. Verify released target state and no
   later clicks. Repeat Stop, link loss and session-lock cases separately.
5. For long logging, observe visible Record status, file growth and segment
   limits. Simulate unwritable storage only in an isolated test folder; failures
   must leave an explicit failure, not a seemingly complete recording.

Automated state/parser/writer checks do not replace this hardware acceptance.

## Remote PC route

1. Start disarmed with normal repeat OFF. Select the intended local/virtual
   sources and Remote PC route.
2. Ten slow left clicks: Pico DOWN/UP counts both increase by ten, Held=0.
3. Ten short keys, modifiers, arrows, keypad, mouse side buttons, drag and both
   wheel axes. Check final releases and application behavior.
4. Hold the same key/modifier/button physically and virtually. Release in both
   orders: output stays held until the last owner releases.
5. Check Ctrl+Alt+click order at the target, including very short clicks.
6. Use the actual AnyDesk phone → laptop path. Verify driver classification,
   ordinary keys and clicks; check Greek/English and AltGr. Unicode packet/IME
   limitations must not produce unrelated function keys.

## Click pacing and repeat

- Start with virtual limit 10 CPS and external AutoClicker 100 ms. Then try
  1 ms input for 60 seconds on the counter: no unbounded queue, normal overload
  limits new pairs without overload-only disarm, and final state releases.
- Enable hold-repeat for a specific source/button at 5 and 10 CPS. Hold for a
  measured interval and compare actual counts/rate. Try higher rates only after
  the baseline is stable. Rates are requested targets, not exact timer promises.
- Release, OFF, DISARM, PANIC, source change and route change cancel future
  pulses and release current intent. No catch-up burst after a pause.
- Repeat OFF restores normal dragging. Another source's continuous same-button
  hold can intentionally mask repeat pulses under OR ownership.

## Gaming PC route

1. Stage the client and counter on the Pico USB host. Permit that PC's exact
   private IP at the relay; do not remove the laptop peer accidentally.
2. Choose Gaming PC through Pico. Only Virtual is forwarded; native physical
   input remains local. Repeat cannot transform native bypass input.
   Start with Native position for the pointer; only keys/buttons/wheels use
   Pico in that setting. Test the relative Pico pointer separately if needed.
3. One remote click/key must cause one target action and one Pico press/release.
   Compare legacy window messages with device-specific rows to identify double
   delivery. Stop immediately if counts continue after input stops.
   Sunshine testing exposed original mouse Raw Input as well as Pico reports,
   despite one legacy click. Raw-input applications cannot be marked passed
   from the legacy totals.
4. A virtual UP belonging to a pre-ARM DOWN must release locally.
5. Test native physical and Pico same-key/button overlap in the actual app.
   Windows/app semantics are outside client source merging.
6. Check cursor placement, drag, DPI and monitors separately. Correct counts do
   not prove correct absolute cursor translation.

## Failure and soak

Check initial absent relay, active disconnect/reconnect, lock, UAC, focus loss,
source changes while held, hook-thread/queue failure, stale local generation,
endpoint busy, serial backpressure, sequence wrap and a 30-minute real session.

Expected: no stale replay or stuck target state. Genuine queue expiry/overflow,
capture failure or a GUI pause over the local age budget releases/disarms with
an in-app reason. A reported receiver fault requires rearm. Silent hook removal
cannot be universally diagnosed by the current Windows API.

## Firmware update

Confirm the exact board before selecting UF2; Pico 2 uses the Pico 2 image.
Keep a rollback image built from retained pre-change source. That is a
recovery build, not a byte-for-byte dump of the device.

Enter BOOTSEL using the Pico USB connected to the target PC. Verify the boot
volume and INFO_UF2 board family before copying a board-matched image. Do not
write a UF2 to an unrelated disk. Verify re-enumeration, neutral state, relay
serial/target feedback and slow baseline input before rapid tests.

A client-only update supplies the Windows features. Receiver FIFO/fault
protections require new relay and Pico firmware. UDP still has no per-edge
delivery guarantee, even after all three components are updated.
