#!/usr/bin/env bash
# Assemble the release files from the CI build outputs:
#   OUT_DIR/HIDway-<version>-windows.zip   client, probe, Setup.cmd, firmware
#   OUT_DIR/hidway_fw_<board>.uf2          the firmware files on their own
#   OUT_DIR/SHA256SUMS.txt
#
#   tools/ci/package.sh VERSION WINDOWS_DIR FIRMWARE_DIR OUT_DIR
#
# WINDOWS_DIR holds hidway-client.exe and hidway-probe.exe, FIRMWARE_DIR the
# hidway_fw_*.uf2 files. Needs zip and sha256sum.
set -euo pipefail

if (($# != 4)); then
    echo "usage: package.sh VERSION WINDOWS_DIR FIRMWARE_DIR OUT_DIR" >&2
    exit 2
fi
version=$1
[[ $version =~ ^[A-Za-z0-9._-]+$ ]] || { echo "invalid package version" >&2; exit 2; }
win=$2
fw=$3
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
mkdir -p "$4"
out=$(cd "$4" && pwd)

name=HIDway-$version
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
stage=$work/$name
mkdir -p "$stage/firmware" "$stage/docs"

cp "$win/hidway-client.exe" "$win/hidway-probe.exe" "$win/hidway-input-monitor.exe" "$stage/"
cp "$win/HIDway-Setup.exe" "$stage/"
cp "$root/tools/input-monitor/Start-Input-Monitor.cmd" "$root/docs/INPUT_MONITOR.md" "$stage/"
cp "$root/docs/"*.md "$stage/docs/"
mkdir -p "$stage/docs/images"
cp "$root/docs/images/client.png" "$stage/docs/images/"
cp "$root/SECURITY.md" "$stage/"
cp "$root/tools/setup/Setup.cmd" "$root/tools/setup/hidway-setup.ps1" \
    "$root/client/hidway.ini.example" "$root/LICENSE" "$root/README.md" "$stage/"
cp "$root/tools/setup/hidway-setup-gui.ps1" "$root/tools/setup/hidway-setup-core.ps1" \
    "$root/tools/setup/hidway-uninstall.ps1" "$root/tools/setup/hidway-pi-setup.ps1" "$stage/"
cp "$root/tools/setup/hidway-updates.ps1" "$root/tools/setup/hidway-update-core.ps1" "$stage/"
cp "$root/tools/setup/hidway-updater.psm1" "$stage/"
cp "$root/relay/deploy/install.sh" "$stage/relay-install.sh"
shopt -s nullglob
uf2=("$fw"/hidway_fw_*.uf2)
((${#uf2[@]})) || { echo "no firmware in $fw" >&2; exit 1; }
cp "${uf2[@]}" "$stage/firmware/"
printf '%s\r\n' "$version" >"$stage/VERSION"
# The manifest is excluded from its own listing, so it is not read while written.
# shellcheck disable=SC2094
(cd "$stage" && find . -type f ! -name PAYLOAD.sha256 -print0 | sort -z | \
    while IFS= read -r -d '' file; do sha256sum -- "${file#./}"; done >PAYLOAD.sha256)

(cd "$work" && zip -qrX "$out/$name-windows.zip" "$name")
cp "${uf2[@]}" "$out/"
(cd "$out" && sha256sum -- "$name-windows.zip" hidway_fw_*.uf2 >SHA256SUMS.txt)
cat "$out/SHA256SUMS.txt"
