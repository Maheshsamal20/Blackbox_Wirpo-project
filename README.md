# BlackBox – a flight recorder for Linux

A Linux kernel character driver keeps the most recent events in a ring buffer.
When something goes wrong, a C++ daemon freezes that history and saves it to disk –
like the black box of an aircraft.

```
 apps / bbctl ──write──▶  /dev/blackbox  ◀──read/poll/ioctl──  blackboxd ──▶ blackbox.log
                          (ring buffer,                         (reader + worker threads)   *.snap
                           blackbox.ko)                          snapshot on ERROR / SIGUSR1
```

Individual project covering **Linux device drivers**, **system programming** and **C++**,
developed in six documented stages.

## Features
- Kernel module `blackbox.ko`: ring buffer, `read/write/poll/ioctl`, per-file read cursors, `/proc/blackbox`, optional heartbeat timer
- `blackboxd`: multi-threaded daemon, rotating live log, automatic snapshots on ERROR events or `SIGUSR1`, pidfile, `-d` daemon mode
- `bbctl`: `log`, `tail`, `dump`, `freeze`, `unfreeze`, `clear`, `stats`, `snapshot`
- `crash_demo`: logs events and then segfaults, to demonstrate the recorder
- Unit tests, sanitizer runs, mock-device integration test, kernel smoke and stress tests

## Requirements
Linux VM (Ubuntu 22.04/24.04 recommended), `build-essential`, `linux-headers-$(uname -r)`, `git`, `python3` (tests only).
Use a VM: you are loading your own kernel code.

## Build
```bash
make            # user-space: build/blackboxd, build/bbctl, build/crash_demo, build/stress
make driver     # kernel module: driver/blackbox.ko
```

## Quick start
```bash
sudo insmod driver/blackbox.ko buf_events=1024 heartbeat_ms=0
./build/bbctl tail &                          # follow events
echo "<2>disk getting slow" > /dev/blackbox   # <0..3> = debug, info, warn, error
./build/bbctl log -l error "disk failure"
./build/bbctl dump                            # whole stored history
./build/bbctl stats
cat /proc/blackbox
sudo rmmod blackbox
```

## Full demo (crash → automatic snapshot)
```bash
sudo scripts/demo.sh
```

## Running the daemon
```bash
./build/blackboxd -o /var/log/blackbox              # foreground (needs write access to the dir)
./build/blackboxd -d -o /var/log/blackbox -t error  # background
./build/bbctl snapshot                              # ask it for a snapshot now (SIGUSR1)
```
Options: `--trigger LEVEL`, `--keep N`, `--cooldown MS`, `--history N`, `--mock` (see `./build/blackboxd --help`).

## Message format
`echo "<N>text" > /dev/blackbox` where N is 0 DEBUG, 1 INFO, 2 WARN, 3 ERROR (default INFO). Messages are truncated to 103 characters.
`read()` returns whole `struct bb_event` records (see `include/bb_uapi.h`).

## Tests
```bash
make test               # unit tests + mock-device integration test (no kernel module needed)
make asan               # unit tests with AddressSanitizer + UBSan
make tsan               # unit tests with ThreadSanitizer
sudo bash tests/kernel_smoke.sh     # kernel module smoke test (VM, root)
sudo bash tests/kernel_stress.sh    # kernel module stress test (VM, root)
```

## Repository layout
| Path | Content |
|---|---|
| `driver/` | kernel module and kbuild Makefile |
| `include/bb_uapi.h` | ABI shared by kernel and user space |
| `src/common/` | `BlackBoxDevice`, `EventFormat`, `BoundedQueue`, `RotatingLog`, `SnapshotWriter` |
| `src/daemon/` | `blackboxd` |
| `src/cli/` | `bbctl` |
| `src/demo/` | `crash_demo` |
| `tests/` | unit, integration, kernel smoke and stress tests |
| `scripts/` | end-to-end demo |
| `docs/` | one document per project stage, progress log, final report |

## Documentation by stage
| Stage | Document | Git tag |
|---|---|---|
| 1 Introduction | [docs/01_introduction.md](docs/01_introduction.md) | `stage-1` |
| 2 Requirements and plan | [docs/02_requirements_and_plan.md](docs/02_requirements_and_plan.md) | `stage-2` |
| 3 Design and architecture (UML) | [docs/03_design.md](docs/03_design.md) | `stage-3` |
| 4 Prototype | [docs/04_prototype.md](docs/04_prototype.md) | `stage-4` |
| 5 Testing and improvement | [docs/05_testing.md](docs/05_testing.md) | `stage-5` |
| 6 Final report | [docs/06_final_report.md](docs/06_final_report.md) | `stage-6` |

Progress log: [docs/PROGRESS.md](docs/PROGRESS.md). Presentation outline: [docs/presentation_outline.md](docs/presentation_outline.md).

## License
The kernel module declares `MODULE_LICENSE("GPL")`. Add a `LICENSE` file of your choice (GPL-2.0 recommended) before publishing.
