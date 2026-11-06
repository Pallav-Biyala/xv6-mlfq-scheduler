#include "types.h"
#include "stat.h"
#include "user.h"
#include "mlfq_simulator/mlfqtrace.h"

// mlfq_sim [-q] name=pattern ...
//   pattern = segments separated by commas, optional "*N" to repeat the whole pattern
//   segment = cN (N ticks of CPU)  or  iN (N ticks of I/O)
//   example: mlfq_sim a=c40 b=c40 c=c3,i2*20
// Real processes run under the real scheduler. The kernel records what it does,
// and this program replays that record as a tick-by-tick timeline.
// -q prints only ticks where something happened.

#define MAXP   8
#define MAXSEG 12
#define MAXEV  4096

struct seg { char type; int len; };

struct spec {
  char name[8];
  struct seg seg[MAXSEG];
  int nseg, reps, pid;
  int st;       // 0 runnable, 1 running, 2 blocked on I/O, 3 finished
  int q;        // current queue
  int ndemote;
  int dem;      // demoted in this scheduling round (suppresses "quantum" message)
  int tend;     // tick it finished
};

static struct spec sp[MAXP];
static int np, exited, quiet;
static struct mlfqev ev[MAXEV];
static int nev;
static char msg[200];

// ---------------- helpers ----------------

static void
cat(char* d, char* s)
{
  while (*d)
    d++;
  while ((*d = *s) != 0) {
    d++;
    s++;
  }
}

static void
say(char* who, char* what)
{
  if (strlen(msg) + strlen(who) + strlen(what) + 4 >= sizeof(msg))
    return;
  if (msg[0])
    cat(msg, "; ");
  if (who[0]) {
    cat(msg, who);
    cat(msg, " ");
  }
  cat(msg, what);
}

static void
pads(char* s, int w)
{
  int l = strlen(s);
  printf(1, "%s", s);
  while (l++ < w)
    printf(1, " ");
}

static void
itos(int v, char* b)
{
  char t[12];
  int n = 0, i = 0;
  if (v <= 0)
    t[n++] = '0';
  while (v > 0) {
    t[n++] = '0' + v % 10;
    v /= 10;
  }
  while (n > 0)
    b[i++] = t[--n];
  b[i] = 0;
}

static int
pidx(int pid)
{
  int i;
  for (i = 0; i < np; i++)
    if (sp[i].pid == pid)
      return i;
  return -1;
}

// ---------------- parsing ----------------

static int
num(char** pp)
{
  int n = 0;
  char* p = *pp;
  if (*p < '0' || *p > '9')
    return -1;
  while (*p >= '0' && *p <= '9') {
    n = n * 10 + (*p - '0');
    p++;
  }
  *pp = p;
  return n;
}

static int
parse(char* arg, struct spec* s)
{
  char* p = arg, t;
  int i = 0, n;

  while (*p && *p != '=') {
    if (i < 7)
      s->name[i++] = *p;
    p++;
  }
  s->name[i] = 0;
  if (*p != '=' || i == 0)
    return -1;
  p++;
  s->nseg = 0;
  s->reps = 1;
  while (*p && *p != '*') {
    t = *p++;
    if (t != 'c' && t != 'i')
      return -1;
    n = num(&p);
    if (n <= 0 || s->nseg >= MAXSEG)
      return -1;
    s->seg[s->nseg].type = t;
    s->seg[s->nseg].len = n;
    s->nseg++;
    if (*p == ',')
      p++;
    else if (*p && *p != '*')
      return -1;
  }
  if (*p == '*') {
    p++;
    n = num(&p);
    if (n <= 0 || *p)
      return -1;
    s->reps = n;
  }
  return s->nseg > 0 ? 0 : -1;
}

// ---------------- the workload (runs as a real process) ----------------

static void
busy(void)
{
  volatile int x = 1;
  int i;
  for (i = 0; i < 2000; i++)
    x = x * 3 + 1;
}

static void
worker(struct spec* s)
{
  int r, k, start;

  for (r = 0; r < s->reps; r++) {
    for (k = 0; k < s->nseg; k++) {
      if (s->seg[k].type == 'c') {
        start = mlfqctl(OP_CPU, 0, 0);
        while (mlfqctl(OP_CPU, 0, 0) - start < s->seg[k].len)
          busy();
      }
      else {
        mlfqctl(OP_IO, 0, s->seg[k].len);
      }
    }
  }
  exit();
}

// ---------------- collecting the kernel trace ----------------

static void
collect(void)
{
  struct mlfqev tmp[64];
  int n, i;

  while ((n = mlfqctl(OP_DRAIN, tmp, 64)) > 0) {
    for (i = 0; i < n; i++) {
      if (tmp[i].type != EV_BOOST && pidx(tmp[i].pid) < 0)
        continue;                    // not one of ours (sh, this monitor)
      if (nev < MAXEV)
        ev[nev++] = tmp[i];
      if (tmp[i].type == EV_EXIT)
        exited++;
    }
  }
}

// ---------------- replay ----------------

