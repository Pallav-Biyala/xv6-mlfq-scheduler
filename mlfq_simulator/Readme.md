# MLFQ Simulator — Shared Trace Definitions

This directory contains the shared definitions used by the MLFQ simulator and the xv6 kernel's event-tracing implementation.

## Files

- **`mlfqtrace.h`** — Defines the event types (`RUN`, `YIELD`, `SLEEP`, `WAKE`, `EXIT`, `DEMOTE`, `BOOST`), trace operation codes, and the `mlfqev` event structure shared between the kernel and userspace simulator.

## Where Is the Simulator Implemented?

The simulator is implemented across the main xv6 source files:

- **`../mlfq_sim.c`** — Userspace simulator. Creates worker processes, executes configurable workloads, collects trace events, and reconstructs the scheduling timeline.
- **`../proc.c`** — Kernel-side event recording and trace buffer management.
- **`../trap.c`** — Timer interrupt handling, scheduling decisions, and event generation.
- **`../sysproc.c`** — System call interface for simulator operations.

The simulator uses the actual xv6 MLFQ scheduler rather than implementing a separate scheduling algorithm.

## Running the Simulator

Build and boot xv6 from the repository root:

```sh
make
make qemu CPUS=1
```

Then execute workloads inside the xv6 shell:

```sh
mlfq_sim a=c40 b=c10,i5*3
```

For complete usage instructions, workload syntax, examples, and output interpretation, see the main repository's `README.md`.
