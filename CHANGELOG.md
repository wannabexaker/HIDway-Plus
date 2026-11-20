# Changelog

One version number: the annotated git tag `vX.Y.Z` on `main`. The client, the
relay and the firmware take their version from it, and the newest section of
this file has the same number; the release workflow refuses a tag that does
not match. Small changes are patch releases (`v0.7.1`, `v0.7.2`); the middle
or the first number changes only for large changes.

Unless a section says otherwise, the relay, the firmware and the wire protocol
are unchanged and the Pico does not need to be flashed again.

## v0.7.0 - 2026-11-20

New window: the Rail.

- A permanent left rail shows the state (DISARMED, or ARMED with a timer), the
  route, the relay, the Pico and the target, with ARM, Pico on this PC and
  PANIC always in the same place. PANIC stands alone at the bottom.
- The four tabs Control, Connection, Input and Tools sit on the right. The
  window is 700 x 432 and no longer has an empty band.
- One message strip for everything the window has to say, marked as
  information, success, warning or error.
- Control shows what ARM will send before it is pressed.
- The update prompt is a bar under the tabs; a refused value on the Connection
  tab marks its field.
- No function, setting or name changed; `hidway.ini` is read as before.

## v0.6.1 - 2026-11-20

- When HIDway opens it checks once whether a newer release exists and offers
  **Update now** and **Later** in the window. Update now closes HIDway,
  installs through the Updates window and opens HIDway again; it never arms.
- `update_check = 0` in `hidway.ini` turns the check off. Nothing is
  downloaded or installed without the click.

## v0.6.0 - 2026-11-20

- Gaming PC route: optional **Physical** source. The keys, mouse buttons and
  wheel of the PC the Pico is plugged into go to the relay and come back
  through the Pico. Off by default (`loopback_physical`); an update never
  switches it on. ARM is refused when the Pico is not on that PC.
- Limits: programs that read Raw Input still see the original devices; six
  keys at once plus modifiers; pointer movement stays native.
- The LIVE INPUT card on the Control tab fills the space above the action row.

## v0.5.0 - 2026-11-20

- The client window has four tabs: Control, Connection, Input and Tools, with
  ARM, Pico on this PC and PANIC on every tab.
- Remote PC and Gaming PC are two saved profiles (`remote.*` and `gaming.*`
  in `hidway.ini`); a file from an earlier version loads with every value
  kept.
- The Control tab shows whether the Pico is connected to this PC.
- Invalid values on the Connection and Input tabs are refused with a message,
  and a failed save no longer looks like a success.
- `tools/Update-All.cmd` updates two PCs and the relay in one run.
- The setup smoke test allows a slow first start.

## v0.4.2 - 2026-11-20

- The repository is `wannabexaker/HIDway-Plus`; the updater, the setup wizard,
  the firmware's program URL and the guides use the new name.
- The setup wizard installs the relay from the latest published release.
- `v0.4.0` and `v0.4.1` cannot follow the rename (their updater refuses
  redirects by design) and are replaced once with the setup wizard.

## v0.4.1 - 2026-11-20

- After an update or rollback, Windows Installed apps shows the installed
  version.
- When the installation folder is in use by another program, the updater says
  so, changes nothing and leaves no recovery pending.
- The release workflow verifies a draft the way GitHub reports it.

## v0.4.0 - 2026-11-20

First release from this repository. Version numbers continue after `v0.3.0`
of the earlier repository (`wannabexaker/HIDway-beta`).

- Setup wizard (`HIDway-Setup.exe`) for the Windows tools, the Raspberry Pi
  relay and the Pico firmware.
- Input Monitor with device names, recording and guarded replay.
- Physical, touchpad and virtual input sources; Remote PC and Gaming PC
  routes; the "Pico on this PC" preset.
- Check for updates from this repository, with a local rollback.
