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
