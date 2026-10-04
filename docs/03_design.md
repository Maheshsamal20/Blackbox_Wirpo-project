# Stage 3 – System Design and Architecture

## 1. High-level architecture

```mermaid
flowchart TB
    subgraph APPS["User applications"]
        A1["Any process<br/>echo '&lt;3&gt;msg' &gt; /dev/blackbox"]
        A2["crash_demo"]
        A3["bbctl (CLI)"]
    end

    subgraph DAEMON["blackboxd (C++ daemon)"]
        R["Reader thread<br/>poll() + read()"]
        Q["BoundedQueue&lt;Item&gt;"]
        W["Worker thread<br/>format, live log, trigger check"]
        S["SnapshotService<br/>freeze, read all, write, unfreeze"]
        R --> Q --> W
        W --> S
    end

    subgraph KERNEL["Kernel: blackbox.ko"]
        FOPS["file_operations<br/>open / read / write / poll / ioctl"]
        RING["Ring buffer<br/>(spinlock protected)"]
        WQ["Wait queue"]
        TMR["Heartbeat timer"]
        PROC["/proc/blackbox"]
        FOPS --- RING
        RING --- WQ
        TMR --> RING
        PROC --- RING
    end

    DISK[("Disk: blackbox.log<br/>*.snap snapshots")]

    A1 --> FOPS
    A2 --> FOPS
    A3 --> FOPS
    R <--> FOPS
    S <--> FOPS
    W --> DISK
    S --> DISK
```

## 2. Components and responsibilities
| Component | Responsibility |
|---|---|
| `blackbox.ko` | Owns the ring buffer and all synchronisation. Accepts writes, serves reads with per-file cursors, wakes readers, exposes ioctl and procfs. |
| `BlackBoxDevice` | RAII wrapper around the device file descriptor; typed `readEvents`, `ioctl` helpers, `pollReadable`. |
| `EventFormat` | Converts a `bb_event` to a text line; level name ↔ number. |
| `SnapshotWriter` | Writes a snapshot atomically (temp file + rename) and rotates old snapshots. |
| `RotatingLog` | Size-based rotating text log for the live stream. |
| `BoundedQueue<T>` | Thread-safe producer/consumer queue with close semantics. |
| `Daemon` | Orchestrates threads, trigger logic, state machine, signals. |
| `bbctl` | Thin command-line front end for the same device API. |

## 3. Data structures
**Event (128 bytes, shared ABI in `include/bb_uapi.h`)**

| Field | Type | Meaning |
|---|---|---|
| `ts_ns` | u64 | wall-clock time in ns |
| `seq` | u64 | sequence number |
| `pid` | u32 | writer PID |
| `level` | u8 | DEBUG/INFO/WARN/ERROR |
| `source` | u8 | user / heartbeat / driver |
| `len` | u16 | message length |
| `msg` | char[104] | NUL-terminated text |

**Kernel state (`struct bb_dev`)**
```
events[capacity]   array of bb_event (ring)
head_seq           next sequence number to assign
base_seq           events with seq < base_seq are cleared
lock               spinlock (irq-safe: the timer also pushes events)
wq                 wait queue for blocked readers
frozen             1 = reject writes
total_written, dropped_frozen   counters
```
Slot of an event = `seq % capacity`.
Oldest valid sequence = `max(base_seq, head_seq - capacity)`.

**Per-open-file state (`struct bb_session`)**: `next_seq` – the next event this reader will receive.
If a slow reader falls behind the oldest event it silently jumps forward (events were overwritten).

## 4. Locking rules
1. All ring state is protected by one `spinlock_t` taken with `spin_lock_irqsave`.
2. `copy_from_user` / `copy_to_user` are **never** called with the lock held (they may sleep).
3. Writers build the event on the stack first, then lock, push, unlock, then wake readers.
4. Readers copy one event at a time into a stack buffer under the lock, unlock, then copy to user.

## 5. UML diagrams

