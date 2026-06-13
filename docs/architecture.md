# Architecture Notes

This document explains the design of the Cockpit IPC Communication Demo in more technical detail.

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

The project separates communication into three planes.

```text
Data Plane            -> POSIX shared memory
Synchronization Plane -> POSIX named semaphores
Control Plane         -> POSIX message queues
```

This separation keeps each IPC mechanism focused on what it does best.

---

## 3. Data Plane: POSIX Shared Memory

Frame data is transferred through a named shared memory object:

```text
/cockpit_frame_buffer
```

Both `camera_service` and `display_manager` map this object into their own virtual address spaces using `mmap()`.

Important point:

> The processes do not share normal variables. They map the same OS-backed shared memory object.

The shared frame layout is defined in `include/common.hpp`.

The frame buffer uses fixed-size fields and a fixed-size payload array. It intentionally avoids dynamic C++ containers such as `std::string`, `std::vector`, or pointers because those would refer to process-local heap memory and would not be valid across processes.

---

## 4. Synchronization Plane: Named Semaphores

The project uses two named semaphores:

```text
/cockpit_buffer_empty
/cockpit_frame_ready
```

Initial values:

```text
buffer_empty = 1
frame_ready  = 0
```

### Producer Flow

```text
wait(buffer_empty)
write complete frame into shared memory
post(frame_ready)
```

### Consumer Flow

```text
wait(frame_ready)
read complete frame from shared memory
post(buffer_empty)
```

This prevents the camera from overwriting the shared frame buffer before the display has consumed the previous frame.

A semaphore does not make a large frame write atomic by itself. It provides ordering and access coordination between producer and consumer.

---

## 5. Control Plane: POSIX Message Queues

Runtime commands are sent using POSIX message queues.

The project uses service-specific command queues:

```text
/cockpit_camera_cmd_queue
/cockpit_display_cmd_queue
```

The HMI process sends small command messages such as:

```text
START_CAMERA
STOP_CAMERA
SHUTDOWN
```

Commands are represented by a trivially-copyable `CommandMessage` structure. This is important because POSIX message queue APIs send and receive raw bytes.

---

## 6. Why Not Use One Shared Command Queue?

A POSIX message queue is not a broadcast mechanism.

If camera and display both read from the same queue, each message is consumed by only one receiver. That means a single `SHUTDOWN` command might reach camera but not display, or display but not camera.

To avoid this ambiguity, the design uses separate queues:

```text
hmi_service -> /cockpit_camera_cmd_queue  -> camera_service
hmi_service -> /cockpit_display_cmd_queue -> display_manager
```

This gives explicit command routing and enables coordinated shutdown.

---

## 7. Runtime State Model

### Camera Service

The camera has two main runtime states:

```text
STOPPED
RUNNING
```

Transitions:

```text
STOPPED --START_CAMERA--> RUNNING
RUNNING --STOP_CAMERA---> STOPPED
RUNNING/STOPPED --SHUTDOWN--> EXIT
```

The camera uses non-blocking message queue receive so it can check commands while continuing to produce frames when enabled.

### Display Manager

The display manager consumes frames and also listens for shutdown commands.

A plain blocking `sem_wait(frame_ready)` would be unsafe for shutdown because display could block forever if the camera stopped producing frames.

Therefore, display uses a timed wait pattern:

```text
wait briefly for frame
if frame received -> consume frame
if timeout -> check command queue
if shutdown command -> exit
```

This lets display respond to shutdown even when no frames are arriving.

---

## 8. Cleanup Ownership

Services close their own local handles:

```text
mq_close
sem_close
munmap
close
```

The `cleanup_ipc` utility owns unlinking named IPC objects:

```text
shm_unlink
sem_unlink
mq_unlink
```

This centralizes cleanup and avoids each service unexpectedly deleting IPC objects while another service may still be using them.

---

## 9. Failure Modes Considered

### Stale IPC Objects

Named POSIX IPC objects can remain after crashes or forced termination. `cleanup_ipc` removes stale objects before fresh test runs.

### Display Blocking Forever

Solved by replacing an infinite semaphore wait with a timed wait in `display_manager`.

### Message Queue Not Existing

The HMI expects camera and display services to be started first because they create their command queues.

### Stale Message Queue Attributes

If a message queue was created earlier with a different message size, `mq_send()` or `mq_receive()` may fail with message-size errors. Running `cleanup_ipc` before testing avoids this.

---

## 10. Demo Correctness vs Benchmark Correctness vs Production Robustness

### Demo Correctness

The current project demonstrates the intended IPC architecture and runtime command flow.

### Benchmark Correctness

Throughput should be measured separately using controlled conditions:

- fixed frame count
- fixed payload size
- Release build
- reduced logging
- payload touched or checksummed by consumer

### Production Robustness

A production system would need additional features:

- supervisor process
- stronger error recovery
- protocol versioning
- permissions/security hardening
- resource ownership strategy
- health monitoring
- watchdog integration

This project does not claim to be production-ready. It is a focused Linux IPC learning/demo project.
