#include "types.h"
#include "stat.h"
#include "user.h"

#define CPU_WORK  5
#define IO_WORK   3

/*
 * CPU-bound worker:
 * Repeatedly performs computation without sleeping.
 */
void
cpu_worker(int id)
{
	volatile int x = id + 1;
	int i, j;

	printf(1, "CPU worker %d started, pid=%d\n", id, getpid());

	for (i = 0; i < CPU_WORK; i++) {
		for (j = 0; j < 5000000; j++)
			x = x * 1664525 + 1013904223;
	}

	printf(1, "CPU worker %d finished, pid=%d\n", id, getpid());
	exit();
}

/*
 * I/O-style worker:
 * Alternates short bursts of computation with sleep.
 */
void
io_worker(void)
{
	volatile int x = 1;
	int i, j;

	printf(1, "I/O-style worker started, pid=%d\n", getpid());

	for (i = 0; i < IO_WORK; i++) {
		for (j = 0; j < 500000; j++)
			x = x * 1664525 + 1013904223;

		sleep(5);
	}

	printf(1, "I/O-style worker finished, pid=%d\n", getpid());
	exit();
}

/*
 * Test 1:
 * Launch several CPU-bound processes.
 */
void
test_cpu(void)
{
	int i, pid;

	printf(1, "\n=== CPU-bound test ===\n");

	for (i = 0; i < 3; i++) {
		pid = fork();

		if (pid < 0) {
			printf(1, "fork failed\n");
			break;
		}

		if (pid == 0)
			cpu_worker(i);
	}

	while (wait() >= 0)
		;

	printf(1, "CPU-bound test finished\n");
}

/*
 * Test 2:
 * Mix CPU-bound workers with a sleeping worker.
 */
void
test_mixed(void)
{
	int i, pid;

	printf(1, "\n=== Mixed workload test ===\n");

	pid = fork();
	if (pid < 0) {
		printf(1, "fork failed\n");
		return;
	}
	if (pid == 0)
		io_worker();

	for (i = 0; i < 2; i++) {
		pid = fork();

		if (pid < 0) {
			printf(1, "fork failed\n");
			break;
		}

		if (pid == 0)
			cpu_worker(i);
	}

	while (wait() >= 0)
		;

	printf(1, "Mixed workload test finished\n");
}

/*
 * Test 3:
 * Keep a CPU-bound child alive long enough to observe
 * scheduling and periodic priority boosting.
 */
void
test_boost(void)
{
	int pid, i;
	volatile int x = 1;

	printf(1, "\n=== Long-running workload test ===\n");

	pid = fork();

	if (pid < 0) {
		printf(1, "fork failed\n");
		return;
	}

	if (pid == 0) {
		for (i = 0; i < 500; i++) {
			int j;

			for (j = 0; j < 5000000; j++)
				x = x * 1664525 + 1013904223;
		}

		printf(1, "Long-running worker finished, pid=%d\n", getpid());
		exit();
	}

	wait();
	printf(1, "Long-running workload test finished\n");
}

int
main(int argc, char* argv[])
{
	int test;

	if (argc < 2) {
		printf(1, "Usage: mlfqtest 1|2|3\n");
		exit();
	}

	test = atoi(argv[1]);

	if (test == 1)
		test_cpu();
	else if (test == 2)
		test_mixed();
	else if (test == 3)
		test_boost();
	else
		printf(1, "Unknown test. Use 1, 2, or 3.\n");

	exit();
}
