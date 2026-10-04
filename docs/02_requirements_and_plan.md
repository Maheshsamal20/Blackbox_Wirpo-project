# Stage 2 – Project Requirements Document (PRD) and Development Plan

## 1. Purpose
Define what BlackBox must do, how well it must do it, and how the work will be delivered.

## 2. Modules
| ID | Module | Language | Responsibility |
|---|---|---|---|
| M1 | `blackbox.ko` | C (kernel) | Ring buffer, `/dev/blackbox`, ioctl, poll, `/proc/blackbox`, heartbeat timer |
| M2 | `libbbcommon` | C++17 | Device wrapper (RAII), event formatting, snapshot writer, rotating log, bounded queue |
| M3 | `blackboxd` | C++17 | Daemon: reader thread, worker thread, trigger logic, snapshots, signals, pidfile |
| M4 | `bbctl` | C++17 | Command-line control tool |
| M5 | `crash_demo` | C++17 | Demo workload that logs and then crashes on purpose |
| M6 | tests | C++ / shell | Unit tests, mock-device integration test, kernel smoke + stress tests |

## 3. Functional requirements
| ID | Requirement | Module |
|---|---|---|
| FR-1 | Any process can write a text message to `/dev/blackbox`; an optional `<N>` prefix sets the level (0 DEBUG, 1 INFO, 2 WARN, 3 ERROR). | M1 |
| FR-2 | Each event stores timestamp (ns), sequence number, PID, level, source and message (max 103 chars). | M1 |
| FR-3 | The buffer holds the newest N events (module parameter `buf_events`); oldest are overwritten. | M1 |
| FR-4 | `read()` returns whole events; each open file has its own read cursor; blocking and `O_NONBLOCK` supported. | M1 |
| FR-5 | `poll()`/`epoll` reports readability when unread events exist. | M1 |
| FR-6 | ioctl: FREEZE, UNFREEZE, CLEAR, GET_STATS, SEEK_OLDEST. While frozen, writes are rejected and counted. | M1 |
| FR-7 | `/proc/blackbox` shows capacity, used, total written, dropped, frozen state. | M1 |
| FR-8 | Optional periodic heartbeat event via kernel timer (`heartbeat_ms`). | M1 |
| FR-9 | `blackboxd` streams events to a rotating live log. | M3 |
| FR-10 | `blackboxd` creates a snapshot when an event reaches the trigger level or on `SIGUSR1`. | M3 |
| FR-11 | A snapshot is: freeze → read all history from oldest → write file → unfreeze. | M3 |
| FR-12 | Old snapshots are rotated (keep newest K). | M2 |
| FR-13 | `bbctl` provides `log`, `tail`, `dump`, `freeze`, `unfreeze`, `clear`, `stats`, `snapshot`. | M4 |
| FR-14 | `blackboxd` can run in the foreground or daemonize (`-d`), writes a pidfile and shuts down cleanly on `SIGINT`/`SIGTERM`. | M3 |

## 4. Non-functional requirements
| ID | Requirement | How it is checked |
|---|---|---|
| NFR-1 | **Safety:** the driver never crashes the kernel on bad input (null, oversized, partial buffers, bad ioctl). | negative tests, code review |
| NFR-2 | **Concurrency:** many writers and readers at once with no data races or deadlocks. | stress test, locking review |
| NFR-3 | **Low overhead:** `write()` takes a short spinlock only; `copy_from_user` happens outside the lock. | design review |
| NFR-4 | **No unbounded memory:** fixed-size ring; daemon queue is bounded. | design review, test |
| NFR-5 | **Clean unload:** `rmmod` succeeds with no leaked resources (timer stopped, device and class removed). | smoke test |
| NFR-6 | **Portability:** builds on recent 5.x–6.x kernels (version guards for API changes). | build on target VM |
| NFR-7 | **Maintainability:** shared header for kernel/user ABI, documented design, unit tests, warnings clean. | `-Wall -Wextra` build |

## 5. Constraints and assumptions
- Linux VM (recommended Ubuntu 22.04/24.04) with kernel headers installed; root access for `insmod`.
- Development in a VM so a kernel bug never affects the host.
- Single machine; buffer is volatile.

## 6. Deliverables
Source code, this documentation, UML diagrams, test results, Git repository with stage tags,
final report and demo.

## 7. Development plan and timeline
| Week | Stage | Work | Evidence | Git tag |
|---|---|---|---|---|
| 1 | 1 | Idea, scope, objectives | `01_introduction.md` | `stage-1` |
| 1 | 2 | PRD, plan | this document | `stage-2` |
| 2 | 3 | Architecture, UML, ABI header, Makefiles, Git workflow | `03_design.md` | `stage-3` |
| 3 | 4 | Driver with open/read/write + ring; minimal daemon and `bbctl` | `04_prototype.md` | `stage-4` |
| 4–5 | 5 | poll, ioctl, procfs, timer, trigger logic, tests, fixes | `05_testing.md` | `stage-5` |
| 6 | 6 | Final polish, demo, report, presentation | `06_final_report.md` | `stage-6` |

## 8. Risks
| Risk | Mitigation |
|---|---|
| Kernel bug freezes the machine | Work in a VM; take a snapshot before loading new builds |
| Race conditions in the ring | One spinlock for all ring state; keep critical sections tiny; stress test |
| Kernel API differences between versions | `LINUX_VERSION_CODE` guards; test on the VM kernel |
| Scope creep | Out-of-scope list in Stage 1; extras only after core works |

## 9. Acceptance criteria
1. `insmod blackbox.ko` creates `/dev/blackbox`; `rmmod` removes it cleanly.
2. `echo "<3>boom" > /dev/blackbox` triggers a snapshot containing the preceding events.
3. Stress test (several writers + reader) finishes with consistent sequence numbers and no kernel warnings in `dmesg`.
4. All unit tests and the mock integration test pass.
