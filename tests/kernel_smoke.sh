#!/usr/bin/env bash
# Smoke test for the kernel module. Run as root inside your VM:
#   make driver && make && sudo tests/kernel_smoke.sh
set -u
cd "$(dirname "$0")/.."
[ "$(id -u)" -eq 0 ] || { echo "run as root (needs insmod/rmmod)"; exit 2; }
[ -f driver/blackbox.ko ] || { echo "build the module first: make driver"; exit 2; }

BBCTL=./build/bbctl
PASS=0; FAIL=0
ok()   { echo "  [ OK ] $1"; PASS=$((PASS+1)); }
bad()  { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); }
check(){ if eval "$2"; then ok "$1"; else bad "$1"; fi; }

lsmod | grep -q '^blackbox ' && rmmod blackbox
dmesg -C 2>/dev/null

insmod driver/blackbox.ko buf_events=64 heartbeat_ms=0 || { echo "insmod failed"; exit 1; }
sleep 0.3
check "device node exists"                    '[ -c /dev/blackbox ]'
check "/proc/blackbox exists"                 '[ -r /proc/blackbox ]'
check "initial state is RECORDING"            'grep -q "RECORDING" /proc/blackbox'
check "capacity is 64"                        'grep -q "capacity:       64" /proc/blackbox'

echo "== write / read =="
echo "hello" > /dev/blackbox
echo "<3>failure case" > /dev/blackbox
$BBCTL log -l warn "from bbctl"
OUT=$($BBCTL dump)
check "dump shows plain message"              'echo "$OUT" | grep -q "INFO  user: hello"'
check "level prefix <3> becomes ERROR"        'echo "$OUT" | grep -q "ERROR user: failure case"'
check "bbctl log -l warn becomes WARN"        'echo "$OUT" | grep -q "WARN  user: from bbctl"'
check "three events stored"                   '[ "$($BBCTL dump | wc -l)" -eq 3 ]'
check "dump -n 1 prints only the last"        '[ "$($BBCTL dump -n 1 | wc -l)" -eq 1 ]'

echo "== edge cases =="
python3 -c "import os; fd=os.open('/dev/blackbox', os.O_WRONLY); os.write(fd, b'')"
check "empty write is accepted and stores nothing" '[ "$($BBCTL dump | wc -l)" -eq 3 ]'
python3 -c "import os; fd=os.open('/dev/blackbox', os.O_WRONLY); os.write(fd, b'A'*500)"
check "oversized write is truncated, not rejected" '[ "$($BBCTL dump | wc -l)" -eq 4 ]'
check "stored message length <= 103"          '[ "$($BBCTL dump -n 1 | sed "s/.*user: //" | tr -d "\n" | wc -c)" -le 103 ]'

echo "== ring overwrite =="
for i in $(seq 1 100); do echo "fill $i" > /dev/blackbox; done
check "buffer holds only capacity (64) events" '[ "$($BBCTL dump | wc -l)" -eq 64 ]'
check "oldest events were overwritten"        '! $BBCTL dump | grep -q "fill 1$"'
check "newest event is present"               '$BBCTL dump | grep -q "fill 100$"'

echo "== freeze / unfreeze =="
$BBCTL freeze
check "state FROZEN in /proc"                 'grep -q FROZEN /proc/blackbox'
check "write while frozen fails (EBUSY)"      '! (echo "x" > /dev/blackbox) 2>/dev/null'
$BBCTL stats | grep -q "dropped frozen: 1" && ok "dropped counter incremented" || bad "dropped counter incremented"
$BBCTL unfreeze
check "write works after unfreeze"            '(echo "after" > /dev/blackbox) 2>/dev/null'

echo "== clear =="
$BBCTL clear
check "clear empties the buffer"              '[ "$($BBCTL dump | wc -l)" -eq 0 ]'

echo "== poll / blocking read =="
( sleep 0.5; echo "<1>wake up" > /dev/blackbox ) &
RES=$(timeout 3 $BBCTL tail | head -1)
check "tail wakes up on a new event"          'echo "$RES" | grep -q "wake up"'

echo "== heartbeat timer =="
rmmod blackbox
insmod driver/blackbox.ko buf_events=64 heartbeat_ms=200
sleep 1.2
check "heartbeat events are generated"        '$BBCTL dump | grep -q "heartbeat"'

echo "== unload =="
rmmod blackbox
check "rmmod succeeded"                       '! lsmod | grep -q "^blackbox "'
check "device node removed"                   '[ ! -e /dev/blackbox ]'
check "no BUG/WARN/Oops in dmesg"             '! dmesg | grep -Eq "BUG:|WARNING:|Oops|general protection"'

echo
echo "kernel smoke: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ]
