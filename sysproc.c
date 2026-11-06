#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"
#include "mlfq_simulator/mlfqtrace.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{
  return wait();
}

int
sys_kill(void)
{
  int pid;

  if (argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if (argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if (growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if (argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while (ticks - ticks0 < n) {
    if (myproc()->killed) {
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}

// adding getprio to kernel
int
sys_getprio(void)
{
  int pid;

  if (argint(0, &pid) < 0)
    return -1;

  return getprio(pid);
}

// mlfq simulator
int
sys_mlfqctl(void)
{
  int op;

  if (argint(0, &op) < 0)
    return -1;

  if (op == OP_CPU)
    return get_cpu_ticks();

  if (op == OP_TRACE_ON) {
    mlfq_trace_set(1);
    return 0;
  }

  if (op == OP_TRACE_OFF) {
    mlfq_trace_set(0);
    return 0;
  }

  if (op == OP_DRAIN) {
    int n;
    struct mlfqev* buf;

    if (argint(2, &n) < 0 || n <= 0 || n > 64)
      return -1;

    if (argptr(1, (char**)&buf, n * sizeof(*buf)) < 0)
      return -1;

    return mlfq_drain(buf, n);
  }

  if (op == OP_IO) {
    int n;

    if (argint(2, &n) < 0 || n <= 0)
      return -1;

    return mlfq_iowait(n);
  }

  return -1;
}