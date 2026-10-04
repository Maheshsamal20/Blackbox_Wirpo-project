#!/usr/bin/env bash
# End-to-end demo (run as root in your VM):  sudo scripts/demo.sh
#   1. loads the kernel module
#   2. starts blackboxd
#   3. runs crash_demo, which logs events and then segfaults on purpose
#   4. shows the snapshot that was written automatically
set -e
cd "$(dirname "$0")/.."
[ "$(id -u)" -eq 0 ] || { echo "run as root: sudo scripts/demo.sh"; exit 2; }
[ -f driver/blackbox.ko ] && [ -x build/blackboxd ] || { echo "build first: make driver && make"; exit 2; }

OUT=/tmp/blackbox-demo
rm -rf "$OUT"
lsmod | grep -q '^blackbox ' || insmod driver/blackbox.ko buf_events=256 heartbeat_ms=1000
cat /proc/blackbox
echo

./build/blackboxd -o "$OUT" -c 500 &
DPID=$!
sleep 1

echo ">>> starting crash_demo (it will segfault at the end)"
./build/crash_demo || echo ">>> crash_demo died as planned (exit code $?)"
sleep 1

echo
echo ">>> snapshot written by blackboxd:"
SNAP=$(ls -t "$OUT"/blackbox-*.snap | head -1)
echo "$SNAP"
echo "-------------------------------------------------------------"
head -12 "$SNAP"
echo "   ..."
tail -8 "$SNAP"
echo "-------------------------------------------------------------"

echo
./build/bbctl stats

kill -TERM "$DPID"; wait "$DPID" 2>/dev/null || true
rmmod blackbox
echo "demo finished; files are in $OUT"
