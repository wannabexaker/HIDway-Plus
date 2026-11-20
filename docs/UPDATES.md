# Windows release updates

The updater owns only local Windows Client, Probe and bundled companions.
The network KVM's Pi relay and Pico firmware have separate installation/update
procedures. No remote machine, SSH connection or input session is part of this
Windows updater.

## User flow

1. Extract the complete release ZIP. First installation still uses
   `HIDway-Setup.exe`. Run the installed **Check for updates** shortcut, HIDway's
   **Updates** button, or Monitor's **Check for updates** button. HIDway also
   checks once by itself when it opens (see below).
2. **Check** makes one unauthenticated request to the latest stable GitHub release.
   Installed version comes from `VERSION` beside the local executables. Latest,
   installed and local previous versions appear separately.
3. **Download** is enabled only for a newer stable release or an incomparable
   preview. The ZIP is downloaded to a unique local cache directory, bounded,
   SHA-256 checked, and validated before installation can be selected.
4. Save any recording and close Client, Probe, Monitor and Setup. Choose
   **Install**. A preview requires a clearly labelled stable-install confirmation.
   The installed VERSION and archive digest are checked again at install time;
   a stale prepared download cannot downgrade a newer installation.
5. **Rollback** restores verified previous local tools, retaining current
   settings. It is an explicit recovery action, not a remote downgrade offer.
   Reopen the application when ready. ARM is always manual.

Check/Download/Install/Rollback buttons are disabled when their prerequisites
are absent or an operation is busy. Errors stay in the window. Closing during
a file transaction waits for it to finish. There are no automatic downloads,
installs, process kills, Windows notification popups or scheduled retries.

## The check when HIDway opens

Once per start, after its window is up (always disarmed), HIDway runs
`hidway-updates.ps1 -Check` from its own folder in the background. That makes
one unauthenticated request to GitHub for the latest release of the fixed
repository, the same request as **Check**: GitHub sees the PC's IP address and
the updater's user agent, nothing else is sent, and nothing is downloaded,
written or started. The script prints one line,
`HIDWAY-UPDATE state=<State> tag=<vX.Y.Z or empty>`; the client gives it at
most 20 seconds and accepts only `state=Available` with a `vX.Y.Z` tag. Any
other answer, a timeout or a missing script shows nothing (a failed check adds
one line to the recent list on the Tools tab). The check never delays ARM and
is skipped in previews and when HIDway is opened to load a recording.

When a newer stable release exists, the line under the title reads
"HIDway vX.Y.Z is available" with two buttons:

- **Later** hides it until HIDway is opened again. Nothing is remembered.
- **Update now** (only while disarmed; the line is hidden while armed or
  replaying) starts `HIDway-Setup.exe --update-now` and closes HIDway the
  normal way, so its settings are saved as on any exit. The updates window
  opens, waits up to 30 seconds for HIDway to close, then runs Check,
  Download and Install by itself, with the same code and checks as the
  buttons, showing progress in its status text. On success it opens HIDway
  again (disarmed) and closes. On any refusal or error (another HIDway tool
  still running, the folder in use, network, digest mismatch, a preview
  installed) it stops, keeps the window open with the reason and the normal
  buttons, and starts nothing. A preview is never replaced without the usual
  explicit confirmation. If the updater cannot be started, HIDway stays open
  and says so in that line.

To turn the check off, for example on a company network, set
`update_check = 0` in `hidway.ini` (see `hidway.ini.example`). A file without
the key keeps the check on. **Updates** on the Tools tab still works when
clicked.

## Everything at once

`tools/Update-All.cmd` in the source checkout updates a whole setup with one
double-click: HIDway on this PC, HIDway on a second Windows PC over SSH, and
the relay on the Raspberry Pi over SSH (`sudo hidway-update`). The Windows
parts use the updater module described below, with the same checks and the
same retained previous version; the module is copied to the second PC for the
run and removed afterwards, so it also works on an installation whose own
updater is older.

Name the two hosts once in `tools/update-all.local.ini` (copy
`update-all.example.ini`; the names are those of your own `~/.ssh/config`, and
the file is not committed). `Update-All.cmd -CheckOnly` reports without
downloading or installing.

Nothing is closed, stopped or armed. A PC on which HIDway, the Monitor, Probe
or Setup is running is skipped and named, and the relay installs a new release
only after a live session has ended. A preview build is replaced only with
`-AllowPreview`. The Pico firmware is never flashed from here: that needs the
BOOTSEL button and the setup wizard's Pico step, and only when the release
notes say the firmware changed.

