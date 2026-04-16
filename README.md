# OS-Jackfruit — Lightweight Linux Container Runtime

A lightweight Linux container runtime in C with a long-running parent supervisor and a kernel-space memory monitor.
---
## Team Information
| Name | SRN |
|------|-----|
| Deana Naveen A | PES1UG24AM077 |
| Bhoomika Kshatriya | PES1UG24AM068 |
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

<img width="792" height="1599" alt="image" src="https://github.com/user-attachments/assets/602f31f4-ec55-4bfe-9ffa-2e7fd79f4e9d" />

<img width="940" height="379" alt="image" src="https://github.com/user-attachments/assets/22c5a36e-ef08-4a30-b128-1a224caf507f" />

<img width="940" height="678" alt="image" src="https://github.com/user-attachments/assets/c9fda91c-60bc-4413-ae02-a95cf3b0952d" />

<img width="940" height="464" alt="image" src="https://github.com/user-attachments/assets/4da080ef-6e68-421e-9ab6-9d7ef85d6811" />

<img width="987" height="155" alt="image" src="https://github.com/user-attachments/assets/babcb5d9-f390-41b9-9283-9d8357b0fb3c" />

<img width="494" height="95" alt="image" src="https://github.com/user-attachments/assets/c5d99108-692d-47d0-911d-491deeca0a93" />

<img width="494" height="95" alt="image" src="https://github.com/user-attachments/assets/32417699-790c-4469-84f2-e0b287991c1c" />

<img width="935" height="258" alt="image" src="https://github.com/user-attachments/assets/597d16f4-0cbc-4a8b-a1b8-ba24a2963006" />

<img width="1600" height="332" alt="image" src="https://github.com/user-attachments/assets/4bfa01ec-3276-4d61-b601-8225726c6a81" />

