// Shared by kernel (proc.c, sysproc.c) and user (mlfq_sim.c).
// Include after types.h.

#define EV_RUN     1   // scheduler picked pid          (q = its queue)
#define EV_YIELD   2   // pid gave up CPU, still runnable (q = its queue now)
#define EV_SLEEP   3   // pid blocked (I/O)
#define EV_WAKE    4   // pid became runnable again     (q = its queue)
#define EV_EXIT    5   // pid exited
#define EV_DEMOTE  6   // pid demoted                   (q = new queue)
#define EV_BOOST   7   // priority boost                (pid = 0, q = #moved)

#define OP_CPU        0  // returns CPU ticks charged to the caller so far
#define OP_IO         1  // block for n ticks (woken by the timer interrupt)
#define OP_TRACE_ON   2  // clear buffer and start recording
#define OP_TRACE_OFF  3
#define OP_DRAIN      4  // copy up to n events into buf, returns count

struct mlfqev {
  uint tick;
  int type;
  int pid;
  int q;
};
