#include "types.h"
#include "user.h"
#include "mlfq_simulator/mlfqtrace.h"

int
main(void)
{
    struct mlfqev events[64];
    int i, count;
    volatile int j;

    printf(1, "Enabling scheduler trace...\n");
    mlfqctl(OP_TRACE_ON, 0, 0);

    // Generate some CPU activity.
    for(j = 0; j < 50000000; j++)
        ;

    mlfqctl(OP_TRACE_OFF, 0, 0);

    count = mlfqctl(OP_DRAIN, events, 64);

    printf(1, "\nEvents collected: %d\n", count);

    for(i = 0; i < count; i++) {
        printf(1, "tick=%d pid=%d type=%d queue=Q%d\n",
               events[i].tick,
               events[i].pid,
               events[i].type,
               events[i].q);
    }

    exit();
}