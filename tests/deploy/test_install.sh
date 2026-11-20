#!/usr/bin/env bash
# Tests for relay/deploy/install.sh in a fake root: no root, no systemd, no
# network. Covers the settings options (--configure), the GPIO UART switch and
# the service-file refresh.
#
#   bash tests/deploy/test_install.sh      (needs bash, GNU coreutils/findutils)
set -uo pipefail

here=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
DEPLOY=$here/../../relay/deploy
work=$(mktemp -d)
trap 'rm -rf "$work"' EXIT
pass=0
fail=0

check() { # check <name> <command...>
    local name=$1
    shift
    if "$@"; then
        pass=$((pass + 1))
    else
        fail=$((fail + 1))
        echo "FAIL: $name"
        sed 's/^/      /' "$work/out" 2>/dev/null | tail -n 5
    fi
}

setup() { # a fresh fake root with stub commands
    R=$work/root
    rm -rf "${R:?}" "${work:?}/bin"
    mkdir -p "$R/etc/hidway" "$R/boot/firmware" "$work/bin"
    # shellcheck disable=SC2016 # the stubs expand their own arguments
    printf '#!/bin/sh\necho "$*" >>"%s/systemctl.log"\n' "$work" >"$work/bin/systemctl"
    printf '#!/bin/sh\nexit 0\n' >"$work/bin/udevadm"
    # shellcheck disable=SC2016
    printf '#!/bin/sh\n[ "$1 $2" = "ip -4" ] && echo 100.64.1.2\n' >"$work/bin/tailscale"
    chmod +x "$work/bin/"*
    : >"$work/systemctl.log"
    printf 'Raspberry Pi 4 Model B Rev 1.5\0' >"$work/model"
    printf 'arm_64bit=1\n[all]\nenable_uart=1\n' >"$R/boot/firmware/config.txt"
}

run() { # run <install.sh args...>; output in $work/out
    HIDWAY_ROOT=$R HIDWAY_SYSTEMCTL=$work/bin/systemctl HIDWAY_UDEVADM=$work/bin/udevadm \
        HIDWAY_TAILSCALE=$work/bin/tailscale HIDWAY_MODEL_FILE=$work/model \
        bash "$DEPLOY/install.sh" "$@" >"$work/out" 2>&1
}

conf() { grep -E "^$1=" "$R/etc/hidway/hidwayd.conf" | tail -n 1 | cut -d= -f2-; }
count() { grep -cE "$1" "$2"; }
out_has() { grep -q -- "$1" "$work/out"; }
exists() { compgen -G "$1" >/dev/null; }
not() { ! "$@"; }

# --configure before anything is installed
setup
check "configure needs an installation" not run --configure --allow 100.64.9.9
check "  ...and says so" out_has "not installed yet"

# settings into an installed config
setup
cp "$DEPLOY/hidwayd.conf.example" "$R/etc/hidway/hidwayd.conf"
check "configure with settings" run --configure --bind auto --allow 100.64.9.9 \
    --wake-mac aa:bb:cc:dd:ee:ff --wake-to 10.0.0.1:9
check "  --bind auto asks Tailscale" test "$(conf HIDWAY_BIND)" = 100.64.1.2
check "  --allow" test "$(conf HIDWAY_ALLOW)" = 100.64.9.9
check "  optional --wake-mac is added" test "$(conf HIDWAY_WAKE_MAC)" = aa:bb:cc:dd:ee:ff
check "  optional --wake-to is added" test "$(conf HIDWAY_WAKE_TO)" = 10.0.0.1:9
check "  other settings kept" test "$(conf HIDWAY_PORT)" = 47800
check "  relay restarted once configured" grep -q "restart hidwayd.service" "$work/systemctl.log"
check "  previous config backed up" exists "$R/var/backups/hidway/*/etc/hidway/hidwayd.conf"

# the same again changes nothing and adds no duplicates
cp "$R/etc/hidway/hidwayd.conf" "$work/before"
check "configure again" run --configure --bind auto --allow 100.64.9.9 \
    --wake-mac aa:bb:cc:dd:ee:ff --wake-to 10.0.0.1:9
check "  idempotent" cmp -s "$work/before" "$R/etc/hidway/hidwayd.conf"
check "  one HIDWAY_WAKE_MAC line" test "$(count '^HIDWAY_WAKE_MAC=' "$R/etc/hidway/hidwayd.conf")" = 1

