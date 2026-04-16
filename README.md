# OS-Jackfruit — Lightweight Linux Container Runtime

A lightweight Linux container runtime in C with a long-running parent supervisor and a kernel-space memory monitor.
---
## Team Information
| Name | SRN |
|------|-----|
| DHRITI KAMANI | PES1UG24AM085 |
| DIYA AGARWAL | PES1UG24AM096 |
---
## Build, Load, and Run Instructions
### 1. Install Dependencies
```bash
sudo apt update
sudo apt install -y build-essential linux-headers-$(uname -r)
```
### 2. Build Everything
```bash
make
```
### 3. Prepare Root Filesystems
```bash
mkdir rootfs-base
wget https://dl-cdn.alpinelinux.org/alpine/v3.20/releases/x86_64/alpine-minirootfs-3.20.3-x86_64.tar.gz
tar -xzf alpine-minirootfs-3.20.3-x86_64.tar.gz -C rootfs-base
cp -a ./rootfs-base ./rootfs-alpha
cp -a ./rootfs-base ./rootfs-beta
```
### 4. Load the Kernel Module
```bash
sudo insmod monitor.ko
ls -l /dev/container_monitor
```
### 5. Run the Supervisor and Containers
```bash
# Terminal 1 — start the supervisor
sudo ./engine supervisor ./rootfs-base

# Terminal 2 — launch containers
sudo ./engine start alpha ./rootfs-alpha /bin/sh --soft-mib 48 --hard-mib 80
sudo ./engine start beta  ./rootfs-beta  /bin/sh --soft-mib 64 --hard-mib 96

# List tracked containers
sudo ./engine ps
# Inspect a container log
sudo ./engine logs alpha
# Stop a container
sudo ./engine stop alpha
# Run and wait for exit
sudo ./engine run alpha ./rootfs-alpha /bin/echo hello
```
### 6. Run Workloads Inside a Container
```bash
cp memory_hog ./rootfs-alpha/
cp cpu_hog    ./rootfs-alpha/
sudo ./engine start alpha ./rootfs-alpha /memory_hog
```
### 7. Inspect Kernel Logs
```bash
dmesg | tail
```
### 8. Unload and Clean Up
```bash
sudo ./engine stop alpha
sudo ./engine stop beta
# Ctrl+C the supervisor
sudo rmmod monitor
sudo rm -rf /tmp/engine-logs/
sudo rm -f /tmp/engine.sock
```
### CI-Safe Build
```bash
make -C boilerplate ci
```
---
## Demo Screenshots

### 1 — Multi-container supervision
**Caption:** Two containers (alpha and beta) running concurrently under one supervisor process.
<img width="940" height="379" alt="image" src="https://github.com/user-attachments/assets/831988ab-91a7-4a5f-af47-23cbadc180df" /> <br>

### 2 — Metadata tracking
**Caption:** Output of `engine ps` showing container ID, host PID, start time, state, soft/hard memory limits, and log path.
<img width="940" height="678" alt="image" src="https://github.com/user-attachments/assets/6b72e625-dd4f-4d49-b58d-d711356967bc" />

### 3 — Bounded-buffer logging
**Caption:** Supervisor terminal showing `[producer]` and `[consumer]` thread activity; `engine logs` output showing captured log contents.
<img width="940" height="464" alt="image" src="https://github.com/user-attachments/assets/8a18f8c9-d8ba-4605-b0e9-dd3e7e603995" />

### 4 — CLI and IPC
**Caption:** CLI terminal issuing `engine start` with flags; supervisor terminal showing `[supervisor] received command: start` and responding with `OK:` over the UNIX domain socket.
<img width="487" height="75" alt="image" src="https://github.com/user-attachments/assets/3ec72e50-75df-4afc-bd4e-ef10eba1e835" />
<img width="1408" height="179" alt="image" src="https://github.com/user-attachments/assets/2f8cfc26-54b3-4ce9-a679-5d6b7a740c3b" />

### 5 — Soft-limit warning
**Caption:** `dmesg` output showing a soft-limit warning event for a container exceeding its soft memory limit.
<img width="987" height="155" alt="image" src="https://github.com/user-attachments/assets/b8c1e3ea-1d63-4555-9b8d-55a39aefda3a" />

### 6 — Hard-limit enforcement
**Caption:** `dmesg` output showing a container killed after exceeding its hard limit; `engine ps` showing state as `hard_limit_killed`.
<img width="494" height="95" alt="image" src="https://github.com/user-attachments/assets/72143014-4d28-4430-a0c3-42078110a33f" />

### 7 — Scheduling experiment
**Caption:** Terminal output showing measurable differences between two scheduling configurations.
<img width="935" height="258" alt="image" src="https://github.com/user-attachments/assets/f33d5c8a-4903-4af4-9042-c0922557436b" />


### 8 — Clean teardown
**Caption:** Supervisor printing `[supervisor] shutting down... done.`; `ps aux` showing no zombie processes.
<img width="1600" height="332" alt="image" src="https://github.com/user-attachments/assets/79adebab-e1e8-475e-b2f0-2c12f933ac9a" />


