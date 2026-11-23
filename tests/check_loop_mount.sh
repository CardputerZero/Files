#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 || $EUID != 0 ]]; then
    echo "Usage (root, mount namespace recommended): $0 /path/to/storage_package_tests [ext4|vfat]" >&2
    exit 2
fi

filesystem=${2:-ext4}
case "$filesystem" in
    ext4|vfat) ;;
    *) exit 2 ;;
esac

tmp=$(mktemp -d)
mount_dir=$(mktemp -d /media/files-test-XXXXXX)
mount_options=nosuid,nodev,noexec
if [[ $filesystem == vfat ]]; then mount_options+=,utf8; fi
loop_device=''
cleanup() {
    if mountpoint -q -- "$mount_dir"; then umount -- "$mount_dir"; fi
    if [[ -n $loop_device ]]; then losetup --detach "$loop_device"; fi
    rm -f -- "$tmp/drive.img" "$mount_dir/z disku č. 2.txt"
    rmdir -- "$mount_dir" "$tmp"
}
trap cleanup EXIT
truncate -s 48M "$tmp/drive.img"
case "$filesystem" in
    ext4) mkfs.ext4 -q -F "$tmp/drive.img" ;;
    vfat) mkfs.vfat "$tmp/drive.img" ;;
esac
loop_device=$(losetup --find --show "$tmp/drive.img")
mount -o "$mount_options" "$loop_device" "$mount_dir"
"$1" "$mount_dir"
rm -- "$mount_dir/z disku č. 2.txt"
mount -o "$mount_options" "$loop_device" "$mount_dir"
"$1" "$mount_dir" --disconnect-during-copy
