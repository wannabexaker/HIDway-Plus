# Guided installation

Download the complete Windows release ZIP and **extract all files**. Open
`HIDway-Setup.exe` (or `Setup.cmd`) inside that folder. Do not run it inside the
ZIP or copy only the executable. The package contains HIDway, Input Monitor,
the setup guide, relay installer, documentation and firmware for four Pico boards.
The native bootstrap opens a dark Windows wizard using built-in PowerShell/WPF.
The current preview is unsigned; Windows may display its normal publisher prompt.

## 1. License and Windows tools

Accept the MIT license. Choose an installation folder; the default is
`%LOCALAPPDATA%\Programs\HIDway`, with no administrator requirement. Both tools
are installed together. Choose Remote PC for the sending laptop, or Gaming PC
for remote virtual input returning through a Pico connected to this same PC.
These route choices initialize a new configuration; an existing configuration
is retained. An existing per-user installation is detected so its folder and
saved PC role are selected. The Start menu contains HIDway, Input Monitor,
Setup guide and Check for updates.
An optional desktop shortcut and an Installed apps uninstall entry are provided.

**Folders.** Use a normal local folder. OneDrive and other cloud-synced folders
are refused on purpose, for the installation folder and for the folder the ZIP
was extracted to: the wizard says so in its status line as soon as it can tell,
and names the remedy (a local folder such as `C:\HIDway`). Junctions and
symbolic links are refused too. The full path needs at least 8 characters
(`D:\HIDway` is fine, `D:\HID` is not), the same rule the updater applies.

**Setup guide from the Start menu.** Opened from the installed folder, the
wizard does not install again: the Windows step shows that the tools are already
there and **Next** continues with the network, Pi relay and Pico steps.

**Remote PC** means the sending laptop/PC. **Gaming PC** means the computer with
the Pico HID USB connected. The Gaming PC role runs virtual-only loopback when
HIDway itself runs there. These names replace the earlier Another PC / This PC
route labels; the underlying configuration and wire protocol remain compatible.

For the confirmed AION2 same-PC setup, the game was elevated: HIDway also needed
an explicitly approved **Run as administrator** launch on the Gaming PC. Accept
the normal Windows UAC prompt yourself. This is a per-launch requirement, not a
change to UAC or an automatic elevated startup. After launch, verify the Pi relay,
click **Pico on this PC** beside ARM, and enable ARM when ready. The same preset
is available inside Input options as **Use Pico on this PC**. Keep laptop HIDway
closed for this route. Laptop Moonlight should use relative/game mouse mode;
its **Optimize mouse for remote desktop instead of games** option should be off.
The corrected setup was confirmed by the user.

Close HIDway and Input Monitor before switching versions, including old portable
copies. A copy running from another folder is left untouched and prevents opening
a second input owner. The installer refuses locked files
and does not terminate input sessions or other portable copies. The payload is
checked against SHA-256 hashes, staged and replaced with rollback copies.
Settings, monitor aliases and recordings are preserved. Hashes detect corruption;
they do not establish a signed publisher identity.

## 2. Private network

The sending Windows PC and Raspberry Pi must reach each other on a private LAN
or Tailscale network. The wizard links to official Tailscale, Moonlight and
Sunshine downloads. Their installation, accounts and pairing remain separate.
HIDway only forwards input; it does not transmit video/audio.

Enter the Pi SSH hostname/IP, Linux user, Pi relay IPv4 and sending Windows PC
IPv4. Use addresses from the same reachable private network. The relay IP becomes
the new client Target; the default port is UDP 47800. Existing client settings
are not overwritten. Additional permitted senders and matching optional
encryption keys are configured using [relay operations](RELAY.md).

When Tailscale is installed on this PC, the sending-PC box is pre-filled with
its Tailscale IPv4 (from the local `tailscale ip -4`); check it and correct it
for a LAN setup. If the relay is already set up, tick **The relay is already
configured, or I will set it up later** at the top of this page: the address
boxes are then optional and the wizard skips the Pi relay step.

## 3. Raspberry Pi relay

