#include "types.h"
#include "user.h"
#include "mlfq_simulator/mlfqtrace.h"

int
main(void)
{
    struct mlfqev events[64];
    int i, count, before, after;
    int pid = getpid();

    mlfqctl(OP_TRACE_ON, 0, 0);

    before = uptime();

    printf(1, "\nStarting 5-tick I/O wait...\n");
    mlfqctl(OP_IO, 0, 5);

    after = uptime();

    mlfqctl(OP_TRACE_OFF, 0, 0);
    count = mlfqctl(OP_DRAIN, events, 64);

    printf(1, "Elapsed global ticks: %d\n", after - before);
    printf(1, "Events collected: %d\n\n", count);

    printf(1, "Events for PID %d:\n", pid);

    for(i = 0; i < count; i++) {
        if(events[i].pid == pid) {
            printf(1, "tick=%d type=%d queue=Q%d\n",
                   events[i].tick, events[i].type, events[i].q);
        }
    }
    exit();
}