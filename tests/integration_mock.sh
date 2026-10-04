#!/usr/bin/env bash
# Integration test of blackboxd without the kernel module: a FIFO carrying binary
# bb_event records plays the role of /dev/blackbox (daemon --mock mode).
set -u
cd "$(dirname "$0")/.."

TMP=$(mktemp -d)
FIFO=$TMP/fake_blackbox
OUT=$TMP/out
mkfifo "$FIFO"
PASS=0; FAIL=0
ok()   { echo "  [ OK ] $1"; PASS=$((PASS+1)); }
bad()  { echo "  [FAIL] $1"; FAIL=$((FAIL+1)); }
check(){ if eval "$2"; then ok "$1"; else bad "$1"; fi; }
snaps(){ ls "$OUT"/blackbox-*.snap 2>/dev/null | wc -l; }

./build/blackboxd --mock -D "$FIFO" -o "$OUT" -c 1500 -k 5 2> "$TMP/daemon.err" &
DPID=$!
for _ in $(seq 1 50); do [ -f "$OUT/blackboxd.pid" ] && break; sleep 0.1; done
check "daemon wrote its pidfile" '[ -f "$OUT/blackboxd.pid" ]'

exec 3>"$FIFO"                                    # we are now the "kernel" side

echo "== 20 INFO events, then one ERROR =="
python3 tests/gen_events.py 0 20 1 "normal event" >&3
sleep 0.5
check "no snapshot before any trigger" '[ "$(snaps)" -eq 0 ]'
python3 tests/gen_events.py 20 1 3 "disk failure" >&3
sleep 1
check "ERROR event produced exactly one snapshot" '[ "$(snaps)" -eq 1 ]'
S1=$(ls "$OUT"/blackbox-*.snap | head -1)
check "snapshot header reports 21 events"        'grep -q "^# events:  21" "$S1"'
check "snapshot contains the oldest event"        'grep -q "normal event 0" "$S1"'
check "snapshot contains the triggering ERROR"    'grep -q "disk failure" "$S1"'
check "snapshot names the trigger reason"         'grep -q "^# reason:.*trigger: ERROR" "$S1"'

echo "== second ERROR inside the cooldown window =="
python3 tests/gen_events.py 21 1 3 "another failure" >&3
sleep 0.5
check "cooldown suppressed a second snapshot"     '[ "$(snaps)" -eq 1 ]'

echo "== ERROR after the cooldown =="
sleep 1.5
python3 tests/gen_events.py 22 1 3 "later failure" >&3
sleep 1
check "snapshot taken again after cooldown"       '[ "$(snaps)" -eq 2 ]'

echo "== manual snapshot via SIGUSR1 =="
kill -USR1 "$DPID"
sleep 1
check "SIGUSR1 produced a snapshot"               '[ "$(snaps)" -eq 3 ]'
check "manual snapshot says so"                   'grep -lq "manual request" "$OUT"/blackbox-*.snap'

echo "== live log =="
check "live log has every event (23)"             '[ "$(wc -l < "$OUT/blackbox.log")" -eq 23 ]'

echo "== bbctl snapshot command =="
./build/bbctl -p "$OUT/blackboxd.pid" snapshot > /dev/null
sleep 1
check "bbctl snapshot produced a snapshot"        '[ "$(snaps)" -eq 4 ]'

echo "== clean shutdown =="
kill -TERM "$DPID"
WAITED=0
while kill -0 "$DPID" 2>/dev/null && [ $WAITED -lt 50 ]; do sleep 0.1; WAITED=$((WAITED+1)); done
check "daemon exited after SIGTERM"               '! kill -0 "$DPID" 2>/dev/null'
wait "$DPID"; RC=$?
check "exit code 0"                               '[ "$RC" -eq 0 ]'
check "pidfile removed"                           '[ ! -f "$OUT/blackboxd.pid" ]'
check "state transitions were logged"             'grep -q "RECORDING -> FROZEN" "$TMP/daemon.err"'
exec 3>&-

echo
echo "integration: $PASS passed, $FAIL failed"
[ $FAIL -eq 0 ] && rm -rf "$TMP" || echo "kept $TMP for inspection"
[ $FAIL -eq 0 ]