Start from Raspberry Pi OS with SSH enabled and your own Linux account. Install
Windows OpenSSH Client from Optional features if absent. Choose the actual serial
device and GPIO/USB UART topology. Read [the wiring table](RELAY.md#serial-link-to-the-pico):
Pico GP1 receives, GP0 transmits, with common ground and 3.3 V signalling.
The Pico USB connects to the target Windows PC. A Pi USB UART adapter is a
separate device; do not confuse it with the Pico HID USB cable.

Click **Connect and install relay**. A visible terminal handles SSH host-key
verification, login and sudo. Verify the fingerprint against the Pi. The wizard
never stores the password or suppresses these prompts. It transfers the bundled
installer, installs this repository's latest published release, sets the chosen bind
and adds the sender while preserving previously permitted PCs (maximum eight).
This allows installing Remote PC and Gaming PC without losing the first peer.
Only one ARM session owns the relay at a time. It then checks `hidwayd` service
status. The existing
installer keeps configuration backups; an active relay can restart during this
explicit action. GPIO automation supports Pi 4/400/CM4 UART3; use a USB UART
adapter for other Pi models. Internet access on the Pi is required for source,
dependencies and build. This is guided installation, not a Raspberry Pi OS imager.

**Serial device.** Choosing **USB serial adapter** or **Pi GPIO UART** fills in
the usual device (`/dev/ttyUSB0`, or `/dev/hidway-serial` for the Pi 4 GPIO
UART3). That default is used only for a first installation: a relay that is
already configured keeps its serial device, so running this step again to add
a second PC does not change it. Type a different path to change the device of
an existing relay deliberately.

The bundled installer is copied to the Pi on its own; it takes its example
configuration and updater from the repository checkout it creates under
`/opt/hidway/src`, and checks that they exist before changing anything in
`/etc/hidway`. The temporary copy under `~/.cache/hidway-setup-*` is removed
when the installation command ends.

For an already configured relay, choose **already configured / later** on the
network page and keep
its settings. Authentication failures, a wrong serial device or missing wiring
must be corrected before the hardware path can work. Service-active alone does
not prove USB input delivery.

## 4. Pico firmware

Use this step on the Windows PC physically connected to the Pico. Select the
exact board: Pico, Pico W, Pico 2 or Pico 2 W. Hold BOOTSEL while reconnecting
its USB. The wizard looks for a drive containing `INFO_UF2.TXT` when this page
opens and when you click **Detect BOOTSEL drive**; a single match is filled in,
and you can always type the drive root yourself. Click **Verify drive
and copy firmware**. The wizard checks the RP2040/RP2350 family and package hashes,
then asks for confirmation before copying the selected UF2. BOOTSEL cannot
distinguish W/non-W variants reliably; read the board label yourself.

The drive disappears when the Pico reboots. Confirm both HID keyboard and mouse
in Input Monitor. A completed file copy is not an end-to-end acceptance test.
Skip this step on a laptop if the Pico is connected to the other PC. Existing
compatible firmware does not require flashing for Windows recording/replay.

## 5. Test and daily use

Open HIDway, confirm Target/port/encryption and route, and enable the intended
sources. Open a blank test window on the target with Input Monitor. ARM and
test each button/key, movement, wheel rotation and horizontal scrolling separately.
`Ctrl+Shift+F12` toggles ARM; `Ctrl+Alt+Shift+End` is the default PANIC.

The **Monitor** button launches the companion; its **HIDway** button opens the
client again. An existing instance from the same folder is brought forward.
For a single-person test, choose
a recording scope, **Record**, enter a new filename, perform a short sequence,
then **F9** (or **Stop / Save**). F9 avoids recording a toolbar click.
**Replay in HIDway** loads the file; click **Replay** in
HIDway to review the destination and confirm. It starts after three seconds,
runs once with live capture off, and releases on completion or cancellation.
See [recording limitations](INPUT_MONITOR.md) before comparing timing/coordinates.

## Update, uninstall and build

Use **Updates** in HIDway or **Check for updates** in Monitor/the Start menu.
Check, download and verify the complete stable release; then save any recording,
DISARM and close both apps before **Install**. No input session is
terminated automatically. See [Windows updates](UPDATES.md) for release availability,
preview versions and trust limits. You can also run the new extracted package's
wizard to update the detected installation folder. Uninstall via
Windows Installed apps. Local configuration, labels and recordings remain.
Recorded key identities can reveal typed information; choose a private output
folder and avoid passwords. Recordings are never included in the release.

The wizard's guide buttons open the bundled Markdown files with the program
Windows associates with `.md`, or in Notepad when there is none.

CLI setup remains available using `Setup.cmd -Role player -Install HIDway -Yes`.
With `-Yes` an existing "start with Windows" entry is left as it is. The
optional Sunshine step keeps the other lines of `sunshine.conf` and saves a
backup copy next to it before changing the three input settings.
Developers can build the root CMake project and package existing firmware with
`tools/package-windows.ps1 -BuildDir <build> -FirmwareDir <four-UF2-folder>
-OutDir <ignored-output> -Version <version>`. CI uses `tools/ci/package.sh` and
tests the recording codec, replay state and Windows setup functions before packaging.
