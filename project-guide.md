# Multi-Container Runtime

**Team Size:** 2 Students

---

### Project Summary

This project involves building a lightweight Linux container runtime in C with a long-running parent supervisor and a kernel-space memory monitor. The container runtime must manage multiple containers at once, coordinate concurrent logging safely, expose a small supervisor CLI, and include controlled experiments related to Linux scheduling.

The project has two integrated parts:

1. **User-Space Runtime + Supervisor (`engine.c`)**  
   Launches and manages multiple isolated containers, maintains metadata for each container, accepts CLI commands, captures container output through a bounded-buffer logging system, and handles container lifecycle signals correctly.
2. **Kernel-Space Monitor (`monitor.c`)**  
   Implements a Linux Kernel Module (LKM) that tracks container processes, enforces soft and hard memory limits, and integrates with the user-space runtime through `ioctl`.

---

### Environment and Setup

The project is designed for this environment:

- **Ubuntu 22.04 or 24.04 in a VM**
- **Secure Boot OFF** for module loading
- **No WSL**

Install dependencies:

```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r)
```

Run the environment preflight check before implementation:

```bash
cd boilerplate
chmod +x environment-check.sh
sudo ./environment-check.sh
```

Prepare the Alpine mini root filesystem:

```bash
mkdir rootfs
wget https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.3-x86_64.tar.gz
tar -xzf alpine-minirootfs-3.20.3-x86_64.tar.gz -C rootfs
```

No need to keep `rootfs/` in GitHub repo.

---

### Implementation Scope

#### Task 1: Multi-Container Runtime with Parent Supervisor

Implement a parent supervisor process that can manage multiple containers at the same time instead of launching only one shell and exiting.

Demonstrate:

- Supervisor process remains alive while containers run
- Multiple containers can be started and tracked concurrently
- Each container has isolated PID, UTS, and mount namespaces
- Each container uses the provided `rootfs`
- `/proc` works correctly inside each container
- Parent reaps exited children correctly with no zombies

For each container, the supervisor must maintain metadata in user space. At minimum track:

- Container ID or name
- Host PID
- Start time
- Current state (`starting`, `running`, `stopped`, `killed`, etc.)
- Configured soft and hard memory limits
- Log file path
- Exit status or terminating signal after completion

The internal representation is up to you to design, but it must be safe under concurrent access.

#### Task 2: Supervisor CLI and Signal Handling

Implement a CLI interface for interacting with the supervisor.

Required commands:

- `start` to launch a new container in the background
- `run` to launch a container and wait for it in the foreground
- `ps` to list tracked containers and their metadata
- `logs` to inspect a container log file
- `stop` to terminate a running container cleanly

You may add more commands if you want, but the above are required.

Demonstrate:

- CLI requests reach the long-running supervisor correctly
- Supervisor updates container state after each command
- `SIGCHLD` handling is correct and does not leak zombies
- `SIGINT`/`SIGTERM` to the supervisor trigger orderly shutdown
- Container termination path distinguishes graceful stop vs forced kill

The control channel between the CLI client and the supervisor must use a second IPC mechanism different from the logging pipe. A UNIX domain socket, FIFO, or shared-memory-based command channel are all acceptable if justified.

#### Task 3: Bounded-Buffer Logging and IPC Design

Capture container output through a concurrent logging pipeline rather than writing directly to the terminal.

Demonstrate:

- File-descriptor-based IPC from each container into the supervisor
- Capture of both `stdout` and `stderr`
- A bounded buffer in user space between producers and consumers
- Correct producer-consumer synchronization
- Persistent per-container log files
- Clean logger shutdown when containers exit or the supervisor stops

Minimum concurrency expectation:

- At least one producer path inserts log data into a bounded shared buffer
- At least one consumer thread removes data and writes to log files
- Shared metadata access is synchronized separately from the log buffer

Your README must explain:

- Why you chose your synchronization primitives
- What race conditions exist without them
- How your bounded buffer avoids lost data, corruption, or deadlock

#### Task 4: Kernel Memory Monitoring with Soft and Hard Limits

Extend the kernel monitor beyond a single kill-on-limit policy.

Demonstrate:

- Control device at `/dev/container_monitor`
- PID registration from the supervisor via `ioctl`
- Tracking of monitored processes in a kernel linked list
- Lock-protected shared list access (`mutex` or `spinlock`)
- Periodic RSS checks
- Separate soft-limit and hard-limit behavior
- Removal of stale or exited entries

Required policy behavior:

- **Soft limit:** log a warning event when the process first exceeds the soft limit
- **Hard limit:** terminate the process when it exceeds the hard limit

Integration detail:

- The supervisor must send the container's **host PID** to the kernel module
- The user-space metadata must reflect whether a container exited normally, was stopped by the supervisor, or was killed due to the hard limit

