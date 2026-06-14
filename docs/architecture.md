# Architecture Notes

Technical deep-dive into the design of the Cockpit IPC Communication Demo.  
For setup and run instructions see the [README](../README.md).

---

## 1. Design Goal

The goal of this project is to model a small automotive cockpit-style IPC system using independent Linux processes.

The system is intentionally split into separate services:

- `camera_service`
- `display_manager`
- `hmi_service`
- `cleanup_ipc`

This makes process boundaries explicit and demonstrates why IPC is needed when independent programs need to coordinate.

---

## 2. IPC Plane Separation

The project separates communication into three planes:

```text
Data Plane            → POSIX shared memory
Synchronization Plane → POSIX named semaphores
Control Plane         → POSIX message queues
```

This keeps each IPC mechanism focused on what it does best rather than routing everything through one channel.

---

## 3. Data Plane: POSIX Shared Memory

Frame data is transferred through a named shared memory object:

```text
/cockpit_frame_buffer
```

Both `camera_service` and `display_manager` map this object into their own virtual address spaces using `mmap()`. The processes do not share normal variables — they map the same OS-backed shared memory object independently.

The shared frame layout is defined in `include/common.hpp`. It uses fixed-size fields and a fixed-size payload array. Dynamic C++ containers such as `std::string`, `std::vector`, or raw pointers are intentionally avoided — those types store addresses into the process-local heap, which are meaningless when read from a different process's virtual address space.

---

## 4. Synchronization Plane: Named Semaphores

The project uses two named semaphores:

```text
/cockpit_buffer_empty    (initial value: 1)
/cockpit_frame_ready     (initial value: 0)
```

### Producer Flow (camera_service)

```text
wait(buffer_empty)
write complete frame into shared memory
post(frame_ready)
```

### Consumer Flow (display_manager)

```text
wait(frame_ready)
read complete frame from shared memory
post(buffer_empty)
```

This prevents the camera from overwriting the shared frame buffer before the display has consumed the previous frame. Note that the semaphore does not make the frame write atomic — it provides ordering and mutual exclusion between producer and consumer.

---

## 5. Control Plane: POSIX Message Queues

Runtime commands are sent using POSIX message queues:

```text
/cockpit_camera_cmd_queue
/cockpit_display_cmd_queue
```

The HMI process sends small discrete commands:

```text
START_CAMERA
STOP_CAMERA
SHUTDOWN
```

Commands are represented by a trivially-copyable `CommandMessage` struct. This is required because POSIX message queue APIs transfer raw bytes — non-trivial types with vtables or heap pointers cannot be safely sent this way.

---

## 6. Why Not One Shared Command Queue?

POSIX message queues are not broadcast channels. Each message is consumed by exactly one receiver. If camera and display both read from the same queue, a single `SHUTDOWN` message reaches one service only — the other never sees it.

The design uses separate queues to give explicit, reliable command routing:

```text
hmi_service → /cockpit_camera_cmd_queue  → camera_service
hmi_service → /cockpit_display_cmd_queue → display_manager
```

---

## 7. Runtime State Model

### camera_service

```text
States:      STOPPED, RUNNING

STOPPED  --START_CAMERA-->  RUNNING
RUNNING  --STOP_CAMERA--->  STOPPED
RUNNING/STOPPED --SHUTDOWN--> EXIT
```

The camera uses non-blocking message queue receive (`O_NONBLOCK`) so it can poll for commands while continuing to produce frames when enabled.

### display_manager

The display manager consumes frames and listens for shutdown commands.

A plain blocking `sem_wait(frame_ready)` would be unsafe for shutdown — display would block indefinitely if the camera stopped producing frames. Instead, display uses a timed wait pattern:

```text
wait up to 100ms for frame_ready
if frame received  → consume frame
if timeout         → poll command queue
if SHUTDOWN seen   → exit
```

This allows display to respond to shutdown even when no frames are arriving.

---

## 8. Cleanup Ownership

Each service closes its own local handles on exit:

```text
mq_close / sem_close / munmap / close
```

`cleanup_ipc` owns unlinking the named IPC objects:

```text
shm_unlink  → /cockpit_frame_buffer
sem_unlink  → /cockpit_buffer_empty, /cockpit_frame_ready
mq_unlink   → /cockpit_camera_cmd_queue, /cockpit_display_cmd_queue
```

Centralizing unlink in one utility prevents a service from removing an IPC object while another service still holds it open.

---

## 9. Failure Modes Considered

**Stale IPC objects**  
Named POSIX IPC objects persist after crashes or forced termination. Running `cleanup_ipc` before each test run removes them and avoids stale-state issues.

**Stale message queue attributes**  
If a queue was previously created with a different `mq_msgsize`, subsequent `mq_open` with `O_CREAT` may silently reuse the old queue with the old size, causing `mq_send` / `mq_receive` to fail. `cleanup_ipc` unlinks queues to prevent this.

**Display blocking forever**  
Solved by replacing the infinite `sem_wait` with a 100ms `sem_timedwait` in `display_manager`, allowing periodic command queue polling.

**HMI started before services**  
The HMI opens queues with `O_WRONLY` (no `O_CREAT`). If camera or display have not started yet their queues do not exist and `mq_open` will fail. Start camera and display before HMI.

---

## 10. Demo Correctness vs Benchmark Correctness vs Production Robustness

### Demo Correctness

The project demonstrates the intended IPC architecture and runtime command flow correctly.

### Benchmark Correctness

The throughput result (~6.3 GB/s, Release build, 1MB frames, checksum enabled) reflects a controlled back-to-back producer-consumer run on a single machine. Numbers vary with CPU, memory bandwidth, cache behavior, payload size, and synchronization overhead. Benchmarks should be run on a Release build with reduced logging and a clean IPC state.

### Production Robustness

A production cockpit system would additionally require:

- Supervisor process with automatic service restart
- Watchdog integration
- Protocol versioning for `CommandMessage` and `FrameBuffer`
- Permission and security hardening on IPC objects
- Health monitoring
- Defined resource ownership and lifecycle strategy
- Recovery from unexpected process termination

This project does not claim production readiness. It is a focused Linux IPC learning and portfolio project.