## Fixed source and package contract

`tools/setup/hidway-updater.psm1` is a standalone PowerShell 5.1 module.
`hidway-updates.ps1` is its ASCII WPF front end. The compatibility script
`hidway-update-core.ps1` imports the module; the module does not import the
setup core or any external module.

The only Owner/Repo pair is the module's script-constant `wannabexaker` / `HIDway-Plus`,
the repository the application is published from.
All project API/download URLs are derived from it. Neither GUI nor module APIs
accept another URL, owner, repository or credentials. API redirects are rejected,
including repository-renaming redirects. Asset redirects are limited to HTTPS
GitHub release-storage hosts for the validated download, with a bounded hop count.
TLS 1.2 is explicitly selected; normal certificate validation remains enabled.

Latest stable tags must match `^v[0-9]+\.[0-9]+\.[0-9]+$`. Drafts and prereleases
are rejected explicitly. The release must contain exactly one
`HIDway-<tag>-windows.zip`, uploaded, size greater than 1 byte and at most 128 MiB,
with its exact canonical project download URL and lowercase
`sha256:[0-9a-f]{64}` digest. Numeric version comparison avoids integer overflow.
An installed numeric version without the tag's `v` prefix is also comparable.

The ZIP has one `HIDway-<tag>/` root and a complete `PAYLOAD.sha256` manifest.
It must include Client, Probe, VERSION, the updater module and GUI. It may include
the existing Monitor/setup/docs/firmware files, all from the strict allowlist.
Files outside the manifest, live INIs, traversal, duplicate paths, symlinks and
oversized expansion are refused. The total expanded size is capped at 128 MiB
and archive entries at 256. Firmware files may be stored with the package but
are never flashed by this updater. Downloaded scripts are not executed during
verification or installation.

An HTTP 404 means no public stable release or unavailable fixed repository;
it never means up to date. HTTP 403/429 reports rate limiting/access restrictions.
Network, HTTP and malformed-metadata failures are non-fatal check results. There
is no retry loop or authentication fallback.

Releases from `v0.4.0` on are produced by the release workflow of this
repository. The older line, up to `v0.3.0`, stays in the
`wannabexaker/HIDway-beta` repository; those archives predate PAYLOAD.sha256
and the self-update files and are not consulted.

Because API redirects are refused, an installed updater stops finding releases
when the repository is renamed. That happened once, on 2026-11-20
(`HIDway-` became `HIDway-Plus`): versions `v0.4.0` and `v0.4.1` report the
redirect as an error and have to be replaced once with the setup wizard of a
newer package. Versions from `v0.4.2` on look in `HIDway-Plus`.

## Local transaction and recovery

The updater refuses UNC and mapped-network destinations, reparse points and
running Client/Probe/Monitor/Setup processes. It never closes applications.
An exclusive per-installation file lock serializes mutating updaters. Existing
Client/Monitor ownership mutexes block their startup while a swap is underway;
file/process checks run again before replacing the directory.

The candidate, journal and previous snapshots live in a hidden sibling directory
named `.<installation-folder>.hidway-updates`, on the same volume. Current local
files are copied to the candidate, verified release files overlay only their
allowlisted paths, and `hidway.ini` is never a release payload file. The current
directory handle is released before the rename.

Windows does not provide one ordinary .NET operation exchanging two directory
names. The implementation performs two atomic renames with a flushed journal
covering the intervening gap. A failed second rename restores the old directory.
A later mutation repairs a validated pending journal before proceeding. This
is process-interruption recovery, not a claim of tested power-loss durability.

For manual recovery after an interruption that left the normal installation
path absent, run the retained module from the extracted ZIP in Windows
PowerShell, and call `Repair-HidwayUpdate` with the original local installation
folder. No network request is made. Close all Windows tools first. Invalid or
ambiguous recovery data is refused rather than guessed.

Previous managed files are hash-checked before rollback. Current INI, aliases
and recordings are copied forward; previous managed tools are restored. The
current fixed-source updater module/GUI are retained, so restoring a legacy
client cannot switch the update feed back to the old repository. A regenerated
local manifest records that intentional mixture. Installed apps/shortcuts keep
the same paths, and after Install or Rollback the version shown in Windows
Installed apps is set to the installed `VERSION` (only for the entry that points
at this folder; opening the Updates window also corrects an entry left stale by
an older updater). The transaction does not change system policies or elevate.

