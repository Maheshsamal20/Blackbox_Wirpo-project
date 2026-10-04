# Stage 5 – Testing, Integration and Improvement

## What was added in this stage
| Area | Change |
|---|---|
| Driver | `poll()`, ioctl (FREEZE / UNFREEZE / CLEAR / GET_STATS / SEEK_OLDEST), `/proc/blackbox`, heartbeat kernel timer, device node mode 0666, safe timer shutdown |
| `libbbcommon` | `BoundedQueue<T>`, `RotatingLog`, `SnapshotWriter` |
| `blackboxd` | two-thread design (reader + worker), trigger logic with cooldown, snapshot state machine, SIGUSR1/SIGINT/SIGTERM handling, pidfile, `-d` daemonize, `--mock` mode |
| `bbctl` | `dump`, `freeze`, `unfreeze`, `clear`, `stats`, `snapshot` |
| Demo | `crash_demo` – logs, degrades, then crashes on purpose |
| Tests | unit tests, mock-device integration test, kernel smoke test, kernel stress test |

## Test strategy
| Level | What | Where | Needs kernel module? |
|---|---|---|---|
| Unit | formatting, level parsing, ABI struct sizes, bounded queue (order, blocking, close), rotating log, snapshot writer + rotation, device wrapper (partial reads, unsupported ioctl) | `tests/test_main.cpp` (`make unit`) | no |
| Sanitizers | the same unit tests under AddressSanitizer + UBSan and ThreadSanitizer | `make asan`, `make tsan` | no |
| Integration | real `blackboxd` + `bbctl` against a FIFO carrying binary events (`--mock`): trigger, cooldown, SIGUSR1, live log, shutdown | `tests/integration_mock.sh` (`make integration`) | no |
| System | load the module; write/read, level prefix, overwrite, freeze, clear, poll wake-up, heartbeat, unload, dmesg clean | `tests/kernel_smoke.sh` | **yes (root, VM)** |
| Stress | 8 writers × 20 000 events + 1 reader; checks ordering and counters | `tests/kernel_stress.sh` / `build/stress` | **yes (root, VM)** |

## Results

### User-space tests (executed during development)
| Suite | Result |
|---|---|
| Unit tests | 10 tests, 2049 checks, 0 failures |
| ASan + UBSan | clean |
| ThreadSanitizer | clean (after fixing a race in the test harness, see below) |
| Integration (mock) | 17 / 17 checks passed |
| Compiler warnings (`-Wall -Wextra`) | none |

### Kernel tests (run these in your VM and record the output here)
The kernel module could not be built or loaded in the environment where the code was written
(no kernel headers, no module loading). Run the commands below and fill in the table.

```bash
make driver && make
sudo bash tests/kernel_smoke.sh
sudo bash tests/kernel_stress.sh
```

| Suite | Date | Kernel version | Result | Notes |
|---|---|---|---|---|
| `kernel_smoke.sh` | | | | |
| `kernel_stress.sh` | | | | |
| `valgrind ./build/blackboxd` (optional) | | | | |

## Issues found and fixed
| # | Issue | Cause | Fix |
|---|---|---|---|
| 1 | Daemon spun at 100 % CPU when the writer side of the mock FIFO closed | `poll()` reports hang-up and `read()` returns 0 | reader sleeps 50 ms on a zero read |
| 2 | A burst of ERROR events would create many snapshots | trigger fired for every ERROR | cooldown (`--cooldown`, default 2 s) |
| 3 | Snapshot could leave the recorder frozen if an exception occurred | manual freeze/unfreeze pairing | RAII `FreezeGuard` always unfreezes |
| 4 | Partially written snapshot visible to readers | direct write to final name | write `*.tmp`, then `rename()` |
| 5 | ThreadSanitizer data race report | the test runner's global check counter was updated from two threads | made the counters `std::atomic` (test bug, not a library bug) |
| 6 | Partial event read from a pipe would be lost | `read()` may return half an event | `BlackBoxDevice` keeps a carry buffer (covered by a unit test) |
| 7 | Timer could re-arm itself during `rmmod` | classic timer shutdown race | `stopping` flag + two `del_timer_sync` calls |
| 8 | Build error: `umask` undeclared | missing `<sys/stat.h>` | include added |

## Quality improvements
- Locking rules documented and followed (no user copies under the spinlock).
- Version guards for kernel API changes (`class_create`, `devnode`, timer deletion).
- Bounded daemon queue (no unbounded memory growth); kernel ring is fixed-size.
- Snapshot header records `dropped_while_frozen` so data loss during the freeze window is visible.

## Known limitation discovered
While the recorder is frozen (a few milliseconds during a snapshot), writes from applications fail with `EBUSY`
and are only counted. This is intentional (a stable snapshot) and is listed in the final report as a limitation.

## Next stage
Final polish, README, demo script, final report and presentation.