## Engineering Analysis
### Isolation Mechanisms
Linux namespaces are the kernel mechanism that makes container isolation possible. When `clone()` is called with `CLONE_NEWPID`, the kernel creates a new PID namespace — the first process inside sees itself as PID 1 and cannot see or signal any process outside its namespace. `CLONE_NEWUTS` gives each container its own hostname so changes inside do not affect the host. `CLONE_NEWNS` creates a private mount namespace so mounts inside the container do not propagate to the host.
`chroot()` complements namespaces by restricting the container's filesystem view. After `chroot(rootfs_path)`, the container cannot reference any path outside its assigned directory.
The host kernel is still shared across all containers. They run on the same kernel version, use the same system call interface, share the same CPU scheduler, and consume memory from the same physical pool.
### Supervisor and Process Lifecycle
When a process exits in Linux, it becomes a zombie until its parent calls `waitpid()` to collect its exit status. Without a persistent parent, every exited container would remain a zombie indefinitely. A long-running supervisor solves this by staying alive for the entire lifetime of all containers it manages.
Process creation uses `clone()` rather than `fork()` because `clone()` accepts namespace flags directly. The child calls `chroot()`, mounts `/proc`, and `execvp()`s the requested command. The parent records the child's PID in a metadata table.
`SIGCHLD` is delivered asynchronously when any container exits. A self-pipe is used: the handler writes one byte to a pipe, and the main loop drains the pipe and calls `waitpid(-1, WNOHANG)` to reap all children. Termination is classified as `stopped` (if `stop_requested` was set), `hard_limit_killed` (if SIGKILL without a stop request), or `killed` otherwise.
### IPC, Threads, and Synchronization
**Path A — Logging (pipes):** Before `clone()`, the supervisor creates a pipe. The child's stdout and stderr are redirected into the write end via `dup2()`. The parent passes the read end to a producer thread, which reads lines and inserts them into a bounded ring buffer. A consumer thread drains the buffer and writes to a per-container log file.
The ring buffer's shared variables (`head`, `tail`, `count`) would be corrupted without mutual exclusion. A `pthread_mutex_t` protects them. Two `pthread_cond_t` variables (`not_full` and `not_empty`) allow threads to sleep rather than busy-wait. The `done` flag is set inside the lock and broadcast to all waiting consumers so no consumer sleeps forever after the producer exits.
**Path B — Control (UNIX domain socket):** CLI client processes connect to `/tmp/engine.sock`, send a command string, and read the response. This is completely separate from the logging pipes. The metadata table is protected by its own `pthread_mutex_t` (`containers_lock`) so CLI operations and logging never block each other.
### Memory Management and Enforcement
RSS (Resident Set Size) measures the physical memory pages currently mapped into a process's address space and present in RAM. It does not count pages swapped out to disk, memory-mapped files not yet accessed, or shared library pages. This means RSS can both overestimate and underestimate true memory usage.
Soft and hard limits are different policies. A soft limit triggers a warning — the process continues but the operator is notified. A hard limit triggers termination. Two thresholds give operators a chance to react before resorting to forced termination.
Memory enforcement belongs in kernel space because user-space monitors are inherently racy — a process could allocate memory between two polling intervals. The kernel has full visibility into actual memory usage and cannot be bypassed by the monitored process.
### Scheduling Behavior
*(To be completed by teammate with experiment results.)*
---
## Design Decisions and Tradeoffs
### Namespace Isolation
- **Choice:** `clone()` with `CLONE_NEWPID | CLONE_NEWUTS | CLONE_NEWNS` and `chroot()`
- **Tradeoff:** `chroot()` is simpler than `pivot_root()` but a privileged process can escape via `..` traversal
- **Justification:** Sufficient for this project's scope with no observable difference in demo scenarios
### Supervisor Architecture
- **Choice:** Single supervisor with a self-pipe for SIGCHLD and a dedicated accept thread for CLI connections
- **Tradeoff:** CLI connections are handled sequentially — a blocking `run` command delays other commands
- **Justification:** Sequential handling is safe and simpler for a single-user CLI tool
### IPC and Logging
- **Choice:** UNIX domain socket for CLI, anonymous pipes for logging, bounded ring buffer with mutex and condition variables
- **Tradeoff:** Reading one byte at a time is simple but inefficient for high-throughput output
- **Justification:** Not a high-throughput scenario; byte-at-a-time reading makes line detection trivial
### Kernel Monitor
- **Choice:** Linux Kernel Module with a periodic kernel timer polling RSS
- **Tradeoff:** Polling introduces a window where a process could exceed the hard limit undetected
- **Justification:** Event-driven enforcement requires hooking the kernel allocator, which is far more complex
### Scheduling Experiments
*(To be completed by teammate.)*
---
## Scheduler Experiment Results
*(To be completed by teammate. Must include raw timing measurements, at least one comparison table, and explanation of results in terms of Linux CFS scheduling goals.)*