Exact event-reporting design is open to you. You may choose a simple `dmesg`-only design, or you may add a user-space notification path if you can justify it clearly.

#### Task 5: Scheduler Experiments and Analysis

Use the runtime to run controlled experiments that connect the project to Linux scheduling behavior.

Demonstrate:

- At least two concurrent workloads with different behavior, such as CPU-bound and I/O-bound processes
- At least two scheduling configurations, such as different `nice` values or CPU affinities
- Measurement of observable outcomes such as completion time, responsiveness, or CPU share
- A short analysis of how the Linux scheduler treated the workloads

The goal is not to reimplement a scheduler. The goal is to use your runtime as an experimental platform and explain scheduling behavior using evidence.

At least one experiment must compare:

- Two containers running CPU-bound work with different priorities, or
- A CPU-bound container and an I/O-bound container running at the same time

#### Task 6: Resource Cleanup

Demonstrate clean teardown in both user and kernel space:

- Child process reap in the supervisor
- Logging threads exit and join correctly
- File descriptors are closed on all paths
- User-space heap resources are released
- Kernel list entries are freed on module unload
- No lingering zombie processes or stale metadata after demo run

---

### Engineering Analysis

In your `README.md`, include an analysis section that connects your implementation to OS fundamentals. This is not a description of what you coded - it is an explanation of _why the OS works this way_ and how your project exercises those mechanisms.

Address these five areas:

1. **Isolation Mechanisms**  
   How does your runtime achieve process and filesystem isolation? Explain the role of namespaces (PID, UTS, mount) and `chroot`/`pivot_root` at the kernel level. What does the host kernel still share with all containers?

2. **Supervisor and Process Lifecycle**  
   Why is a long-running parent supervisor useful here? Explain process creation, parent-child relationships, reaping, metadata tracking, and signal delivery across the container lifecycle.

3. **IPC, Threads, and Synchronization**  
   Your project uses at least two IPC mechanisms and a bounded-buffer logging design. For each shared data structure, identify the possible race conditions and justify your synchronization choice (`mutex`, `condition variable`, `semaphore`, `spinlock`, etc.).

4. **Memory Management and Enforcement**  
   Explain what RSS measures and what it does not measure. Why are soft and hard limits different policies? Why does the enforcement mechanism belong in kernel space rather than only in user space?

5. **Scheduling Behavior**  
   Use your experiment results to explain how Linux scheduling affected your workloads. Relate your results to scheduling goals such as fairness, responsiveness, and throughput.

### Boilerplate Contents

The `boilerplate/` folder will contain starter files for the runtime, kernel monitor, shared `ioctl` definitions, test workloads, and build flow so that you have structure for starting out.

---

### Submission Package

Submit a GitHub repository containing (You can structure the repo however you want, these are what we expect there to be):

1. `engine.c`
2. `monitor.c`
3. `monitor_ioctl.h`
4. At least two workload/test programs used for memory and scheduling demonstrations
5. `Makefile`
6. `README.md` with:
   - Team members with SRNs
   - Build/load/run instructions
   - A Demo with screenshots demonstrating all the features
   - Design decisions for the five analysis areas above
   - Reasoning evidence for major synchronization and IPC choices
   - Scheduler experiment results with observations

---

### Reference Run Sequence

```bash
# Build
make

# Load kernel module
sudo insmod monitor.ko

# Verify control device
ls -l /dev/container_monitor

# Start supervisor
sudo ./engine supervisor ./rootfs

# In another terminal: start two containers
sudo ./engine start alpha ./rootfs /bin/sh
sudo ./engine start beta ./rootfs /bin/sh

# List tracked containers
sudo ./engine ps

# Inspect one container
sudo ./engine logs alpha

# Run memory test inside a container
# (copy the test program into rootfs before launch if needed)

# Run scheduling experiment workloads
# and compare observed behavior

# Stop containers
sudo ./engine stop alpha
sudo ./engine stop beta

# Stop supervisor if your design keeps it separate

# Inspect kernel logs
dmesg | tail

# Unload module
sudo rmmod monitor
```

To run helper binaries inside the container, copy them into `rootfs/` before launching:

```bash
cp workload_binary ./rootfs/
```

---

### Demo Expectations

During demo/viva, all demonstration sections of each task must be covered:

1. Show two containers running under one supervisor
2. Show metadata tracking through the `ps` command
3. Show logging behavior through the bounded-buffer pipeline and log files
4. Show the second IPC mechanism being used by the CLI and supervisor
5. Show soft-limit warning behavior and hard-limit enforcement
6. Show one scheduling experiment and explain the observed result
7. Explain design choices and one tradeoff for each major subsystem
