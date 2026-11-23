#!/usr/bin/env bash
set -euo pipefail

if [[ $# != 2 || $EUID != 0 ]]; then
    echo "Usage (root): $0 /path/to/files_hardening_probe /path/to/Files-sdl" >&2
    exit 2
fi
if [[ ${FILES_TEST_MOUNT_NAMESPACE:-} != private ]]; then
    export FILES_TEST_MOUNT_NAMESPACE=private
    exec unshare --mount --propagation private -- "$0" "$(realpath "$1")" "$(realpath "$2")"
fi

probe=$1
app=$2
tmp=$(mktemp -d)
appdir=/usr/share/APPLaunch
created_appdir=false
test_mounts=()
cleanup() {
    if [[ -s $tmp/detached-child ]]; then
        kill "$(cat "$tmp/detached-child")" 2>/dev/null || :
    fi
    for ((index=${#test_mounts[@]}-1; index>=0; --index)); do
        umount -- "${test_mounts[index]}" || umount -l -- "${test_mounts[index]}"
    done
    if $created_appdir; then
        rmdir -- "$appdir"
    fi
    rm -rf -- "$tmp"
}
trap cleanup EXIT
chmod 0755 "$tmp"
install -m 0755 -- "$probe" "$tmp/probe"
probe="$tmp/probe"
record_mount() {
    local target=$1
    shift
    mount "$@" "$target"
    test_mounts+=("$target")
}

drive="$tmp/Cíl bez jmenovky"
mkdir "$drive" "$tmp/Jiný" "$tmp/ro" "$tmp/full"
truncate -s 16M "$tmp/image"
mkfs.ext4 -q -F "$tmp/image"
record_mount "$drive" -o loop,nosuid,nodev "$tmp/image"
[[ -z $(lsblk -no LABEL "$(findmnt -nro SOURCE --mountpoint "$drive")") ]]
record_mount "$tmp/full" -t tmpfs -o size=32k tmpfs
record_mount "$tmp/ro" -t tmpfs -o ro tmpfs
record_mount "$tmp/Jiný" -t tmpfs tmpfs

for mode in sync-file sync-directory sync-parent move mount bind; do
    source="$tmp/$mode strom"
    mkdir -p "$source/Jiný svazek"
    printf 'verified source\n' >"$source/Příliš žluťoučký.txt"
    if [[ $mode == move ]]; then
        ln "$source/Příliš žluťoučký.txt" "$source/Jiný svazek/Tvrdý odkaz.txt"
    fi
    if [[ $mode == mount || $mode == bind ]]; then
        from="$tmp/Jiný"
        if [[ $mode == bind ]]; then
            from="$tmp/Same filesystem"
            mkdir "$from"
        fi
        record_mount "$source/Jiný svazek" --bind "$from"
        printf 'protected\n' >"$source/Jiný svazek/chráněný.txt"
    fi
    "$probe" "$mode" "$source" "$drive"
    echo "PASS $mode"
done
if command -v mkfs.vfat >/dev/null; then
    mkdir "$tmp/fat" "$tmp/fat-source"
    chmod 0777 "$tmp/fat-source"
    mkdir "$tmp/fat-source/Přesun na FAT"
    printf 'FAT filename fixture\n' >"$tmp/fat-source/Přesun na FAT/Příliš žluťoučký.txt"
    chown -R nobody "$tmp/fat-source/Přesun na FAT"
    truncate -s 32M "$tmp/fat.img"
    mkfs.vfat "$tmp/fat.img"
    record_mount "$tmp/fat" -o "loop,utf8,uid=$(id -u nobody),gid=$(id -g nobody),fmask=0133,dmask=0022" "$tmp/fat.img"
    runuser -u nobody -- "$probe" move "$tmp/fat-source/Přesun na FAT" "$tmp/fat"
    grep -Fx 'FAT filename fixture' "$tmp/fat/Přesun na FAT/Příliš žluťoučký.txt"
    echo "PASS move and fsync to unlabeled FAT image"
fi
dd if=/dev/zero of="$tmp/velký soubor" bs=1M count=1 status=none
"$probe" space "$tmp/velký soubor" "$tmp/full"
"$probe" readonly "$tmp/velký soubor" "$tmp/ro"
mkdir -m 0555 "$tmp/protected"
runuser -u nobody -- "$probe" permission "$tmp/velký soubor" "$tmp/protected"
"$probe" pipe
echo "PASS ENOSPC, EROFS, EACCES and pipe drain deadline"

truncate -s 2G "$tmp/oversized.deb"
"$probe" reject "$tmp/oversized.deb" '64 MiB review limit'
unshare --mount --propagation private -- bash -ec '
    mount -t tmpfs -o size=32k tmpfs /var/tmp
    "$1" reject "$2" "Not enough free space for Debian package staging"
' _ "$probe" "$tmp/velký soubor"
echo "PASS sparse 2 GiB archive rejected and staging free space checked"

# Isolate recovery artifacts from the host and retain them through all assertions.
record_mount /var/tmp -t tmpfs -o mode=1777 tmpfs

for version in 1 2; do
    mkdir -p "$tmp/pkg/DEBIAN" "$tmp/pkg/usr/share/doc/cz-zkouska-files"
    printf 'Package: cz-zkouska-files\nArchitecture: all\nVersion: 1.0.0\nMaintainer: Test <test@example.invalid>\nDescription: Fixture %s\n' "$version" >"$tmp/pkg/DEBIAN/control"
    printf '%s\n' "$version" >"$tmp/pkg/usr/share/doc/cz-zkouska-files/payload"
    printf '#!/bin/sh\nexit 0\n' >"$tmp/pkg/DEBIAN/postinst"
    chmod 0755 "$tmp/pkg/DEBIAN/postinst"
    dpkg-deb --build --root-owner-group "$tmp/pkg" "$tmp/balíček $version.deb"
done
deb="$tmp/balíček 1.deb"
ln -s "$deb" "$drive/odkaz.deb"
"$probe" symlink "$drive/odkaz.deb"
"$probe" review "$deb"

if [[ ! -d $appdir ]]; then
    mkdir -- "$appdir"
    created_appdir=true
fi
record_mount "$appdir" -t tmpfs -o mode=0755 tmpfs
mkdir "$appdir/bin"
install -o root -g root -m 0755 -- "$app" "$appdir/bin/M5CardputerZero-Files"
record_mount /run -t tmpfs tmpfs
mkdir "$tmp/store-state"
touch "$tmp/real-dpkg" "$tmp/real-apt"
record_mount "$tmp/real-dpkg" --bind /usr/bin/dpkg
record_mount "$tmp/real-apt" --bind /usr/bin/apt-get
export TEST_DIR="$tmp" TEST_LOG="$tmp/manager.log" M5APPSTORE_STATE_DIR="$tmp/store-state"

cat >"$tmp/sudo" <<'FAKE'
#!/bin/sh
set -eu
case "$*" in
    "-n -- /usr/share/APPLaunch/bin/M5CardputerZero-Files --files-install-reviewed-deb "*) ;;
    *) echo "unexpected privileged command"; exit 1;;
esac
shift 2
export SUDO_UID=0
exec "$@"
FAKE
cat >"$tmp/dpkg" <<'FAKE'
#!/bin/sh
set -eu
case "${1:-}" in
    --audit)
        if [ -f "$TEST_DIR/broken" ]; then echo "cz-zkouska-files is unpacked but unconfigured"; fi
        exit 0;;
    --force-confdef)
        [ "$#" = 4 ] && [ "$2" = --force-confold ] && [ "$3" = --install ] || exit 24
        [ "$(stat -c '%u:%a' "$4")" = 0:644 ]
        [ "$(stat -c '%u:%a' "$(dirname "$4")")" = 0:711 ]
        if runuser -u nobody -- test -w "$4"; then exit 25; fi
        printf 'dpkg %s\n' "$*" >>"$TEST_LOG"
        if [ -n "${TEST_SYNC_TRACE:-}" ]; then printf 'manager\n' >>"$TEST_SYNC_TRACE"; fi
        touch "$TEST_DIR/running"
        if [ -n "${TEST_TIMEOUT:-}" ]; then
            touch "$TEST_DIR/broken"
            (trap '' TERM; exec sleep 60) &
            echo "$!" >"$TEST_DIR/manager-child"
            exec >&- 2>&-
            wait
        fi
        if [ -n "${TEST_HOLD:-}" ]; then
            echo "$$" >"$TEST_DIR/held-manager"
            for attempt in $(seq 1 200); do
                if [ -e "$TEST_DIR/release-manager" ]; then break; fi
                sleep 0.05
            done
            [ -e "$TEST_DIR/release-manager" ]
        fi
        sleep 1
        if [ -n "${TEST_FAIL:-}" ]; then touch "$TEST_DIR/broken"; echo "fixture postinst failed"; exit 42; fi
        if [ -n "${TEST_DETACHED:-}" ]; then
            setsid sh -c 'echo "$$" >"$TEST_DIR/detached-child"; exec sleep 30' </dev/null >/dev/null 2>&1 &
            for attempt in $(seq 1 100); do
                [ ! -s "$TEST_DIR/detached-child" ] || break
                sleep 0.02
            done
            [ -s "$TEST_DIR/detached-child" ]
        fi
        touch "$TEST_DIR/installed"
        exit 0;;
    --install) echo 'missing conffile options'; exit 26;;
    *) exec "$TEST_DIR/real-dpkg" "$@";;