### 5.1 Class diagram (user space)
```mermaid
classDiagram
    class BlackBoxDevice {
        -int fd_
        -vector~uint8_t~ carry_
        +BlackBoxDevice(path, nonblocking)
        +~BlackBoxDevice()
        +fd() int
        +pollReadable(timeoutMs) bool
        +readEvents(out, maxEvents) ssize_t
        +write(text, level) bool
        +freeze() bool
        +unfreeze() bool
        +clear() bool
        +seekOldest() bool
        +stats(out) bool
    }
    class EventFormat {
        <<utility>>
        +levelName(level) string
        +parseLevel(text) optional~int~
        +format(event) string
        +formatTime(ts_ns) string
    }
    class BoundedQueue~T~ {
        -deque~T~ q_
        -mutex m_
        -condition_variable cv_
        -size_t cap_
        -bool closed_
        +push(T) bool
        +pop(T) bool
        +close()
    }
    class RotatingLog {
        -path path_
        -size_t maxBytes_
        -int keep_
        +append(line)
        -rotate()
    }
    class SnapshotWriter {
        -path dir_
        -int keep_
        +write(events, reason, stats) path
        -rotateOld()
    }
    class Daemon {
        -Config cfg_
        -State state_
        -BoundedQueue~Item~ queue_
        +run() int
        -readerLoop()
        -workerLoop()
        -takeSnapshot(reason)
    }
    Daemon --> BlackBoxDevice : uses
    Daemon --> BoundedQueue : owns
    Daemon --> RotatingLog : owns
    Daemon --> SnapshotWriter : owns
    Daemon ..> EventFormat : uses
    SnapshotWriter ..> EventFormat : uses
```

### 5.2 Sequence diagram – automatic snapshot on ERROR
```mermaid
sequenceDiagram
    participant App as Application
    participant K as blackbox.ko
    participant R as Reader thread
    participant W as Worker thread
    participant S as SnapshotService
    participant D as Disk

    App->>K: write("<3>disk failure")
    K->>K: lock, push event, unlock
    K-->>R: wake_up (poll readable)
    R->>K: read(events)
    K-->>R: bb_event[]
    R->>W: queue.push(event)
    W->>D: append to blackbox.log
    W->>W: level >= trigger ?
    W->>S: takeSnapshot("ERROR event")
    S->>K: ioctl(FREEZE)
    S->>K: ioctl(SEEK_OLDEST)
    S->>K: read(all history)
    K-->>S: bb_event[]
    S->>D: write blackbox-<time>.snap (temp + rename)
    S->>K: ioctl(UNFREEZE)
```

### 5.3 State machine – recorder (daemon view)
```mermaid
stateDiagram-v2
    [*] --> RECORDING
    RECORDING --> FROZEN : trigger (ERROR event or SIGUSR1)
    FROZEN --> DUMPING : history cursor at oldest
    DUMPING --> RECORDING : snapshot written, unfreeze
    DUMPING --> RECORDING : write failed, unfreeze anyway
    RECORDING --> [*] : SIGINT / SIGTERM
```

### 5.4 State machine – kernel ring buffer
```mermaid
stateDiagram-v2
    [*] --> Running : insmod
    Running --> Frozen : ioctl FREEZE
    Frozen --> Running : ioctl UNFREEZE
    Running --> Running : write (push, overwrite oldest)
    Frozen --> Frozen : write (rejected, counted)
    Running --> Running : ioctl CLEAR
    Running --> [*] : rmmod
    Frozen --> [*] : rmmod
```

## 6. Implementation plan
1. Driver skeleton: register chrdev, class, device; `open/release`.
2. Ring buffer + `write` + `read` (cursor per file).
3. `poll`, ioctl, procfs, heartbeat timer, module parameters.
4. `libbbcommon`: device wrapper, formatter, queue, log, snapshot writer.
5. `blackboxd`: reader/worker threads, trigger, state machine, signals, daemonize.
6. `bbctl` and `crash_demo`.
7. Tests (unit, mock integration, kernel smoke and stress), fixes, documentation.

## 7. Development environment
| Item | Choice |
|---|---|
| OS | Ubuntu 22.04/24.04 in a VM (VirtualBox/VMware/QEMU) |
| Packages | `build-essential linux-headers-$(uname -r) git make valgrind` |
| Compiler | `g++` with `-std=c++17 -Wall -Wextra -pthread` |
| Kernel build | kbuild out-of-tree module (`driver/Makefile`) |
| Debugging | `dmesg -w`, `cat /proc/blackbox`, `valgrind`, `-fsanitize=address,undefined` |

## 8. Git workflow
- `main` – stable, tagged at the end of each stage (`stage-1` … `stage-6`).
- `dev` – integration branch; feature branches `feature/<name>` merge into `dev`.
- Commit messages: `<area>: <what changed>` (e.g. `driver: add poll support`).
- Each stage ends with documentation, a commit and a tag, so progress is visible in history.

## 9. Progress tracking
`docs/PROGRESS.md` records, per stage: what was done, issues found, solutions, and the plan for the next stage.
