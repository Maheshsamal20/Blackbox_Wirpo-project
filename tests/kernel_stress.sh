#!/usr/bin/env bash
# Stress test for the kernel module (run as root in your VM): several writers + one reader.
set -u
cd "$(dirname "$0")/.."
[ "$(id -u)" -eq 0 ] || { echo "run as root"; exit 2; }
lsmod | grep -q '^blackbox ' && rmmod blackbox
dmesg -C 2>/dev/null
insmod driver/blackbox.ko buf_events=1024 heartbeat_ms=10 || exit 1
./build/stress -w 8 -n 20000
RC=$?
rmmod blackbox
if dmesg | grep -Eq "BUG:|WARNING:|Oops|soft lockup|general protection"; then
    echo "kernel reported problems:"; dmesg | tail -20; RC=1
else
    echo "dmesg clean"
fi
exit $RC
