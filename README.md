# Cockpit IPC Communication Demo

A Linux/C++ multi-process IPC demo inspired by an automotive cockpit system.

This project demonstrates how independent Linux processes can communicate using POSIX IPC mechanisms. It uses:

- **POSIX shared memory** as the high-throughput data plane for frame transfer
- **POSIX named semaphores** for producer-consumer synchronization
- **POSIX message queues** as the control plane for runtime HMI commands

The goal of the project is to show a clean, practical IPC architecture similar to how separate services in an embedded Linux or automotive cockpit environment may coordinate with each other.

---

## Features

- Multi-process Linux IPC design in C++
- Separate camera, display, HMI, and cleanup processes
- Shared-memory frame buffer for simulated camera/display frames
- Named semaphore synchronization using a two-semaphore producer-consumer model
- POSIX message queue based command/control path
- Runtime HMI commands for camera start, stop, and system shutdown
- Service-specific command queues for coordinated shutdown
- Centralized cleanup utility for stale POSIX IPC objects
- Designed as a learning and resume-worthy Linux IPC project

---

## System Overview

The demo contains the following executables:

| Executable | Role |
|---|---|
| `camera_service` | Produces simulated camera/display frames |
| `display_manager` | Consumes frames from shared memory |
| `hmi_service` | Sends runtime control commands |
| `cleanup_ipc` | Removes stale POSIX IPC objects |

---

## Architecture

The system separates IPC into three logical planes:

1. **Data plane**  
   Frame data is transferred using POSIX shared memory.

2. **Synchronization plane**  
   Named semaphores coordinate safe producer-consumer access to the shared frame buffer.

3. **Control plane**  
   POSIX message queues carry small runtime commands such as start, stop, and shutdown.

```text
                         Control Plane
                  POSIX Message Queues

              +-------------------------+
              |       hmi_service       |
              +-----------+-------------+
                          |
          +---------------+----------------+
          |                                |
          v                                v
/cockpit_camera_cmd_queue      /cockpit_display_cmd_queue
          |                                |
          v                                v
+----------------+              +-----------------+
| camera_service |              | display_manager |
+-------+--------+              +--------+--------+
        |                                ^
        | Data Plane                     |
        | POSIX Shared Memory           |
        | + Named Semaphores            |
        v                                |
+-----------------------------------------+
|           /cockpit_frame_buffer         |
+-----------------------------------------+
```

---

## IPC Objects

The project uses named POSIX IPC objects.

### Shared Memory

```text
/cockpit_frame_buffer
```

### Named Semaphores

```text
/cockpit_buffer_empty
/cockpit_frame_ready
```

### Message Queues

```text
/cockpit_camera_cmd_queue
/cockpit_display_cmd_queue
```

---

## Project Structure

Recommended repository structure:

