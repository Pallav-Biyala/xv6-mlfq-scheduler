#include "types.h"
#include "user.h"
#include "mlfq_simulator/mlfqtrace.h"

int
main(void)
{
    int before, after;
    volatile int i;

    before = mlfqctl(OP_CPU, 0, 0);

    for(i = 0; i < 20000000; i++)
        ;

    after = mlfqctl(OP_CPU, 0, 0);

    printf(1, "CPU ticks before: %d\n", before);
    printf(1, "CPU ticks after:  %d\n", after);
    printf(1, "CPU ticks used:   %d\n", after - before);

    exit();
}