esac
FAKE
cat >"$tmp/dpkg-query" <<'FAKE'
#!/bin/sh
set -eu
test -f "$TEST_DIR/installed"
printf 'install ok installed\n1.0.0'
FAKE
cat >"$tmp/apt-get" <<'FAKE'
#!/bin/sh
set -eu
if [ -z "${TEST_APT:-}" ]; then exec "$TEST_DIR/real-apt" "$@"; fi
for arg in "$@"; do
    case "$arg" in
        --simulate)
            printf 'Inst cz-files-fixture-dependency (1.0 Test [all])\nInst cz-zkouska-files (1.0.0 local-deb [all])\nConf cz-zkouska-files (1.0.0 local-deb [all])\n'
            exit 0;;
        Dpkg::Options::=--force-confdef) confdef=yes;;
        Dpkg::Options::=--force-confold) confold=yes;;
        DPkg::Pre-Install-Pkgs::=*) hook=${arg#*=};;
    esac
done
[ "${confdef:-}:${confold:-}" = yes:yes ]
[ -x "$hook" ]
if printf '/wrong/archive.deb\n' | "$hook"; then exit 23; fi
printf '%s\n' "$arg" | "$hook"
printf 'apt-get %s\n' "$*" >>"$TEST_LOG"
touch "$TEST_DIR/installed"
FAKE
chmod 0755 "$tmp"/{sudo,dpkg,dpkg-query,apt-get}
for tool in sudo dpkg dpkg-query apt-get; do
    record_mount "/usr/bin/$tool" --bind "$tmp/$tool"
done

install_fake_store() {
cat >"$appdir/bin/M5CardputerZero-AppStore" <<'FAKE'
#!/bin/sh
set -eu
[ "$#" = 8 ] && [ "$1" = --package-helper ] && [ "$2" = install ] &&
    [ "$3" = --package-value ] && [ "$5" = --package-transaction ] &&
    [ "$6" = files-local ] && [ "$7" = --package-pending-path ]
[ "$8" = "$M5APPSTORE_STATE_DIR/pending-package.json" ]
grep -F "\"deb_path\":\"$4\"" "$8"
grep -F 'expected_md5' "$8"
printf 'Store helper invoked\n' >>"$TEST_LOG"
if [ -n "${TEST_ORPHAN:-}" ]; then
    dpkg --install "$4" &
    for attempt in $(seq 1 200); do
        if [ -s "$TEST_DIR/held-manager" ]; then
            touch "$TEST_DIR/store-exiting"
            exit 40
        fi
        sleep 0.05
    done
    exit 41
fi
exec dpkg --install "$4"
FAKE
chmod 0755 "$appdir/bin/M5CardputerZero-AppStore"
}
"$probe" changed "$deb" "$tmp/balíček 2.deb"
[[ ! -e $TEST_LOG ]]
echo "PASS archive replacement rejected before dpkg"

TEST_HOLD=1 "$probe" install "$deb" >"$tmp/first.log" &
first=$!
for ((try=0; try<400; ++try)); do
    [[ ! -s $tmp/held-manager ]] || break
    sleep 0.02
done
[[ -s $tmp/held-manager ]]
if "$probe" install "$deb" >"$tmp/second.log"; then exit 27; fi
touch "$tmp/release-manager"
wait "$first"
cat "$tmp/first.log" "$tmp/second.log"
grep -F 'Another Files or Store package transaction is running' "$tmp/second.log"
[[ $(grep -c '^dpkg ' "$TEST_LOG") == 1 ]]
echo "PASS overlapping Files transactions"

install_fake_store
exec 8>"$M5APPSTORE_STATE_DIR/package-transaction.lock"
flock -n 8
if "$probe" install "$deb" >"$tmp/busy-store.log"; then exit 28; fi
grep -F 'Another Files or Store package transaction is running' "$tmp/busy-store.log"
flock -u 8
exec 8>&-
"$probe" install "$deb"
grep -F 'Store helper invoked' "$TEST_LOG"
echo "PASS Store lock, helper, flags and protected input"

TEST_APT=1 "$probe" install "$deb" | tee "$tmp/apt.log"
grep -F 'Inst cz-files-fixture-dependency' "$tmp/apt.log"
grep -F 'apt-get -y --reinstall --no-remove --no-install-recommends' "$TEST_LOG"
echo "PASS Store APT dependencies, conffile flags and archive check"

TEST_FAIL=1 "$probe" install-failure "$deb" | tee "$tmp/failure.log"
grep -F 'fixture postinst failed' "$tmp/failure.log"
grep -F 'dpkg --audit reported unfinished packages' "$tmp/failure.log"
recovery_record() {
    local directory
    directory=$(sed -n 's/^Recovery files kept in \(\/var\/tmp\/cardputer-files-install-[[:alnum:]]*\)\..*/\1/p' "$1" | sort -u)
    [[ -n $directory && -f $directory/pending-package.json && -f $directory/package.deb ]]
    grep -F '"package":"cz-zkouska-files"' "$directory/pending-package.json"
    grep -F "$(sha256sum "$deb" | cut -d ' ' -f 1)" "$directory/pending-package.json"
    cmp "$directory/package.deb" "$deb"
}
recovery_record "$tmp/failure.log"
echo "PASS failed Store installation retained its recovery record and reviewed archive"

rm -- "$appdir/bin/M5CardputerZero-AppStore" "$tmp/broken"
SECONDS=0
M5APPSTORE_PACKAGE_TIMEOUT=1 TEST_TIMEOUT=1 "$probe" install-timeout "$deb" | tee "$tmp/timeout.log"
[[ $SECONDS -lt 8 ]]
child=$(cat "$tmp/manager-child")
if [[ -r /proc/$child/stat ]]; then
    read -r _ _ state _ </proc/"$child"/stat || state=Z
    [[ $state == Z ]]
fi
recovery_record "$tmp/timeout.log"
echo "PASS direct install deadline after output closed, process group stopped and recovery retained"

rm -- "$tmp/broken"
"$probe" install "$deb"
shopt -s nullglob
stages=(/var/tmp/cardputer-files-install-*)
[[ ${#stages[@]} -eq 2 ]]
echo "PASS successful install cleaned its own stage and kept both earlier recovery records"

# Exercise the privileged entry point directly so the test can kill its exact PID.
plan=$(apt-get --simulate -y --reinstall --no-remove --no-install-recommends install "$deb" |
    sed -n '/^\(Inst\|Conf\|Remv\|Purg\) /p' | sha256sum | cut -d ' ' -f 1)
archive=$(sha256sum "$deb" | cut -d ' ' -f 1)
helper=("$appdir/bin/M5CardputerZero-Files" --files-install-reviewed-deb "$archive" "$plan"
        cz-zkouska-files 1.0.0 "$M5APPSTORE_STATE_DIR")
export SUDO_UID=0
wait_for_manager() {
    for ((try=0; try<400; ++try)); do
        [[ ! -s $tmp/held-manager ]] || return 0
        sleep 0.02
    done
    return 1
}

install_fake_store
if TEST_DETACHED=1 timeout 8 "${helper[@]}" <"$deb" >"$tmp/detached.log" 2>&1; then
    detached_status=0
else
    detached_status=$?
fi
cat "$tmp/detached.log"
[[ -s $tmp/detached-child ]]
kill -0 "$(cat "$tmp/detached-child")"
if (( detached_status != 0 )); then
    echo "FAIL detached maintainer child: installer exit $detached_status"
    exit "$detached_status"
fi
flock -n /run/cardputer-files-package.lock true
flock -n "$M5APPSTORE_STATE_DIR/package-transaction.lock" true
kill "$(cat "$tmp/detached-child")"
rm -- "$tmp/detached-child" "$appdir/bin/M5CardputerZero-AppStore"
echo "PASS installer and both locks released while a detached maintainer child stayed alive"

if [[ ${FILES_DEB_REGRESSION:-all} != d2 ]]; then
    failures=0
    for variant in direct store orphan; do
        rm -f -- "$tmp/held-manager" "$tmp/release-manager"
        unset TEST_ORPHAN
        if [[ $variant == direct ]]; then
            rm -f -- "$appdir/bin/M5CardputerZero-AppStore"
        else
            install_fake_store
            if [[ $variant == orphan ]]; then export TEST_ORPHAN=1; fi
        fi
        TEST_HOLD=1 "${helper[@]}" <"$deb" >"$tmp/held.log" 2>&1 &
        owner=$!
        wait_for_manager || { cat "$tmp/held.log"; exit 1; }
        manager=$(cat "$tmp/held-manager")
        if [[ $variant != orphan ]]; then
            kill -KILL "$owner"
            wait "$owner" 2>/dev/null || :
        else
            # The mock reports its exit before we challenge the locks.
            for ((try=0; try<100; ++try)); do
                [[ ! -e $tmp/store-exiting ]] || break
                sleep 0.02
            done
            (( try < 100 ))
            sleep 0.3
        fi
        kill -0 "$manager"
        failure=0
        if [[ $variant != direct ]] && flock -n "$M5APPSTORE_STATE_DIR/package-transaction.lock" true; then
            echo "FAIL D1 ($variant): Store acquired its lock while manager $manager was alive"
            failure=1
        fi
        if "${helper[@]}" <"$deb" >"$tmp/peer.log" 2>&1; then
            echo "FAIL D1 ($variant): second Files install started while manager $manager was alive"
            failure=1
        elif ! grep -F 'Another Files or Store package transaction is running' "$tmp/peer.log"; then
            cat "$tmp/peer.log"
            failure=1
        fi
        touch "$tmp/release-manager"
        for ((try=0; try<400; ++try)); do
            if flock -n /run/cardputer-files-package.lock true &&
                flock -n "$M5APPSTORE_STATE_DIR/package-transaction.lock" true &&
                { [[ ! -r /proc/$manager/stat ]] || grep -q ') Z ' "/proc/$manager/stat"; }; then break; fi
            sleep 0.02
        done
        (( try < 400 ))
        if [[ $variant == orphan ]]; then wait "$owner" && exit 1; fi
        if (( failure == 0 )); then
            echo "PASS D1 ($variant): peers excluded until the orphan transaction ended"
        else
            failures=$((failures + 1))
        fi
    done
    (( failures == 0 ))
fi

if [[ ${FILES_DEB_REGRESSION:-all} != d1 ]]; then
    unset TEST_ORPHAN
    cc -shared -fPIC -o "$tmp/deb-sync.so" "$(dirname "$(realpath "$0")")/deb_sync_fault.c" -ldl
    export TEST_SYNC_TRACE="$tmp/deb-sync.log"
    failures=0
    for variant in direct store; do
        if [[ $variant == direct ]]; then
            rm -f -- "$appdir/bin/M5CardputerZero-AppStore"
        else
            install_fake_store
        fi
        : >"$TEST_SYNC_TRACE"
        LD_PRELOAD="$tmp/deb-sync.so" "${helper[@]}" <"$deb" >"$tmp/sync.log" 2>&1
        printf '%s\n' archive record stage parent manager >"$tmp/expected-sync"
        if ! diff -u "$tmp/expected-sync" "$TEST_SYNC_TRACE"; then
            echo "FAIL D2 ($variant): package manager ran without all four successful barriers"
            failures=$((failures + 1))
            continue
        fi
        for point in archive record stage parent; do
            : >"$TEST_SYNC_TRACE"
            if TEST_SYNC_FAIL=$point LD_PRELOAD="$tmp/deb-sync.so" "${helper[@]}" <"$deb" >"$tmp/sync.log" 2>&1; then
                echo "FAIL D2 ($variant): install succeeded with failed $point fsync"
                exit 1
            fi
            grep -Fx "failed $point" "$TEST_SYNC_TRACE"
            if grep -Fx manager "$TEST_SYNC_TRACE"; then exit 1; fi
        done
        echo "PASS D2 ($variant): archive, record, stage and parent synced before launch; all four EIOs blocked it"
    done
    (( failures == 0 ))
fi