The swap renames the installation folder, and Windows refuses that while any
program has the folder as its working folder or keeps a file in it open: a
terminal, an editor or a coding assistant started in that folder is enough. In
that case nothing is moved, no recovery is left pending, and the window says
that the folder is in use; close that program and choose Install again. The
setup wizard of an extracted package replaces files one by one and is not
affected, so it remains the alternative.

The latest previous snapshot is offered by Rollback. Older transaction folders
and download caches are retained for diagnosis, not executed or silently deleted.
They may contain private local configuration. Keep them private and remove old
ones manually only when no update/recovery operation needs them. Sufficient disk
space is needed for the download, candidate and retained previous directory.

## Release publication

The annotated tag must be on `main` in the exact repository read from the
module's Owner/Repo constants. `.github/workflows/release.yml` refuses lightweight
tags, malformed versions, a different repository and existing releases. CI builds
Windows Client/Probe/companions, runs tests and validates both WPF entry points.
`tools/ci/package.sh` includes the updater and creates the ZIP, internal manifest
and SHA256SUMS. Tagged Windows builds receive the exact tag as HIDWAY_VERSION.

After CI succeeds, publication uploads a draft and verifies that GitHub reports
one correctly named ZIP, uploaded, with the expected size and digest. GitHub
does not return a draft from "get a release by tag" and gives a draft's assets
provisional `untagged-...` download URLs, so the draft is read from the release
list and the download URL is checked only after publishing; if the published
release does not match what the updater requires it is returned to draft.
A failed upload/digest check leaves a draft for maintainer inspection. Never
replace published update assets; use a new version. The built-in, short-lived
GitHub Actions token is used only for publication. No user PAT/secrets are needed.

On 2026-11-20 the owner decided that updates come from the same repository as
the application, and renamed that repository to `HIDway-Plus`. The pin, the
setup wizard's Pi step, the firmware's program URL and the README all name it.
The repository guard prevents an accidental release from any other repository.

## Versioning

- A stable release is an annotated `vX.Y.Z` tag on `main` of the pinned
  repository. The tag is the version: CI passes it to the build, the package
  writes it to `VERSION`, and the updater compares it numerically.
- Small changes are patch releases (`v0.6.1`, `v0.6.2`, ...). The middle or
  the first number changes only for large changes (the owner's rule).
- `CHANGELOG.md` carries the same number: its newest section is written
  before the tag, and the release workflow refuses a tag that does not match
  it.
- Numbers continue from the earlier repository, whose last release was
  `v0.3.0`. The first release from this repository is `v0.4.0`. Never reuse or
  move a published tag; fix a release by publishing the next number.
- Between releases, `git describe` names a build `vX.Y.Z-N-g<hash>`. Until the
  first tag exists here it is a bare commit hash.
- Hand-built preview packages carry a descriptive label such as
  `same-pc-preview-2026.10.30`. A label is not comparable with a release, so
  the updater shows it as a preview and asks before installing a stable
  release over it. It never treats a preview as up to date.
- The Raspberry Pi relay follows the same tags (`HIDWAY_REF=latest-tag`, see
  [relay operations](RELAY.md)); the setup wizard installs the relay that way.

## Tests and trust

Run in Windows PowerShell 5.1 from a writable local workspace:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/test_updates_win.ps1 -Work .scratch/updater-tests
powershell -NoProfile -STA -ExecutionPolicy Bypass -File tools/setup/hidway-updates.ps1 -ValidateOnly
```

The updater test mocks its HTTP boundary and process inventory, while using
real isolated archives, hashes, version files, directory swaps and rollback.
It covers 404, rate limits, network failure, prereleases, invalid metadata,
hash mismatch, downgrade refusal, running-instance refusal, preview consent,
INI preservation, failed-swap recovery and checkpoint tampering. It also runs
`hidway-updates.ps1 -Check` (the exact line for Available, Current, Error and
RateLimited) and `-AutoInstall` (install and report; refusal, preview and
network error leaving the copy untouched) as separate processes against a
copied installation, with the same mocks passed through the test-only
`-TestHook` switch, under which the client is never started. Live tools
and actual application ownership mutexes are not used by those tests.

HTTPS, a fixed repository and SHA-256 bind the download to the trusted release
metadata; they are not an independent signed publisher identity. The maintainer
and GitHub account remain trusted. Binaries/scripts are unsigned. The updater
does not claim to defend against a malicious same-user process rewriting its
code or trusted local state. See SECURITY.md for the wider input-path boundary.