# bad values are refused and change nothing
cp "$R/etc/hidway/hidwayd.conf" "$work/before"
for bad in "--allow 1.2.3" "--allow 100.64.1.300" "--bind 10.0.0.256" "--wake-mac zz:bb:cc:dd:ee:ff" \
    "--wake-to 10.0.0.1:70000" "--wake-to host:9" "--serial /dev/x;reboot" "--uart wifi"; do
    # shellcheck disable=SC2086 # split the option from its value on purpose
    check "refuses $bad" not run --configure $bad
done
check "  config untouched after refusals" cmp -s "$work/before" "$R/etc/hidway/hidwayd.conf"
check "add Gaming PC while preserving Remote PC" run --configure --allow-add 100.64.8.8
check "  Remote PC primary retained" test "$(conf HIDWAY_ALLOW)" = 100.64.9.9
check "  Gaming PC added" test "$(conf HIDWAY_ALLOW_EXTRA)" = 100.64.8.8
cp "$R/etc/hidway/hidwayd.conf" "$work/before"
check "add same PC again" run --configure --allow-add 100.64.8.8
check "  add is idempotent" cmp -s "$work/before" "$R/etc/hidway/hidwayd.conf"
check "refuse invalid new sender" not run --configure --allow-add '100.64.8.8;reboot'
check "refuse ambiguous leading-zero address" not run --configure --allow-add 010.0.0.1
check "refuse more than eight peers" not run --configure --allow-add 10.0.0.1 --allow-add 10.0.0.2 --allow-add 10.0.0.3 --allow-add 10.0.0.4 --allow-add 10.0.0.5 --allow-add 10.0.0.6 --allow-add 10.0.0.7
check "  failed add keeps both PCs" cmp -s "$work/before" "$R/etc/hidway/hidwayd.conf"
check "refuses an unknown option" not run --configure --colour red
check "refuses an option without a value" not run --configure --allow
check "  ...and says so" out_has "needs a value"

# GPIO UART on a Pi 4: kept in config.txt once, with a backup
check "--uart gpio on a Pi 4" run --configure --uart gpio
check "  dtoverlay=uart3 added" test "$(count '^dtoverlay=uart3$' "$R/boot/firmware/config.txt")" = 1
check "  config.txt backed up" exists "$R/var/backups/hidway/*/boot/firmware/config.txt"
check "--uart gpio again" run --configure --uart gpio
check "  still one dtoverlay=uart3" test "$(count '^dtoverlay=uart3$' "$R/boot/firmware/config.txt")" = 1

# GPIO UART elsewhere: refused before anything changes
setup
cp "$DEPLOY/hidwayd.conf.example" "$R/etc/hidway/hidwayd.conf"
printf 'Raspberry Pi 5 Model B Rev 1.0\0' >"$work/model"
cp "$R/boot/firmware/config.txt" "$work/cfg-before"
check "--uart gpio refused on a Pi 5" not run --configure --uart gpio --allow 100.64.9.9
check "  config.txt untouched" cmp -s "$work/cfg-before" "$R/boot/firmware/config.txt"
check "  hidwayd.conf untouched" test "$(conf HIDWAY_ALLOW)" = "<client-tailscale-ip>"

# service files
setup
check "--refresh installs the service files" run --refresh "$DEPLOY"
for f in usr/local/sbin/hidway-update usr/local/bin/hidway-wake etc/systemd/system/hidwayd.service \
    etc/udev/rules.d/99-hidway.rules; do
    check "  $f" test -f "$R/$f"
done
check "  hidway-wake is executable" test -x "$R/usr/local/bin/hidway-wake"
check "--refresh again changes nothing" run --refresh "$DEPLOY"
check "  quiet the second time" not out_has "installed"


# --serial-default is validated like --serial
for bad in "--serial-default /dev/x;reboot" "--serial-default ttyUSB0"; do
    setup
    # shellcheck disable=SC2086
    check "refuses $bad" not run --configure $bad --allow 100.64.9.9
done

