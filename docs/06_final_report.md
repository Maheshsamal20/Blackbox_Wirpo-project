# Stage 6 – Final Report

## 1. Summary
BlackBox is a flight recorder for Linux. A kernel module (`blackbox.ko`) exposes `/dev/blackbox`
and keeps the newest N events in a ring buffer. A multi-threaded C++ daemon (`blackboxd`) streams
events to a rotating log and, on an ERROR event or `SIGUSR1`, freezes the recorder, reads the whole
history and writes a snapshot file. `bbctl` controls the system; `crash_demo` shows the idea end to end.

## 2. Requirements coverage
| Requirement | Status | Evidence |
|---|---|---|
| FR-1 … FR-3 write, event fields, ring buffer | implemented | `driver/blackbox.c`, `tests/kernel_smoke.sh` |
| FR-4, FR-5 read with cursors, poll | implemented | driver, kernel smoke (poll/blocking test), `tests/stress.cpp` |
| FR-6 ioctl set | implemented | driver, `bbctl`, kernel smoke |
| FR-7 `/proc/blackbox` | implemented | driver, kernel smoke |
| FR-8 heartbeat timer | implemented | driver, kernel smoke |
| FR-9 live rotating log | implemented, tested | unit test `rotating_log`, integration |
| FR-10, FR-11 snapshot on trigger / SIGUSR1 | implemented, tested | integration test (mock), `scripts/demo.sh` |
| FR-12 snapshot rotation | implemented, tested | unit test `snapshot_writer` |
| FR-13 `bbctl` commands | implemented | integration (`snapshot`), kernel smoke (others) |
| FR-14 daemon mode, pidfile, clean shutdown | implemented, tested | integration test |
| NFR-1 … NFR-7 | see section 4 | |

## 3. Final architecture
See `docs/03_design.md` for the architecture diagram, class diagram, sequence diagram and state machines.

Key design points:
- one irq-safe spinlock protects the ring; user copies are done outside it
- per-open-file read cursors let several readers share the same history
- writers never block; old events are overwritten
- the daemon separates reading (poll + read) from processing (format, log, trigger, snapshot) using a bounded queue
- snapshots are freeze → seek to oldest → drain → write temp file → rename → unfreeze (guarded by RAII)

## 4. Testing and results
| Level | Result |
|---|---|
| Unit tests | 10 tests, 2049 checks, all pass |
| ASan + UBSan, TSan | clean |
| Mock-device integration | 17 / 17 pass |
| Compiler warnings | none with `-Wall -Wextra` |
| Kernel smoke test | **fill in after running in the VM** (`sudo bash tests/kernel_smoke.sh`) |
| Kernel stress test | **fill in after running in the VM** (`sudo bash tests/kernel_stress.sh`) |
| `dmesg` after unload | **fill in** (should show no BUG/WARN) |

Paste the final console output of the kernel tests into `docs/results/` (create the folder) and commit it
as evidence for the presentation.

## 5. Achievements
- A complete kernel ↔ user-space system with a shared, fixed ABI.
- Exercises char device operations, wait queues, spinlocks, timers, procfs, ioctl and module parameters.
- Modern C++: RAII, templates, threads, `std::filesystem`, bounded queue, state machine.
- Testing without hardware or kernel access through the mock mode.
- Documentation, UML and Git history organised by project stage.

## 6. Limitations
- The ring buffer is volatile: it is lost on reboot or `rmmod` (no persistence).
- While a snapshot is being taken (a few milliseconds), application writes fail with `EBUSY` and are only counted.
- The buffer size is fixed at module load (`buf_events`); no resize at runtime.
- Messages are limited to 103 characters; timestamps are wall-clock time (can jump if the clock is changed).
- Device node is world-writable (mode 0666) for demo convenience; a production system should use a group and udev rules.
- No authentication or rate limiting for writers.

## 7. Possible future improvements
- Hook kernel tracepoints / kprobes so the recorder captures system events automatically.
- Persist the buffer (pstore / ramoops) so history survives a kernel panic.
- Per-writer rate limiting and per-process filters.
- Binary snapshot format plus a viewer, or a small web UI.
- Runtime resizing of the buffer via ioctl; `epoll`-based multi-device support.
- systemd unit file and udev rule for packaging.

## 8. Lessons learned
- Kernel APIs change between versions, so version guards and testing on the target kernel matter.
- Keep critical sections tiny and never touch user memory under a spinlock.
- A mock device for the user-space half made it possible to test the daemon logic early and repeatably.
- Writing the design (UML, locking rules) first made the implementation much simpler.

## 9. Deliverables checklist
- [x] Source code (driver, daemon, CLI, demo, tests)
- [x] Documentation per stage and UML diagrams (Mermaid, rendered by GitHub)
- [x] Git repository with a tag per stage
- [x] Presentation outline
- [ ] Kernel test results recorded from your VM
- [ ] Short screen recording of the demo (recommended)
