# Cockpit IPC Communication Demo

A Linux/C++ multi-process IPC demo modeled after an automotive cockpit system.

Demonstrates how independent Linux processes communicate using POSIX IPC — the same mechanisms used in embedded Linux and automotive middleware stacks.

---

## IPC Architecture

The system separates communication into three planes:

| Plane | Mechanism | Purpose |
|---|---|---|
| Data | POSIX shared memory | High-throughput frame transfer |
| Sync | POSIX named semaphores | Producer-consumer coordination |
| Control | POSIX message queues | HMI runtime commands |

```text
                         Control Plane
                    POSIX Message Queues

              +---------------------------+
              |        hmi_service        |
              +------------+--------------+
                           |
           +---------------+----------------+
           |                                |
           v                                v
 /cockpit_camera_cmd_queue      /cockpit_display_cmd_queue
           |                                |
           v                                v
 +------------------+            +-------------------+
 |  camera_service  |            |  display_manager  |
 +--------+---------+            +---------+---------+
          |                                ^
          |   Data Plane                   |
          |   POSIX Shared Memory          |
          |   + Named Semaphores           |
          v                                |
 +------------------------------------------+
 |         /cockpit_frame_buffer            |
 +------------------------------------------+
```

---

## Processes

| Executable | Role |
|---|---|
| `camera_service` | Produces simulated frames into shared memory |
| `display_manager` | Consumes frames; runs throughput benchmark |
| `hmi_service` | Interactive command sender |
| `cleanup_ipc` | Removes stale POSIX IPC objects |

---

## IPC Objects

```text
Shared memory:   /cockpit_frame_buffer
Semaphores:      /cockpit_buffer_empty
                 /cockpit_frame_ready
Message queues:  /cockpit_camera_cmd_queue
                 /cockpit_display_cmd_queue
```

---

## Project Structure

```text
cockpit-ipc-demo/
├── CMakeLists.txt
├── README.md
├── include/
│   └── common.hpp
├── src/
│   ├── camera_service.cpp
│   ├── display_manager.cpp
│   ├── hmi_service.cpp
│   └── cleanup_ipc.cpp
└── docs/
    └── architecture.md
```

---

## Build

Requires Linux. Links against `rt` for POSIX message queue support.

```bash
# Debug
cmake -S . -B build
cmake --build build

# Release (use for benchmarking)
cmake -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build
```

---

## Run

Always clean stale IPC objects first:

```bash
./build/cleanup_ipc
```

Open three terminals:

**Terminal 1 — Display:**
```bash
./build/display_manager
```

**Terminal 2 — Camera:**
```bash
./build/camera_service
```

**Terminal 3 — HMI:**
```bash
./build/hmi_service
```

---

## HMI Commands

```text
1 → START_CAMERA      Enable frame production
2 → STOP_CAMERA       Pause frame production (service stays alive)
3 → SHUTDOWN SYSTEM   Send shutdown to both camera and display
q → Quit HMI only     Exit HMI without affecting other services
```

---

## Throughput

Measured on a Release build with 1MB frames and checksum validation enabled:

```text
Payload size:   1,048,576 bytes/frame
Frames:         1000
Throughput:     ~6.3 GB/s
```

Throughput reflects shared-memory bandwidth between two processes on the same machine — no serialization, no copying through the kernel. Semaphore round-trip and checksum computation are included in the timing.

> Numbers vary with CPU, memory bandwidth, cache behavior, and system load.

---

## Further Reading

For design decisions — IPC plane separation, semaphore model, shutdown strategy, failure modes, and production gap analysis — see [`docs/architecture.md`](docs/architecture.md).

---
