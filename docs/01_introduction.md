# Stage 1 – Project Introduction

## Project name
**BlackBox** – a flight recorder for Linux.

## Idea in one line
A Linux kernel character driver keeps a ring buffer of recent events. When something goes
wrong, a C++ daemon freezes that history and saves it to disk – like the black box in an aircraft.

## Problem
When an application or service crashes, the evidence that explains *why* usually vanishes
with it. Normal log files can be incomplete (buffered writes lost on crash), spread across
many processes, or rotated away. Engineers need the **last few seconds before the failure**,
in order, with timestamps and process IDs.

## Objective
Build a small, self-contained system that:

1. Lets any process record events cheaply through `/dev/blackbox`.
2. Keeps the most recent N events in kernel memory (older ones are overwritten).
3. Preserves that history on demand (or automatically on an ERROR event) as a snapshot file.
4. Gives the user tools to inspect, query and control the recorder.

## Scope
| In scope | Out of scope |
|---|---|
| Kernel char driver with ring buffer, `read/write/poll/ioctl`, `/proc` entry | Persisting the buffer across reboot |
| Multi-threaded C++ daemon (`blackboxd`) | Network transport / remote collection |
| C++ CLI (`bbctl`) | GUI / web viewer |
| Crash demo program, unit + integration tests | Hooking kernel tracepoints (future work) |

## Expected outcome
- Loadable module `blackbox.ko` exposing `/dev/blackbox`.
- `blackboxd` writing a live log and automatic snapshots.
- `bbctl` for log / tail / dump / freeze / stats / snapshot.
- A reproducible demo: a program logs events, crashes on purpose, and the snapshot shows
  exactly what happened in the final moments.

## Applications
- Post-mortem debugging of services and embedded Linux devices.
- Teaching: device drivers, IPC, threading and C++ design in one coherent project.
- Basis for extension: tracepoint hooks, watchdog triggers, remote upload.

## Skills demonstrated
| Area | Where |
|---|---|
| Linux device drivers | char device, file_operations, wait queues, spinlocks, ioctl, procfs, kernel timer, module params |
| System programming | `poll`, signals, threads, daemonization, pidfile, file rotation, `ioctl` from user space |
| C++ | RAII wrappers, classes, STL, `std::thread`, templates (bounded queue), `std::filesystem` |
