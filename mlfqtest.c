#include "types.h"
#include "stat.h"
#include "user.h"

// MLFQ test suite. Needs the getprio(pid) syscall. Assumes ONE CPU.
// Usage: mlfqtest all | 1..7
//  1 hog sinks to Q3, fork child starts Q0   5 boost rescues a Q3 process
//  2 interactive stays in Q0                 6 equal hogs finish fairly
//  3 gamer is caught by the allotment        7 stress: 30 processes, no hang
//  4 mixed load: interactive is not starved
// Boost fires every 500 ticks, so tests wait (fit) to avoid straddling one.

struct res { int a, b; };

static int rate;          // loop iterations per timer tick
static int npass, nfail;

static void
work(int iters)
{
	volatile int x = 1;
	int i;
	for (i = 0; i < iters; i++)
		x = x * 1664525 + 1013904223;
}

// Burn about n ticks of CPU time.
static void
spin(int n)
{
	int i;
	for (i = 0; i < n; i++)
		work(rate);
}

static void
calibrate(void)
{
	int s = uptime(), n = 0, e;
	while (uptime() - s < 10) {
		work(100000);
		n += 100000;
	}
	e = uptime() - s;
	rate = n / e;
	if (rate < 1)
		rate = 1;
}

// Wait until the next boost is at least `need` ticks away.
static void
fit(int need)
{
	int left = 500 - uptime() % 500;
	if (left < need + 5)
		sleep(left + 3);
}

static void
verdict(char* name, int ok)
{
	printf(1, "  %s  %s\n", ok ? "PASS" : "FAIL", name);
	if (ok) npass++; else nfail++;
}

// ---- child bodies: each fills r->a / r->b ----

static void
c_hog(int cpu, struct res* r)
{
	int s = uptime();
	spin(cpu);
	r->a = uptime() - s;
	r->b = getprio(getpid());
}

static void
c_inter(int rounds, struct res* r)
{
	int i, s = uptime();
	for (i = 0; i < rounds; i++) {
		work(rate / 2);
		sleep(2);
	}
	r->a = uptime() - s;
	r->b = getprio(getpid());
}

// Runs 4 ticks (< Q0 quantum of 6), sleeps 1, repeats. Only the allotment can catch it.
static void
c_gamer(int rounds, struct res* r)
{
	int i, s = uptime();
	for (i = 0; i < rounds; i++) {
		spin(4);
		sleep(1);
	}
	r->a = uptime() - s;
	r->b = getprio(getpid());
}

// Sink to Q3, then fork a child and report its starting priority.
static void
c_sink(int duration, struct res* r)
{
	int g[2], v = -1, s = uptime();

	while (uptime() - s < duration)
		work(rate / 10);

	r->a = getprio(getpid());

	pipe(g);
	if (fork() == 0) {
		v = getprio(getpid());
		write(g[1], &v, sizeof(v));
		exit();
	}

	read(g[0], &v, sizeof(v));
	wait();
	r->b = v;

	close(g[0]);
	close(g[1]);
}
// Spin past a boost boundary; report priority just before and just after it.
static void
c_boost(int unused, struct res* r)
{
	int b = (uptime() / 500 + 1) * 500;
	if (b - uptime() < 220)
		b += 500;
	while (uptime() < b - 3)
		work(rate / 4);
	r->a = getprio(getpid());
	while (uptime() < b + 10)
		work(rate / 4);
	r->b = getprio(getpid());
}

// ---- plumbing: run a child body, get its result back through a pipe ----

static void
launch(void (*fn)(int, struct res*), int arg, int* rd)
{
	int fd[2];
	struct res r;

	pipe(fd);
	if (fork() == 0) {
		close(fd[0]);
		r.a = r.b = -1;
		fn(arg, &r);
		write(fd[1], &r, sizeof(r));
		exit();
	}
	close(fd[1]);
	*rd = fd[0];
}

static struct res
collect(int rd)
{
	struct res r;
	r.a = r.b = -1;
	read(rd, &r, sizeof(r));
	close(rd);
	wait();
	return r;
}

// ---- tests ----

static void
t_sink(void)
{
	int rd;
	struct res r;

	printf(1, "\n[1] single hog sinks; forked child starts in Q0\n");
	fit(260);
	launch(c_sink, 200, &rd);
	r = collect(rd);
	printf(1, "  hog ended in Q%d, its child started in Q%d\n", r.a, r.b);
	verdict("hog reaches Q3 after 160+ CPU ticks", r.a == 3);
	verdict("fork() child starts in Q0", r.b == 0);
}