# full install with stub tools: install.sh copied alone (as the Windows wizard
# does) and run from a normal checkout. git clone is faked: the "checkout" is a
# copy of relay/deploy whose hidway-update only logs its arguments.
full_setup() {
    setup
    mkdir -p "$work/clone-src"
    for t in getent groupadd useradd runuser flock make cc; do
        printf '#!/bin/sh\nexit 0\n' >"$work/bin/$t"
    done
    # shellcheck disable=SC2016
    printf '#!/bin/sh\nif [ "$1" = clone ]; then\n  mkdir -p "$4/.git"\n  [ -n "$STUB_EMPTY" ] && exit 0\n  mkdir -p "$4/relay" && cp -r "%s" "$4/relay/deploy"\n  printf "#!/bin/sh\necho \\"\$*\\" >>%s/update.log\n" >"$4/relay/deploy/hidway-update"\n  exit 0\nfi\n[ "$3" = remote ] || [ "$1" = remote ] && echo https://example.invalid/hidway.git\nexit 0\n' \
        "$DEPLOY" "$work" >"$work/bin/git"
    chmod +x "$work/bin/"*
    : >"$work/update.log"
    rm -rf "$work/alone" "$work/normal"
    mkdir -p "$work/alone" "$work/normal"
    cp "$DEPLOY/install.sh" "$work/alone/install.sh"
    cp -r "$DEPLOY/." "$work/normal/"
    printf '#!/bin/sh\necho "$*" >>%s/update.log\n' "$work" >"$work/normal/hidway-update"
}
run_full() { # run_full <script> <args...>
    local script=$1
    shift
    PATH=$work/bin:$PATH HIDWAY_ROOT=$R HIDWAY_BUILD_USER= HIDWAY_SYSTEMCTL=$work/bin/systemctl \
        HIDWAY_UDEVADM=$work/bin/udevadm HIDWAY_TAILSCALE=$work/bin/tailscale HIDWAY_MODEL_FILE=$work/model \
        bash "$script" "$@" >"$work/out" 2>&1
}
ARGS=(--repo https://example.invalid/hidway.git --ref main --bind 100.64.1.2 --allow-add 100.64.9.9 --uart usb)

full_setup
check "alone: fresh install succeeds" run_full "$work/alone/install.sh" "${ARGS[@]}" --serial-default /dev/ttyUSB0
check "  configuration from the checkout" out_has "relay files of the checkout"
check "  serial default used on a first install" test "$(conf HIDWAY_SERIAL)" = /dev/ttyUSB0
check "  sender added" test "$(conf HIDWAY_ALLOW)" = 100.64.9.9
check "  update.conf has the ref" grep -q '^HIDWAY_REF=main$' "$R/etc/hidway/update.conf"
check "  hidway-update ran from the checkout" grep -q -- '--now' "$work/update.log"
check "  service files applied" grep -q -- '--apply-system' "$work/update.log"

full_setup
printf 'HIDWAY_BIND=100.64.1.2\nHIDWAY_ALLOW=100.64.7.7\nHIDWAY_SERIAL=/dev/hidway-serial\n' >"$R/etc/hidway/hidwayd.conf"
check "alone: serial-default keeps an existing device" run_full "$work/alone/install.sh" "${ARGS[@]}" --serial-default /dev/ttyUSB0
check "  serial unchanged" test "$(conf HIDWAY_SERIAL)" = /dev/hidway-serial
check "  other sender kept, new one added" test "$(conf HIDWAY_ALLOW_EXTRA)" = 100.64.9.9

full_setup
printf 'HIDWAY_BIND=100.64.1.2\nHIDWAY_ALLOW=100.64.7.7\nHIDWAY_SERIAL=/dev/hidway-serial\n' >"$R/etc/hidway/hidwayd.conf"
check "alone: --serial still forces the device" run_full "$work/alone/install.sh" "${ARGS[@]}" --serial /dev/ttyUSB1
check "  serial changed" test "$(conf HIDWAY_SERIAL)" = /dev/ttyUSB1

full_setup
export STUB_EMPTY=1
check "alone: checkout without relay files fails, fresh root" not run_full "$work/alone/install.sh" "${ARGS[@]}"
check "  ...with a clear message" out_has "neither next to install.sh nor in the checkout"
check "  no configuration written" not test -e "$R/etc/hidway/hidwayd.conf"
printf 'HIDWAY_BIND=100.64.1.2\nHIDWAY_ALLOW=100.64.7.7\nHIDWAY_SERIAL=/dev/hidway-serial\n' >"$R/etc/hidway/hidwayd.conf"
cp "$R/etc/hidway/hidwayd.conf" "$work/before"
check "alone: same with an existing config" not run_full "$work/alone/install.sh" "${ARGS[@]}" --serial /dev/ttyUSB1
check "  existing config untouched" cmp -s "$work/before" "$R/etc/hidway/hidwayd.conf"
check "  no backup taken" not exists "$R/var/backups/hidway/*/etc/hidway/*"
unset STUB_EMPTY

full_setup
check "normal checkout: install succeeds" run_full "$work/normal/install.sh" "${ARGS[@]}"
check "  uses its own files, as before" not out_has "relay files of the checkout"
check "  default serial from the example" test "$(conf HIDWAY_SERIAL)" = /dev/hidway-serial
check "  hidway-update ran from the checkout layout" grep -q -- '--now' "$work/update.log"

echo "install.sh: $pass passed, $fail failed"
((fail == 0))
