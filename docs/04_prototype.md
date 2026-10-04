# Stage 4 – Initial Implementation and Prototype

## What was implemented
| Module | Prototype features |
|---|---|
| `driver/blackbox.c` | char device `/dev/blackbox`; ring buffer (`buf_events` parameter); `write()` with optional `<N>` level prefix; `read()` with per-file cursor, blocking and `O_NONBLOCK`; wait queue wake-up; clean init/exit error unwinding |
| `libbbcommon` | `BlackBoxDevice` (RAII fd wrapper, `readEvents`, `write`, ioctl helpers), `EventFormat` (level names, time and event formatting) |
| `bbctl` | `log` and `tail` commands |
| `blackboxd` | prototype: reads events and prints them |

## Key design decisions
- **Per-file cursors**: several readers (daemon, `bbctl tail`) can read the same history without consuming it.
- **Writers never block**: when the ring is full the oldest event is overwritten.
- **Lock discipline**: one irq-safe spinlock; `copy_to_user`/`copy_from_user` always outside it.
- **`write()` always reports full consumption**, so shell tools such as `echo` do not retry partial writes.
- Kernel API differences (`class_create` signature, `nonseekable_open`) handled with version checks.

## How to try it (in your VM)
```bash
sudo apt install build-essential linux-headers-$(uname -r)
make driver            # builds driver/blackbox.ko
make                   # builds user-space tools
sudo insmod driver/blackbox.ko buf_events=1024
sudo chmod 666 /dev/blackbox
./build/bbctl tail &                       # follow events
echo "<2>hello from the shell" > /dev/blackbox
./build/bbctl log -l error "something broke"
dmesg | tail -3
sudo rmmod blackbox
```

## Prototype demonstration checklist
- [ ] `insmod` prints "blackbox: loaded" in `dmesg`
- [ ] `/dev/blackbox` exists
- [ ] event written with `echo` appears in `bbctl tail`
- [ ] `rmmod` succeeds

## Progress, issues and solutions
| Issue | Solution |
|---|---|
| `class_create()` signature changed in kernel 6.4 | `LINUX_VERSION_CODE` guard |
| `no_llseek` was removed from recent kernels | use `nonseekable_open()` in `open()` |
| User-space reader needs testing without loading a kernel module | the reader was exercised against a FIFO carrying binary `bb_event` records (same 128-byte layout) |
| Building directories with `mkdir -p {a,b}` failed under `dash` | use explicit directory list / bash |

## Next stage
Add `poll`, ioctl, `/proc`, heartbeat timer to the driver; build the multi-threaded daemon with snapshots; add unit, integration and stress tests; fix issues found.