static void
apply(struct mlfqev* e)
{
  int i;
  struct spec* s;
  char d[4];

  if (e->type == EV_BOOST) {
    for (i = 0; i < np; i++)
      sp[i].q = 0;
    say("", "BOOST: everyone back to Q0");
    return;
  }
  i = pidx(e->pid);
  if (i < 0)
    return;
  s = &sp[i];
  switch (e->type) {
  case EV_RUN:
    s->st = 1;
    s->q = e->q;
    say(s->name, "runs");
    break;
  case EV_YIELD:
    s->st = 0;
    s->q = e->q;
    if (!s->dem)
      say(s->name, "quantum used up, to back of its queue");
    s->dem = 0;
    break;
  case EV_SLEEP:
    s->st = 2;
    say(s->name, "starts I/O");
    break;
  case EV_WAKE:
    s->st = 0;
    s->q = e->q;
    say(s->name, "I/O done, runnable");
    break;
  case EV_EXIT:
    s->st = 3;
    s->tend = e->tick;
    say(s->name, "finished");
    break;
  case EV_DEMOTE:
    s->ndemote++;
    s->dem = 1;
    say(s->name, "used its allotment, demoted");
    cat(msg, " Q");
    d[0] = '0' + s->q;
    d[1] = 0;
    cat(msg, d);
    cat(msg, "->Q");
    d[0] = '0' + e->q;
    cat(msg, d);
    s->q = e->q;
    break;
  }
}

static void
qcol(int q, char* b)
{
  int i, first = 1;

  strcpy(b, "[");
  for (i = 0; i < np; i++) {
    if (sp[i].st == 0 && sp[i].q == q) {
      if (!first)
        cat(b, " ");
      cat(b, sp[i].name);
      first = 0;
    }
  }
  cat(b, "]");
}

static void
line(int t)
{
  char b[80], nb[12];
  int i, q, first;

  itos(t, nb);
  pads(nb, 6);
  for (q = 0; q < 4; q++) {
    qcol(q, b);
    pads(b, 14);
  }
  strcpy(b, "-");
  for (i = 0; i < np; i++)
    if (sp[i].st == 1)
      strcpy(b, sp[i].name);
  pads(b, 8);
  strcpy(b, "");
  first = 1;
  for (i = 0; i < np; i++) {
    if (sp[i].st == 2) {
      if (!first)
        cat(b, " ");
      cat(b, sp[i].name);
      first = 0;
    }
  }
  if (first)
    strcpy(b, "-");
  pads(b, 10);
  if (msg[0] == 0) {
    strcpy(b, "idle");
    for (i = 0; i < np; i++)
      if (sp[i].st == 1) {
        strcpy(b, sp[i].name);
        cat(b, " running");
      }
    printf(1, "%s\n", b);
  }
  else {
    printf(1, "%s\n", msg);
  }
}

static void
replay(int t0)
{
  int i = 0, t, tend = t0;

  if (nev > 0)
    tend = ev[nev - 1].tick;
  printf(1, "\ntick  Q0            Q1            Q2            Q3            CPU     I/O       what happened\n");
  for (t = t0; t <= tend; t++) {
    msg[0] = 0;
    while (i < nev && ev[i].tick <= (uint)t) {
      apply(&ev[i]);
      i++;
    }
    if (!quiet || msg[0])
      line(t - t0);
  }
  printf(1, "\nprocess  pid   finished  demotions  last queue\n");
  for (i = 0; i < np; i++) {
    char b[12];
    pads(sp[i].name, 9);
    itos(sp[i].pid, b);
    pads(b, 6);
    itos(sp[i].tend - t0, b);
    pads(b, 10);
    itos(sp[i].ndemote, b);
    pads(b, 11);
    printf(1, "Q%d\n", sp[i].q);
  }
}

int
main(int argc, char* argv[])
{
  int i, a, t0, pid;

  for (a = 1; a < argc; a++) {
    if (argv[a][0] == '-' && argv[a][1] == 'q') {
      quiet = 1;
      continue;
    }
    if (np >= MAXP || parse(argv[a], &sp[np]) < 0) {
      printf(1, "bad spec '%s'\n", argv[a]);
      printf(1, "usage: mlfq_sim [-q] name=pattern ...   e.g. a=c40 b=c3,i2*20\n");
      printf(1, "  cN = N ticks of CPU, iN = N ticks of I/O, *N repeats (max %d procs)\n", MAXP);
      exit();
    }
    np++;
  }
  if (np == 0) {
    printf(1, "usage: mlfq_sim [-q] name=pattern ...   e.g. a=c40 b=c3,i2*20\n");
    exit();
  }

  mlfqctl(OP_TRACE_ON, 0, 0);
  t0 = uptime();
  for (i = 0; i < np; i++) {
    pid = fork();
    if (pid < 0) {
      printf(1, "fork failed\n");
      break;
    }
    if (pid == 0)
      worker(&sp[i]);
    sp[i].pid = pid;
  }

  // Drain the kernel trace cheaply while the workers run; print nothing yet,
  // so this monitor disturbs the scheduler as little as possible.
  while (exited < np && uptime() - t0 < 30000) {
    collect();
    if (exited >= np)
      break;
    sleep(2);
  }
  collect();
  while (wait() >= 0)
    ;
  replay(t0);
  mlfqctl(OP_TRACE_OFF, 0, 0);
  exit();
}