```text
cockpit-ipc-demo/
├── CMakeLists.txt
├── README.md
├── .gitignore
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

### File Responsibilities

- `include/common.hpp`  
  Shared IPC contract used by all processes. Contains IPC names, frame layout, command types, and message structures.

- `src/camera_service.cpp`  
  Produces simulated frames into shared memory. Receives HMI commands through the camera command queue.

- `src/display_manager.cpp`  
  Consumes frames from shared memory. Receives shutdown commands through the display command queue.

- `src/hmi_service.cpp`  
  Interactive command sender. Sends start/stop/shutdown commands to service-specific message queues.

- `src/cleanup_ipc.cpp`  
  Removes stale POSIX shared memory, semaphores, and message queues.

---

## Build Instructions

This project is intended for Linux environments.

```bash
cmake -S . -B build
cmake --build build
```

For performance or throughput testing, use a Release build:

```bash
cmake -DCMAKE_BUILD_TYPE=Release -S . -B build
cmake --build build
```

> Note: Some Linux environments may require linking with `rt` for POSIX message queue APIs. The CMake configuration can link `rt` if needed.

---

## Run Instructions

First remove stale IPC objects:

```bash
./build/cleanup_ipc
```

Then open three terminals.

### Terminal 1

```bash
./build/display_manager
```

### Terminal 2

```bash
./build/camera_service
```

### Terminal 3

```bash
./build/hmi_service
```

Use the HMI menu to control the system.

---

## Runtime Commands

```text
1 -> START_CAMERA
2 -> STOP_CAMERA
3 -> SHUTDOWN SYSTEM
q -> Quit HMI only
```

### Command Behavior

- `START_CAMERA`  
  Enables frame production in `camera_service`.

- `STOP_CAMERA`  
  Pauses frame production while keeping `camera_service` alive.

- `SHUTDOWN SYSTEM`  
  Sends shutdown commands to both `camera_service` and `display_manager`.

- `q`  
  Exits only the HMI process. It does not shut down camera or display services.

---

## Design Notes

### Why shared memory for frames?

Camera/display frames can be large. Copying them through a message queue would be inefficient. Shared memory lets both processes map the same OS-backed memory object and exchange frame data with minimal copying.

### Why semaphores?

A single shared buffer needs synchronization. The project uses a two-semaphore producer-consumer model:

```text
buffer_empty = 1
frame_ready  = 0
```

Producer flow:

```text
wait(buffer_empty)
write frame
post(frame_ready)
```

Consumer flow:

```text
wait(frame_ready)
read frame
post(buffer_empty)
```

This prevents the producer from overwriting a frame before the consumer has finished reading it.

### Why message queues for commands?

Commands such as start, stop, and shutdown are small discrete events. POSIX message queues are a good fit for this control-plane traffic, while shared memory remains dedicated to high-throughput frame transfer.

### Why service-specific command queues?

POSIX message queues are not broadcast channels. If multiple services read from the same queue, only one service receives each message. Therefore, the design uses separate queues for camera and display so the HMI can route shutdown commands explicitly.

### Why timed waits in display?

If `display_manager` blocks forever waiting for a frame, it cannot react to shutdown commands when the camera stops producing frames. A timed semaphore wait allows display to periodically wake up, check its command queue, and exit cleanly.

---

## Benchmarking Note

Earlier development versions measured shared-memory throughput using fixed frame counts and fixed payload sizes. The current service-oriented version focuses on runtime control and coordinated shutdown.

A final benchmark mode can be added separately to measure throughput under controlled conditions:

- Release build
- Fixed frame count
- Fixed payload size
- Reduced logging inside the hot path
- Consumer touches or checksums the payload
- Clean IPC state before each run

Benchmark numbers should be interpreted carefully because shared-memory throughput depends on CPU, memory bandwidth, cache behavior, payload size, synchronization overhead, build type, and measurement method.

---

## Limitations

This is a learning/demo project, not a production cockpit system.

Current limitations:

- No supervisor process for automatic service restart
- No versioned command/message protocol
- No real camera input
- No real display rendering
- No security or permission hardening
- No cross-machine IPC support
- Limited recovery from unexpected process termination
- Benchmark mode is planned as a separate final step

---

## Learning Outcomes

This project demonstrates:

- Difference between processes and threads
- Why independent processes require IPC
- POSIX shared memory creation, sizing, mapping, and cleanup
- Named semaphore synchronization
- Producer-consumer coordination using two semaphores
- Why dynamic C++ containers should not be stored directly in shared memory
- POSIX message queue command/control communication
- Service-specific command routing
- Graceful shutdown using command queues and timed waits
- Difference between demo correctness, benchmark correctness, and production robustness

---

## Resume Summary

A concise resume description for this project:

> Designed a multi-process Linux cockpit IPC simulation in C++ using POSIX shared memory for high-throughput frame transfer, named semaphores for producer-consumer synchronization, and POSIX message queues for HMI-driven runtime commands and coordinated service shutdown.
