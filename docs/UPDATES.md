# Windows release updates

The updater owns only local Windows Client, Probe and bundled companions.
The network KVM's Pi relay and Pico firmware have separate installation/update
procedures. No remote machine, SSH connection or input session is part of this
Windows updater.

## User flow

1. Extract the complete release ZIP. First installation still uses
   `HIDway-Setup.exe`. Run the installed **Check for updates** shortcut, HIDway's
   **Updates** button, or Monitor's **Check for updates** button.
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

## Fixed source and package contract

`tools/setup/hidway-updater.psm1` is a standalone PowerShell 5.1 module.
`hidway-updates.ps1` is its ASCII WPF front end. The compatibility script
`hidway-update-core.ps1` imports the module; the module does not import the
setup core or any external module.

The only Owner/Repo pair is the module's script-constant `wannabexaker` / `HIDway-`,
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

The pinned repository has no published release yet, so **Check** reports that
none is available. A workflow-produced release is needed for an end-to-end
public install. The `v0.3.0` archive in the earlier `wannabexaker/HIDway`
repository predates PAYLOAD.sha256 and the self-update files; it was refused
when checked on 2026-11-06 and is no longer consulted.

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
the same paths. The transaction does not change system policies or elevate.

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

After CI succeeds, publication uploads a draft, verifies that GitHub reports
one correctly named ZIP with the expected URL, size and digest, then publishes.
A failed upload/digest check leaves a draft for maintainer inspection. Never
replace published update assets; use a new version. The built-in, short-lived
GitHub Actions token is used only for publication. No user PAT/secrets are needed.

On 2026-11-20 the owner decided that updates come from the same repository as
the application. The pin moved from `HIDway` to `HIDway-`, matching this
checkout's remote, the setup wizard's Pi step and the README. The repository
guard prevents an accidental release from any other repository.

## Versioning

- A stable release is an annotated `vX.Y.Z` tag on `main` of the pinned
  repository. The tag is the version: CI passes it to the build, the package
  writes it to `VERSION`, and the updater compares it numerically.
- Numbers continue from the earlier repository, whose last release was
  `v0.3.0`. The first release from this repository is `v0.4.0`. Never reuse or
  move a published tag; fix a release by publishing the next number.
- Between releases, `git describe` names a build `vX.Y.Z-N-g<hash>`. Until the
  first tag exists here it is a bare commit hash.
- Hand-built preview packages carry a descriptive label such as
  `same-pc-preview-2026.10.30`. A label is not comparable with a release, so
  the updater shows it as a preview and asks before installing a stable
  release over it. It never treats a preview as up to date.
- The Raspberry Pi relay follows the same tags once `HIDWAY_REF=latest-tag` is
  set (see [relay operations](RELAY.md)). Until the first tag is published the
  setup wizard installs the relay from `main`.

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
INI preservation, failed-swap recovery and checkpoint tampering. Live tools
and actual application ownership mutexes are not used by those tests.

HTTPS, a fixed repository and SHA-256 bind the download to the trusted release
metadata; they are not an independent signed publisher identity. The maintainer
and GitHub account remain trusted. Binaries/scripts are unsigned. The updater
does not claim to defend against a malicious same-user process rewriting its
code or trusted local state. See SECURITY.md for the wider input-path boundary.