#!/usr/bin/env bash
set -euo pipefail
if [[ $# != 1 || $EUID != 0 ]]; then
    echo "Usage (root): $0 /path/to/files_hardening_probe" >&2
    exit 2
fi
if [[ ${FILES_TEST_MOUNT_NAMESPACE:-} != private ]]; then
    export FILES_TEST_MOUNT_NAMESPACE=private
    exec unshare --mount --propagation private -- "$0" "$(realpath "$1")"
fi
probe=$1

tmp=$(mktemp -d)
test_mounts=()
cleanup() {
    for ((index=${#test_mounts[@]}-1; index>=0; --index)); do
        umount -- "${test_mounts[index]}" || umount -l -- "${test_mounts[index]}"
    done
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
