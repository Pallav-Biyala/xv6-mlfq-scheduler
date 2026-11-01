# MLFQ Scheduler for xv6

This project extends the original **xv6 - x86 operating system** with a
**Multi-Level Feedback Queue (MLFQ) CPU scheduler**.

The goal is to explore how an operating system can balance responsiveness
for interactive processes with CPU time for CPU-bound processes.

## Overview

The scheduler uses four priority levels:

- **Q0** — highest priority
- **Q1**
- **Q2**
- **Q3** — lowest priority

New processes begin at the highest priority. The scheduler considers
runnable processes from higher-priority queues first and uses round-robin
selection within a priority level.

## Scheduling Parameters

| Queue | Time Quantum | CPU Allotment |
|:-----:|-------------:|--------------:|
| Q0    | 6 ticks      | 30 ticks      |
| Q1    | 10 ticks     | 50 ticks      |
| Q2    | 16 ticks     | 80 ticks      |
| Q3    | 24 ticks     | 120 ticks     |

These are the project's current configuration values and can be adjusted
for experimentation.

## Features

- **Priority-based scheduling:** higher-priority runnable processes are
  considered before lower-priority processes.
- **Round-robin selection:** processes at the same priority share CPU time.
- **CPU-time accounting:** execution time is tracked for each process.
- **Priority demotion:** CPU-bound processes can move to lower-priority
  levels according to the scheduling policy.
- **Periodic priority boosting:** processes are periodically promoted to
  the highest queue to help prevent starvation.

## Implementation

The main implementation changes are in:

- `proc.c` — scheduling, process accounting, demotion, and priority boosting
- `proc.h` — per-process scheduling fields
- `trap.c` — timer-tick accounting and scheduling decisions
- `defs.h` — kernel function declarations
- `Makefile` — build configuration

Supporting declarations and interfaces may also be present in the syscall
and user-interface files.

## Build and Run

Build the project in a Linux environment with the required compiler,
GNU Make, and QEMU dependencies installed.

From the repository root:

```sh
make clean
make
make qemu-nox
```

If the build succeeds, xv6 boots in the terminal.

To exit QEMU, press Ctrl-A, followed by X.

**Toolchain note:** newer GCC versions may treat warnings in the original
xv6 code as errors. The current Makefile includes warning options used
during development to accommodate the compiler.

## Testing

After booting xv6, use the xv6 shell to run available user programs and
check that the system boots and remains responsive.

Scheduler testing should include CPU-bound workloads and processes that
sleep or perform I/O. These workloads help evaluate priority changes,
time-sharing, and periodic priority boosts.

## Project Scope

This is an educational kernel-level scheduling project intended for 
learning and experimentation, not a production operating system.

## Attribution

This project is based on
<a href="https://github.com/mit-pdos/xv6-public">MIT's xv6-public</a>,
the original x86 version of xv6.
Please refer to the upstream repository for the original source and
licensing information.

---
