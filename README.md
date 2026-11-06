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

## MLFQ Simulator

We also have added a userspace simulator (`mlfq_sim`) that visualizes the execution of processes under the actual xv6 MLFQ scheduler. Hence, now we can run mlfq_sim and adding processes, and then we can see at each time tick the current process running and status of queues etc. 

Unlike a standalone scheduling simulator, this tool uses real xv6 processes. It generates configurable CPU-bound and I/O-bound workloads, records scheduling events inside the kernel, and reconstructs a tick-by-tick execution timeline.

### How It Works

1. The simulator creates worker processes according to the specified workloads.
2. Each worker alternates between CPU execution and simulated I/O, as configured.
3. The actual xv6 MLFQ scheduler decides which process executes and at what priority.
4. The kernel records events such as process execution, yielding, sleeping, waking, exiting, priority demotion, and priority boosting.
5. The simulator collects these events and reconstructs the execution timeline in the xv6 shell.

Therefore, the displayed scheduling decisions come from the actual kernel scheduler, not from a separate implementation of MLFQ.

### Building and Running

Build xv6:

```sh
make clean
make
```

Start QEMU with a single CPU:

```sh
make qemu CPUS=1
```

The simulator is available as a userspace program inside xv6.

Run it from the xv6 shell:

```sh
mlfq_sim <process1> <process2> ...
```

### Workload Syntax

Each process is specified using:

```text
name=pattern
```

The pattern consists of comma-separated CPU and I/O operations.

| Symbol | Meaning |
|:------:|---------|
| `cN` | Execute for N CPU ticks |
| `iN` | Perform simulated I/O for N ticks |
| `*N` | Repeat the preceding CPU/I/O pattern N times |

For example:

```sh
mlfq_sim a=c40 b=c10,i5*3
```

This creates two processes:

- **Process a:** CPU-bound process that requires 40 CPU ticks.
- **Process b:** Performs 10 CPU ticks followed by 5 ticks of simulated I/O, repeating this pattern 3 times.

Multiple processes can be specified in the same command, separated by spaces.

### Example Workloads

**1. Two CPU-bound processes**

```sh
mlfq_sim a=c100 b=c100
```

Useful for observing round-robin scheduling, CPU sharing, and priority demotion.

**2. CPU-bound versus interactive process**

```sh
mlfq_sim g=c4,i1*20 h=c60
```

- `g` performs short CPU bursts of 4 ticks followed by 1 tick of I/O, repeated 20 times.
- `h` continuously consumes CPU time for 60 ticks.

Useful for observing how interactive and CPU-bound processes behave under MLFQ.

**3. Multiple processes with different workloads**

```sh
mlfq_sim a=c40 b=c40 c=c3,i2*20
```

Useful for observing competition between multiple CPU-bound processes and an I/O-intensive process.

**4. Observing priority demotion**

```sh
mlfq_sim a=c200
```

A long CPU-bound workload makes it easier to observe successive priority demotions.

### Understanding the Output

The simulator displays a tick-by-tick timeline with the following columns:

| Column | Description |
|--------|-------------|
| `tick` | Relative execution tick |
| `Q0` | Runnable processes in the highest-priority queue |
| `Q1` | Runnable processes in the second-priority queue |
| `Q2` | Runnable processes in the third-priority queue |
| `Q3` | Runnable processes in the lowest-priority queue |
| `CPU` | Process currently executing |
| `I/O` | Processes currently waiting for simulated I/O |
| `what happened` | Scheduling event occurring at that tick |

The timeline also displays important events, including:

- Process execution and queue transitions.
- Processes going to sleep for I/O.
- Processes becoming runnable after I/O completion.
- Priority demotions due to CPU allotment exhaustion.
- Periodic priority boosts.

At the end, a summary is displayed for each process, including its PID, completion time, number of demotions, and final priority queue.

### Notes and Limitations

- The simulator uses the actual xv6 scheduler and kernel event tracing.
- Run with `CPUS=1` to ensure a single, ordered execution timeline.
- I/O is simulated using timed blocking rather than actual disk or device I/O.
- I/O-bound processes retain their accumulated CPU allotment when they sleep; sleeping does not reset their CPU-time accounting.
- A process waking from I/O becomes runnable at its existing priority. Wake-up does not immediately preempt the currently running process.
- The timeline is reconstructed from recorded kernel events after the workload completes.

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