static void
t_inter(void)
{
	int rd;
	struct res r;

	printf(1, "\n[2] interactive process stays in Q0\n");
	fit(120);
	launch(c_inter, 15, &rd);
	r = collect(rd);
	printf(1, "  finished in %d ticks, ended in Q%d\n", r.a, r.b);
	verdict("interactive stays in Q0", r.b == 0);
	verdict("finishes in reasonable time", r.a > 0 && r.a < 150);
}

static void
t_gamer(void)
{
	int rd;
	struct res r;

	printf(1, "\n[3] gamer (4 ticks, sleep, repeat) is demoted by the allotment\n");
	fit(160);
	launch(c_gamer, 20, &rd);
	r = collect(rd);
	printf(1, "  finished in %d ticks, ended in Q%d\n", r.a, r.b);
	verdict("gamer demoted below Q0 without ever filling a quantum", r.b >= 1);
}

static void
t_mixed(void)
{
	int rd[4], i, minh = 1000000, hogs_deep = 1;
	struct res r[4];

	printf(1, "\n[4] 3 hogs + 1 interactive: interactive is not starved\n");
	fit(420);
	for (i = 0; i < 3; i++)
		launch(c_hog, 120, &rd[i]);
	launch(c_inter, 15, &rd[3]);
	for (i = 0; i < 4; i++)
		r[i] = collect(rd[i]);
	for (i = 0; i < 3; i++) {
		printf(1, "  hog %d: %d ticks, ended in Q%d\n", i, r[i].a, r[i].b);
		if (r[i].a < minh) minh = r[i].a;
		if (r[i].b < 2) hogs_deep = 0;
	}
	printf(1, "  interactive: %d ticks, ended in Q%d\n", r[3].a, r[3].b);
	verdict("hogs sank to Q2 or lower", hogs_deep);
	verdict("interactive stayed in Q0", r[3].b == 0);
	verdict("interactive finished before any hog",
        r[3].a < minh);
}

static void
t_boost(void)
{
	int rd;
	struct res r;

	printf(1, "\n[5] boost rescues a process stuck in Q3 (can take ~7 s)\n");
	launch(c_boost, 0, &rd);
	r = collect(rd);
	printf(1, "  just before boost: Q%d, just after: Q%d\n", r.a, r.b);
	verdict("process was in Q3 before the boost", r.a == 3);
	verdict("process is back in Q0 after the boost", r.b == 0);
}

static void
t_fair(void)
{
	int rd[4], i, mn = 1000000, mx = 0;
	struct res r[4];

	printf(1, "\n[6] 4 equal hogs finish at similar times (round-robin fairness)\n");
	fit(300);
	for (i = 0; i < 4; i++)
		launch(c_hog, 60, &rd[i]);
	for (i = 0; i < 4; i++) {
		r[i] = collect(rd[i]);
		printf(1, "  hog %d: %d ticks\n", i, r[i].a);
		if (r[i].a < mn) mn = r[i].a;
		if (r[i].a > mx) mx = r[i].a;
	}
	verdict("all hogs finished", mn > 0);
	verdict("slowest is within 1.5x of fastest", mx * 2 <= mn * 3);
}

static void
t_stress(void)
{
	int i, k, pid, made = 0, got = 0, s = uptime();

	printf(1, "\n[7] stress: 30 processes (hogs + sleepers), all must finish\n");
	for (i = 0; i < 30; i++) {
		pid = fork();
		if (pid < 0)
			break;
		if (pid == 0) {
			if (i % 3 == 0) {
				spin(8);
			}
			else {
				for (k = 0; k < 10; k++) {
					work(rate / 2);
					sleep(1);
				}
			}
			exit();
		}
		made++;
	}
	while (wait() >= 0)
		got++;
	printf(1, "  forked %d, reaped %d, took %d ticks\n", made, got, uptime() - s);
	verdict("every child was reaped", made > 0 && made == got);
	verdict("finished without hanging", uptime() - s < 1000);
}

int
main(int argc, char* argv[])
{
	int all, t;

	if (argc < 2) {
		printf(1, "Usage: mlfqtest all | 1..7\n");
		exit();
	}
	all = (argv[1][0] == 'a');
	t = atoi(argv[1]);

	calibrate();
	printf(1, "calibrated: %d iterations per tick\n", rate);

	if (all || t == 1) t_sink();
	if (all || t == 2) t_inter();
	if (all || t == 3) t_gamer();
	if (all || t == 4) t_mixed();
	if (all || t == 5) t_boost();
	if (all || t == 6) t_fair();
	if (all || t == 7) t_stress();

	printf(1, "\n%d passed, %d failed\n", npass, nfail);
	exit();
}
